#pragma once

#include <obs-module.h>

void register_scene_bypass_source();
obs_source_t *scene_bypass_source_create(const char *name);
void scene_bypass_set_target(obs_source_t *source, obs_source_t *target);
