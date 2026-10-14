#pragma once

#include <string>

#include "app/AppConfig.h"

namespace minirec {

// 鼠标点击特效滤镜：以宿主内注册的 obs_source_info（video filter）实现，
// 不额外做插件 DLL。坐标数据来自 MouseEventHook 的全局鼠标钩子。
namespace mousefx {

// 注册滤镜源类型（libobs 启动后、创建场景前调用一次）
void registerSource();

// 源类型 id
const char *sourceId();

} // namespace mousefx
} // namespace minirec
