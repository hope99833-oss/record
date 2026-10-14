#pragma once

#include <string>

namespace minirec {

// libobs 运行时：进程级唯一，负责 startup / 模块加载 / 视音频参数设置 / shutdown。
// 初始化顺序严格按 libobs 要求：obs_startup -> 注册模块与数据路径 -> 重置视频/音频 -> 加载模块。
class LibobsRuntime {
public:
	static LibobsRuntime &instance();

	// 返回 false 时 error 里有可直接展示给用户的原因
	bool init(std::string *error);
	void shutdown();
	bool ready() const { return m_ready; }

	// 录制前按配置应用视频参数（分辨率/帧率）。baseWidth/baseHeight 传 0 表示跟随采集源。
	bool applyVideo(int fps, int outputWidth, int outputHeight, int baseWidth, int baseHeight, std::string *error);
	bool applyAudio(std::string *error);

	const std::string &exeDir() const { return m_exeDir; }
	const std::string &dataDir() const { return m_dataDir; }
	const std::string &pluginDir() const { return m_pluginDir; }
	const std::string &configDir() const { return m_configDir; }

	// 关键运行时文件自检（缺少 obs.dll / obs-ffmpeg-mux.exe / data/libobs 时给出明确提示）
	bool selfCheck(std::string *error) const;

private:
	LibobsRuntime() = default;
	~LibobsRuntime() = default;
	LibobsRuntime(const LibobsRuntime &) = delete;
	LibobsRuntime &operator=(const LibobsRuntime &) = delete;

	bool ensureGraphicsModule(std::string *error);

	bool m_started = false;
	bool m_ready = false;
	std::string m_exeDir;
	std::string m_dataDir;
	std::string m_pluginDir;
	std::string m_configDir;
};

} // namespace minirec
