#pragma once

#include <QObject>
#include <QString>

class QAction;
class QMenu;
class QSystemTrayIcon;

namespace minirec {

// 系统托盘：图标状态、右键菜单与气泡提示。
// 本类不直接操作录制，所有动作以信号形式抛出，由 MainWindow 决定如何响应。
class TrayController : public QObject {
	Q_OBJECT
public:
	explicit TrayController(QObject *parent = nullptr);
	~TrayController() override;

	// 切换托盘图标与菜单项可用性/文案
	void setRecordingState(bool recording, bool paused);

	// 录制时长文本（如 "00:12:30"），显示在 tooltip 中
	void setElapsedText(const QString &text);

	// 托盘气泡提示
	void showInfoMessage(const QString &title, const QString &message);

signals:
	void toggleRecordingRequested();
	void togglePauseRequested();
	void showWindowRequested();
	void openOutputFolderRequested();
	void openSettingsRequested();
	void quitRequested();

private:
	void refreshUi();

	QSystemTrayIcon *m_tray = nullptr;
	QMenu *m_menu = nullptr;
	QAction *m_toggleAction = nullptr;
	QAction *m_pauseAction = nullptr;
	QAction *m_showAction = nullptr;
	QAction *m_openDirAction = nullptr;
	QAction *m_settingsAction = nullptr;
	QAction *m_quitAction = nullptr;

	bool m_recording = false;
	bool m_paused = false;
	QString m_elapsedText;
};

} // namespace minirec
