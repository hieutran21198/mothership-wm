//go:build linux && cgo && wlroots

#include <limits.h>
#include <string.h>
#include <wayland-client.h>

#include "nested-parent.h"
#include "parent-fractional-scale-client.h"
#include "parent-viewporter-client.h"
#include "parent-xdg-shell-client.h"
#include "parent-xdg-decoration-client.h"

// The wlroots backend owns buffers/presentation. This bridge owns only the
// parent xdg role and surface geometry, so configure(0, 0) stays logical even
// when a previous wlroots output mode was expressed in physical pixels.
static void parent_global(void *data, struct wl_registry *registry, uint32_t name,
		const char *interface, uint32_t version) {
	struct nested_parent *p = data;
	if (!p->compositor && !strcmp(interface, wl_compositor_interface.name) && version >= 4) {
		p->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
	} else if (!p->shell && !strcmp(interface, xdg_wm_base_interface.name)) {
		p->shell = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
	} else if (!p->scale_manager && !strcmp(interface, wp_fractional_scale_manager_v1_interface.name)) {
		p->scale_manager = wl_registry_bind(registry, name, &wp_fractional_scale_manager_v1_interface, 1);
	} else if (!p->viewporter && !strcmp(interface, wp_viewporter_interface.name)) {
		p->viewporter = wl_registry_bind(registry, name, &wp_viewporter_interface, 1);
	} else if (!p->decoration_manager && !strcmp(interface, zxdg_decoration_manager_v1_interface.name)) {
		p->decoration_manager = wl_registry_bind(registry, name, &zxdg_decoration_manager_v1_interface, 1);
	}
}

static void parent_remove(void *data, struct wl_registry *registry, uint32_t name) {
	(void)data; (void)registry; (void)name;
}

static const struct wl_registry_listener registry_listener = {
	.global = parent_global, .global_remove = parent_remove,
};

static void ping(void *data, struct xdg_wm_base *shell, uint32_t serial) {
	(void)data;
	xdg_wm_base_pong(shell, serial);
}

static const struct xdg_wm_base_listener shell_listener = {.ping = ping};

static void toplevel_configure(void *data, struct xdg_toplevel *toplevel,
		int32_t width, int32_t height, struct wl_array *states) {
	(void)toplevel; (void)states;
	struct nested_parent *p = data;
	p->requested_width = width;
	p->requested_height = height;
}

static void toplevel_close(void *data, struct xdg_toplevel *toplevel) {
	(void)toplevel;
	struct nested_parent *p = data;
	p->closed = true;
	p->close(p->data);
}

static const struct xdg_toplevel_listener toplevel_listener = {
	.configure = toplevel_configure, .close = toplevel_close,
};

static void surface_configure(void *data, struct xdg_surface *role, uint32_t serial) {
	struct nested_parent *p = data;
	xdg_surface_ack_configure(role, serial);
	int width = p->requested_width > 0 ? p->requested_width : p->width;
	int height = p->requested_height > 0 ? p->requested_height : p->height;
	p->requested_width = p->requested_height = 0;
	bool changed = p->width != width || p->height != height;
	p->width = width;
	p->height = height;
	p->configured = true;
	if (changed) p->changed(p->data);
}

static const struct xdg_surface_listener surface_listener = {.configure = surface_configure};

static void preferred_scale(void *data, struct wp_fractional_scale_v1 *fractional, uint32_t scale) {
	(void)fractional;
	struct nested_parent *p = data;
	if (scale == 0 || scale == p->scale_120) return;
	p->scale_120 = scale;
	p->changed(p->data);
}

static const struct wp_fractional_scale_v1_listener scale_listener = {.preferred_scale = preferred_scale};

static void decoration_configure(void *data, struct zxdg_toplevel_decoration_v1 *decoration, uint32_t mode) {
	(void)data; (void)decoration; (void)mode;
}

