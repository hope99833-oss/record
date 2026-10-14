#include "core/SceneBuilder.h"

#include <obs.h>

#include <windows.h>

#include <chrono>
#include <thread>

#include "core/EnumHelpers.h"
#include "core/Logging.h"
#include "core/MouseFxSource.h"

namespace minirec {
namespace {

constexpr int kPipMargin = 24; // 画中画与画布边缘的间距（以 1080p 为基准）

struct MonitorLookup {
	const std::string *id;
	RegionRect rect;
	bool found;
};

BOOL CALLBACK monitorLookupProc(HMONITOR monitor, HDC, LPRECT, LPARAM data)
{
	auto *ctx = reinterpret_cast<MonitorLookup *>(data);

	MONITORINFOEXA info;
	info.cbSize = sizeof(info);
	if (!GetMonitorInfoA(monitor, static_cast<LPMONITORINFO>(&info)))
		return TRUE;

	// 与 win-capture 生成 monitor_id 的方式一致：优先使用设备接口路径
	DISPLAY_DEVICEA device;
	ZeroMemory(&device, sizeof(device));
	device.cb = sizeof(device);

	std::string value;
	if (EnumDisplayDevicesA(info.szDevice, 0, &device, EDD_GET_DEVICE_INTERFACE_NAME))
		value = device.DeviceID;
	else
		value = info.szDevice;

	if (value != *ctx->id)
		return TRUE;

	ctx->rect.x = info.rcMonitor.left;
	ctx->rect.y = info.rcMonitor.top;
	ctx->rect.w = info.rcMonitor.right - info.rcMonitor.left;
	ctx->rect.h = info.rcMonitor.bottom - info.rcMonitor.top;
	ctx->rect.valid = true;
	ctx->found = true;
	return FALSE;
}

int evenFloor(int value)
{
	return value - (value % 2);
}

} // namespace

SceneBuilder::~SceneBuilder()
{
	// libobs 关闭后不能再释放对象，destroy() 需要在关停前显式调用
}

bool SceneBuilder::monitorRectFromId(const std::string &monitorId, RegionRect *out)
{
	MonitorLookup ctx{&monitorId, RegionRect(), false};
	EnumDisplayMonitors(nullptr, nullptr, monitorLookupProc, reinterpret_cast<LPARAM>(&ctx));
	if (!ctx.found)
		return false;
	*out = ctx.rect;
	return true;
}

bool SceneBuilder::build(const AppConfig &config, std::string *error)
{
	destroy();

	m_scene = obs_scene_create("minirec_scene");
	if (!m_scene) {
		if (error)
			*error = "创建场景失败";
		return false;
	}
	m_sceneSource = obs_scene_get_source(m_scene); // 借用引用，不单独释放

	m_videoSource = createCaptureSource(config, error);
	if (!m_videoSource) {
		destroy();
		return false;
	}

	m_videoItem = obs_scene_add(m_scene, m_videoSource);
	if (!m_videoItem) {
		if (error)
			*error = "把采集源加入场景失败";
		destroy();
		return false;
	}

	m_cameraWidth = config.cameraWidth > 0 ? config.cameraWidth : 320;
	m_cameraCorner = config.cameraCorner;

	// 等待采集源给出尺寸（DXGI / 窗口捕获是异步的），再做区域裁剪与画中画布局
	waitForSourceSize(1500);
	applyRegionCrop(config);

	createAudioSources(config);
	createCamera(config);
	createMouseFx(config);

	// 场景作为通道 0 的输出源
	obs_set_output_source(0, m_sceneSource);

	logInfo("场景已构建：采集源=%s 屏幕矩形=%d,%d %dx%d 源尺寸=%dx%d 摄像头=%s 鼠标特效=%s",
		m_target.sourceId.c_str(), m_target.screenRect.x, m_target.screenRect.y, m_target.screenRect.w,
		m_target.screenRect.h, m_target.sourceWidth, m_target.sourceHeight, m_camera ? "开" : "关",
		m_mouseFx ? "开" : "关");
	return true;
}

obs_source_t *SceneBuilder::createCaptureSource(const AppConfig &config, std::string *error)
{
	obs_data_t *settings = obs_data_create();
	obs_source_t *source = nullptr;

	if (config.captureMode == CaptureMode::Window) {
		m_target.sourceId = enums::ids::WindowCapture;
		m_target.supportsMouseFx = false; // 窗口会移动，屏幕坐标无法稳定映射

		if (config.windowId.empty()) {
			if (error)
				*error = "还没有选择要录制的窗口";
			obs_data_release(settings);
			return nullptr;
		}

		obs_data_set_string(settings, "window", config.windowId.c_str());
		obs_data_set_bool(settings, "cursor", config.captureCursor);
		obs_data_set_int(settings, "method", 0);   // 自动（Win10 1903+ 用 WGC）
		obs_data_set_int(settings, "priority", 1); // 标题优先匹配

		source = obs_source_create(enums::ids::WindowCapture, "窗口采集", settings, nullptr);
		obs_data_release(settings);

		if (!source && error)
			*error = "窗口采集源创建失败（win-capture 插件是否已打包？）";
		return source;
	}

	// 显示器 / 区域：都基于 monitor_capture，区域通过对场景项裁剪实现
	m_target.sourceId = enums::ids::MonitorCapture;
	m_target.supportsMouseFx = true;

	const auto monitorList = enums::monitors();
	if (monitorList.empty()) {
		if (error)
			*error = "没有检测到可用显示器（monitor_capture 插件是否已打包？）";
		obs_data_release(settings);
		return nullptr;
	}

	std::string monitorId;
	RegionRect monitorRect;

	if (config.captureMode == CaptureMode::Region && config.region.valid) {
		for (const auto &monitor : monitorList) {
			RegionRect rect;
			if (!monitorRectFromId(monitor.value, &rect))
				continue;
			if (config.region.x >= rect.x && config.region.y >= rect.y &&
			    config.region.x + config.region.w <= rect.x + rect.w &&
			    config.region.y + config.region.h <= rect.y + rect.h) {
				monitorId = monitor.value;
				monitorRect = rect;
				break;
			}
		}
		logInfo("区域录制：选区 %d,%d %dx%d 命中显示器 %s", config.region.x, config.region.y, config.region.w,
			config.region.h, monitorId.empty() ? "(未命中，回退到指定显示器)" : monitorId.c_str());
	}

	if (monitorId.empty()) {
		const size_t index = static_cast<size_t>(config.monitorIndex) < monitorList.size()
					     ? static_cast<size_t>(config.monitorIndex)
					     : 0;
		monitorId = monitorList[index].value;
		if (!monitorRectFromId(monitorId, &monitorRect))
			monitorRect = RegionRect();
	}

	m_monitorRect = monitorRect;

	if (config.captureMode == CaptureMode::Region && config.region.valid) {
		m_target.screenRect = config.region;
	} else {
		m_target.screenRect = monitorRect;
	}

	// 画布尺寸用偶数，避免 H.264 编码出现奇数宽高
	if (m_target.screenRect.valid) {
		m_target.screenRect.w = evenFloor(m_target.screenRect.w);
		m_target.screenRect.h = evenFloor(m_target.screenRect.h);
	}

	obs_data_set_string(settings, "monitor_id", monitorId.c_str());
	obs_data_set_bool(settings, "capture_cursor", config.captureCursor);
	obs_data_set_bool(settings, "force_sdr", false);
	obs_data_set_int(settings, "method", 0); // 自动

	source = obs_source_create(enums::ids::MonitorCapture, "显示器采集", settings, nullptr);
	obs_data_release(settings);

	if (!source && error)
		*error = "显示器采集源创建失败（win-capture 插件是否已打包？）";

	return source;
}

void SceneBuilder::applyRegionCrop(const AppConfig &config)
{
	if (!m_videoItem || config.captureMode != CaptureMode::Region || !config.region.valid || !m_monitorRect.valid)
		return;

	struct obs_sceneitem_crop crop;
	ZeroMemory(&crop, sizeof(crop));

	crop.left = config.region.x - m_monitorRect.x;
	crop.top = config.region.y - m_monitorRect.y;
	crop.right = m_monitorRect.w - crop.left - config.region.w;
	crop.bottom = m_monitorRect.h - crop.top - config.region.h;

	if (crop.right < 0 || crop.bottom < 0) {
		logWarn("区域裁剪量异常（left=%d top=%d right=%d bottom=%d），跳过裁剪", crop.left, crop.top, crop.right,
			crop.bottom);
		return;
	}

	obs_sceneitem_set_crop(m_videoItem, &crop);
	logInfo("区域裁剪已应用：left=%d top=%d right=%d bottom=%d", crop.left, crop.top, crop.right, crop.bottom);
}

void SceneBuilder::createAudioSources(const AppConfig &config)
{
	if (config.desktopAudio) {
		obs_data_t *settings = obs_data_create();
		obs_data_set_string(settings, "device_id", config.desktopDeviceId.c_str());
		m_desktopAudio = obs_source_create(enums::ids::DesktopAudio, "桌面音频", settings, nullptr);
		obs_data_release(settings);

		if (m_desktopAudio)
			obs_scene_add(m_scene, m_desktopAudio);
		else
			logWarn("桌面音频（系统声音）源创建失败，本次录制将只有麦克风或静音");
	}

	if (config.micAudio) {
		obs_data_t *settings = obs_data_create();
		obs_data_set_string(settings, "device_id", config.micDeviceId.c_str());
		m_micAudio = obs_source_create(enums::ids::Microphone, "麦克风", settings, nullptr);
		obs_data_release(settings);

		if (m_micAudio)
			obs_scene_add(m_scene, m_micAudio);
		else
			logWarn("麦克风源创建失败，本次录制将没有麦克风声音");
	}
}

void SceneBuilder::createCamera(const AppConfig &config)
{
	if (!config.cameraEnabled || config.cameraDeviceId.empty())
		return;

	if (!enums::hasSourceType(enums::ids::Camera)) {
		logWarn("win-dshow 插件未打包，摄像头画中画不可用");
		return;
	}

	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "video_device_id", config.cameraDeviceId.c_str());
	obs_data_set_bool(settings, "active", true);
	m_camera = obs_source_create(enums::ids::Camera, "摄像头", settings, nullptr);
	obs_data_release(settings);

