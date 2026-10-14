#pragma once

#include <string>
#include <vector>

#include "app/AppConfig.h"

struct obs_source;
typedef struct obs_source obs_source_t;
struct obs_scene;
typedef struct obs_scene obs_scene_t;
// 注意：libobs 里场景项的标签名是 obs_scene_item（obs.h 中的 typedef 也是它）
struct obs_scene_item;
typedef struct obs_scene_item obs_sceneitem_t;

namespace minirec {

// 采集目标描述（供鼠标特效做屏幕坐标 -> 源坐标映射）
struct CaptureTarget {
	std::string sourceId;
	RegionRect screenRect; // 采集内容在屏幕上的物理像素矩形（区域录制时为选区）
	int sourceWidth = 0;   // 采集源实际输出的像素尺寸（未就绪时为 0）
	int sourceHeight = 0;
	bool supportsMouseFx = false;
};

// 把 AppConfig 翻译成 libobs 的场景/源图（采集源 + 音频源 + 摄像头 + 鼠标特效）
class SceneBuilder {
public:
	SceneBuilder() = default;
	~SceneBuilder();

	// 创建场景与所有源；返回 false 时 error 有原因
	bool build(const AppConfig &config, std::string *error);

	// 释放所有 libobs 对象（必须在 libobs 仍可用时调用）
	void destroy();

	obs_source_t *sceneSource() const { return m_sceneSource; }
	const CaptureTarget &target() const { return m_target; }
	bool mouseFxActive() const { return m_mouseFx != nullptr; }

	// 等采集源给出尺寸（DXGI/窗口捕获是异步的），最多等待 timeoutMs 毫秒
	void waitForSourceSize(int timeoutMs);

	// 画布尺寸确定后重新布局：采集画面铺满画布、摄像头画中画、鼠标特效对齐
	void layout(int canvasWidth, int canvasHeight);

	// 采集源原始尺寸（未就绪返回 0）
	int sourceWidth() const { return m_target.sourceWidth; }
	int sourceHeight() const { return m_target.sourceHeight; }

	// 区域录制时画布应使用的尺寸
	void capturedSize(int *width, int *height) const;

private:
	obs_source_t *createCaptureSource(const AppConfig &config, std::string *error);
	void createAudioSources(const AppConfig &config);
	void createCamera(const AppConfig &config);
	void createMouseFx(const AppConfig &config);
	void applyRegionCrop(const AppConfig &config);
	// 依据显示器设备 id 找到它的物理矩形（与 win-capture 生成 monitor_id 的方式一致）
	static bool monitorRectFromId(const std::string &monitorId, RegionRect *out);

	obs_scene_t *m_scene = nullptr;
	obs_source_t *m_sceneSource = nullptr;
	obs_source_t *m_videoSource = nullptr;
	obs_source_t *m_desktopAudio = nullptr;
	obs_source_t *m_micAudio = nullptr;
	obs_source_t *m_camera = nullptr;
	obs_source_t *m_mouseFx = nullptr;

	obs_sceneitem_t *m_videoItem = nullptr;
	obs_sceneitem_t *m_cameraItem = nullptr;
	obs_sceneitem_t *m_mouseFxItem = nullptr;

	CaptureTarget m_target;
	RegionRect m_monitorRect; // 采集源所在显示器的物理矩形（区域裁剪用）
	int m_cameraWidth = 320;
	OverlayCorner m_cameraCorner = OverlayCorner::BottomRight;
};

} // namespace minirec
