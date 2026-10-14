#include "app/AppConfig.h"

#include <obs.h>

#include "core/Logging.h"

#include <windows.h>

#include <shlobj.h>

namespace minirec {
namespace {

std::string defaultOutputDir()
{
	char path[MAX_PATH] = {0};
	if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_MYVIDEO, nullptr, 0, path)) && path[0] != '\0')
		return std::string(path) + "\\MiniRec";

	if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_PERSONAL, nullptr, 0, path)) && path[0] != '\0')
		return std::string(path) + "\\MiniRec";

	return "C:\\MiniRec";
}

void getHotkey(const obs_data_t *data, const char *key, HotkeyBinding &out)
{
	obs_data_t *obj = obs_data_get_obj(const_cast<obs_data_t *>(data), key);
	if (!obj)
		return;
	out.vk = static_cast<int>(obs_data_get_int(obj, "vk"));
	out.modifiers = static_cast<uint32_t>(obs_data_get_int(obj, "modifiers"));
	obs_data_release(obj);
}

void setHotkey(obs_data_t *data, const char *key, const HotkeyBinding &value)
{
	obs_data_t *obj = obs_data_create();
	obs_data_set_int(obj, "vk", value.vk);
	obs_data_set_int(obj, "modifiers", value.modifiers);
	obs_data_set_obj(data, key, obj);
	obs_data_release(obj);
}

} // namespace

void AppConfig::applyQualityPreset(int preset)
{
	quality = preset;
	switch (preset) {
	case 0: // 低：720p30
		fps = 30;
		outputWidth = 1280;
		outputHeight = 720;
		break;
	case 2: // 高：1080p60（源更高分辨率时保持 1080p 输出）
		fps = 60;
		outputWidth = 1920;
		outputHeight = 1080;
		break;
	case 3: // 原始分辨率（跟随屏幕）：2K 屏录 2K、1080p 屏录 1080p，不缩放
		fps = 60;
		outputWidth = 0;  // 0 = 跟随采集分辨率（由 computeOutputSize 解析）
		outputHeight = 0;
		break;
	case 1:
	default: // 中：1080p30
		fps = 30;
		outputWidth = 1920;
		outputHeight = 1080;
		break;
	}
}

AppConfig AppConfig::load(const std::string &path)
{
	AppConfig cfg;
	cfg.outputDir = defaultOutputDir();

	obs_data_t *data = obs_data_create_from_json_file(path.c_str());
	if (!data) {
		logInfo("配置文件不存在或无法解析，使用默认配置：%s", path.c_str());
		return cfg;
	}

	cfg.captureMode = static_cast<CaptureMode>(obs_data_get_int(data, "captureMode"));
	cfg.monitorIndex = static_cast<int>(obs_data_get_int(data, "monitorIndex"));
	cfg.windowId = obs_data_get_string(data, "windowId");
	cfg.windowName = obs_data_get_string(data, "windowName");

	obs_data_t *region = obs_data_get_obj(data, "region");
	if (region) {
		cfg.region.x = static_cast<int>(obs_data_get_int(region, "x"));
		cfg.region.y = static_cast<int>(obs_data_get_int(region, "y"));
		cfg.region.w = static_cast<int>(obs_data_get_int(region, "w"));
		cfg.region.h = static_cast<int>(obs_data_get_int(region, "h"));
		cfg.region.valid = obs_data_get_bool(region, "valid") && cfg.region.w >= 16 && cfg.region.h >= 16;
		obs_data_release(region);
	}

	cfg.desktopAudio = obs_data_get_bool(data, "desktopAudio");
	cfg.desktopDeviceId = obs_data_get_string(data, "desktopDeviceId");
	cfg.micAudio = obs_data_get_bool(data, "micAudio");
	cfg.micDeviceId = obs_data_get_string(data, "micDeviceId");

	cfg.fps = static_cast<int>(obs_data_get_int(data, "fps"));
	cfg.outputWidth = static_cast<int>(obs_data_get_int(data, "outputWidth"));
	cfg.outputHeight = static_cast<int>(obs_data_get_int(data, "outputHeight"));
	cfg.quality = static_cast<int>(obs_data_get_int(data, "quality"));
	cfg.encoderOverride = obs_data_get_string(data, "encoderOverride");
	// 旧配置文件没有该字段，缺省时保持 struct 默认值 true（延续"录制光标"的既有行为）
	if (obs_data_has_user_value(data, "captureCursor"))
		cfg.captureCursor = obs_data_get_bool(data, "captureCursor");

	cfg.cameraEnabled = obs_data_get_bool(data, "cameraEnabled");
	cfg.cameraDeviceId = obs_data_get_string(data, "cameraDeviceId");
	cfg.cameraDeviceName = obs_data_get_string(data, "cameraDeviceName");
	cfg.cameraCorner = static_cast<OverlayCorner>(obs_data_get_int(data, "cameraCorner"));
	cfg.cameraWidth = static_cast<int>(obs_data_get_int(data, "cameraWidth"));

	cfg.mouseFxEnabled = obs_data_get_bool(data, "mouseFxEnabled");
	cfg.mouseFxRadius = static_cast<int>(obs_data_get_int(data, "mouseFxRadius"));
	cfg.mouseFxDurationMs = static_cast<int>(obs_data_get_int(data, "mouseFxDurationMs"));
	cfg.mouseFxColor = static_cast<uint32_t>(obs_data_get_int(data, "mouseFxColor"));

	cfg.outputDir = obs_data_get_string(data, "outputDir");
	if (cfg.outputDir.empty())
		cfg.outputDir = defaultOutputDir();
	cfg.autoRemuxMp4 = obs_data_get_bool(data, "autoRemuxMp4");
	cfg.keepMkv = obs_data_get_bool(data, "keepMkv");
	cfg.countdownSeconds = static_cast<int>(obs_data_get_int(data, "countdownSeconds"));
	cfg.crashRecoveryAccepted = obs_data_get_bool(data, "crashRecoveryAccepted");

	getHotkey(data, "hkStartStop", cfg.hkStartStop);
	getHotkey(data, "hkPause", cfg.hkPause);

	obs_data_release(data);

	// 防御：配置被手工改坏时回落到合理值。0 表示"跟随屏幕"，是合法值，不在此处兜底。
	if (cfg.fps <= 0 || cfg.fps > 240)
		cfg.fps = 60;
	if (cfg.outputWidth < 0 || cfg.outputWidth > 7680)
		cfg.outputWidth = 1920;
	if (cfg.outputHeight < 0 || cfg.outputHeight > 4320)
		cfg.outputHeight = 1080;
	if (cfg.countdownSeconds < 0 || cfg.countdownSeconds > 10)
		cfg.countdownSeconds = 3;
	if (cfg.cameraWidth < 64 || cfg.cameraWidth > 1280)
		cfg.cameraWidth = 320;
	if (cfg.mouseFxRadius < 1 || cfg.mouseFxRadius > 512)
		cfg.mouseFxRadius = 48;
	if (cfg.mouseFxDurationMs < 100 || cfg.mouseFxDurationMs > 5000)
		cfg.mouseFxDurationMs = 700;

	logInfo("已加载配置：%s", path.c_str());
	return cfg;
}