	if (!m_camera) {
		logWarn("摄像头打开失败：%s", config.cameraDeviceName.c_str());
		return;
	}

	// 等摄像头出图，便于按真实比例摆放
	for (int waited = 0; waited < 1000; waited += 50) {
		if (obs_source_get_width(m_camera) > 0)
			break;
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}

	m_cameraItem = obs_scene_add(m_scene, m_camera);
}

void SceneBuilder::createMouseFx(const AppConfig &config)
{
	if (!config.mouseFxEnabled)
		return;

	if (!m_target.supportsMouseFx) {
		logInfo("当前采集方式（窗口）不支持鼠标点击特效，已自动跳过");
		return;
	}

	if (!config.region.valid && !m_target.screenRect.valid)
		return;

	int canvasWidth = m_target.screenRect.w;
	int canvasHeight = m_target.screenRect.h;
	capturedSize(&canvasWidth, &canvasHeight);

	obs_data_t *settings = obs_data_create();
	obs_data_set_int(settings, "radius", config.mouseFxRadius);
	obs_data_set_int(settings, "duration_ms", config.mouseFxDurationMs);
	obs_data_set_int(settings, "color", static_cast<int>(config.mouseFxColor));
	obs_data_set_int(settings, "width", canvasWidth > 0 ? canvasWidth : 1920);
	obs_data_set_int(settings, "height", canvasHeight > 0 ? canvasHeight : 1080);
	obs_data_set_int(settings, "capture_x", m_target.screenRect.x);
	obs_data_set_int(settings, "capture_y", m_target.screenRect.y);
	obs_data_set_int(settings, "capture_w", m_target.screenRect.w);
	obs_data_set_int(settings, "capture_h", m_target.screenRect.h);

	m_mouseFx = obs_source_create(mousefx::sourceId(), "鼠标点击特效", settings, nullptr);
	obs_data_release(settings);

	if (!m_mouseFx) {
		logWarn("鼠标点击特效源创建失败");
		return;
	}

	m_mouseFxItem = obs_scene_add(m_scene, m_mouseFx); // 后加入 → 绘制在采集画面之上
}