static const struct zxdg_toplevel_decoration_v1_listener decoration_listener = {.configure = decoration_configure};

bool nested_parent_open(struct nested_parent *p, struct wl_display *display,
		void (*changed)(void *), void (*close)(void *), void *data) {
	p->width = 800;
	p->height = 600;
	p->scale_120 = 120;
	p->changed = changed;
	p->close = close;
	p->data = data;
	struct wl_registry *registry = wl_display_get_registry(display);
	if (registry == NULL) return false;
	wl_registry_add_listener(registry, &registry_listener, p);
	int synced = wl_display_roundtrip(display);
	wl_registry_destroy(registry);
	if (synced < 0 || !p->compositor || !p->shell) return false;
	xdg_wm_base_add_listener(p->shell, &shell_listener, p);
	p->surface = wl_compositor_create_surface(p->compositor);
	if (!p->surface) return false;
	p->role = xdg_wm_base_get_xdg_surface(p->shell, p->surface);
	if (!p->role) return false;
	xdg_surface_add_listener(p->role, &surface_listener, p);
	p->toplevel = xdg_surface_get_toplevel(p->role);
	if (!p->toplevel) return false;
	xdg_toplevel_add_listener(p->toplevel, &toplevel_listener, p);
	xdg_toplevel_set_title(p->toplevel, "Mothership WM (nested)");
	xdg_toplevel_set_app_id(p->toplevel, "mothership-wm");
	if (p->decoration_manager) {
		p->decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(p->decoration_manager, p->toplevel);
		if (!p->decoration) return false;
		zxdg_toplevel_decoration_v1_add_listener(p->decoration, &decoration_listener, p);
		zxdg_toplevel_decoration_v1_set_mode(p->decoration, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
	}
	if (p->scale_manager && p->viewporter) {
		p->viewport = wp_viewporter_get_viewport(p->viewporter, p->surface);
		p->fractional = wp_fractional_scale_manager_v1_get_fractional_scale(p->scale_manager, p->surface);
		if (!p->viewport || !p->fractional) return false;
		wp_fractional_scale_v1_add_listener(p->fractional, &scale_listener, p);
	}
	wl_surface_commit(p->surface);
	while (!p->configured && !p->closed && wl_display_dispatch(display) >= 0) {}
	return p->configured && !p->closed && wl_display_get_error(display) == 0;
}

bool nested_parent_dimensions(struct nested_parent *p, int *pixel_width, int *pixel_height) {
	// Protocol rounds to the nearest physical pixel, with ties outwards.
	uint64_t width = ((uint64_t)p->width * p->scale_120 + 60) / 120;
	uint64_t height = ((uint64_t)p->height * p->scale_120 + 60) / 120;
	if (!width || !height || width > INT_MAX || height > INT_MAX) return false;
	*pixel_width = (int)width;
	*pixel_height = (int)height;
	return true;
}

void nested_parent_viewport(struct nested_parent *p) {
	if (p->viewport) wp_viewport_set_destination(p->viewport, p->width, p->height);
}

void nested_parent_finish(struct nested_parent *p) {
	if (p->fractional) wp_fractional_scale_v1_destroy(p->fractional);
	if (p->viewport) wp_viewport_destroy(p->viewport);
	if (p->decoration) zxdg_toplevel_decoration_v1_destroy(p->decoration);
	if (p->toplevel) xdg_toplevel_destroy(p->toplevel);
	if (p->role) xdg_surface_destroy(p->role);
	if (p->surface) wl_surface_destroy(p->surface);
	if (p->decoration_manager) zxdg_decoration_manager_v1_destroy(p->decoration_manager);
	if (p->scale_manager) wp_fractional_scale_manager_v1_destroy(p->scale_manager);
	if (p->viewporter) wp_viewporter_destroy(p->viewporter);
	if (p->shell) xdg_wm_base_destroy(p->shell);
	if (p->compositor) wl_compositor_destroy(p->compositor);
}
