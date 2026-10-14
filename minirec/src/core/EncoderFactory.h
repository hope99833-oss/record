#pragma once

#include <string>
#include <vector>

#include "app/AppConfig.h"

struct obs_encoder;
typedef struct obs_encoder obs_encoder_t;

namespace minirec {

// 编码器选择策略：按优先级探测可用硬编，失败自动回退到下一个
class EncoderFactory {
public:
	// 按配置与硬件能力选择视频编码器；chooseLog 记录尝试过程（写入日志与界面提示）
	static obs_encoder_t *createVideoEncoder(const AppConfig &config, const std::string &name, std::string *chosenId,
						 std::vector<std::string> *attemptLog);
	static obs_encoder_t *createAudioEncoder(const AppConfig &config, const std::string &name);

	// 候选列表（未安装驱动/显卡不支持时会在 create 阶段被跳过）
	static std::vector<std::string> videoEncoderPriority();
};

} // namespace minirec
