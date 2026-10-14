#include "ui/MainWindow.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStringList>
#include <QStyle>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <windows.h>

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

#include "app/HotkeyManager.h"
#include "app/SingleInstance.h"
#include "app/TrayController.h"
#include "core/EnumHelpers.h"
#include "core/Logging.h"
#include "ui/RecordingOverlay.h"
#include "ui/SettingsDialog.h"
#include "ui/Theme.h"

namespace minirec {
namespace {

QString toQString(const std::string &value)
{
	return QString::fromUtf8(value.c_str());
}

const char *captureModeName(CaptureMode mode)
{
	switch (mode) {
	case CaptureMode::Window:
		return "窗口";
	case CaptureMode::Region:
		return "区域";
	case CaptureMode::Monitor:
	default:
		return "显示器";
	}
}

} // namespace

MainWindow::MainWindow(RecorderController *controller, HotkeyManager *hotkeys, QWidget *parent)
	: QMainWindow(parent), m_controller(controller), m_hotkeys(hotkeys)
{
	setWindowTitle(QStringLiteral("MiniRec 录屏"));
	setWindowIcon(theme::appIcon());
	setMinimumWidth(560);

	buildUi();

	m_tray = std::make_unique<TrayController>(this);
	m_overlay = std::make_unique<RecordingOverlay>(nullptr);

	// 托盘菜单
	connect(m_tray.get(), &TrayController::toggleRecordingRequested, this, &MainWindow::onRecordClicked);
	connect(m_tray.get(), &TrayController::togglePauseRequested, m_controller, &RecorderController::togglePause);
	connect(m_tray.get(), &TrayController::showWindowRequested, this, &MainWindow::showAndActivate);
	connect(m_tray.get(), &TrayController::openOutputFolderRequested, this, &MainWindow::onOpenOutputDir);
	connect(m_tray.get(), &TrayController::openSettingsRequested, this, &MainWindow::onOpenSettings);
	connect(m_tray.get(), &TrayController::quitRequested, this, [this]() {
		m_quitting = true;
		if (m_controller->isRecording()) {
			m_controller->stopRecording();
			// 等收尾完成再退出（onStateChanged 里检测到 Idle 且 m_quitting 时会退出）
			return;
		}
		syncConfigFromUi();
		m_controller->saveConfig();
		qApp->quit();
	});

	// 悬浮控制条
	connect(m_overlay.get(), &RecordingOverlay::pauseRequested, m_controller, &RecorderController::togglePause);
	connect(m_overlay.get(), &RecordingOverlay::stopRequested, m_controller, &RecorderController::stopRecording);

	// 录制状态
	connect(m_controller, &RecorderController::stateChanged, this, &MainWindow::onStateChanged);
	connect(m_controller, &RecorderController::countdownTick, this, &MainWindow::onCountdownTick);
	connect(m_controller, &RecorderController::statsUpdated, this, &MainWindow::onStatsUpdated);
	connect(m_controller, &RecorderController::errorOccurred, this, &MainWindow::onError);
	connect(m_controller, &RecorderController::recordingFinished, this, &MainWindow::onRecordingFinished);
	connect(m_controller, &RecorderController::encoderChanged, this, &MainWindow::onEncoderChanged);

	// 全局热键
	if (m_hotkeys) {
		connect(m_hotkeys, &HotkeyManager::startStopTriggered, m_controller, &RecorderController::toggleRecording);
		connect(m_hotkeys, &HotkeyManager::pauseTriggered, m_controller, &RecorderController::togglePause);
	}

	applyConfigToUi();
	setRecordingUiState(m_controller->state());
	updateHint();

	if (m_controller->hadUncleanShutdown()) {
		statusBar()->showMessage(QStringLiteral("上次录制未正常收尾，请到录制目录检查残留的 .mkv 文件"), 10000);
	}
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUi()
{
	auto *central = new QWidget(this);
	auto *root = new QVBoxLayout(central);
	root->setContentsMargins(16, 16, 16, 12);
	root->setSpacing(12);

	// ---- 录制目标 ----
	auto *targetGroup = new QGroupBox(QStringLiteral("录制目标"), central);
	auto *targetForm = new QFormLayout(targetGroup);
	targetForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

	m_captureModeCombo = new QComboBox(targetGroup);
	m_captureModeCombo->addItem(QStringLiteral("显示器（全屏）"));
	m_captureModeCombo->addItem(QStringLiteral("窗口"));
	m_captureModeCombo->addItem(QStringLiteral("自定义区域"));
	targetForm->addRow(QStringLiteral("录制内容"), m_captureModeCombo);

	auto *targetRow = new QWidget(targetGroup);
	auto *targetRowLayout = new QHBoxLayout(targetRow);
	targetRowLayout->setContentsMargins(0, 0, 0, 0);
	targetRowLayout->setSpacing(8);
	m_targetCombo = new QComboBox(targetRow);
	m_targetCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	m_pickRegionButton = new QPushButton(QStringLiteral("选择区域…"), targetRow);
	m_refreshButton = new QPushButton(QStringLiteral("刷新"), targetRow);
	targetRowLayout->addWidget(m_targetCombo, 1);
	targetRowLayout->addWidget(m_pickRegionButton);
	targetRowLayout->addWidget(m_refreshButton);
	targetForm->addRow(QStringLiteral("目标"), targetRow);

	root->addWidget(targetGroup);

	// ---- 声音 / 画面 两列 ----
	auto *columns = new QHBoxLayout();
	columns->setSpacing(12);

	auto *audioGroup = new QGroupBox(QStringLiteral("声音"), central);
	auto *audioLayout = new QVBoxLayout(audioGroup);
	m_desktopAudioCheck = new QCheckBox(QStringLiteral("录制系统声音"), audioGroup);
	m_desktopDeviceCombo = new QComboBox(audioGroup);
	m_micCheck = new QCheckBox(QStringLiteral("录制麦克风"), audioGroup);
	m_micDeviceCombo = new QComboBox(audioGroup);
	audioLayout->addWidget(m_desktopAudioCheck);
	audioLayout->addWidget(m_desktopDeviceCombo);
	audioLayout->addWidget(m_micCheck);
	audioLayout->addWidget(m_micDeviceCombo);
	audioLayout->addStretch(1);

	auto *videoGroup = new QGroupBox(QStringLiteral("画面"), central);
	auto *videoLayout = new QVBoxLayout(videoGroup);
	m_qualityCombo = new QComboBox(videoGroup);
	m_qualityCombo->addItem(QStringLiteral("原始（跟随屏幕，最高画质）"), 3);
	m_qualityCombo->addItem(QStringLiteral("高（1080p60，画质优先）"), 2);
	m_qualityCombo->addItem(QStringLiteral("中（1080p30，均衡）"), 1);
	m_qualityCombo->addItem(QStringLiteral("低（720p30，省资源）"), 0);
	m_fpsCombo = new QComboBox(videoGroup);
	m_fpsCombo->addItem(QStringLiteral("60 fps"), 60);
	m_fpsCombo->addItem(QStringLiteral("30 fps"), 30);
	m_encoderLabel = new QLabel(videoGroup);
	m_encoderLabel->setObjectName(QStringLiteral("hint"));

	m_cameraCheck = new QCheckBox(QStringLiteral("摄像头画中画"), videoGroup);
	m_cameraDeviceCombo = new QComboBox(videoGroup);
	m_cameraCornerCombo = new QComboBox(videoGroup);
	m_cameraCornerCombo->addItem(QStringLiteral("右下角"));
	m_cameraCornerCombo->addItem(QStringLiteral("左下角"));
	m_cameraCornerCombo->addItem(QStringLiteral("右上角"));
	m_cameraCornerCombo->addItem(QStringLiteral("左上角"));

	videoLayout->addWidget(new QLabel(QStringLiteral("质量档"), videoGroup));
	videoLayout->addWidget(m_qualityCombo);
	videoLayout->addWidget(new QLabel(QStringLiteral("帧率"), videoGroup));
	videoLayout->addWidget(m_fpsCombo);
	m_captureCursorCheck = new QCheckBox(QStringLiteral("录制鼠标光标"), videoGroup);
	videoLayout->addWidget(m_captureCursorCheck);
	videoLayout->addWidget(m_cameraCheck);
	videoLayout->addWidget(m_cameraDeviceCombo);
	videoLayout->addWidget(m_cameraCornerCombo);
	videoLayout->addWidget(m_encoderLabel);
	videoLayout->addStretch(1);

	columns->addWidget(audioGroup, 1);
	columns->addWidget(videoGroup, 1);
	root->addLayout(columns);

	// ---- 保存位置 ----
	auto *outputGroup = new QGroupBox(QStringLiteral("保存位置"), central);
	auto *outputBox = new QVBoxLayout(outputGroup);

	auto *outputLayout = new QHBoxLayout();
	m_outputDirEdit = new QLineEdit(outputGroup);
	m_outputDirEdit->setReadOnly(true);
	auto *browseButton = new QPushButton(QStringLiteral("浏览…"), outputGroup);
	auto *openButton = new QPushButton(QStringLiteral("打开"), outputGroup);
	outputLayout->addWidget(m_outputDirEdit, 1);
	outputLayout->addWidget(browseButton);
	outputLayout->addWidget(openButton);

	auto *countdownRow = new QHBoxLayout();
	countdownRow->addWidget(new QLabel(QStringLiteral("开始前倒计时"), outputGroup));
	m_countdownSpin = new QSpinBox(outputGroup);
	m_countdownSpin->setRange(0, 10);
	m_countdownSpin->setSuffix(QStringLiteral(" 秒"));
	countdownRow->addWidget(m_countdownSpin);
	countdownRow->addStretch(1);

	outputBox->addLayout(outputLayout);
	outputBox->addLayout(countdownRow);
	root->addWidget(outputGroup);

	// ---- 录制按钮与状态 ----
	m_recordButton = new QPushButton(QStringLiteral("开始录制"), central);
	m_recordButton->setObjectName(QStringLiteral("recordButton"));
	m_recordButton->setMinimumHeight(52);
	m_recordButton->setProperty("recording", false);
	m_recordButton->setCursor(Qt::PointingHandCursor);
	root->addWidget(m_recordButton);

	m_statusLabel = new QLabel(central);
	m_statusLabel->setObjectName(QStringLiteral("statusLabel"));
	m_statsLabel = new QLabel(central);
	m_statsLabel->setObjectName(QStringLiteral("statusLabel"));
	m_hintLabel = new QLabel(central);
	m_hintLabel->setObjectName(QStringLiteral("hint"));
	m_hintLabel->setWordWrap(true);

	root->addWidget(m_statusLabel);
	root->addWidget(m_statsLabel);
	root->addWidget(m_hintLabel);

	setCentralWidget(central);
	statusBar();

	// ---- 交互 ----
	connect(m_recordButton, &QPushButton::clicked, this, &MainWindow::onRecordClicked);
	connect(m_captureModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&MainWindow::onCaptureModeChanged);
	connect(m_targetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onTargetChanged);
	connect(m_pickRegionButton, &QPushButton::clicked, this, &MainWindow::onPickRegion);
	connect(m_refreshButton, &QPushButton::clicked, this, &MainWindow::onRefreshTargets);
	connect(browseButton, &QPushButton::clicked, this, &MainWindow::onBrowseOutputDir);
	connect(openButton, &QPushButton::clicked, this, &MainWindow::onOpenOutputDir);
	connect(m_cameraCheck, &QCheckBox::toggled, this, &MainWindow::onCameraToggled);
	connect(m_qualityCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
		const int quality = m_qualityCombo->itemData(index).toInt();
		AppConfig config = m_controller->config();
		config.applyQualityPreset(quality);
		m_controller->setConfig(config);
		// 质量档会改 fps，回填到界面
		const int fpsIndex = m_fpsCombo->findData(config.fps);
		if (fpsIndex >= 0)
			m_fpsCombo->setCurrentIndex(fpsIndex);
	});
}

void MainWindow::applyConfigToUi()
{
	const AppConfig config = m_controller->config();

	m_captureModeCombo->setCurrentIndex(static_cast<int>(config.captureMode));
	const int qualityIndex = m_qualityCombo->findData(config.quality);
	m_qualityCombo->setCurrentIndex(qualityIndex < 0 ? 1 : qualityIndex);

	const int fpsIndex = m_fpsCombo->findData(config.fps);
	m_fpsCombo->setCurrentIndex(fpsIndex >= 0 ? fpsIndex : 1);

	m_desktopAudioCheck->setChecked(config.desktopAudio);
	m_micCheck->setChecked(config.micAudio);
	m_captureCursorCheck->setChecked(config.captureCursor);

	reloadMonitors();
	reloadWindows();
	reloadAudioDevices();

	m_cameraCheck->setChecked(config.cameraEnabled);
	m_cameraCornerCombo->setCurrentIndex(static_cast<int>(config.cameraCorner));
	reloadCameraDevices();

	m_outputDirEdit->setText(m_controller->outputDirectory());
	m_countdownSpin->setValue(config.countdownSeconds);

	if (!config.windowId.empty()) {
		const int index = m_targetCombo->findData(toQString(config.windowId));
		if (index >= 0)
			m_targetCombo->setCurrentIndex(index);
	}

	m_encoderLabel->setText(m_controller->currentEncoder().isEmpty()
					? QStringLiteral("编码器在开始录制时自动选择")
					: QStringLiteral("当前编码器：%1").arg(m_controller->currentEncoder()));

	onCaptureModeChanged(m_captureModeCombo->currentIndex());
}

void MainWindow::reloadMonitors()
{
	m_targetCombo->clear();
	for (const auto &monitor : enums::monitors())
		m_targetCombo->addItem(toQString(monitor.label), toQString(monitor.value));
}

void MainWindow::reloadWindows()
{
	if (m_captureModeCombo->currentIndex() != static_cast<int>(CaptureMode::Window))
		return;

	m_targetCombo->clear();
	for (const auto &window : enums::windows())
		m_targetCombo->addItem(toQString(window.label), toQString(window.value));
}

void MainWindow::reloadAudioDevices()
{
	const AppConfig config = m_controller->config();

	m_desktopDeviceCombo->clear();
	for (const auto &device : enums::desktopAudioDevices())
		m_desktopDeviceCombo->addItem(toQString(device.label), toQString(device.value));
	if (m_desktopDeviceCombo->count() == 0)
		m_desktopDeviceCombo->addItem(QStringLiteral("默认设备"), QStringLiteral("default"));
	m_desktopDeviceCombo->setCurrentIndex(
		qMax(0, m_desktopDeviceCombo->findData(toQString(config.desktopDeviceId))));

	m_micDeviceCombo->clear();
	for (const auto &device : enums::microphoneDevices())
		m_micDeviceCombo->addItem(toQString(device.label), toQString(device.value));
	if (m_micDeviceCombo->count() == 0)
		m_micDeviceCombo->addItem(QStringLiteral("默认设备"), QStringLiteral("default"));
	m_micDeviceCombo->setCurrentIndex(qMax(0, m_micDeviceCombo->findData(toQString(config.micDeviceId))));
}

void MainWindow::reloadCameraDevices()
{
	const AppConfig config = m_controller->config();

	m_cameraDeviceCombo->clear();
	for (const auto &device : enums::cameraDevices())
		m_cameraDeviceCombo->addItem(toQString(device.label), toQString(device.value));

	if (m_cameraDeviceCombo->count() == 0) {
		m_cameraDeviceCombo->addItem(QStringLiteral("未检测到摄像头"), QString());
		m_cameraCheck->setEnabled(false);
	} else {
		const int index = m_cameraDeviceCombo->findData(toQString(config.cameraDeviceId));
		m_cameraDeviceCombo->setCurrentIndex(index >= 0 ? index : 0);
	}
}

void MainWindow::onCaptureModeChanged(int index)
{
	const auto mode = static_cast<CaptureMode>(index);

	switch (mode) {
	case CaptureMode::Window:
		m_targetCombo->setEnabled(true);
		m_targetCombo->setToolTip(QStringLiteral("选择一个正在显示的窗口"));
		m_pickRegionButton->setVisible(false);
		reloadWindows();
		break;
	case CaptureMode::Region: {
		m_targetCombo->setEnabled(false);
		m_pickRegionButton->setVisible(true);
		const AppConfig config = m_controller->config();
		if (config.region.valid) {
			m_targetCombo->clear();
			m_targetCombo->addItem(QStringLiteral("已选区：%1, %2 · %3×%4")
						       .arg(config.region.x)
						       .arg(config.region.y)
						       .arg(config.region.w)
						       .arg(config.region.h));
		} else {
			m_targetCombo->clear();
			m_targetCombo->addItem(QStringLiteral("还没有选择区域，请点击「选择区域…」"));
		}
		break;
	}
	case CaptureMode::Monitor:
	default:
		m_targetCombo->setEnabled(true);
		m_targetCombo->setToolTip(QStringLiteral("录制整块显示器"));
		m_pickRegionButton->setVisible(false);
		reloadMonitors();
		break;
	}

	updateHint();
}

void MainWindow::onTargetChanged(int index)
{
	if (index < 0)
		return;

	const auto mode = static_cast<CaptureMode>(m_captureModeCombo->currentIndex());
	if (mode == CaptureMode::Window) {
		AppConfig config = m_controller->config();
		config.windowId = m_targetCombo->itemData(index).toString().toStdString();
		config.windowName = m_targetCombo->itemText(index).toStdString();
		m_controller->setConfig(config);
	} else if (mode == CaptureMode::Monitor) {
		AppConfig config = m_controller->config();
		config.monitorIndex = index;
		m_controller->setConfig(config);
	}
}

void MainWindow::onRefreshTargets()
{
	const auto mode = static_cast<CaptureMode>(m_captureModeCombo->currentIndex());
	if (mode == CaptureMode::Window)
		reloadWindows();
	else if (mode == CaptureMode::Monitor)
		reloadMonitors();
	else
		reloadCameraDevices();

	statusBar()->showMessage(QStringLiteral("列表已刷新"), 2000);
}

void MainWindow::onPickRegion()
{
	RegionRect region;
	if (!m_controller->pickRegion(this, &region))
		return;

	AppConfig config = m_controller->config();
	config.captureMode = CaptureMode::Region;
	config.region = region;
	m_controller->setConfig(config);

	m_captureModeCombo->setCurrentIndex(static_cast<int>(CaptureMode::Region));
	onCaptureModeChanged(static_cast<int>(CaptureMode::Region));
	updateHint();
}

void MainWindow::onBrowseOutputDir()
{
	const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("选择保存目录"),
							      m_outputDirEdit->text());
	if (dir.isEmpty())
		return;

