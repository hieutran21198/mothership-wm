#ifndef MOTHERSHIP_OWNED_PREVIEW_H
#define MOTHERSHIP_OWNED_PREVIEW_H
#include <stdbool.h>
#include <stdint.h>

struct wl_event_loop;
struct wlr_buffer;
struct wlr_drm_syncobj_timeline;
struct wlr_output;
struct wlr_scene_buffer;
struct wlr_surface;
struct wlr_swapchain;

struct owned_preview {
	struct wlr_swapchain *swapchain;
	struct wlr_drm_syncobj_timeline *timeline;
	struct wlr_buffer *buffer;
	uint64_t next_point, point;
	uint64_t generation, attempted_generation;
	uint32_t format;
	int width, height;
	bool attempted, broken;
};

void owned_preview_clear(struct owned_preview *p);
struct wlr_buffer *owned_preview_update(struct owned_preview *p, struct wlr_output *output,
	struct wlr_surface *surface, uint64_t generation, struct wl_event_loop *loop);
void owned_preview_bind(struct owned_preview *p, struct wlr_scene_buffer *image);
#endif
