#pragma once

#include <functional>
#include <string>

#include "app/AppConfig.h"

struct obs_output;
typedef struct obs_output obs_output_t;

struct calldata;
typedef struct calldata calldata_t;

namespace minirec {

// 录像输出（ffmpeg_muxer）的创建 / 启动 / 暂停 / 停止，以及停止后的收尾（remux）。
// 所有 libobs 回调都发生在 libobs 的输出线程上，这里只做状态缓存，不做 UI 操作。
class OutputController {
public:
	using StopCallback = std::function<void(const std::string &recordingPath)>;

	OutputController();
	~OutputController();

	// 创建输出并挂好编码器；返回 false 时 error 有原因
	bool prepare(const AppConfig &config, const std::string &outputPath, std::string *error);
	bool start(std::string *error);
	void stop();
	void setPaused(bool paused);
	bool paused() const { return m_paused; }
	bool active() const;
	void destroy();

	void setStopCallback(StopCallback callback) { m_stopCallback = std::move(callback); }

	const std::string &recordingPath() const { return m_recordingPath; }

	// 状态/统计
	uint64_t totalBytes() const;
	double droppedPercent() const;

private:
	static void onStopSignal(void *data, calldata_t *params);

	obs_output_t *m_output = nullptr;
	void *m_videoEncoder = nullptr; // obs_encoder_t*
	void *m_audioEncoder = nullptr;
	std::string m_recordingPath;
	bool m_paused = false;
	StopCallback m_stopCallback;
};

} // namespace minirec
