#include "core/Logging.h"

#include <obs.h>

#include <windows.h>

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace minirec {
namespace {

std::mutex g_logMutex;
std::string g_logPath;
// 兜底日志：放在 exe 同级目录。%APPDATA% 不可写时也能留痕，GUI 程序没有控制台，
// 没有这个就只能"双击没反应"而无从查起。
std::string g_fallbackPath;
std::string g_fatalError;
bool g_initialized = false;

std::string exeDirPath()
{
	wchar_t buffer[MAX_PATH * 2] = {0};
	const DWORD length = GetModuleFileNameW(nullptr, buffer, _countof(buffer));
	if (length == 0)
		return {};

	const int size = WideCharToMultiByte(CP_UTF8, 0, buffer, static_cast<int>(length), nullptr, 0, nullptr,
					     nullptr);
	std::string full(static_cast<size_t>(size), '\0');
	WideCharToMultiByte(CP_UTF8, 0, buffer, static_cast<int>(length), full.data(), size, nullptr, nullptr);

	const size_t pos = full.find_last_of('\\');
	return pos == std::string::npos ? full : full.substr(0, pos);
}

// 以二进制追加方式写出：源码按 /utf-8 编译，std::string 本身就是 UTF-8 字节，
// 直接落盘即可。不要用 fopen 的 "ccs=UTF-8" —— 那种模式下窄字符输出要先按当前
// locale 转宽字符，默认 "C" locale 下中文会转换失败，整行都写不进去。
void appendLine(const std::string &path, const std::string &line)
{
	if (path.empty())
		return;

	FILE *fp = fopen(path.c_str(), "ab");
	if (!fp)
		return;
	fwrite(line.data(), 1, line.size(), fp);
	fclose(fp);
}

void writeLine(const char *level, const std::string &message)
{
	std::lock_guard<std::mutex> lock(g_logMutex);

	time_t t = time(nullptr);
	struct tm tmv;
	localtime_s(&tmv, &t);
	char stamp[32];
	strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tmv);

	std::string line;
	line.reserve(message.size() + 48);
	line += '[';
	line += stamp;
	line += "] [";
	line += level;
	line += "] ";
	line += message;
	line += '\n';

	appendLine(g_logPath, line);
	if (g_fallbackPath != g_logPath)
		appendLine(g_fallbackPath, line);
}

void formatTo(std::string &out, const char *format, va_list args)
{
	char stackBuf[1024];
	va_list copy;
	va_copy(copy, args);
	int written = vsnprintf(stackBuf, sizeof(stackBuf), format, copy);
	va_end(copy);

	if (written < 0) {
		out = format;
		return;
	}
	if (static_cast<size_t>(written) < sizeof(stackBuf)) {
		out.assign(stackBuf, static_cast<size_t>(written));
		return;
	}

	std::vector<char> heap(static_cast<size_t>(written) + 1);
	vsnprintf(heap.data(), heap.size(), format, args);
	out.assign(heap.data(), static_cast<size_t>(written));
}

// libobs 的日志回调（签名与 util/base.h 中 log_handler_t 一致）
void obsLogHandler(int level, const char *msg, va_list args, void *param)
{
	UNUSED_PARAMETER(param);

	std::string message;
	formatTo(message, msg, args);

	const char *levelName = "debug";
	switch (level) {
	case LOG_ERROR:
		levelName = "error";
		break;
	case LOG_WARNING:
		levelName = "warn";
		break;
	case LOG_INFO:
		levelName = "info";
		break;
	case LOG_DEBUG:
		levelName = "debug";
		break;
	default:
		break;
	}

	writeLine(levelName, message);
}

void cleanupOldLogs(const std::string &logDir)
{
	namespace fs = std::filesystem;
	std::error_code ec;
	const auto now = fs::file_time_type::clock::now();

	for (const auto &entry : fs::directory_iterator(logDir, ec)) {
		if (ec)
			return;
		if (!entry.is_regular_file(ec))
			continue;
		const std::string name = entry.path().filename().string();
		if (name.rfind("minirec-", 0) != 0)
			continue;

		const auto written = entry.last_write_time(ec);
		if (ec)
			continue;
		const auto ageHours = std::chrono::duration_cast<std::chrono::hours>(now - written).count();
		if (ageHours > 7 * 24)
			fs::remove(entry.path(), ec);
	}
}

} // namespace

void initLogging(const std::string &logDir)
{
	namespace fs = std::filesystem;
	std::error_code ec;
	fs::create_directories(logDir, ec);

	cleanupOldLogs(logDir);

	time_t t = time(nullptr);
	struct tm tmv;
	localtime_s(&tmv, &t);
	char stamp[16];
	snprintf(stamp, sizeof(stamp), "%04d%02d%02d", tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);

	const std::string primary = logDir + "\\minirec-" + stamp + ".log";
	const std::string fallback = exeDirPath() + "\\minirec-startup.log";

	// 新建的日志文件先写 UTF-8 BOM，记事本能正确识别中文
	for (const std::string &path : {primary, fallback}) {
		if (path.empty() || fs::exists(path, ec))
			continue;
		FILE *fp = fopen(path.c_str(), "wb");
		if (fp) {
			fwrite("\xEF\xBB\xBF", 1, 3, fp);
			fclose(fp);
		}
	}

	{
		std::lock_guard<std::mutex> lock(g_logMutex);
		g_logPath = primary;
		g_fallbackPath = fallback;
	}

	base_set_log_handler(obsLogHandler, nullptr);
	g_initialized = true;
	logInfo("======== minirec " MINIREC_VERSION " 启动（日志初始化完成）========");
}

void shutdownLogging()
{
	if (!g_initialized)
		return;
	g_initialized = false;
	base_set_log_handler(nullptr, nullptr);
}

void traceStartup(const char *stage)
{
	const std::string path = exeDirPath() + "\\minirec-startup.log";
	std::lock_guard<std::mutex> lock(g_logMutex);

	time_t t = time(nullptr);
	struct tm tmv;
	localtime_s(&tmv, &t);
	char stamp[32];
	strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tmv);

	std::string line = "[";
	line += stamp;
	line += "] [startup] ";
	line += stage;
	line += '\n';
	appendLine(path, line);
}

void logInfo(const char *format, ...)
{
	std::string message;
	va_list args;
	va_start(args, format);
	formatTo(message, format, args);
	va_end(args);
	writeLine("info", message);
}

void logWarn(const char *format, ...)
{
	std::string message;
	va_list args;
	va_start(args, format);
	formatTo(message, format, args);
	va_end(args);
	writeLine("warn", message);
}

void logError(const char *format, ...)
{
	std::string message;
	va_list args;
	va_start(args, format);
	formatTo(message, format, args);
	va_end(args);
	writeLine("error", message);
}

std::string lastFatalError()
{
	std::lock_guard<std::mutex> lock(g_logMutex);
	return g_fatalError;
}

void setFatalError(const std::string &message)
{
	{
		std::lock_guard<std::mutex> lock(g_logMutex);
		g_fatalError = message;
	}
	logError("%s", message.c_str());
}

} // namespace minirec
