#pragma once

#include <string>
#include <vector>

// 采集/编码能力枚举。
// 关键点：显示器、窗口、音频/视频设备的候选项**不自研枚举**，而是直接读取对应插件的
// obs_properties_t（obs_get_source_properties + obs_property_list_*），这样得到的取值格式
// 与插件内部完全一致（例如 monitor_id 是设备接口路径、音频是 device_id、摄像头是 video_device_id），
// 避免自己拼错设置键或取值格式。
namespace minirec {
namespace enums {

struct PropertyItem {
	std::string value; // 写进源设置的原始值
	std::string label; // 给用户看的描述（插件已包含分辨率/主显示器等提示）
};

// 读取源类型（如 "monitor_capture"）下某个列表属性的候选项
std::vector<PropertyItem> listItems(const char *sourceId, const char *propertyName);

// 读取源类型下某个字符串属性的默认值（无则返回空串）
std::string defaultString(const char *sourceId, const char *propertyName);

// 采集源 / 编码器 / 输出类型是否已注册（对应插件是否被打包）
bool hasSourceType(const char *id);
bool hasEncoderType(const char *id);
bool hasOutputType(const char *id);

// 已注册的编码器列表（设置页展示与诊断用）
std::vector<std::string> encoderTypes();

// 便捷封装：显示器 / 窗口 / 桌面音频 / 麦克风 / 摄像头
std::vector<PropertyItem> monitors();
std::vector<PropertyItem> windows();
std::vector<PropertyItem> desktopAudioDevices();
std::vector<PropertyItem> microphoneDevices();
std::vector<PropertyItem> cameraDevices();

// OBS 内建源类型 id（供各处引用，避免散落魔法字符串）
namespace ids {
constexpr const char *MonitorCapture = "monitor_capture";
constexpr const char *WindowCapture = "window_capture";
constexpr const char *DesktopAudio = "wasapi_output_capture";
constexpr const char *Microphone = "wasapi_input_capture";
constexpr const char *Camera = "dshow_input";
constexpr const char *FfmpegMuxer = "ffmpeg_muxer";
constexpr const char *AacEncoder = "ffmpeg_aac";
constexpr const char *SilenceSource = "silence_source";
constexpr const char *ColorSource = "color_source";
} // namespace ids

// 静音占位源：设备异常时兜底，避免输出等待音频数据
bool silenceSourceAvailable();
const char *silenceSourceId();

} // namespace enums
} // namespace minirec
