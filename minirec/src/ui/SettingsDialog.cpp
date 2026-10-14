#include "ui/SettingsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSize>
#include <QSpinBox>
#include <QVBoxLayout>

#include "app/HotkeyManager.h"
#include "core/EnumHelpers.h"

namespace minirec {
namespace {

// libobs 的 INTERACT_* 修饰键标志，与 AppConfig / HotkeyManager 中的取值保持一致
constexpr uint32_t kInteractShift = 1u << 1;
constexpr uint32_t kInteractControl = 1u << 2;
constexpr uint32_t kInteractAlt = 1u << 3;
constexpr uint32_t kInteractCommand = 1u << 7;

QLabel *makeHintLabel(const QString &text, QWidget *parent)
{
	auto *label = new QLabel(text, parent);
	label->setObjectName(QStringLiteral("hint"));
	label->setWordWrap(true);
	return label;
}

} // namespace

SettingsDialog::SettingsDialog(const AppConfig &config, QWidget *parent)
	: QDialog(parent), m_config(config)
{
	setWindowTitle(QStringLiteral("设置"));
	setModal(true);
	setMinimumWidth(540);

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(16, 16, 16, 16);
	layout->setSpacing(12);

	layout->addWidget(buildVideoGroup());
	layout->addWidget(buildEncoderGroup());
	layout->addWidget(buildOutputGroup());
	layout->addWidget(buildMouseFxGroup());
	layout->addWidget(buildCameraGroup());
	layout->addWidget(buildHotkeyGroup());
	layout->addStretch(1);

	auto *buttons = new QDialogButtonBox(this);
	buttons->addButton(QStringLiteral("保存"), QDialogButtonBox::AcceptRole);
	buttons->addButton(QStringLiteral("取消"), QDialogButtonBox::RejectRole);
	connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::onSave);
	connect(buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);
	layout->addWidget(buttons);
}

QGroupBox *SettingsDialog::buildVideoGroup()
{
	auto *group = new QGroupBox(QStringLiteral("视频"), this);
	auto *form = new QFormLayout(group);

	m_fpsCombo = new QComboBox(group);
	m_fpsCombo->addItem(QStringLiteral("30 fps"), 30);
	m_fpsCombo->addItem(QStringLiteral("60 fps"), 60);
	m_fpsCombo->addItem(QStringLiteral("120 fps"), 120);

	m_resolutionCombo = new QComboBox(group);
	m_resolutionCombo->addItem(QStringLiteral("跟随屏幕（推荐）"), QSize(0, 0));
	m_resolutionCombo->addItem(QStringLiteral("1920×1080"), QSize(1920, 1080));
	m_resolutionCombo->addItem(QStringLiteral("2560×1440"), QSize(2560, 1440));
	m_resolutionCombo->addItem(QStringLiteral("3840×2160"), QSize(3840, 2160));

	m_qualityCombo = new QComboBox(group);
	m_qualityCombo->addItem(QStringLiteral("低"), 0);
	m_qualityCombo->addItem(QStringLiteral("中"), 1);
	m_qualityCombo->addItem(QStringLiteral("高"), 2);
	m_qualityCombo->addItem(QStringLiteral("原始（跟随屏幕）"), 3);

	form->addRow(QStringLiteral("帧率"), m_fpsCombo);
	form->addRow(QStringLiteral("输出分辨率"), m_resolutionCombo);
	form->addRow(QStringLiteral("质量档"), m_qualityCombo);

	connect(m_qualityCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&SettingsDialog::onQualityChanged);

	syncVideoWidgetsFromConfig();
	return group;
}

QGroupBox *SettingsDialog::buildEncoderGroup()
{
	auto *group = new QGroupBox(QStringLiteral("编码器"), this);
	auto *form = new QFormLayout(group);

	m_encoderCombo = new QComboBox(group);
	m_encoderCombo->addItem(QStringLiteral("自动（优先硬件编码）"), QString());
	for (const std::string &id : enums::encoderTypes())
		m_encoderCombo->addItem(QString::fromStdString(id), QString::fromStdString(id));

	const QString current = QString::fromStdString(m_config.encoderOverride);
	int index = current.isEmpty() ? 0 : m_encoderCombo->findData(current);
	if (index < 0) {
		// 配置指定的编码器当前未注册：保留原值，避免被静默改成别的
		m_encoderCombo->addItem(QStringLiteral("%1（当前不可用）").arg(current), current);
		index = m_encoderCombo->count() - 1;
	}
	m_encoderCombo->setCurrentIndex(index);

	form->addRow(QStringLiteral("编码器"), m_encoderCombo);

	m_encoderHint = makeHintLabel(QString(), group);
	form->addRow(m_encoderHint);

	connect(m_encoderCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		[this]() { updateEncoderHint(); });

	updateEncoderHint();
	return group;
}

