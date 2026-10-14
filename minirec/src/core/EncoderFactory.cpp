#include "core/EncoderFactory.h"

#include <obs.h>

#include <cstring>

#include "core/EnumHelpers.h"
#include "core/Logging.h"

namespace minirec {
namespace {

// 质量预设 -> 具体编码参数的映射（0=低 1=中 2=高）
struct QualityParams {
	int bitrateKbps;   // 直播式码控用
	int cqp;           // 恒定质量用
	int audioBitrate;  // kbps
};

QualityParams qualityParams(int preset)
{
	switch (preset) {
	case 0:
		return {4000, 24, 128};
	case 2:
		return {12000, 18, 192};
	case 3: // 原始分辨率：2K 用更高码率档 + 更紧的 CQP
		return {20000, 18, 192};
	case 1:
	default:
		return {8000, 22, 160};
	}
}

const char *encoderKindName(const std::string &id)
{
	if (id.rfind("obs_nvenc", 0) == 0)
		return "NVIDIA NVENC";
	if (id.rfind("obs_qsv11", 0) == 0)
		return "Intel Quick Sync";
	if (id.find("amf") != std::string::npos)
		return "AMD AMF";
	if (id.rfind("obs_x264", 0) == 0)
		return "x264（CPU）";
	return id.c_str();
}

// 按编码器家族填充设置键：不同硬编对 rate_control 取值大小写与预设键名并不统一，
// 这里分别处理（依据 obs-nvenc / obs-qsv11 / obs-x264 / texture-amf 的属性定义）。
void applyVideoSettings(const std::string &id, obs_data_t *settings, const QualityParams &params, bool highQuality)
{
	if (id.rfind("obs_nvenc", 0) == 0) {
		obs_data_set_string(settings, "rate_control", "CQP");
		obs_data_set_int(settings, "cqp", params.cqp);
		obs_data_set_int(settings, "bitrate", params.bitrateKbps);
		obs_data_set_string(settings, "preset", highQuality ? "p5" : "p4");
		obs_data_set_string(settings, "tuning", "hq");
		obs_data_set_string(settings, "multipass", highQuality ? "qres" : "disabled");
		obs_data_set_string(settings, "profile", "high");
		obs_data_set_int(settings, "keyint_sec", 2);
		obs_data_set_int(settings, "gpu", 0);
		obs_data_set_int(settings, "bf", 2);
		return;
	}

	if (id.rfind("obs_qsv11", 0) == 0) {
		obs_data_set_string(settings, "rate_control", "CQP");
		obs_data_set_int(settings, "cqp", params.cqp);
		obs_data_set_int(settings, "icq_quality", params.cqp);
		obs_data_set_int(settings, "bitrate", params.bitrateKbps);
		obs_data_set_int(settings, "max_bitrate", params.bitrateKbps * 2);
		obs_data_set_string(settings, "profile", "high");
		obs_data_set_string(settings, "target_usage", highQuality ? "TU4" : "TU7");
		obs_data_set_string(settings, "latency", "normal");
		obs_data_set_int(settings, "keyint_sec", 2);
		return;
	}

	if (id.find("amf") != std::string::npos) {
		obs_data_set_string(settings, "rate_control", "CQP");
		obs_data_set_int(settings, "cqp", params.cqp);
		obs_data_set_int(settings, "bitrate", params.bitrateKbps);
		obs_data_set_string(settings, "profile", "high");
		obs_data_set_int(settings, "keyint_sec", 2);
		return;
	}

	// x264 兜底：恒定质量 + veryfast 预设，兼顾画质与 CPU 占用
	obs_data_set_string(settings, "rate_control", "CQP");
	obs_data_set_int(settings, "cqp", params.cqp);
	obs_data_set_int(settings, "bitrate", params.bitrateKbps);
	obs_data_set_string(settings, "preset", highQuality ? "faster" : "veryfast");
	obs_data_set_string(settings, "profile", "high");
	obs_data_set_int(settings, "keyint_sec", 2);
}

} // namespace

std::vector<std::string> EncoderFactory::videoEncoderPriority()
{
	// 注意：某个编码器是否**真正可用**要看创建是否成功（显卡/驱动不支持时插件会拒绝创建），
	// 因此这里只给出优先级顺序。
	return {
		"obs_nvenc_h264_tex", // NVIDIA NVENC（GPU 纹理直通）
		"h264_texture_amf",   // AMD AMF
		"obs_qsv11_v2",       // Intel Quick Sync
		"obs_x264",           // 软件兜底
	};
}

obs_encoder_t *EncoderFactory::createVideoEncoder(const AppConfig &config, const std::string &name, std::string *chosenId,
						  std::vector<std::string> *attemptLog)
{
	std::vector<std::string> candidates;
	if (!config.encoderOverride.empty())
		candidates.push_back(config.encoderOverride);
	else
		candidates = videoEncoderPriority();

	const QualityParams params = qualityParams(config.quality);
	const bool highQuality = config.quality >= 2;

	obs_data_t *settings = obs_data_create();
	obs_encoder_t *encoder = nullptr;

	for (const std::string &id : candidates) {
		if (!enums::hasEncoderType(id.c_str())) {
			const std::string message = id + "：未注册（对应插件未打包）";
			if (attemptLog)
				attemptLog->push_back(message);
			logInfo("跳过编码器 %s", message.c_str());
			continue;
		}

		encoder = obs_video_encoder_create(id.c_str(), name.c_str(), settings, nullptr);
		if (!encoder) {
			const std::string message = id + "：创建失败（通常是显卡/驱动不支持）";
			if (attemptLog)
				attemptLog->push_back(message);
			logWarn("编码器 %s 创建失败，尝试下一个", id.c_str());
			continue;
		}

		obs_data_t *encoderSettings = obs_data_create();
		applyVideoSettings(id, encoderSettings, params, highQuality);
		obs_encoder_update(encoder, encoderSettings);
		obs_data_release(encoderSettings);

		// 硬编码要求 NV12（配合 obs_reset_video 的 gpu_conversion）
		const char *codec = obs_get_encoder_codec(id.c_str());
		if (codec && strcmp(codec, "h264") == 0)
			obs_encoder_set_preferred_video_format(encoder, VIDEO_FORMAT_NV12);

		if (chosenId)
			*chosenId = id;
		if (attemptLog)
			attemptLog->push_back(id + "：可用（" + encoderKindName(id) + "）");
		logInfo("已选择视频编码器：%s（%s），质量档=%d", id.c_str(), encoderKindName(id), config.quality);
		break;
	}

	obs_data_release(settings);
	return encoder;
}

obs_encoder_t *EncoderFactory::createAudioEncoder(const AppConfig &config, const std::string &name)
{
	const QualityParams params = qualityParams(config.quality);

	obs_data_t *settings = obs_data_create();
	obs_data_set_int(settings, "bitrate", params.audioBitrate);

	obs_encoder_t *encoder = obs_audio_encoder_create(enums::ids::AacEncoder, name.c_str(), settings, 0, nullptr);
	if (!encoder)
		logError("AAC 音频编码器创建失败（obs-ffmpeg 是否已打包？）");

	obs_data_release(settings);
	return encoder;
}

} // namespace minirec
