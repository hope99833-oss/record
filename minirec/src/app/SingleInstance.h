#pragma once

#include <string>

namespace minirec {

// 单实例守卫：第二个实例启动时不创建窗口，而是广播一条消息让已有实例把窗口显示出来，然后退出。
class SingleInstance {
public:
	// 返回 false 表示已有实例在运行（此时已广播激活消息）
	bool acquire();
	void release();

	// 注册的窗口消息 id；主窗口在 nativeEvent 中处理它即可被"唤醒"
	static unsigned int activationMessage();

private:
	void *m_mutex = nullptr;
};

// 把 Windows 错误码转成可读字符串（日志与提示用）
std::string winErrorMessage(unsigned long code);

} // namespace minirec
