#ifndef MOTHERSHIP_SCENE_GLASS_H
#define MOTHERSHIP_SCENE_GLASS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wlr/util/box.h>
struct wl_event_loop;
struct wlr_output;
struct wlr_scene_tree;
struct wlr_scene_node;
struct scene_glass;
enum { SCENE_GLASS_GRID, SCENE_GLASS_LOG, SCENE_GLASS_COUNT };

struct scene_glass *scene_glass_create(struct wlr_output *, struct wlr_scene_tree *shell);
// Bits 0–5 are reserved by render_debug.h for other source categories.
enum scene_glass_trace {
	SCENE_GLASS_OFF=0, SCENE_GLASS_REBUILD=1u<<6, SCENE_GLASS_HIT=1u<<7,
	SCENE_GLASS_DRAG=1u<<8, SCENE_GLASS_FALLBACK=1u<<9,
};
unsigned scene_glass_update(struct scene_glass *, struct wlr_box view, unsigned opacity,
	bool dragging, struct wl_event_loop *,
	struct wlr_scene_node *const *sources, size_t count);
unsigned scene_glass_update_masked(struct scene_glass *, struct wlr_box view, unsigned opacity,
	bool dragging, struct wl_event_loop *,
	struct wlr_scene_node *const *sources, size_t count, const struct wlr_box *holes,size_t count_holes);
void scene_glass_place_below(struct scene_glass *,struct wlr_scene_node *);
void scene_glass_destroy(struct scene_glass *);
#endif
