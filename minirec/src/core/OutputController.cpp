#include "core/OutputController.h"

#include <obs.h>

#include <cstring>

#include "core/EncoderFactory.h"
#include "core/EnumHelpers.h"
#include "core/Logging.h"

namespace minirec {

OutputController::OutputController() = default;

OutputController::~OutputController()
{
	destroy();
}

bool OutputController::prepare(const AppConfig &config, const std::string &outputPath, std::string *error)
{
	destroy();

	// ffmpeg_muxer：本地录像输出，自带 CAN_PAUSE，编码数据经 IPC 交给 obs-ffmpeg-mux.exe 落盘
	m_output = obs_output_create(enums::ids::FfmpegMuxer, "minirec_recording", nullptr, nullptr);
	if (!m_output) {
		if (error)
			*error = "创建录像输出失败（obs-ffmpeg 插件是否已打包？）";
		return false;
	}

	obs_data_t *outputSettings = obs_data_create();
	obs_data_set_string(outputSettings, "path", outputPath.c_str());
	obs_data_set_string(outputSettings, "format", "mkv"); // 容器由扩展名/此键共同决定，显式声明更稳妥
	obs_data_set_string(outputSettings, "muxer_settings", "");
	obs_output_update(m_output, outputSettings);
	obs_data_release(outputSettings);

	// ---- 视频编码器（含硬编回退） ----
	std::string chosenEncoder;
	std::vector<std::string> attempts;
	m_videoEncoder = EncoderFactory::createVideoEncoder(config, "minirec_video", &chosenEncoder, &attempts);
	for (const std::string &line : attempts)
		logInfo("编码器探测：%s", line.c_str());

	if (!m_videoEncoder) {
		if (error)
			*error = "没有可用的视频编码器（硬编与 x264 均创建失败）";
		destroy();
		return false;
	}

	obs_encoder_t *videoEncoder = static_cast<obs_encoder_t *>(m_videoEncoder);
	obs_encoder_set_video(videoEncoder, obs_get_video());

	// ---- 音频编码器（AAC） ----
	m_audioEncoder = EncoderFactory::createAudioEncoder(config, "minirec_audio");
	if (!m_audioEncoder) {
		if (error)
			*error = "音频编码器创建失败（ffmpeg_aac 不可用）";
		destroy();
		return false;
	}

	obs_encoder_t *audioEncoder = static_cast<obs_encoder_t *>(m_audioEncoder);
	obs_encoder_set_audio(audioEncoder, obs_get_audio());

	// ---- 组装输出 ----
	obs_output_set_video_encoder(m_output, videoEncoder);
	obs_output_set_audio_encoder(m_output, audioEncoder, 0);

	signal_handler_t *handler = obs_output_get_signal_handler(m_output);
	signal_handler_connect(handler, "stop", &OutputController::onStopSignal, this);

	m_recordingPath = outputPath;
	m_paused = false;
	logInfo("录像输出已就绪：%s（编码器 %s）", outputPath.c_str(), chosenEncoder.c_str());
	return true;
}

bool OutputController::start(std::string *error)
{
	if (!m_output) {
		if (error)
			*error = "输出尚未创建";
		return false;
	}

	if (!obs_output_start(m_output)) {
		const char *lastError = obs_output_get_last_error(m_output);
		if (error)
			*error = std::string("启动录制失败：") + (lastError && *lastError ? lastError : "未知原因");
		logError("obs_output_start 失败：%s", lastError ? lastError : "(null)");
		return false;
	}

	logInfo("录制已启动");
	return true;
}

void OutputController::stop()
{
	if (!m_output || !obs_output_active(m_output))
		return;

	logInfo("请求停止录制…");
	obs_output_stop(m_output); // 异步：等 "stop" 信号回来才算结束
}

void OutputController::setPaused(bool paused)
{
	if (!m_output || !obs_output_active(m_output))
		return;

	if (!obs_output_can_pause(m_output))
		return;

	if (obs_output_pause(m_output, paused)) {
		m_paused = paused;
		logInfo("%s", paused ? "录制已暂停" : "录制已继续");
	} else {
		logWarn("暂停/继续操作被输出拒绝");
	}
}

bool OutputController::active() const
{
	return m_output && obs_output_active(m_output);
}

uint64_t OutputController::totalBytes() const
{
	return m_output ? obs_output_get_total_bytes(m_output) : 0;
}

double OutputController::droppedPercent() const
{
	if (!m_output)
		return 0.0;

	const int total = obs_output_get_total_frames(m_output);
	const int dropped = obs_output_get_frames_dropped(m_output);
	if (total <= 0)
		return 0.0;
	return (static_cast<double>(dropped) / static_cast<double>(total)) * 100.0;
}

void OutputController::destroy()
{
	if (!m_output)
		return;

	signal_handler_t *handler = obs_output_get_signal_handler(m_output);
	if (handler)
		signal_handler_disconnect(handler, "stop", &OutputController::onStopSignal, this);

	// 输出持有编码器引用，先释放输出再释放编码器
	obs_output_release(m_output);
	m_output = nullptr;

	if (m_videoEncoder) {
		obs_encoder_release(static_cast<obs_encoder_t *>(m_videoEncoder));
		m_videoEncoder = nullptr;
	}
	if (m_audioEncoder) {
		obs_encoder_release(static_cast<obs_encoder_t *>(m_audioEncoder));
		m_audioEncoder = nullptr;
	}

	m_paused = false;
}

void OutputController::onStopSignal(void *data, calldata_t *params)
{
	UNUSED_PARAMETER(params);

	auto *self = static_cast<OutputController *>(data);
	if (!self)
		return;

	logInfo("输出已停止（文件：%s）", self->m_recordingPath.c_str());

	// 该回调运行在 libobs 的输出线程：这里只做转发，实际的资源释放由上层在主线程完成
	if (self->m_stopCallback)
		self->m_stopCallback(self->m_recordingPath);
}

} // namespace minirec