void SceneBuilder::waitForSourceSize(int timeoutMs)
{
	if (!m_videoSource)
		return;

	const int stepMs = 20;
	int waited = 0;

	while (waited < timeoutMs) {
		const uint32_t width = obs_source_get_width(m_videoSource);
		const uint32_t height = obs_source_get_height(m_videoSource);

		if (width > 0 && height > 0) {
			m_target.sourceWidth = static_cast<int>(width);
			m_target.sourceHeight = static_cast<int>(height);
			logInfo("采集源已就绪：%ux%u", width, height);
			return;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(stepMs));
		waited += stepMs;
	}

	logWarn("等待采集源尺寸超时（%d ms），将按显示器/选区尺寸继续", timeoutMs);
}

void SceneBuilder::capturedSize(int *width, int *height) const
{
	// 区域录制时画布是选区尺寸；显示器录制时画布是源尺寸（两者通常相等）
	if (m_target.screenRect.valid && m_target.screenRect.w > 0 && m_target.screenRect.h > 0) {
		if (width)
			*width = m_target.screenRect.w;
		if (height)
			*height = m_target.screenRect.h;
		return;
	}

	if (width)
		*width = m_target.sourceWidth;
	if (height)
		*height = m_target.sourceHeight;
}

void SceneBuilder::layout(int canvasWidth, int canvasHeight)
{
	if (!m_scene || canvasWidth <= 0 || canvasHeight <= 0)
		return;

	const vec2 canvas{static_cast<float>(canvasWidth), static_cast<float>(canvasHeight)};
	const vec2 origin{0.0f, 0.0f};

	// 采集画面：等比铺满画布（画布按采集尺寸设定，正常为 1:1）
	if (m_videoItem) {
		obs_sceneitem_set_alignment(m_videoItem, OBS_ALIGN_LEFT | OBS_ALIGN_TOP);
		obs_sceneitem_set_pos(m_videoItem, &origin);
		obs_sceneitem_set_bounds_type(m_videoItem, OBS_BOUNDS_SCALE_INNER);
		obs_sceneitem_set_bounds_alignment(m_videoItem, OBS_ALIGN_LEFT | OBS_ALIGN_TOP);
		obs_sceneitem_set_bounds(m_videoItem, &canvas);
		obs_sceneitem_set_scale_filter(m_videoItem, OBS_SCALE_BICUBIC);
	}

	// 鼠标特效：铺满整块画布，且尺寸与画布一致（像素坐标直接对应）
	if (m_mouseFxItem) {
		obs_sceneitem_set_alignment(m_mouseFxItem, OBS_ALIGN_LEFT | OBS_ALIGN_TOP);
		obs_sceneitem_set_pos(m_mouseFxItem, &origin);
		obs_sceneitem_set_bounds_type(m_mouseFxItem, OBS_BOUNDS_SCALE_INNER);
		obs_sceneitem_set_bounds_alignment(m_mouseFxItem, OBS_ALIGN_LEFT | OBS_ALIGN_TOP);
		obs_sceneitem_set_bounds(m_mouseFxItem, &canvas);

		obs_data_t *settings = obs_data_create();
		obs_data_set_int(settings, "width", canvasWidth);
		obs_data_set_int(settings, "height", canvasHeight);
		obs_source_update(m_mouseFx, settings);
		obs_data_release(settings);
	}

	// 摄像头画中画
	if (m_cameraItem && m_camera) {
		const float scale = static_cast<float>(canvasWidth) / 1920.0f;
		const float pipWidth = static_cast<float>(m_cameraWidth) * scale;
		const float margin = static_cast<float>(kPipMargin) * scale;

		const uint32_t cameraW = obs_source_get_width(m_camera);
		const uint32_t cameraH = obs_source_get_height(m_camera);
		const float aspect = (cameraW > 0 && cameraH > 0) ? static_cast<float>(cameraH) / cameraW
								 : 9.0f / 16.0f;
		const float pipHeight = pipWidth * aspect;

		vec2 bounds{pipWidth, pipHeight};
		vec2 position{margin, margin};
		uint32_t alignment = OBS_ALIGN_LEFT | OBS_ALIGN_TOP;

		switch (m_cameraCorner) {
		case OverlayCorner::TopLeft:
			position = {margin, margin};
			alignment = OBS_ALIGN_LEFT | OBS_ALIGN_TOP;
			break;
		case OverlayCorner::TopRight:
			position = {canvasWidth - margin, margin};
			alignment = OBS_ALIGN_RIGHT | OBS_ALIGN_TOP;
			break;
		case OverlayCorner::BottomLeft:
			position = {margin, canvasHeight - margin};
			alignment = OBS_ALIGN_LEFT | OBS_ALIGN_BOTTOM;
			break;
		case OverlayCorner::BottomRight:
		default:
			position = {canvasWidth - margin, canvasHeight - margin};
			alignment = OBS_ALIGN_RIGHT | OBS_ALIGN_BOTTOM;
			break;
		}

		obs_sceneitem_set_alignment(m_cameraItem, alignment);
		obs_sceneitem_set_bounds_type(m_cameraItem, OBS_BOUNDS_SCALE_INNER);
		obs_sceneitem_set_bounds_alignment(m_cameraItem, OBS_ALIGN_LEFT | OBS_ALIGN_TOP);
		obs_sceneitem_set_bounds(m_cameraItem, &bounds);
		obs_sceneitem_set_pos(m_cameraItem, &position);
	}

	logInfo("场景布局完成：画布 %dx%d", canvasWidth, canvasHeight);
}

