#include "pch.h"
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <vector>

#define SCENE_BYPASS_SOURCE_ID "multi_rtmp_scene_bypass"
#define SETTING_TARGET "target_source"

struct scene_bypass_source {
	obs_source_t *source;
	obs_weak_source_t *target;
};

struct FilterRecord {
	obs_source_t *filter;
	bool was_enabled;
};

static void collect_filter_cb(obs_source_t *parent, obs_source_t *filter, void *param)
{
	UNUSED_PARAMETER(parent);
	auto *records = static_cast<std::vector<FilterRecord> *>(param);
	records->push_back({filter, obs_source_enabled(filter)});
}

static bool collect_scene_item_filters(obs_scene_t *scene, obs_sceneitem_t *item, void *param)
{
	UNUSED_PARAMETER(scene);
	obs_source_t *source = obs_sceneitem_get_source(item);
	if (!source)
		return true;

	obs_source_enum_filters(source, collect_filter_cb, param);

	obs_scene_t *nested = obs_scene_from_source(source);
	if (nested)
		obs_scene_enum_items(nested, collect_scene_item_filters, param);

	return true;
}

static const char *scene_bypass_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("SceneBypass.Name");
}

static void scene_bypass_update(void *data, obs_data_t *settings)
{
	auto *s = static_cast<scene_bypass_source *>(data);
	const char *target_name = obs_data_get_string(settings, SETTING_TARGET);

	if (s->target) {
		obs_weak_source_release(s->target);
		s->target = nullptr;
	}

	if (target_name && target_name[0]) {
		obs_source_t *target = obs_get_source_by_name(target_name);
		if (target) {
			s->target = obs_source_get_weak_source(target);
			obs_source_release(target);
		}
	}
}

static void *scene_bypass_create(obs_data_t *settings, obs_source_t *source)
{
	auto *s = static_cast<scene_bypass_source *>(bzalloc(sizeof(scene_bypass_source)));
	s->source = source;
	scene_bypass_update(s, settings);
	return s;
}

static void scene_bypass_destroy(void *data)
{
	auto *s = static_cast<scene_bypass_source *>(data);
	if (s->target)
		obs_weak_source_release(s->target);
	bfree(s);
}

static void scene_bypass_video_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	auto *s = static_cast<scene_bypass_source *>(data);

	obs_source_t *target = s->target ? obs_weak_source_get_source(s->target) : nullptr;
	if (!target)
		return;

	std::vector<FilterRecord> filter_states;

	obs_source_enum_filters(target, collect_filter_cb, &filter_states);

	obs_scene_t *scene = obs_scene_from_source(target);
	if (scene)
		obs_scene_enum_items(scene, collect_scene_item_filters,
				     &filter_states);

	for (auto &rec : filter_states)
		obs_source_set_enabled(rec.filter, false);

	obs_source_default_render(target);

	for (auto &rec : filter_states)
		obs_source_set_enabled(rec.filter, rec.was_enabled);

	obs_source_release(target);
}

static uint32_t scene_bypass_width(void *data)
{
	auto *s = static_cast<scene_bypass_source *>(data);
	obs_source_t *target = s->target ? obs_weak_source_get_source(s->target) : nullptr;
	if (!target) return 0;
	uint32_t w = obs_source_get_width(target);
	obs_source_release(target);
	return w;
}

static uint32_t scene_bypass_height(void *data)
{
	auto *s = static_cast<scene_bypass_source *>(data);
	obs_source_t *target = s->target ? obs_weak_source_get_source(s->target) : nullptr;
	if (!target) return 0;
	uint32_t h = obs_source_get_height(target);
	obs_source_release(target);
	return h;
}

static obs_properties_t *scene_bypass_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *props = obs_properties_create();
	obs_property_t *list = obs_properties_add_list(props, SETTING_TARGET,
		obs_module_text("SceneBypass.Target"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(list, "", "");

	struct obs_frontend_source_list sources = {};
	obs_frontend_get_scenes(&sources);
	for (size_t i = 0; i < sources.sources.num; i++) {
		obs_source_t *src = sources.sources.array[i];
		const char *name = obs_source_get_name(src);
		obs_property_list_add_string(list, name, name);
	}
	obs_frontend_source_list_free(&sources);
	return props;
}

static void scene_bypass_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, SETTING_TARGET, "");
}

void scene_bypass_set_target(obs_source_t *source, obs_source_t *target)
{
	if (!source) return;
	obs_data_t *settings = obs_data_create();
	if (target)
		obs_data_set_string(settings, SETTING_TARGET, obs_source_get_name(target));
	obs_source_update(source, settings);
	obs_data_release(settings);
}

obs_source_t *scene_bypass_source_create(const char *name)
{
	obs_source_t *source = obs_source_create(SCENE_BYPASS_SOURCE_ID, name, nullptr, nullptr);
	return source;
}

static struct obs_source_info scene_bypass_info = {
	.id = SCENE_BYPASS_SOURCE_ID,
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_SRGB,
	.get_name = scene_bypass_name,
	.create = scene_bypass_create,
	.destroy = scene_bypass_destroy,
	.get_width = scene_bypass_width,
	.get_height = scene_bypass_height,
	.get_defaults = scene_bypass_defaults,
	.get_properties = scene_bypass_properties,
	.update = scene_bypass_update,
	.video_render = scene_bypass_video_render,
};

void register_scene_bypass_source()
{
	obs_register_source(&scene_bypass_info);
}
