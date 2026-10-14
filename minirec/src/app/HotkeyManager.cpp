#include "app/HotkeyManager.h"

#include <obs.h>

#include <QMetaObject>
#include <QStringList>

#include <windows.h>

#include "core/Logging.h"

namespace minirec {
namespace {

obs_key_combination_t toCombination(const HotkeyBinding &binding)
{
	obs_key_combination_t combination = {};
	combination.modifiers = binding.modifiers; // 使用 libobs 的 INTERACT_* 标志
	combination.key = obs_key_from_virtual_key(binding.vk);
	return combination;
}

} // namespace

HotkeyManager::HotkeyManager(QObject *parent) : QObject(parent) {}

HotkeyManager::~HotkeyManager()
{
	unregisterAll();
}

void HotkeyManager::unregisterAll()
{
	if (m_startStopId != static_cast<size_t>(-1)) {
		obs_hotkey_unregister(static_cast<obs_hotkey_id>(m_startStopId));
		m_startStopId = static_cast<size_t>(-1);
	}
	if (m_pauseId != static_cast<size_t>(-1)) {
		obs_hotkey_unregister(static_cast<obs_hotkey_id>(m_pauseId));
		m_pauseId = static_cast<size_t>(-1);
	}
}

void HotkeyManager::registerOne(const char *name, const char *description, const HotkeyBinding &binding,
				size_t *outId)
{
	if (binding.vk == 0)
		return;

	// 热键回调运行在 libobs 的热键线程上，这里只做一次跨线程投递
	const obs_hotkey_id id = obs_hotkey_register_frontend(
		name, description,
		[](void *data, obs_hotkey_id hotkeyId, obs_hotkey_t *hotkey, bool pressed) {
			UNUSED_PARAMETER(hotkey);

			auto *self = static_cast<HotkeyManager *>(data);
			if (!self || !pressed)
				return; // 只在按下时触发

			const bool isStartStop = (static_cast<size_t>(hotkeyId) == self->m_startStopId);
			QMetaObject::invokeMethod(self, isStartStop ? "startStopTriggered" : "pauseTriggered",
						  Qt::QueuedConnection);
		},
		this);

	if (id == OBS_INVALID_HOTKEY_ID) {
		logWarn("注册热键失败：%s", name);
		return;
	}

	obs_key_combination_t combination = toCombination(binding);
	obs_hotkey_load_bindings(id, &combination, 1);

	*outId = static_cast<size_t>(id);
	logInfo("热键已注册：%s -> %s", name, describe(binding).toStdString().c_str());
}

void HotkeyManager::apply(const AppConfig &config)
{
	if (!obs_initialized()) {
		logWarn("libobs 未启动，热键注册被跳过");
		return;
	}

	unregisterAll();
	registerOne("minirec_start_stop", "开始/停止录制", config.hkStartStop, &m_startStopId);
	registerOne("minirec_pause", "暂停/继续录制", config.hkPause, &m_pauseId);
}

QString HotkeyManager::describe(const HotkeyBinding &binding)
{
	if (binding.vk == 0)
		return QStringLiteral("未设置");

	QStringList parts;
	if (binding.modifiers & INTERACT_CONTROL_KEY)
		parts << QStringLiteral("Ctrl");
	if (binding.modifiers & INTERACT_ALT_KEY)
		parts << QStringLiteral("Alt");
	if (binding.modifiers & INTERACT_SHIFT_KEY)
		parts << QStringLiteral("Shift");
	if (binding.modifiers & INTERACT_COMMAND_KEY)
		parts << QStringLiteral("Win");

	// 用 Win32 取本地化的按键名（F9、A、空格…）
	UINT scanCode = MapVirtualKeyW(static_cast<UINT>(binding.vk), MAPVK_VK_TO_VSC);
	switch (binding.vk) {
	case VK_LEFT:
	case VK_RIGHT:
	case VK_UP:
	case VK_DOWN:
	case VK_HOME:
	case VK_END:
	case VK_PRIOR:
	case VK_NEXT:
	case VK_INSERT:
	case VK_DELETE:
	case VK_DIVIDE:
	case VK_NUMLOCK:
		scanCode |= 0x100; // 扩展键标志
		break;
	default:
		break;
	}

	wchar_t buffer[64] = {0};
	if (GetKeyNameTextW(static_cast<LONG>(scanCode << 16), buffer, 64) > 0)
		parts << QString::fromWCharArray(buffer);
	else
		parts << QStringLiteral("VK 0x%1").arg(binding.vk, 2, 16, QLatin1Char('0'));

	return parts.join(QStringLiteral(" + "));
}

} // namespace minirec
