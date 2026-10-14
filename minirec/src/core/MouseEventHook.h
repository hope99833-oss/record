#pragma once

#include <cstddef>
#include <cstdint>
#include <windows.h>

namespace minirec {

// 一次鼠标点击（屏幕物理像素坐标 + 时间戳）
struct MouseClickEvent {
	int x = 0;
	int y = 0;
	uint64_t timeNs = 0; // 与 libobs 帧时间戳同一时钟（os_gettime_ns）
	bool down = true;    // true=按下 false=抬起
};

// 全局低级鼠标钩子：把点击事件写入无锁环形队列，供鼠标点击特效滤镜消费。
// 钩子必须安装在有消息循环的线程上（这里是 Qt 主线程），因此 start() 要在主线程调用。
class MouseEventHook {
public:
	static MouseEventHook &instance();

	bool start();
	void stop();
	bool running() const;

	// 取走队列中的事件，返回实际取出的条数
	size_t drain(MouseClickEvent *out, size_t maxCount);

private:
	MouseEventHook() = default;
	~MouseEventHook() = default;
	MouseEventHook(const MouseEventHook &) = delete;
	MouseEventHook &operator=(const MouseEventHook &) = delete;

	void push(const MouseClickEvent &event);
	static LRESULT CALLBACK hookProc(int code, WPARAM wparam, LPARAM lparam);

	static constexpr size_t kCapacity = 128;
	MouseClickEvent m_ring[kCapacity];
	volatile LONG m_head = 0; // 写入位置
	volatile LONG m_tail = 0; // 读取位置
	HHOOK m_hook = nullptr;
};

} // namespace minirec
