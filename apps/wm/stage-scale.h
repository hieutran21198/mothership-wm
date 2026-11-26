#ifndef MOTHERSHIP_STAGE_SCALE_H
#define MOTHERSHIP_STAGE_SCALE_H

#include <stdbool.h>
#include <wlr/util/box.h>

struct wlr_scene_tree;
struct wlr_scene_buffer;
struct wlr_surface;
struct owned_preview;

enum { STAGE_SCALE_LAYERS = 48 };

struct stage_scale {
	struct wlr_scene_tree *tree;
	struct wlr_box full;
	struct {
		struct wlr_scene_buffer *buffer;
		struct wlr_box box;
	} layers[STAGE_SCALE_LAYERS];
	int count;
	bool failed, has_surface;
	struct wlr_surface *owned_surface;
	struct owned_preview *owned;
};

bool stage_scale_capture(struct stage_scale *,struct wlr_scene_tree *parent,
	struct wlr_scene_tree *source,struct wlr_box full,
	struct wlr_surface *owned_surface,struct owned_preview *owned);
void stage_scale_draw(struct stage_scale *,struct wlr_box thumbnail,double amount);
void stage_scale_draw_box(struct stage_scale *,struct wlr_box);
void stage_scale_clear(struct stage_scale *);

#endif
