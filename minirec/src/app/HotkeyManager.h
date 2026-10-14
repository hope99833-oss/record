#pragma once

#include <QObject>
#include <QString>

#include <cstddef>

#include "app/AppConfig.h"

namespace minirec {

// 全局热键：走 libobs 的 frontend 热键（Windows 上用低级键盘钩子，焦点不在本程序时也生效）
class HotkeyManager : public QObject {
	Q_OBJECT
public:
	explicit HotkeyManager(QObject *parent = nullptr);
	~HotkeyManager() override;

	// 按配置注册/重注册（libobs 未启动时为空操作）
	void apply(const AppConfig &config);
	void unregisterAll();

	// 把虚拟键码转成可展示的名字（如 "F9"）
	static QString describe(const HotkeyBinding &binding);

signals:
	void startStopTriggered();
	void pauseTriggered();

private:
	void registerOne(const char *name, const char *description, const HotkeyBinding &binding, size_t *outId);

	size_t m_startStopId = static_cast<size_t>(-1);
	size_t m_pauseId = static_cast<size_t>(-1);
};

} // namespace minirec
