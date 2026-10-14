#pragma once

#include <QMainWindow>

#include <memory>

#include "app/AppConfig.h"
#include "core/RecorderController.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

namespace minirec {

class HotkeyManager;
class TrayController;
class RecordingOverlay;

// 主窗口：一屏完成所有选择 + 一个大按钮开始录制，交互对齐主流录屏软件
class MainWindow : public QMainWindow {
	Q_OBJECT
public:
	MainWindow(RecorderController *controller, HotkeyManager *hotkeys, QWidget *parent = nullptr);
	~MainWindow() override;

protected:
	void closeEvent(QCloseEvent *event) override;
	// Qt6 把结果参数从 long* 改成了 qintptr*，需要按版本区分才能构成重写
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
#else
	bool nativeEvent(const QByteArray &eventType, void *message, long *result) override;
#endif

private slots:
	void onRecordClicked();
	void onCaptureModeChanged(int index);
	void onTargetChanged(int index);
	void onPickRegion();
	void onRefreshTargets();
	void onBrowseOutputDir();
	void onOpenOutputDir();
	void onOpenSettings();
	void onCameraToggled(bool checked);

	void onStateChanged(minirec::RecState state);
	void onCountdownTick(int secondsLeft);
	void onStatsUpdated(qint64 elapsedMs, double droppedPercent, qulonglong bytesWritten);
	void onError(const QString &message);
	void onRecordingFinished(const QString &mkvPath, const QString &mp4Path);
	void onEncoderChanged(const QString &encoderId, const QString &description);

private:
	void buildUi();
	void reloadMonitors();
	void reloadWindows();
	void reloadAudioDevices();
	void reloadCameraDevices();
	void syncConfigFromUi();
	void applyConfigToUi();
	void setRecordingUiState(RecState state);
	void updateHint();
	void showAndActivate();
	static QString formatDuration(qint64 elapsedMs);

	RecorderController *m_controller = nullptr;
	HotkeyManager *m_hotkeys = nullptr;
	std::unique_ptr<TrayController> m_tray;
	std::unique_ptr<RecordingOverlay> m_overlay;
	bool m_quitting = false;

	// 录制目标
	QComboBox *m_captureModeCombo = nullptr;
	QComboBox *m_targetCombo = nullptr;
	QPushButton *m_pickRegionButton = nullptr;
	QPushButton *m_refreshButton = nullptr;

	// 音频
	QCheckBox *m_desktopAudioCheck = nullptr;
	QComboBox *m_desktopDeviceCombo = nullptr;
	QCheckBox *m_micCheck = nullptr;
	QComboBox *m_micDeviceCombo = nullptr;

	// 视频与质量
	QComboBox *m_qualityCombo = nullptr;
	QComboBox *m_fpsCombo = nullptr;
	QCheckBox *m_captureCursorCheck = nullptr;
	QLabel *m_encoderLabel = nullptr;

	// 摄像头
	QCheckBox *m_cameraCheck = nullptr;
	QComboBox *m_cameraDeviceCombo = nullptr;
	QComboBox *m_cameraCornerCombo = nullptr;

	// 输出
	QLineEdit *m_outputDirEdit = nullptr;
	QSpinBox *m_countdownSpin = nullptr;

	// 状态区
	QPushButton *m_recordButton = nullptr;
	QLabel *m_statusLabel = nullptr;
	QLabel *m_statsLabel = nullptr;
	QLabel *m_hintLabel = nullptr;
};

} // namespace minirec
