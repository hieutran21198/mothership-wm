#ifndef MOTHERSHIP_PREVIEW_PROJECTION_H
#define MOTHERSHIP_PREVIEW_PROJECTION_H
#include <wlr/util/box.h>
struct wlr_scene_buffer;

struct preview_projection {
	struct wlr_scene_buffer *scene;
	struct wlr_box source,face;
	unsigned opening;
};
void preview_projection_update(struct preview_projection *,struct wlr_box,struct wlr_box,double);
void preview_projection_clear(struct preview_projection *);
#endif
