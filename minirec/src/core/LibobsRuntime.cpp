#include "core/LibobsRuntime.h"

#include <obs.h>
#include <util/platform.h>

#include <windows.h>
#include <shlobj.h>

#include <cstring>
#include <filesystem>
#include <vector>

#include "core/Logging.h"
#include "core/MouseFxSource.h"

namespace minirec {
namespace {

namespace fs = std::filesystem;

std::string toUtf8(const std::wstring &wide)
{
	if (wide.empty())
		return {};
	const int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0,
					     nullptr, nullptr);
	std::string result(static_cast<size_t>(size), '\0');
	WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), result.data(), size, nullptr,
			    nullptr);
	return result;
}

std::wstring toWide(const std::string &utf8)
{
	if (utf8.empty())
		return {};
	const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
	std::wstring result(static_cast<size_t>(size), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), result.data(), size);
	return result;
}

std::string exeDirectory()
{
	wchar_t buffer[MAX_PATH * 2] = {0};
	const DWORD length = GetModuleFileNameW(nullptr, buffer, _countof(buffer));
	if (length == 0)
		return {};

	const std::string full = toUtf8(std::wstring(buffer, length));
	const size_t pos = full.find_last_of('\\');
	return pos == std::string::npos ? full : full.substr(0, pos);
}

std::string appDataDir()
{
	wchar_t path[MAX_PATH] = {0};
	if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, path)))
		return toUtf8(path) + "\\MiniRec";
	return exeDirectory() + "\\config";
}

const char *videoResetResultText(int code)
{
	switch (code) {
	case OBS_VIDEO_SUCCESS:
		return "成功";
	case OBS_VIDEO_FAIL:
		return "失败（图形子系统初始化错误）";
	case OBS_VIDEO_NOT_SUPPORTED:
		return "当前显卡/驱动不支持所选参数";
	case OBS_VIDEO_INVALID_PARAM:
		return "视频参数非法";
	case OBS_VIDEO_CURRENTLY_ACTIVE:
		return "已有输出正在运行，无法重置视频";
	case OBS_VIDEO_MODULE_NOT_FOUND:
		return "找不到图形模块 libobs-d3d11.dll（请检查 bin/64bit 目录）";
	default:
		return "未知错误";
	}
}

} // namespace

LibobsRuntime &LibobsRuntime::instance()
{
	static LibobsRuntime runtime;
	return runtime;
}

bool LibobsRuntime::selfCheck(std::string *error) const
{
	struct CheckItem {
		std::string path;
		const char *description;
	};

	const std::string root = fs::path(m_exeDir).parent_path().parent_path().string();

	const std::vector<CheckItem> required = {
		{fs::path(m_exeDir).append("obs.dll").string(), "libobs 核心库"},
		{fs::path(m_exeDir).append("libobs-d3d11.dll").string(), "D3D11 图形模块"},
		{fs::path(root).append("data").append("libobs").append("default.effect").string(), "libobs 着色器数据"},
		{fs::path(root).append("obs-plugins").append("64bit").string(), "插件目录"},
	};

	for (const auto &item : required) {
		if (!fs::exists(item.path)) {
			if (error)
				*error = std::string("缺少") + item.description + "：" + item.path +
					 "\n请使用 packaging 脚本重新打包（需保持 bin/64bit + data + obs-plugins 的原生目录结构）。";
			return false;
		}
	}

	// obs-ffmpeg-mux.exe 是本地录像的封装子进程，缺失时录制会失败
	std::error_code ec;
	bool foundMux = false;
	for (const char *relative : {"obs-plugins/64bit/obs-ffmpeg-mux.exe", "bin/64bit/obs-ffmpeg-mux.exe"}) {
		if (fs::exists(fs::path(root).append(relative), ec)) {
			foundMux = true;
			break;
		}
	}
	if (!foundMux) {
		if (error)
			*error = "找不到 obs-ffmpeg-mux.exe（录像封装子进程），录制将无法启动。请检查打包产物。";
		return false;
	}

	return true;
}

