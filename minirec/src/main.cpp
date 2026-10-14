#include <QApplication>
#include <QMessageBox>

#include <string>

#include "app/AppConfig.h"
#include "app/HotkeyManager.h"
#include "app/SingleInstance.h"
#include "core/Logging.h"
#include "core/RecorderController.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

int main(int argc, char *argv[])
{
	// 这之后每一步都留痕到 <exe 同级>/minirec-startup.log：
	// GUI 程序没有控制台，出问题时只能靠这个定位卡在哪一步。
	minirec::traceStartup("进入 main");

	QApplication app(argc, argv);
	// 只设置 applicationName（不设置 organizationName），
	// 这样 QStandardPaths::AppDataLocation = %APPDATA%\MiniRec，与 libobs 配置目录保持一致
	QApplication::setApplicationName(QStringLiteral("MiniRec"));
	QApplication::setApplicationDisplayName(QStringLiteral("MiniRec 录屏"));
	QApplication::setQuitOnLastWindowClosed(false); // 关窗口只收起到托盘
	minirec::traceStartup("QApplication 创建完成");

	minirec::theme::apply(app);

	// 单实例：第二次启动时唤醒已有窗口后直接退出
	minirec::SingleInstance instance;
	if (!instance.acquire()) {
		minirec::traceStartup("检测到已有实例，本次启动退出");
		return 0;
	}
	minirec::traceStartup("单实例检查通过");

	minirec::AppConfig config =
		minirec::AppConfig::load(minirec::RecorderController::configFilePath().toStdString());
	minirec::traceStartup("配置加载完成");

	minirec::RecorderController controller;
	std::string error;
	if (!controller.initialize(config, &error)) {
		minirec::traceStartup(("libobs 初始化失败：" + error).c_str());
		QMessageBox::critical(nullptr, QStringLiteral("MiniRec 启动失败"),
				      QString::fromStdString(error));
		return 1;
	}
	minirec::traceStartup("libobs 初始化完成");

	minirec::HotkeyManager hotkeys;
	hotkeys.apply(controller.config());

	minirec::MainWindow window(&controller, &hotkeys);
	window.show();
	minirec::traceStartup("主窗口已显示");

	const int result = app.exec();

	controller.saveConfig();
	return result;
}
