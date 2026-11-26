#ifndef MOTHERSHIP_SCREENSHOT_CAPTURE_H
#define MOTHERSHIP_SCREENSHOT_CAPTURE_H

#include <stdbool.h>
#include <wlr/util/box.h>

struct wl_event_loop;
struct wlr_buffer;
struct wlr_output;
struct wlr_renderer;
struct wlr_scene_node;

struct screenshot_pixels {
	int fd;
	int width, height, stride;
};

bool screenshot_buffer_ready(struct wlr_scene_node *node);
bool screenshot_window_bounds(struct wlr_scene_node *node, struct wlr_box *bounds);
bool screenshot_read_buffer(struct wlr_renderer *, struct wlr_buffer *, struct screenshot_pixels *);
bool screenshot_render_window(struct wlr_output *, struct wlr_scene_node *, const struct wlr_box *,
	struct wl_event_loop *, struct screenshot_pixels *);

#endif