QGroupBox *SettingsDialog::buildOutputGroup()
{
	auto *group = new QGroupBox(QStringLiteral("输出"), this);
	auto *form = new QFormLayout(group);

	m_outputDirEdit = new QLineEdit(group);
	m_outputDirEdit->setReadOnly(true);
	m_outputDirEdit->setText(QString::fromStdString(m_config.outputDir));

	auto *browseButton = new QPushButton(QStringLiteral("浏览…"), group);
	connect(browseButton, &QPushButton::clicked, this, &SettingsDialog::onBrowseOutputDir);

	auto *dirRow = new QWidget(group);
	auto *dirLayout = new QHBoxLayout(dirRow);
	dirLayout->setContentsMargins(0, 0, 0, 0);
	dirLayout->setSpacing(8);
	dirLayout->addWidget(m_outputDirEdit, 1);
	dirLayout->addWidget(browseButton);
	form->addRow(QStringLiteral("保存目录"), dirRow);

	m_autoRemuxCheck = new QCheckBox(QStringLiteral("录制结束后自动生成 MP4"), group);
	m_autoRemuxCheck->setChecked(m_config.autoRemuxMp4);
	form->addRow(m_autoRemuxCheck);

	m_keepMkvCheck = new QCheckBox(QStringLiteral("保留原始 MKV 文件"), group);
	m_keepMkvCheck->setChecked(m_config.keepMkv);
	m_keepMkvCheck->setEnabled(m_config.autoRemuxMp4);
	form->addRow(m_keepMkvCheck);

	connect(m_autoRemuxCheck, &QCheckBox::toggled, this, [this](bool checked) {
		if (!checked)
			m_keepMkvCheck->setChecked(true); // 不生成 MP4 时 MKV 是唯一产物
		m_keepMkvCheck->setEnabled(checked);
	});

	m_countdownSpin = new QSpinBox(group);
	m_countdownSpin->setRange(0, 10);
	m_countdownSpin->setSuffix(QStringLiteral(" 秒"));
	m_countdownSpin->setValue(qBound(0, m_config.countdownSeconds, 10));
	form->addRow(QStringLiteral("录制前倒计时"), m_countdownSpin);

	return group;
}

QGroupBox *SettingsDialog::buildMouseFxGroup()
{
	auto *group = new QGroupBox(QStringLiteral("鼠标特效"), this);
	auto *form = new QFormLayout(group);

	m_mouseFxCheck = new QCheckBox(QStringLiteral("显示鼠标点击特效"), group);
	m_mouseFxCheck->setChecked(m_config.mouseFxEnabled);
	form->addRow(m_mouseFxCheck);

	m_mouseFxRadiusSpin = new QSpinBox(group);
	m_mouseFxRadiusSpin->setRange(16, 160);
	m_mouseFxRadiusSpin->setSuffix(QStringLiteral(" 像素"));
	m_mouseFxRadiusSpin->setValue(qBound(16, m_config.mouseFxRadius, 160));
	form->addRow(QStringLiteral("圆环半径"), m_mouseFxRadiusSpin);

	m_mouseFxDurationSpin = new QSpinBox(group);
	m_mouseFxDurationSpin->setRange(200, 2000);
	m_mouseFxDurationSpin->setSingleStep(50);
	m_mouseFxDurationSpin->setSuffix(QStringLiteral(" 毫秒"));
	m_mouseFxDurationSpin->setValue(qBound(200, m_config.mouseFxDurationMs, 2000));
	form->addRow(QStringLiteral("持续时间"), m_mouseFxDurationSpin);

	form->addRow(makeHintLabel(QStringLiteral("注意：窗口采集模式下不支持鼠标点击特效。"), group));
	return group;
}