void AppConfig::save(const std::string &path) const
{
	obs_data_t *data = obs_data_create();

	obs_data_set_int(data, "captureMode", static_cast<int>(captureMode));
	obs_data_set_int(data, "monitorIndex", monitorIndex);
	obs_data_set_string(data, "windowId", windowId.c_str());
	obs_data_set_string(data, "windowName", windowName.c_str());

	obs_data_t *regionObj = obs_data_create();
	obs_data_set_int(regionObj, "x", region.x);
	obs_data_set_int(regionObj, "y", region.y);
	obs_data_set_int(regionObj, "w", region.w);
	obs_data_set_int(regionObj, "h", region.h);
	obs_data_set_bool(regionObj, "valid", region.valid);
	obs_data_set_obj(data, "region", regionObj);
	obs_data_release(regionObj);

	obs_data_set_bool(data, "desktopAudio", desktopAudio);
	obs_data_set_string(data, "desktopDeviceId", desktopDeviceId.c_str());
	obs_data_set_bool(data, "micAudio", micAudio);
	obs_data_set_string(data, "micDeviceId", micDeviceId.c_str());

	obs_data_set_int(data, "fps", fps);
	obs_data_set_int(data, "outputWidth", outputWidth);
	obs_data_set_int(data, "outputHeight", outputHeight);
	obs_data_set_int(data, "quality", quality);
	obs_data_set_string(data, "encoderOverride", encoderOverride.c_str());
	obs_data_set_bool(data, "captureCursor", captureCursor);

	obs_data_set_bool(data, "cameraEnabled", cameraEnabled);
	obs_data_set_string(data, "cameraDeviceId", cameraDeviceId.c_str());
	obs_data_set_string(data, "cameraDeviceName", cameraDeviceName.c_str());
	obs_data_set_int(data, "cameraCorner", static_cast<int>(cameraCorner));
	obs_data_set_int(data, "cameraWidth", cameraWidth);

	obs_data_set_bool(data, "mouseFxEnabled", mouseFxEnabled);
	obs_data_set_int(data, "mouseFxRadius", mouseFxRadius);
	obs_data_set_int(data, "mouseFxDurationMs", mouseFxDurationMs);
	obs_data_set_int(data, "mouseFxColor", static_cast<long long>(mouseFxColor));

	obs_data_set_string(data, "outputDir", outputDir.c_str());
	obs_data_set_bool(data, "autoRemuxMp4", autoRemuxMp4);
	obs_data_set_bool(data, "keepMkv", keepMkv);
	obs_data_set_int(data, "countdownSeconds", countdownSeconds);
	obs_data_set_bool(data, "crashRecoveryAccepted", crashRecoveryAccepted);

	setHotkey(data, "hkStartStop", hkStartStop);
	setHotkey(data, "hkPause", hkPause);

	if (obs_data_save_json_safe(data, path.c_str(), "tmp", "bak"))
		logInfo("配置已保存：%s", path.c_str());
	else
		logError("配置保存失败：%s", path.c_str());

	obs_data_release(data);
}

} // namespace minirec
