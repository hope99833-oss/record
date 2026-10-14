#pragma once

#include <cstdint>
#include <string>

namespace minirec {

// 全局热键绑定（vk 为 Windows 虚拟键码）
struct HotkeyBinding {
	int vk = 0;
	uint32_t modifiers = 0; // MOD_ALT / MOD_CONTROL / MOD_SHIFT / MOD_WIN，用于 UI 展示
};

// 录制区域（屏幕物理像素坐标）
struct RegionRect {
	int x = 0;
	int y = 0;
	int w = 0;
	int h = 0;
	bool valid = false;
};

enum class CaptureMode { Monitor = 0, Window = 1, Region = 2 };

// 画中画位置
enum class OverlayCorner { TopLeft = 0, TopRight = 1, BottomLeft = 2, BottomRight = 3 };

struct AppConfig {
	// ---- 捕获目标 ----
	CaptureMode captureMode = CaptureMode::Monitor;
	int monitorIndex = 0;
	std::string windowId; // "window_capture" 的 window 设置值
	std::string windowName; // 仅用于界面展示
	RegionRect region;

	// ---- 音频 ----
	bool desktopAudio = true;
	std::string desktopDeviceId = "default";
	bool micAudio = false;
	std::string micDeviceId = "default";

	// ---- 视频 / 质量 ----
	int fps = 60;
	int outputWidth = 1920;
	int outputHeight = 1080;
	int quality = 1; // 0=低 1=中 2=高 3=原始分辨率（跟随屏幕）
	std::string encoderOverride; // 非空时固定使用该编码器 id，不自动回退
	bool captureCursor = true;   // 是否把鼠标光标一并录制进画面

	// ---- 摄像头画中画 ----
	bool cameraEnabled = false;
	std::string cameraDeviceId;   // dshow 设备 id（"name:xxx" 形式由驱动决定）
	std::string cameraDeviceName; // 展示用
	OverlayCorner cameraCorner = OverlayCorner::BottomRight;
	int cameraWidth = 320; // 画中画宽度（像素），高度按比例

	// ---- 鼠标点击特效 ----
	bool mouseFxEnabled = true;
	int mouseFxRadius = 48;
	int mouseFxDurationMs = 700;
	uint32_t mouseFxColor = 0xFFFFFFFF; // ARGB

	// ---- 输出 ----
	std::string outputDir; // 空 = %USERPROFILE%\Videos\MiniRec
	bool autoRemuxMp4 = true;
	bool keepMkv = true;
	int countdownSeconds = 3;

	// ---- 热键 ----
	HotkeyBinding hkStartStop{0x78, 0}; // VK_F9
	HotkeyBinding hkPause{0x79, 0};     // VK_F10

	// ---- 首次运行 / 崩溃恢复标记 ----
	bool crashRecoveryAccepted = false;

	static AppConfig load(const std::string &path);
	void save(const std::string &path) const;

	// 质量预设：0=低 1=中 2=高
	void applyQualityPreset(int preset);
};

} // namespace minirec