QGroupBox *SettingsDialog::buildCameraGroup()
{
	auto *group = new QGroupBox(QStringLiteral("摄像头画中画"), this);
	auto *form = new QFormLayout(group);

	m_cameraWidthSpin = new QSpinBox(group);
	m_cameraWidthSpin->setRange(160, 800);
	m_cameraWidthSpin->setSingleStep(20);
	m_cameraWidthSpin->setSuffix(QStringLiteral(" 像素"));
	m_cameraWidthSpin->setValue(qBound(160, m_config.cameraWidth, 800));
	form->addRow(QStringLiteral("画中画宽度"), m_cameraWidthSpin);

	m_cameraCornerCombo = new QComboBox(group);
	m_cameraCornerCombo->addItem(QStringLiteral("左上"), static_cast<int>(OverlayCorner::TopLeft));
	m_cameraCornerCombo->addItem(QStringLiteral("右上"), static_cast<int>(OverlayCorner::TopRight));
	m_cameraCornerCombo->addItem(QStringLiteral("左下"), static_cast<int>(OverlayCorner::BottomLeft));
	m_cameraCornerCombo->addItem(QStringLiteral("右下"), static_cast<int>(OverlayCorner::BottomRight));

	const int cornerIndex =
		m_cameraCornerCombo->findData(static_cast<int>(m_config.cameraCorner));
	m_cameraCornerCombo->setCurrentIndex(cornerIndex < 0 ? 3 : cornerIndex);
	form->addRow(QStringLiteral("位置"), m_cameraCornerCombo);

	form->addRow(makeHintLabel(QStringLiteral("摄像头设备在主窗口选择。"), group));
	return group;
}

QGroupBox *SettingsDialog::buildHotkeyGroup()
{
	auto *group = new QGroupBox(QStringLiteral("热键"), this);
	auto *form = new QFormLayout(group);

	form->addRow(QStringLiteral("开始 / 停止录制"),
		     createHotkeyRow(&m_startHotkeyEdit, &m_config.hkStartStop, group));
	form->addRow(QStringLiteral("暂停 / 继续录制"),
		     createHotkeyRow(&m_pauseHotkeyEdit, &m_config.hkPause, group));

	form->addRow(makeHintLabel(QStringLiteral("点击「修改」后按下新的组合键，按 Esc 取消。"), group));
	return group;
}

QWidget *SettingsDialog::createHotkeyRow(QLineEdit **edit, HotkeyBinding *binding, QWidget *parent)
{
	auto *row = new QWidget(parent);
	auto *layout = new QHBoxLayout(row);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(8);

	*edit = new QLineEdit(row);
	(*edit)->setReadOnly(true);
	(*edit)->setText(HotkeyManager::describe(*binding));

	auto *button = new QPushButton(QStringLiteral("修改"), row);

	layout->addWidget(*edit, 1);
	layout->addWidget(button);

	connect(button, &QPushButton::clicked, this, [this, edit, binding]() { beginCapture(*edit, binding); });
	return row;
}

void SettingsDialog::syncVideoWidgetsFromConfig()
{
	// 质量档的联动回填不应再次触发 onQualityChanged
	const QSignalBlocker blocker(m_qualityCombo);

	int fpsIndex = m_fpsCombo->findData(m_config.fps);
	if (fpsIndex < 0) {
		m_fpsCombo->addItem(QStringLiteral("%1 fps").arg(m_config.fps), m_config.fps);
		fpsIndex = m_fpsCombo->count() - 1;
	}
	m_fpsCombo->setCurrentIndex(fpsIndex);

	const QSize size(m_config.outputWidth, m_config.outputHeight);
	int resolutionIndex = m_resolutionCombo->findData(size);
	if (resolutionIndex < 0) {
		// 命中不了预设（含质量档带来的 720p）就补一条，保证界面与配置一致
		m_resolutionCombo->addItem(
			QStringLiteral("自定义（%1×%2）").arg(size.width()).arg(size.height()), size);
		resolutionIndex = m_resolutionCombo->count() - 1;
	}
	m_resolutionCombo->setCurrentIndex(resolutionIndex);

	const int qualityIndex = m_qualityCombo->findData(m_config.quality);
	m_qualityCombo->setCurrentIndex(qualityIndex < 0 ? 1 : qualityIndex);
}

void SettingsDialog::onQualityChanged(int index)
{
	if (index < 0)
		return;

	// 质量档给出基础帧率与分辨率，用户仍可在上面两项显式覆盖（保存时以后者为准）
	m_config.applyQualityPreset(m_qualityCombo->itemData(index).toInt());
	syncVideoWidgetsFromConfig();
}

void SettingsDialog::updateEncoderHint()
{
	const QString id = m_encoderCombo->currentData().toString();

	if (id.isEmpty())
		m_encoderHint->setText(QStringLiteral(
			"当前：自动选择（NVENC → AMF → QSV → x264）。硬件编码不可用时会自动回退到 x264。"));
	else
		m_encoderHint->setText(
			QStringLiteral("当前：%1。固定指定编码器时不回退；硬件编码不可用时会自动回退到 "
				       "x264 仅在「自动」模式下生效。")
				.arg(id));
}

