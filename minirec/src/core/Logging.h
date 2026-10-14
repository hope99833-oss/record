#pragma once

#include <string>

namespace minirec {

// 把 libobs 的日志（blog）以及本程序自身的日志统一写入
//   <logDir>/minirec-YYYYMMDD.log
// 同时始终往 <exe 同级>/minirec-startup.log 追加一份，保证 %APPDATA% 不可写时
// 也能查到"双击没反应"的原因。启动时清理超过 7 天的日志文件。
void initLogging(const std::string &logDir);
void shutdownLogging();

// 在 initLogging 之前也能留痕（例如 QApplication / 单实例 / 配置加载阶段就挂了）。
// 只写 <exe 同级>/minirec-startup.log。
void traceStartup(const char *stage);

void logInfo(const char *format, ...);
void logWarn(const char *format, ...);
void logError(const char *format, ...);

// 最近一次错误信息（用于启动自检时给用户展示）
std::string lastFatalError();
void setFatalError(const std::string &message);

} // namespace minirec
