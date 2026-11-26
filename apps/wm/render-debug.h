#ifndef MOTHERSHIP_RENDER_DEBUG_H
#define MOTHERSHIP_RENDER_DEBUG_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_output.h>

struct render_debug;
struct wlr_surface;
struct wlr_scene_output;
struct wlr_scene_output_state_options;
enum render_activity { RENDER_KEY,RENDER_FOCUS,RENDER_CONFIGURE };
enum render_source {
	RENDER_APP=1, RENDER_PANEL=2, RENDER_WALLPAPER=4,
	RENDER_MAP=8, RENDER_MOTION=16, RENDER_FROST=32,
};

struct render_debug *render_debug_init(struct wl_event_loop *,
	struct wlr_output *,const char *backend,bool probe_gles2);
void render_debug_destroy(struct render_debug *);
bool render_debug_toggle(struct render_debug *);
bool render_debug_active(struct render_debug *);
void render_debug_surface(struct render_debug *,unsigned source,uint64_t window,
	struct wlr_surface *);
void render_debug_source(struct render_debug *,unsigned source);
void render_debug_activity(struct render_debug *,enum render_activity);
const struct wlr_scene_output_state_options *render_debug_scene_begin(struct render_debug *,
	struct wlr_scene_output *);
void render_debug_scene_end(struct render_debug *);
void render_debug_frame(struct render_debug *,struct timespec begin,struct timespec submit,
	unsigned frost);

#endif