void SceneBuilder::destroy()
{
	if (m_scene)
		obs_set_output_source(0, nullptr); // 先摘掉通道 0 的引用

	if (m_cameraItem) {
		obs_sceneitem_release(m_cameraItem);
		m_cameraItem = nullptr;
	}
	if (m_mouseFxItem) {
		obs_sceneitem_release(m_mouseFxItem);
		m_mouseFxItem = nullptr;
	}
	if (m_videoItem) {
		obs_sceneitem_release(m_videoItem);
		m_videoItem = nullptr;
	}

	if (m_camera) {
		obs_source_release(m_camera);
		m_camera = nullptr;
	}
	if (m_mouseFx) {
		obs_source_release(m_mouseFx);
		m_mouseFx = nullptr;
	}
	if (m_micAudio) {
		obs_source_release(m_micAudio);
		m_micAudio = nullptr;
	}
	if (m_desktopAudio) {
		obs_source_release(m_desktopAudio);
		m_desktopAudio = nullptr;
	}
	if (m_videoSource) {
		obs_source_release(m_videoSource);
		m_videoSource = nullptr;
	}

	m_sceneSource = nullptr;
	if (m_scene) {
		obs_scene_release(m_scene);
		m_scene = nullptr;
	}

	m_target = CaptureTarget();
	m_monitorRect = RegionRect();
}

} // namespace minirec
