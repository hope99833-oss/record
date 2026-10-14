#pragma once

#include <string>

namespace minirec {

// 录完 MKV 后转封装成 MP4（流拷贝 + faststart）。
// 使用 libobs 自带的 media-remux（与 OBS 界面里的"重新封装录像"同一套实现）。
class Remux {
public:
	static bool remuxToMp4(const std::string &inputFile, const std::string &outputFile, std::string *error,
			       int *percentOut = nullptr);
};

} // namespace minirec
