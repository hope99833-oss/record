#include "core/EnumHelpers.h"

#include <obs.h>

#include <cstring>

#include "core/Logging.h"

namespace minirec {
namespace enums {
namespace {

bool enumContains(bool (*enumerator)(size_t, const char **), const char *id)
{
	if (!id || !*id)
		return false;

	const char *value = nullptr;
	size_t index = 0;
	while (enumerator(index++, &value)) {
		if (value && strcmp(value, id) == 0)
			return true;
	}
	return false;
}

std::vector<std::string> enumAll(bool (*enumerator)(size_t, const char **))
{
	std::vector<std::string> result;
	const char *value = nullptr;
	size_t index = 0;
	while (enumerator(index++, &value)) {
		if (value)
			result.emplace_back(value);
	}
	return result;
}

} // namespace

std::vector<PropertyItem> listItems(const char *sourceId, const char *propertyName)
{
	std::vector<PropertyItem> result;

	obs_properties_t *props = obs_get_source_properties(sourceId);
	if (!props) {
		logWarn("源类型 %s 未注册，无法读取属性 %s", sourceId, propertyName);
		return result;
	}

	obs_property_t *property = obs_properties_get(props, propertyName);
	if (!property) {
		logWarn("源类型 %s 上没有属性 %s", sourceId, propertyName);
		obs_properties_destroy(props);
		return result;
	}

	const size_t count = obs_property_list_item_count(property);
	result.reserve(count);
	for (size_t i = 0; i < count; i++) {
		PropertyItem item;
		const char *value = obs_property_list_item_string(property, i);
		const char *label = obs_property_list_item_name(property, i);
		item.value = value ? value : "";
		item.label = (label && *label) ? label : item.value;
		result.push_back(std::move(item));
	}

	obs_properties_destroy(props);
	return result;
}

std::string defaultString(const char *sourceId, const char *propertyName)
{
	std::string result;

	obs_properties_t *props = obs_get_source_properties(sourceId);
	if (!props)
		return result;

	obs_property_t *property = obs_properties_get(props, propertyName);
	if (property) {
		const char *def = obs_property_list_item_count(property) > 0
					  ? obs_property_list_item_string(property, 0)
					  : nullptr;
		if (def)
			result = def;
	}

	obs_properties_destroy(props);
	return result;
}

bool hasSourceType(const char *id)
{
	return enumContains(obs_enum_source_types, id);
}

bool hasEncoderType(const char *id)
{
	return enumContains(obs_enum_encoder_types, id);
}

bool hasOutputType(const char *id)
{
	return enumContains(obs_enum_output_types, id);
}

std::vector<std::string> encoderTypes()
{
	return enumAll(obs_enum_encoder_types);
}

std::vector<PropertyItem> monitors()
{
	return listItems(ids::MonitorCapture, "monitor_id");
}

std::vector<PropertyItem> windows()
{
	return listItems(ids::WindowCapture, "window");
}

std::vector<PropertyItem> desktopAudioDevices()
{
	return listItems(ids::DesktopAudio, "device_id");
}

std::vector<PropertyItem> microphoneDevices()
{
	return listItems(ids::Microphone, "device_id");
}

std::vector<PropertyItem> cameraDevices()
{
	return listItems(ids::Camera, "video_device_id");
}

bool silenceSourceAvailable()
{
	return true; // 由本程序自己注册（见 SceneBuilder.cpp），始终可用
}

const char *silenceSourceId()
{
	return "minirec_silence_source";
}

} // namespace enums
} // namespace minirec
