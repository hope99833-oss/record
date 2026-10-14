#include "core/MouseFxSource.h"

#include <obs.h>
#include <util/platform.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "core/Logging.h"
#include "core/MouseEventHook.h"

namespace minirec {
namespace mousefx {

// 前置声明：源创建时要用到
void fxUpdate(void *data, obs_data_t *settings);

namespace {

const char *kSourceId = "minirec_mouse_fx";
constexpr size_t kMaxRipples = 16;

// 点击涟漪用的内嵌着色器：按到圆心的距离画一圈环，半径/透明度随时间衰减。
// 内嵌字符串可以避免额外的 data 文件依赖（gs_effect_create 直接编译）。
const char *kEffectSource = R"(
uniform float4x4 ViewProj;
uniform float2 quad_size;
uniform float2 center;
uniform float radius;
uniform float thickness;
uniform float4 color;

struct VertInOut {
	float4 pos : POSITION;
	float2 uv  : TEXCOORD0;
};

VertInOut VSDefault(VertInOut vert_in)
{
	VertInOut vert_out;
	vert_out.pos = mul(float4(vert_in.pos.xyz, 1.0), ViewProj);
	vert_out.uv  = vert_in.uv;
	return vert_out;
}

float4 PSRing(VertInOut vert_in) : TARGET
{
	float2 pixel = vert_in.uv * quad_size;
	float dist = distance(pixel, center);

	// 环带：|dist - radius| < thickness 时为 1，向外淡出
	float band = 1.0 - saturate(abs(dist - radius) / max(thickness, 0.5));
	// 按下瞬间的中心亮点，方便看清点在哪里
	float core = 1.0 - smoothstep(0.0, max(radius * 0.5, 1.0), dist);

	float alpha = saturate(band + core * 0.35);
	return float4(color.rgb, color.a * alpha);
}

technique Draw
{
	pass
	{
		vertex_shader = VSDefault(vert_in);
		pixel_shader  = PSRing(vert_in);
	}
}
)";

struct Ripple {
	float x = 0.0f;
	float y = 0.0f;
	uint64_t startNs = 0;
};

struct FxState {
	gs_effect_t *effect = nullptr;
	gs_eparam_t *paramQuadSize = nullptr;
	gs_eparam_t *paramCenter = nullptr;
	gs_eparam_t *paramRadius = nullptr;
	gs_eparam_t *paramThickness = nullptr;
	gs_eparam_t *paramColor = nullptr;

	int width = 1920;
	int height = 1080;
	float radius = 48.0f;
	int durationMs = 700;
	uint32_t color = 0xFFFFFFFF;

	// 屏幕坐标 -> 源坐标的映射（采集矩形）
	int captureX = 0;
	int captureY = 0;
	int captureW = 0;
	int captureH = 0;

	std::vector<Ripple> ripples;
};

const char *fxGetName(void *)
{
	return "鼠标点击特效";
}

void *fxCreate(obs_data_t *settings, obs_source_t *source)
{
	UNUSED_PARAMETER(source);

	auto *fx = new FxState();

	// 源是在主线程里通过 obs_source_create 创建的，此时 libobs 并未持有图形上下文，
	// gs_effect_create 会直接失败（日志里是"called while not in a graphics context"，
	// 且 error 为空，看上去像"未知错误"）。必须自己进一次图形上下文。
	obs_enter_graphics();

	char *error = nullptr;
	fx->effect = gs_effect_create(kEffectSource, "minirec_mouse_fx.effect", &error);

	obs_leave_graphics();

	if (!fx->effect) {
		logError("鼠标特效着色器编译失败：%s", error ? error : "未知错误");
		bfree(error);
		delete fx;
		return nullptr;
	}

	fx->paramQuadSize = gs_effect_get_param_by_name(fx->effect, "quad_size");
	fx->paramCenter = gs_effect_get_param_by_name(fx->effect, "center");
	fx->paramRadius = gs_effect_get_param_by_name(fx->effect, "radius");
	fx->paramThickness = gs_effect_get_param_by_name(fx->effect, "thickness");
	fx->paramColor = gs_effect_get_param_by_name(fx->effect, "color");

	fxUpdate(fx, settings);
	return fx;
}

void fxDestroy(void *data)
{
	auto *fx = static_cast<FxState *>(data);
	if (!fx)
		return;

	if (fx->effect) {
		obs_enter_graphics();
		gs_effect_destroy(fx->effect);
		obs_leave_graphics();
		fx->effect = nullptr;
	}
	delete fx;
}

void fxDefaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "width", 1920);
	obs_data_set_default_int(settings, "height", 1080);
	obs_data_set_default_int(settings, "radius", 48);
	obs_data_set_default_int(settings, "duration_ms", 700);
	obs_data_set_default_int(settings, "color", 0xFFFFFFFF);
}

uint32_t fxGetWidth(void *data)
{
	auto *fx = static_cast<FxState *>(data);
	return fx ? static_cast<uint32_t>(fx->width) : 0;
}

uint32_t fxGetHeight(void *data)
{
	auto *fx = static_cast<FxState *>(data);
	return fx ? static_cast<uint32_t>(fx->height) : 0;
}

