#include "app/SingleInstance.h"

#include <windows.h>

#include <string>

namespace minirec {

unsigned int SingleInstance::activationMessage()
{
	static const unsigned int message = RegisterWindowMessageW(L"MiniRec.ActivateExistingInstance");
	return message;
}

bool SingleInstance::acquire()
{
	if (m_mutex)
		return true;

	HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\MiniRec.SingleInstance.4F2C9A11");
	if (!mutex)
		return true; // 创建失败时不做限制，避免程序无法启动

	if (GetLastError() == ERROR_ALREADY_EXISTS) {
		CloseHandle(mutex);
		// Qt 主窗口会在 nativeEvent 里处理这条消息并把窗口带到前台
		PostMessageW(HWND_BROADCAST, activationMessage(), 0, 0);
		return false;
	}

	m_mutex = mutex;
	return true;
}

void SingleInstance::release()
{
	if (m_mutex) {
		ReleaseMutex(static_cast<HANDLE>(m_mutex));
		CloseHandle(static_cast<HANDLE>(m_mutex));
		m_mutex = nullptr;
	}
}

std::string winErrorMessage(unsigned long code)
{
	LPWSTR buffer = nullptr;
	const DWORD len = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
						 FORMAT_MESSAGE_IGNORE_INSERTS,
					 nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
					 reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
	if (!len || !buffer)
		return "错误码 " + std::to_string(code);

	std::wstring wide(buffer, len);
	LocalFree(buffer);

	while (!wide.empty() && (wide.back() == L'\r' || wide.back() == L'\n' || wide.back() == L' '))
		wide.pop_back();

	const int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0,
					     nullptr, nullptr);
	std::string result(static_cast<size_t>(size), '\0');
	WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), result.data(), size, nullptr,
			    nullptr);
	return result;
}

} // namespace minirec