void SettingsDialog::onBrowseOutputDir()
{
	const QString dir = QFileDialog::getExistingDirectory(
		this, QStringLiteral("选择录制保存目录"), m_outputDirEdit->text(), QFileDialog::ShowDirsOnly);

	if (!dir.isEmpty())
		m_outputDirEdit->setText(QDir::toNativeSeparators(dir));
}

void SettingsDialog::onSave()
{
	// 丢弃未完成的捕获，保留原热键
	if (m_capturingEdit)
		endCapture();

	// ---- 视频 ----
	const int quality = m_qualityCombo->currentData().toInt();
	m_config.applyQualityPreset(quality); // 先套用质量档的基础参数
	m_config.fps = m_fpsCombo->currentData().toInt();

	const QSize resolution = m_resolutionCombo->currentData().toSize();
	if (resolution.width() > 0 && resolution.height() > 0) {
		m_config.outputWidth = resolution.width();
		m_config.outputHeight = resolution.height();
	} else {
		// 跟随屏幕：保存 0，由 computeOutputSize 在录制时解析为采集分辨率
		m_config.outputWidth = 0;
		m_config.outputHeight = 0;
	}

	// ---- 编码器 ----
	m_config.encoderOverride = m_encoderCombo->currentData().toString().toStdString();

	// ---- 输出 ----
	m_config.outputDir = m_outputDirEdit->text().toStdString();
	m_config.autoRemuxMp4 = m_autoRemuxCheck->isChecked();
	m_config.keepMkv = m_keepMkvCheck->isChecked();
	m_config.countdownSeconds = m_countdownSpin->value();

	// ---- 鼠标特效 ----
	m_config.mouseFxEnabled = m_mouseFxCheck->isChecked();
	m_config.mouseFxRadius = m_mouseFxRadiusSpin->value();
	m_config.mouseFxDurationMs = m_mouseFxDurationSpin->value();

	// ---- 摄像头画中画（设备选择在主窗口，这里不动 cameraDeviceId / cameraDeviceName）----
	m_config.cameraWidth = m_cameraWidthSpin->value();
	m_config.cameraCorner = static_cast<OverlayCorner>(m_cameraCornerCombo->currentData().toInt());

	// 热键在捕获成功时已写入 m_config，这里不再覆盖

	// 不落盘：由 MainWindow 统一保存
	accept();
}

void SettingsDialog::beginCapture(QLineEdit *edit, HotkeyBinding *binding)
{
	if (!edit || !binding)
		return;

	if (m_capturingEdit)
		endCapture(); // 结束上一次未完成的捕获

	m_capturingEdit = edit;
	m_captureBinding = binding;

	edit->setText(QStringLiteral("请按下新的组合键（Esc 取消）"));
	edit->installEventFilter(this);
	edit->setFocus(Qt::OtherFocusReason);
	edit->grabKeyboard();
}

void SettingsDialog::endCapture()
{
	if (!m_capturingEdit)
		return;

	QLineEdit *edit = m_capturingEdit;
	m_capturingEdit = nullptr;

	edit->releaseKeyboard();
	edit->removeEventFilter(this);

	if (m_captureBinding) {
		edit->setText(HotkeyManager::describe(*m_captureBinding));
		m_captureBinding = nullptr;
	}
}

bool SettingsDialog::eventFilter(QObject *watched, QEvent *event)
{
	if (watched != m_capturingEdit || event->type() != QEvent::KeyPress)
		return QDialog::eventFilter(watched, event);

	auto *keyEvent = static_cast<QKeyEvent *>(event);
	const int key = keyEvent->key();

	if (key == Qt::Key_Escape) {
		endCapture();
		return true;
	}

	// 单独的修饰键不算热键，继续等待主键
	if (key == Qt::Key_Shift || key == Qt::Key_Control || key == Qt::Key_Alt ||
	    key == Qt::Key_Meta || key == Qt::Key_AltGr)
		return true;

	const quint32 vk = keyEvent->nativeVirtualKey();
	if (vk == 0)
		return true;

	const Qt::KeyboardModifiers modifiers = keyEvent->modifiers();
	uint32_t flags = 0;
	if (modifiers & Qt::ShiftModifier)
		flags |= kInteractShift;
	if (modifiers & Qt::ControlModifier)
		flags |= kInteractControl;
	if (modifiers & Qt::AltModifier)
		flags |= kInteractAlt;
	if (modifiers & Qt::MetaModifier)
		flags |= kInteractCommand;

	if (m_captureBinding) {
		m_captureBinding->vk = static_cast<int>(vk);
		m_captureBinding->modifiers = flags;
	}

	endCapture();
	return true;
}

} // namespace minirec
