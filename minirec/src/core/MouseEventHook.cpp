#include "core/MouseEventHook.h"

#include <obs.h>
#include <util/platform.h>

#include "core/Logging.h"

namespace minirec {

MouseEventHook &MouseEventHook::instance()
{
	static MouseEventHook hook;
	return hook;
}

bool MouseEventHook::start()
{
	if (m_hook)
		return true;

	m_head = 0;
	m_tail = 0;

	// 低级鼠标钩子：回调在安装线程（Qt 主线程）的输入上下文里执行，
	// 因此回调里只做入队，绝不调用 libobs 图形接口。
	m_hook = SetWindowsHookExW(WH_MOUSE_LL, &MouseEventHook::hookProc, GetModuleHandleW(nullptr), 0);
	if (!m_hook) {
		logWarn("鼠标点击特效：安装低级鼠标钩子失败（错误码 %lu），特效将不可用", GetLastError());
		return false;
	}

	logInfo("鼠标点击特效：全局鼠标钩子已启用");
	return true;
}

void MouseEventHook::stop()
{
	if (m_hook) {
		UnhookWindowsHookEx(m_hook);
		m_hook = nullptr;
		logInfo("鼠标点击特效：全局鼠标钩子已停用");
	}
}

bool MouseEventHook::running() const
{
	return m_hook != nullptr;
}

void MouseEventHook::push(const MouseClickEvent &event)
{
	const LONG head = m_head;
	const LONG next = (head + 1) % static_cast<LONG>(kCapacity);
	if (next == m_tail)
		return; // 队列满，丢弃最旧事件（宁可丢特效也不阻塞输入）

	m_ring[head] = event;
	InterlockedExchange(&m_head, next);
}

size_t MouseEventHook::drain(MouseClickEvent *out, size_t maxCount)
{
	size_t count = 0;
	LONG tail = m_tail;

	while (count < maxCount) {
		const LONG head = m_head;
		if (tail == head)
			break;

		out[count++] = m_ring[tail];
		tail = (tail + 1) % static_cast<LONG>(kCapacity);
	}

	InterlockedExchange(&m_tail, tail);
	return count;
}

LRESULT CALLBACK MouseEventHook::hookProc(int code, WPARAM wparam, LPARAM lparam)
{
	if (code == HC_ACTION) {
		const MSLLHOOKSTRUCT *info = reinterpret_cast<const MSLLHOOKSTRUCT *>(lparam);
		bool relevant = false;
		bool down = true;

		switch (wparam) {
		case WM_LBUTTONDOWN:
		case WM_RBUTTONDOWN:
		case WM_MBUTTONDOWN:
			relevant = true;
			down = true;
			break;
		case WM_LBUTTONUP:
		case WM_RBUTTONUP:
		case WM_MBUTTONUP:
			relevant = true;
			down = false;
			break;
		default:
			break;
		}

		if (relevant && !(info->flags & LLMHF_INJECTED)) {
			MouseClickEvent event;
			event.x = info->pt.x;
			event.y = info->pt.y;
			event.timeNs = os_gettime_ns();
			event.down = down;
			instance().push(event);
		}
	}

	return CallNextHookEx(instance().m_hook, code, wparam, lparam);
}

} // namespace minirec