bool LibobsRuntime::init(std::string *error)
{
	if (m_ready)
		return true;

	m_exeDir = exeDirectory();
	const std::string root = fs::path(m_exeDir).parent_path().parent_path().string();
	m_dataDir = (fs::path(root) / "data").string();
	m_pluginDir = (fs::path(root) / "obs-plugins" / "64bit").string();
	m_configDir = appDataDir();

	std::error_code ec;
	fs::create_directories(m_configDir, ec);

	// libobs 在 Windows 上用相对路径查找核心数据（../../data/libobs）与默认插件目录
	// （../../obs-plugins/64bit），因此工作目录必须是 exe 所在目录。
	if (!SetCurrentDirectoryW(toWide(m_exeDir).c_str()))
		logWarn("切换工作目录失败（错误码 %lu），若数据文件找不到请检查目录结构", GetLastError());

	if (!selfCheck(error))
		return false;

	if (!obs_startup("zh-CN", m_configDir.c_str(), nullptr)) {
		if (error)
			*error = "libobs 初始化失败（obs_startup 返回 false）。";
		return false;
	}
	m_started = true;

	logInfo("libobs 已启动：exe=%s data=%s plugins=%s config=%s", m_exeDir.c_str(), m_dataDir.c_str(),
		m_pluginDir.c_str(), m_configDir.c_str());

	// 顺序与 OBS 前端一致：先重置音频/视频（图形模块在此加载），再加载插件模块。
	// 这样 win-capture 在 obs_module_load 里才能探测到 D3D11 与 WGC 能力。
	if (!applyAudio(error))
		return false;

	if (!applyVideo(60, 1920, 1080, 1920, 1080, error))
		return false;

	struct obs_module_failure_info failure = {0};
	obs_load_all_modules2(&failure);
	obs_module_failure_info_free(&failure);
	obs_post_load_modules();

	// 宿主内注册的鼠标点击特效源（不依赖插件 DLL）
	mousefx::registerSource();

	m_ready = true;
	logInfo("libobs 初始化完成");
	return true;
}

bool LibobsRuntime::applyAudio(std::string *error)
{
	struct obs_audio_info aoi;
	memset(&aoi, 0, sizeof(aoi));
	aoi.samples_per_sec = 48000;
	aoi.speakers = SPEAKERS_STEREO;

	if (!obs_reset_audio(&aoi)) {
		if (error)
			*error = "音频子系统初始化失败（48000Hz / 立体声）。";
		return false;
	}
	return true;
}

bool LibobsRuntime::applyVideo(int fps, int outputWidth, int outputHeight, int baseWidth, int baseHeight,
			       std::string *error)
{
	struct obs_video_info ovi;
	memset(&ovi, 0, sizeof(ovi));

	ovi.graphics_module = "libobs-d3d11";
	ovi.fps_num = static_cast<uint32_t>(fps > 0 ? fps : 60);
	ovi.fps_den = 1;

	ovi.base_width = static_cast<uint32_t>(baseWidth > 0 ? baseWidth : 1920);
	ovi.base_height = static_cast<uint32_t>(baseHeight > 0 ? baseHeight : 1080);
	ovi.output_width = static_cast<uint32_t>(outputWidth > 0 ? outputWidth : ovi.base_width);
	ovi.output_height = static_cast<uint32_t>(outputHeight > 0 ? outputHeight : ovi.base_height);

	// NV12 + GPU 转换是硬件编码走零拷贝路径的前提
	ovi.output_format = VIDEO_FORMAT_NV12;
	ovi.adapter = 0;
	ovi.gpu_conversion = true;
	ovi.colorspace = VIDEO_CS_709;
	ovi.range = VIDEO_RANGE_PARTIAL;
	ovi.scale_type = OBS_SCALE_BICUBIC;

	const int result = obs_reset_video(&ovi);
	if (result != OBS_VIDEO_SUCCESS) {
		if (error)
			*error = std::string("视频初始化失败：") + videoResetResultText(result);
		return false;
	}

	logInfo("视频参数已应用：%ux%u -> %ux%u @%ufps（NV12 / D3D11）", ovi.base_width, ovi.base_height,
		ovi.output_width, ovi.output_height, ovi.fps_num);
	return true;
}

void LibobsRuntime::shutdown()
{
	if (!m_started)
		return;

	obs_shutdown();
	m_started = false;
	m_ready = false;
	logInfo("libobs 已关闭");
}

} // namespace minirec