	AppConfig config = m_controller->config();
	config.outputDir = dir.toStdString();
	m_controller->setConfig(config);
	m_outputDirEdit->setText(dir);
}

void MainWindow::onOpenOutputDir()
{
	const QString dir = m_controller->outputDirectory();
	QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void MainWindow::onOpenSettings()
{
	if (m_controller->isRecording()) {
		QMessageBox::information(this, QStringLiteral("设置"),
					 QStringLiteral("录制过程中无法修改设置，请先停止录制。"));
		return;
	}

	SettingsDialog dialog(m_controller->config(), this);
	if (dialog.exec() != QDialog::Accepted)
		return;

	AppConfig config = dialog.result();
	m_controller->setConfig(config);
	m_controller->saveConfig();
	if (m_hotkeys)
		m_hotkeys->apply(config);

	applyConfigToUi();
	statusBar()->showMessage(QStringLiteral("设置已保存"), 3000);
}

void MainWindow::onCameraToggled(bool checked)
{
	m_cameraDeviceCombo->setEnabled(checked);
	m_cameraCornerCombo->setEnabled(checked);

	if (checked && m_cameraDeviceCombo->currentData().toString().isEmpty()) {
		QMessageBox::information(this, QStringLiteral("摄像头"),
					 QStringLiteral("没有检测到可用的摄像头设备。"));
		m_cameraCheck->setChecked(false);
	}
}

void MainWindow::onRecordClicked()
{
	if (m_controller->isRecording()) {
		m_controller->stopRecording();
		return;
	}

	if (m_controller->state() == RecState::Countdown) {
		m_controller->cancelCountdown();
		return;
	}

	syncConfigFromUi();

	// 录制前把主窗口收起来，避免把自己录进去
	if (isVisible()) {
		hide();
		m_tray->showInfoMessage(QStringLiteral("MiniRec"),
					QStringLiteral("录制已开始，程序已最小化到托盘（F9 停止）"));
	}

	m_controller->startRecording();
}

void MainWindow::syncConfigFromUi()
{
	AppConfig config = m_controller->config();

	config.captureMode = static_cast<CaptureMode>(m_captureModeCombo->currentIndex());
	if (config.captureMode == CaptureMode::Monitor)
		config.monitorIndex = qMax(0, m_targetCombo->currentIndex());
	if (config.captureMode == CaptureMode::Window) {
		config.windowId = m_targetCombo->currentData().toString().toStdString();
		config.windowName = m_targetCombo->currentText().toStdString();
	}

	config.desktopAudio = m_desktopAudioCheck->isChecked();
	config.desktopDeviceId = m_desktopDeviceCombo->currentData().toString().toStdString();
	config.micAudio = m_micCheck->isChecked();
	config.micDeviceId = m_micDeviceCombo->currentData().toString().toStdString();

	config.quality = m_qualityCombo->currentData().toInt();
	if (m_fpsCombo->currentData().isValid())
		config.fps = m_fpsCombo->currentData().toInt();

	config.captureCursor = m_captureCursorCheck->isChecked();

	config.cameraEnabled = m_cameraCheck->isChecked();
	if (!m_cameraDeviceCombo->currentData().toString().isEmpty()) {
		config.cameraDeviceId = m_cameraDeviceCombo->currentData().toString().toStdString();
		config.cameraDeviceName = m_cameraDeviceCombo->currentText().toStdString();
	}
	config.cameraCorner = static_cast<OverlayCorner>(qMax(0, m_cameraCornerCombo->currentIndex()));

	config.countdownSeconds = m_countdownSpin->value();

	m_controller->setConfig(config);
	m_controller->saveConfig();
}

void MainWindow::onStateChanged(RecState state)
{
	setRecordingUiState(state);

	if (state == RecState::Recording) {
		m_overlay->setStatusText(QString());
		m_overlay->startOnScreen(QGuiApplication::primaryScreen());

		// 让悬浮条不被录进画面（Windows 10 2004+ 支持）
		const HWND overlayHandle = reinterpret_cast<HWND>(m_overlay->winId());
		if (!SetWindowDisplayAffinity(overlayHandle, WDA_EXCLUDEFROMCAPTURE))
			logWarn("悬浮控制条无法从采集画面中排除（需要 Windows 10 2004 及以上）");
	} else if (state == RecState::Paused) {
		m_overlay->setPaused(true);
	} else if (state == RecState::Remuxing) {
		m_overlay->setStatusText(QStringLiteral("正在生成 MP4…"));
	} else if (state == RecState::Idle) {
		m_overlay->stop();

		if (m_quitting) {
			syncConfigFromUi();
			m_controller->saveConfig();
			qApp->quit();
		}
	} else if (state == RecState::Stopping) {
		m_overlay->setStatusText(QStringLiteral("正在停止…"));
	} else if (state == RecState::Countdown) {
		showAndActivate();
	}
}

void MainWindow::onCountdownTick(int secondsLeft)
{
	m_recordButton->setText(QStringLiteral("取消（%1 秒后开始）").arg(secondsLeft));
	m_statusLabel->setText(QStringLiteral("倒计时中，可随时取消…"));
}

void MainWindow::onStatsUpdated(qint64 elapsedMs, double droppedPercent, qulonglong bytesWritten)
{
	const QString elapsed = formatDuration(elapsedMs);
	const double megabytes = static_cast<double>(bytesWritten) / (1024.0 * 1024.0);

	m_statsLabel->setText(QStringLiteral("时长 %1　·　文件 %2 MB　·　丢帧 %3%")
				      .arg(elapsed)
				      .arg(QString::number(megabytes, 'f', 1))
				      .arg(QString::number(droppedPercent, 'f', 2)));

	if (m_tray)
		m_tray->setElapsedText(elapsed);
	if (m_overlay)
		m_overlay->setElapsedMs(elapsedMs);

	if (droppedPercent > 1.0)
		statusBar()->showMessage(QStringLiteral("检测到丢帧（%1%），建议降低帧率或分辨率")
						 .arg(QString::number(droppedPercent, 'f', 1)),
					 5000);
}

void MainWindow::onError(const QString &message)
{
	logError("界面错误提示：%s", message.toStdString().c_str());
	m_statusLabel->setText(message);
	QMessageBox::warning(this, QStringLiteral("MiniRec"), message);

	if (isHidden())
		showAndActivate();
}

void MainWindow::onRecordingFinished(const QString &mkvPath, const QString &mp4Path)
{
	const QString file = mp4Path.isEmpty() ? mkvPath : mp4Path;
	m_statusLabel->setText(QStringLiteral("录制完成"));
	if (m_tray)
		m_tray->showInfoMessage(QStringLiteral("录制完成"), QStringLiteral("已保存：%1").arg(file));

	statusBar()->showMessage(QStringLiteral("已保存 %1").arg(file), 8000);
}

void MainWindow::onEncoderChanged(const QString &encoderId, const QString &description)
{
	Q_UNUSED(description);
	m_encoderLabel->setText(QStringLiteral("当前编码器：%1").arg(encoderId));
}

void MainWindow::setRecordingUiState(RecState state)
{
	const bool recording = (state == RecState::Recording || state == RecState::Paused);
	const bool busy = (state != RecState::Idle);
	const bool countdown = (state == RecState::Countdown);

	QString text = QStringLiteral("开始录制");
	switch (state) {
	case RecState::Recording:
		text = QStringLiteral("停止录制");
		break;
	case RecState::Paused:
		text = QStringLiteral("停止录制（已暂停）");
		break;
	case RecState::Stopping:
		text = QStringLiteral("正在停止…");
		break;
	case RecState::Remuxing:
		text = QStringLiteral("正在生成 MP4…");
		break;
	case RecState::Starting:
		text = QStringLiteral("正在启动…");
		break;
	default:
		break;
	}
	if (!countdown)
		m_recordButton->setText(text);

	m_recordButton->setProperty("recording", recording);
	m_recordButton->style()->unpolish(m_recordButton);
	m_recordButton->style()->polish(m_recordButton);
	m_recordButton->setEnabled(!(state == RecState::Starting || state == RecState::Stopping ||
				     state == RecState::Remuxing));

	const bool enabled = !busy;
	const bool regionMode = (m_captureModeCombo->currentIndex() == static_cast<int>(CaptureMode::Region));

	m_captureModeCombo->setEnabled(enabled);
	m_targetCombo->setEnabled(enabled && !regionMode);
	m_pickRegionButton->setEnabled(enabled);
	m_refreshButton->setEnabled(enabled);
	m_desktopAudioCheck->setEnabled(enabled);
	m_desktopDeviceCombo->setEnabled(enabled && m_desktopAudioCheck->isChecked());
	m_micCheck->setEnabled(enabled);
	m_micDeviceCombo->setEnabled(enabled && m_micCheck->isChecked());
	m_qualityCombo->setEnabled(enabled);
	m_fpsCombo->setEnabled(enabled);
	m_cameraCheck->setEnabled(enabled);
	m_cameraDeviceCombo->setEnabled(enabled && m_cameraCheck->isChecked());
	m_cameraCornerCombo->setEnabled(enabled && m_cameraCheck->isChecked());
	m_countdownSpin->setEnabled(enabled);

	if (m_tray) {
		m_tray->setRecordingState(recording, state == RecState::Paused);
		if (!recording)
			m_tray->setElapsedText(QString());
	}

	m_statusLabel->setText(QStringLiteral("状态：%1").arg(QString::fromUtf8(stateText(state))));
	if (!recording)
		m_statsLabel->clear();
}

void MainWindow::updateHint()
{
	const AppConfig config = m_controller->config();
	QStringList hints;

	const QString startKey = m_hotkeys ? HotkeyManager::describe(config.hkStartStop) : QStringLiteral("F9");
	const QString pauseKey = m_hotkeys ? HotkeyManager::describe(config.hkPause) : QStringLiteral("F10");
	hints << QStringLiteral("全局热键：%1 开始/停止，%2 暂停/继续").arg(startKey, pauseKey);

	if (config.captureMode == CaptureMode::Window && config.mouseFxEnabled)
		hints << QStringLiteral("窗口录制时不支持鼠标点击特效");
	if (config.mouseFxEnabled && config.captureMode != CaptureMode::Window)
		hints << QStringLiteral("鼠标点击特效已开启（录制时会在点击处显示圆环）");
	if (!enums::hasSourceType("dshow_input"))
		hints << QStringLiteral("未打包 win-dshow 插件，摄像头画中画不可用");
	hints << QStringLiteral("录制中会先写入 MKV，停止后自动生成 MP4，异常断电也不会丢文件");

	m_hintLabel->setText(hints.join(QStringLiteral("；")) + QStringLiteral("。"));
}

void MainWindow::showAndActivate()
{
	showNormal();
	raise();
	activateWindow();
}

QString MainWindow::formatDuration(qint64 elapsedMs)
{
	const qint64 totalSeconds = elapsedMs / 1000;
	const qint64 hours = totalSeconds / 3600;
	const qint64 minutes = (totalSeconds % 3600) / 60;
	const qint64 seconds = totalSeconds % 60;
	return QStringLiteral("%1:%2:%3")
		.arg(hours, 2, 10, QLatin1Char('0'))
		.arg(minutes, 2, 10, QLatin1Char('0'))
		.arg(seconds, 2, 10, QLatin1Char('0'));
}

void MainWindow::closeEvent(QCloseEvent *event)
{
	if (m_quitting) {
		event->accept();
		return;
	}

	// 关闭窗口 = 收起到托盘（与主流录屏软件一致）
	event->ignore();
	hide();
	if (m_tray)
		m_tray->showInfoMessage(QStringLiteral("MiniRec"),
					QStringLiteral("程序仍在托盘中运行，右键图标可退出"));
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
#else
bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, long *result)
#endif
{
	Q_UNUSED(eventType);
	Q_UNUSED(result);

	auto *msg = static_cast<MSG *>(message);
	if (msg && msg->message == SingleInstance::activationMessage()) {
		showAndActivate();
		return true;
	}
	return false;
}

} // namespace minirec
