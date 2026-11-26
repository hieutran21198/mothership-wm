#ifndef MOTHERSHIP_SURFACE_SYNC_H
#define MOTHERSHIP_SURFACE_SYNC_H

#include <stdbool.h>
#include <wlr/types/wlr_linux_drm_syncobj_v1.h>

// Only the wlroots scene surface tracks both acquire and release points.
// Auxiliary copies must not directly sample the client buffer until they
// track both points as well.
static inline bool surface_has_explicit_sync(struct wlr_surface *surface) {
	struct wlr_linux_drm_syncobj_surface_v1_state *state=
		wlr_linux_drm_syncobj_v1_get_surface_state(surface);
	return state && state->acquire_timeline;
}

#endif
