#include "pch.h"
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <vector>

#define SCENE_BYPASS_SOURCE_ID "multi_rtmp_scene_bypass"
#define SETTING_TARGET "target_source"

struct scene_bypass_source {
	obs_source_t *source;
	obs_weak_source_t *target;
	gs_texrender_t *texrender;
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

static void render_source_without_filters(obs_source_t *source)
{
	uint32_t flags = obs_source_get_output_flags(source);

	if (flags & OBS_SOURCE_ASYNC) {
		std::vector<FilterRecord> filters;
		obs_source_enum_filters(source, collect_filter_cb, &filters);
		for (auto &rec : filters)
			obs_source_set_enabled(rec.filter, false);
		obs_source_video_render(source);
		for (auto &rec : filters)
			obs_source_set_enabled(rec.filter, rec.was_enabled);
	} else {
		obs_source_default_render(source);
	}
}

static bool render_item_no_filters(obs_scene_t *scene, obs_sceneitem_t *item, void *param)
{
	UNUSED_PARAMETER(scene);
	auto *texrender = static_cast<gs_texrender_t *>(param);

	if (!obs_sceneitem_visible(item))
		return true;

	obs_source_t *source = obs_sceneitem_get_source(item);
	if (!source)
		return true;

	uint32_t w = obs_source_get_width(source);
	uint32_t h = obs_source_get_height(source);
	if (w == 0 || h == 0)
		return true;

	obs_scene_t *nested = obs_scene_from_source(source);
	if (nested) {
		struct matrix4 transform;
		obs_sceneitem_get_draw_transform(item, &transform);
		gs_matrix_push();
		gs_matrix_mul(&transform);
		obs_scene_enum_items(nested, render_item_no_filters, param);
		gs_matrix_pop();
		return true;
	}

	struct obs_sceneitem_crop crop;
	obs_sceneitem_get_crop(item, &crop);
	bool has_crop = crop.left || crop.top || crop.right || crop.bottom;

	struct matrix4 transform;
	obs_sceneitem_get_draw_transform(item, &transform);

	gs_matrix_push();
	gs_matrix_mul(&transform);

	if (has_crop && texrender) {
		uint32_t crop_w = w - crop.left - crop.right;
		uint32_t crop_h = h - crop.top - crop.bottom;

		gs_texrender_reset(texrender);
		if (gs_texrender_begin(texrender, crop_w, crop_h)) {
			struct vec4 clear_val;
			vec4_zero(&clear_val);
			gs_clear(GS_CLEAR_COLOR, &clear_val, 1.0f, 0);

			gs_matrix_push();
			gs_matrix_translate3f(-(float)crop.left,
					     -(float)crop.top, 0.0f);
			render_source_without_filters(source);
			gs_matrix_pop();

			gs_texrender_end(texrender);
		}

		gs_texture_t *tex = gs_texrender_get_texture(texrender);
		if (tex) {
			gs_effect_t *eff =
				obs_get_base_effect(OBS_EFFECT_DEFAULT);
			gs_eparam_t *img =
				gs_effect_get_param_by_name(eff, "image");
			gs_effect_set_texture(img, tex);

			gs_technique_t *tech =
				gs_effect_get_technique(eff, "Draw");
			gs_technique_begin(tech);
			gs_technique_begin_pass(tech, 0);
			gs_draw_sprite(tex, 0, crop_w, crop_h);
			gs_technique_end_pass(tech);
			gs_technique_end(tech);
		}
	} else {
		render_source_without_filters(source);
	}

	gs_matrix_pop();

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
	s->texrender = nullptr;
	scene_bypass_update(s, settings);
	return s;
}

static void scene_bypass_destroy(void *data)
{
	auto *s = static_cast<scene_bypass_source *>(data);
	if (s->target)
		obs_weak_source_release(s->target);
	if (s->texrender) {
		obs_enter_graphics();
		gs_texrender_destroy(s->texrender);
		obs_leave_graphics();
	}
	bfree(s);
}

static void scene_bypass_video_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	auto *s = static_cast<scene_bypass_source *>(data);

	obs_source_t *target = s->target ? obs_weak_source_get_source(s->target) : nullptr;
	if (!target)
		return;

	if (!s->texrender)
		s->texrender = gs_texrender_create(GS_RGBA, GS_ZS_NONE);

	obs_scene_t *scene = obs_scene_from_source(target);
	if (scene)
		obs_scene_enum_items(scene, render_item_no_filters,
				     s->texrender);

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