void fxRender(void *data, gs_effect_t *)
{
	auto *fx = static_cast<FxState *>(data);
	if (!fx || !fx->effect || fx->width <= 0 || fx->height <= 0)
		return;

	const float scaleX = fx->captureW > 0 ? static_cast<float>(fx->width) / fx->captureW : 1.0f;
	const float scaleY = fx->captureH > 0 ? static_cast<float>(fx->height) / fx->captureH : 1.0f;

	// 取出全局钩子采集到的点击
	MouseClickEvent events[64];
	const size_t count = MouseEventHook::instance().drain(events, 64);
	for (size_t i = 0; i < count; i++) {
		if (!events[i].down)
			continue; // 只对按下做特效（与主流录屏软件一致）

		Ripple ripple;
		ripple.x = (events[i].x - fx->captureX) * scaleX;
		ripple.y = (events[i].y - fx->captureY) * scaleY;
		ripple.startNs = events[i].timeNs;

		// 点击落在采集范围外时忽略（例如点了副屏）
		if (ripple.x < -fx->radius || ripple.y < -fx->radius || ripple.x > fx->width + fx->radius ||
		    ripple.y > fx->height + fx->radius)
			continue;

		fx->ripples.push_back(ripple);
		if (fx->ripples.size() > kMaxRipples)
			fx->ripples.erase(fx->ripples.begin());
	}

	const uint64_t now = os_gettime_ns();
	const uint64_t durationNs = static_cast<uint64_t>(std::max(fx->durationMs, 100)) * 1000000ull;

	// 清理过期的涟漪
	fx->ripples.erase(std::remove_if(fx->ripples.begin(), fx->ripples.end(),
					 [now, durationNs](const Ripple &ripple) {
						 return now < ripple.startNs ||
							now - ripple.startNs > durationNs;
					 }),
			  fx->ripples.end());

	if (fx->ripples.empty())
		return;

	const vec2 quadSize{static_cast<float>(fx->width), static_cast<float>(fx->height)};
	const float baseAlpha = static_cast<float>((fx->color >> 24) & 0xFF) / 255.0f;
	const float red = static_cast<float>((fx->color >> 16) & 0xFF) / 255.0f;
	const float green = static_cast<float>((fx->color >> 8) & 0xFF) / 255.0f;
	const float blue = static_cast<float>(fx->color & 0xFF) / 255.0f;

	gs_technique_t *technique = gs_effect_get_technique(fx->effect, "Draw");
	if (!technique)
		return;

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA);
	gs_effect_set_vec2(fx->paramQuadSize, &quadSize);

	for (const Ripple &ripple : fx->ripples) {
		const float progress = static_cast<float>(now - ripple.startNs) / static_cast<float>(durationNs);
		const float clamped = std::min(std::max(progress, 0.0f), 1.0f);

		// 半径由小变大，透明度由强变弱
		const float radius = fx->radius * (0.35f + 0.65f * clamped);
		const float thickness = std::max(fx->radius * 0.22f * (1.0f - clamped * 0.5f), 1.0f);
		const float alpha = baseAlpha * (1.0f - clamped) * (1.0f - clamped);

		const vec2 center{ripple.x, ripple.y};
		vec4 color{red, green, blue, alpha};

		gs_effect_set_vec2(fx->paramCenter, &center);
		gs_effect_set_float(fx->paramRadius, radius);
		gs_effect_set_float(fx->paramThickness, thickness);
		gs_effect_set_vec4(fx->paramColor, &color);

		gs_technique_begin(technique);
		gs_technique_begin_pass(technique, 0);
		gs_draw_sprite(nullptr, 0, static_cast<uint32_t>(fx->width), static_cast<uint32_t>(fx->height));
		gs_technique_end_pass(technique);
		gs_technique_end(technique);
	}

	gs_blend_state_pop();
}

struct obs_source_info g_fxInfo = {};

} // namespace

void fxUpdate(void *data, obs_data_t *settings)
{
	auto *fx = static_cast<FxState *>(data);
	if (!fx)
		return;

	fx->width = static_cast<int>(obs_data_get_int(settings, "width"));
	fx->height = static_cast<int>(obs_data_get_int(settings, "height"));
	fx->radius = static_cast<float>(obs_data_get_int(settings, "radius"));
	fx->durationMs = static_cast<int>(obs_data_get_int(settings, "duration_ms"));
	fx->color = static_cast<uint32_t>(obs_data_get_int(settings, "color"));

	fx->captureX = static_cast<int>(obs_data_get_int(settings, "capture_x"));
	fx->captureY = static_cast<int>(obs_data_get_int(settings, "capture_y"));
	fx->captureW = static_cast<int>(obs_data_get_int(settings, "capture_w"));
	fx->captureH = static_cast<int>(obs_data_get_int(settings, "capture_h"));

	if (fx->width <= 0)
		fx->width = 1920;
	if (fx->height <= 0)
		fx->height = 1080;
}

const char *sourceId()
{
	return kSourceId;
}

void registerSource()
{
	memset(&g_fxInfo, 0, sizeof(g_fxInfo));
	g_fxInfo.id = kSourceId;
	g_fxInfo.type = OBS_SOURCE_TYPE_INPUT;
	g_fxInfo.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
	g_fxInfo.get_name = fxGetName;
	g_fxInfo.create = fxCreate;
	g_fxInfo.destroy = fxDestroy;
	g_fxInfo.update = fxUpdate;
	g_fxInfo.get_defaults = fxDefaults;
	g_fxInfo.get_width = fxGetWidth;
	g_fxInfo.get_height = fxGetHeight;
	g_fxInfo.video_render = fxRender;
	g_fxInfo.icon_type = OBS_ICON_TYPE_COLOR;

	obs_register_source(&g_fxInfo);
	logInfo("已注册鼠标点击特效源：%s", kSourceId);
}

} // namespace mousefx
} // namespace minirec
