#include "app/TrayController.h"

#include <QAction>
#include <QMenu>
#include <QSystemTrayIcon>

#include "ui/Theme.h"

namespace minirec {

TrayController::TrayController(QObject *parent) : QObject(parent)
{
	m_menu = new QMenu();

	m_toggleAction = m_menu->addAction(QStringLiteral("开始录制"));
	connect(m_toggleAction, &QAction::triggered, this, &TrayController::toggleRecordingRequested);

	m_pauseAction = m_menu->addAction(QStringLiteral("暂停"));
	connect(m_pauseAction, &QAction::triggered, this, &TrayController::togglePauseRequested);

	m_showAction = m_menu->addAction(QStringLiteral("显示主窗口"));
	connect(m_showAction, &QAction::triggered, this, &TrayController::showWindowRequested);

	m_openDirAction = m_menu->addAction(QStringLiteral("打开录制目录"));
	connect(m_openDirAction, &QAction::triggered, this, &TrayController::openOutputFolderRequested);

	m_menu->addSeparator();

	m_settingsAction = m_menu->addAction(QStringLiteral("设置…"));
	connect(m_settingsAction, &QAction::triggered, this, &TrayController::openSettingsRequested);

	m_quitAction = m_menu->addAction(QStringLiteral("退出"));
	connect(m_quitAction, &QAction::triggered, this, &TrayController::quitRequested);

	m_tray = new QSystemTrayIcon(this);
	m_tray->setContextMenu(m_menu);
	connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
		if (reason == QSystemTrayIcon::DoubleClick)
			emit showWindowRequested();
	});
	m_tray->show();

	refreshUi();
}

TrayController::~TrayController()
{
	if (m_tray) {
		m_tray->hide();
		// 菜单不归托盘所有，先解除引用再手动释放
		m_tray->setContextMenu(nullptr);
	}

	delete m_menu;
	m_menu = nullptr;
	m_toggleAction = nullptr;
	m_pauseAction = nullptr;
	m_showAction = nullptr;
	m_openDirAction = nullptr;
	m_settingsAction = nullptr;
	m_quitAction = nullptr;
}

void TrayController::setRecordingState(bool recording, bool paused)
{
	m_recording = recording;
	m_paused = paused;
	refreshUi();
}

void TrayController::setElapsedText(const QString &text)
{
	m_elapsedText = text;
	refreshUi();
}

void TrayController::showInfoMessage(const QString &title, const QString &message)
{
	if (!m_tray)
		return;

	m_tray->showMessage(title, message, QSystemTrayIcon::Information, 4000);
}

void TrayController::refreshUi()
{
	if (!m_tray)
		return;

	m_tray->setIcon(theme::trayIcon(m_recording));

	if (m_toggleAction)
		m_toggleAction->setText(m_recording ? QStringLiteral("停止录制") : QStringLiteral("开始录制"));

	if (m_pauseAction) {
		m_pauseAction->setEnabled(m_recording);
		m_pauseAction->setText(m_paused ? QStringLiteral("继续") : QStringLiteral("暂停"));
	}

	QString tip = QStringLiteral("MiniRec");
	if (m_recording) {
		tip += m_paused ? QStringLiteral("\n已暂停") : QStringLiteral("\n录制中");
		if (!m_elapsedText.isEmpty())
			tip += QStringLiteral(" · %1").arg(m_elapsedText);
	}
	m_tray->setToolTip(tip);
}

} // namespace minirec
