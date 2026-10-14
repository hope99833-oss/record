#pragma once

#include <QDialog>

#include "app/AppConfig.h"

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QWidget;

namespace minirec {

// 设置对话框：只负责收集界面上的取值。
// 点击「保存」后由调用方读取 result() 并决定何时落盘；点击「取消」时调用方应忽略结果。
class SettingsDialog : public QDialog {
	Q_OBJECT
public:
	explicit SettingsDialog(const AppConfig &config, QWidget *parent = nullptr);

	AppConfig result() const { return m_config; }

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	QGroupBox *buildVideoGroup();
	QGroupBox *buildEncoderGroup();
	QGroupBox *buildOutputGroup();
	QGroupBox *buildMouseFxGroup();
	QGroupBox *buildCameraGroup();
	QGroupBox *buildHotkeyGroup();

	QWidget *createHotkeyRow(QLineEdit **edit, HotkeyBinding *binding, QWidget *parent);

	// 把 m_config 的帧率/分辨率/质量档回填到下拉框
	void syncVideoWidgetsFromConfig();
	void onQualityChanged(int index);
	void updateEncoderHint();
	void onBrowseOutputDir();
	void onSave();

	// 热键捕获：抓取下一个按键写入 binding，Esc 取消
	void beginCapture(QLineEdit *edit, HotkeyBinding *binding);
	void endCapture();

	AppConfig m_config;

	// 视频
	QComboBox *m_fpsCombo = nullptr;
	QComboBox *m_resolutionCombo = nullptr;
	QComboBox *m_qualityCombo = nullptr;

	// 编码器
	QComboBox *m_encoderCombo = nullptr;
	QLabel *m_encoderHint = nullptr;

	// 输出
	QLineEdit *m_outputDirEdit = nullptr;
	QCheckBox *m_autoRemuxCheck = nullptr;
	QCheckBox *m_keepMkvCheck = nullptr;
	QSpinBox *m_countdownSpin = nullptr;

	// 鼠标特效
	QCheckBox *m_mouseFxCheck = nullptr;
	QSpinBox *m_mouseFxRadiusSpin = nullptr;
	QSpinBox *m_mouseFxDurationSpin = nullptr;

	// 摄像头画中画（设备选择在主窗口，这里只提供宽度与位置）
	QSpinBox *m_cameraWidthSpin = nullptr;
	QComboBox *m_cameraCornerCombo = nullptr;

	// 热键
	QLineEdit *m_startHotkeyEdit = nullptr;
	QLineEdit *m_pauseHotkeyEdit = nullptr;
	QLineEdit *m_capturingEdit = nullptr;
	HotkeyBinding *m_captureBinding = nullptr;
};

} // namespace minirec
