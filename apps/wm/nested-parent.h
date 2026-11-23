#ifndef MOTHERSHIP_NESTED_PARENT_H
#define MOTHERSHIP_NESTED_PARENT_H

#include <stdbool.h>
#include <stdint.h>

struct wl_compositor;
struct wl_display;
struct wl_surface;
struct wp_fractional_scale_manager_v1;
struct wp_fractional_scale_v1;
struct wp_viewporter;
struct wp_viewport;
struct xdg_wm_base;
struct xdg_surface;
struct xdg_toplevel;
struct zxdg_decoration_manager_v1;
struct zxdg_toplevel_decoration_v1;

struct nested_parent {
	struct wl_compositor *compositor;
	struct wl_surface *surface;
	struct wp_fractional_scale_manager_v1 *scale_manager;
	struct wp_fractional_scale_v1 *fractional;
	struct wp_viewporter *viewporter;
	struct wp_viewport *viewport;
	struct xdg_wm_base *shell;
	struct xdg_surface *role;
	struct xdg_toplevel *toplevel;
	struct zxdg_decoration_manager_v1 *decoration_manager;
	struct zxdg_toplevel_decoration_v1 *decoration;
	int width, height, requested_width, requested_height;
	uint32_t scale_120;
	bool configured, closed;
	void (*changed)(void *);
	void (*close)(void *);
	void *data;
};

bool nested_parent_open(struct nested_parent *, struct wl_display *,
	void (*changed)(void *), void (*close)(void *), void *data);
void nested_parent_finish(struct nested_parent *);
bool nested_parent_dimensions(struct nested_parent *, int *pixel_width, int *pixel_height);
void nested_parent_viewport(struct nested_parent *);

#endif
