#include "core/Remux.h"

#include <obs.h>
#include <media-io/media-remux.h>

#include "core/Logging.h"

namespace minirec {
namespace {

bool onProgress(void *data, float percent)
{
	if (data)
		*static_cast<int *>(data) = static_cast<int>(percent * 100.0f);
	return true; // 返回 false 可中断
}

} // namespace

bool Remux::remuxToMp4(const std::string &inputFile, const std::string &outputFile, std::string *error, int *percentOut)
{
	media_remux_job_t job = nullptr;
	if (!media_remux_job_create(&job, inputFile.c_str(), outputFile.c_str())) {
		if (error)
			*error = "无法创建转封装任务（源文件不可读或格式不受支持）";
		logError("转封装失败：无法打开 %s", inputFile.c_str());
		return false;
	}

	const bool ok = media_remux_job_process(job, onProgress, percentOut);
	media_remux_job_destroy(job);

	if (!ok) {
		if (error)
			*error = "转封装失败（磁盘空间不足或源文件损坏）";
		logError("转封装失败：%s -> %s", inputFile.c_str(), outputFile.c_str());
		return false;
	}

	logInfo("转封装完成：%s", outputFile.c_str());
	return true;
}

} // namespace minirec
