//go:build linux && cgo && wlroots

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <wayland-client-core.h>
#include <wayland-server-core.h>
#include <wlr/backend/wayland.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/xcursor.h>

#include "nested-seat.h"
#include "nested-layout.h"

enum control_phase {
	STARTING,
	SERVING,
	STARTUP_INTERRUPTED,
};

struct mothership_control {
	atomic_int phase;
	int parent_fd;
	int stop_write_fd;
};

struct mothership_control *mothership_control_create(int parent_fd, int stop_write_fd) {
	struct mothership_control *control = calloc(1, sizeof(*control));
	if (control != NULL) {
		atomic_init(&control->phase, STARTING);
		control->parent_fd = parent_fd;
		control->stop_write_fd = stop_write_fd;
	}
	return control;
}

void mothership_control_destroy(struct mothership_control *control) {
	free(control);
}

int mothership_control_serve(struct mothership_control *control) {
	int expected = STARTING;
	return atomic_compare_exchange_strong(&control->phase, &expected, SERVING);
}

void mothership_control_interrupt(struct mothership_control *control) {
	int expected = STARTING;
	if (atomic_compare_exchange_strong(&control->phase, &expected, STARTUP_INTERRUPTED)) {
		/* wlroots can block in a parent roundtrip before servicing the stop pipe.
		 * Shutdown only our own connection, never libwayland state or the peer's listener. */
		while (shutdown(control->parent_fd, SHUT_RDWR) < 0 && errno == EINTR) {
		}
	} else if (expected == SERVING) {
		/* Both pipe ends stay open until the control watcher has returned. */
		const char wake = 1;
		while (write(control->stop_write_fd, &wake, sizeof(wake)) < 0 && errno == EINTR) {
		}
	}
}

struct xdg_view;
struct shell_panel;

struct domain_member {
	uint64_t id;
	uint64_t epoch;
};

struct nested {
	struct wl_display *server;
	struct wlr_backend *backend;
	struct wlr_output *output;
	struct wlr_output_cursor *cursor;
	struct wlr_xcursor_theme *cursor_theme;
	struct wlr_xcursor_image *cursor_image;
	bool cursor_visible;
	struct wlr_scene_output *scene_output;
	struct wlr_scene_rect *background;
	struct wlr_scene *scene;
	struct wlr_scene_tree *app_tree, *shell_tree;
	struct shell_panel *panel;
	struct wl_listener new_panel;
	struct nested_seat *seat;
	struct xdg_view *slots[NODE_WINDOWS], *drag;
	struct rect drag_rect;
	int drag_x, drag_y;
	bool drag_resize;
	enum node_layout layout;
	int visible_count;
	uint64_t next_window_id;
	struct wl_list views;
	struct domain_member members[NODE_WINDOWS];
	uint64_t focus_order;
	bool domain_active, home_active, overview_active;
	int domain_fd;
	struct wl_event_source *domain_source;
	struct wl_listener backend_destroy;
	struct wl_listener output_destroy;
	struct wl_listener frame;
	struct wl_listener resize;
	struct wl_listener toplevel;
	bool stopping;
	bool cleaning;
	bool ready;
	const char *failure;
};

struct xdg_view {
	struct nested *wm;
	struct wlr_xdg_toplevel *toplevel;
	struct wlr_scene_tree *frame, *tree;
	struct wlr_scene_rect *bar, *grip;
	struct rect floating;
	int configure_w, configure_h;
	uint64_t id;
	uint64_t epoch, focused_at, home_focused_at;
	bool mapped, assigned;
	int slot;
	struct wl_list link;
	struct wl_listener map, unmap, commit, destroy, tree_destroy;
};

struct shell_panel {
	struct nested *wm;
	struct wlr_layer_surface_v1 *layer;
	struct wlr_scene_layer_surface_v1 *scene;
	bool wants_keyboard;
	struct wl_listener commit, map, unmap, destroy;
};

static void position_panel(struct shell_panel *panel) {
	if (panel == NULL || panel->wm->output == NULL || !panel->layer->initialized) {
		return;
	}
	int width, height;
	wlr_output_effective_resolution(panel->wm->output, &width, &height);
	if (width <= 12 || height <= 12) {
		return;
	}
	int pw = width > 532 ? 520 : width - 12;
	int ph = height > 318 ? 306 : height - 12;
	int x = panel->wm->overview_active ? (width - pw) / 2 : width - pw - 12;
	int y = panel->wm->overview_active ? (height - ph) / 2 : height - ph - 12;
	wlr_scene_node_set_position(&panel->scene->tree->node, x, y);
	wlr_layer_surface_v1_configure(panel->layer, pw, ph);
}

static void panel_commit(struct wl_listener *listener, void *data) {
	(void)data;
	struct shell_panel *panel = wl_container_of(listener, panel, commit);
	struct wlr_layer_surface_v1 *layer = panel->layer;
	if (layer->initial_commit) {
		position_panel(panel);
	}
	bool wants = layer->surface->mapped && layer->current.keyboard_interactive ==
		ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND;
	if (wants != panel->wants_keyboard) {
		panel->wants_keyboard = wants;
		nested_seat_panel(panel->wm->seat, layer->surface, wants);
	}
}

static void panel_map(struct wl_listener *listener, void *data) {
	(void)listener;
	(void)data;
	puts("SHELL_READY");
	fflush(stdout);
}

static void panel_unmap(struct wl_listener *listener, void *data) {
	(void)data;
	struct shell_panel *panel = wl_container_of(listener, panel, unmap);
	panel->wants_keyboard = false;
	nested_seat_panel(panel->wm->seat, panel->layer->surface, false);
}

static void panel_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct shell_panel *panel = wl_container_of(listener, panel, destroy);
	nested_seat_panel(panel->wm->seat, panel->layer->surface, false);
	panel->wm->panel = NULL;
	wl_list_remove(&panel->commit.link);
	wl_list_remove(&panel->map.link);
	wl_list_remove(&panel->unmap.link);
	wl_list_remove(&panel->destroy.link);
	free(panel);
}

static void on_new_panel(struct wl_listener *listener, void *data) {
	struct nested *wm = wl_container_of(listener, wm, new_panel);
	struct wlr_layer_surface_v1 *layer = data;
	if (wm->panel != NULL || wm->output == NULL ||
			strcmp(layer->namespace, "mothership-command-center") != 0 ||
			(layer->output != NULL && layer->output != wm->output)) {
		wlr_layer_surface_v1_destroy(layer);
		return;
	}
	layer->output = wm->output;
	struct shell_panel *panel = calloc(1, sizeof(*panel));
	if (panel == NULL) {
		wlr_layer_surface_v1_destroy(layer);
		return;
	}
	panel->scene = wlr_scene_layer_surface_v1_create(wm->shell_tree, layer);
	if (panel->scene == NULL) {
		free(panel);
		wlr_layer_surface_v1_destroy(layer);
		return;
	}
	wm->panel = panel;
	panel->wm = wm;
	panel->layer = layer;
	panel->commit.notify = panel_commit;
	wl_signal_add(&layer->surface->events.commit, &panel->commit);
	panel->map.notify = panel_map;
	wl_signal_add(&layer->surface->events.map, &panel->map);
	panel->unmap.notify = panel_unmap;
	wl_signal_add(&layer->surface->events.unmap, &panel->unmap);
	panel->destroy.notify = panel_destroy;
	wl_signal_add(&layer->events.destroy, &panel->destroy);
}

static void stop(struct nested *wm, const char *reason) {
	if (wm->failure == NULL) {
		wm->failure = reason;
	}
	wm->stopping = true;
	wl_display_terminate(wm->server);
}

static int on_stop_fd(int fd, uint32_t mask, void *data) {
	(void)fd;
	(void)mask;
	stop(data, NULL);
	return 0;
}

static void on_backend_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct nested *wm = wl_container_of(listener, wm, backend_destroy);
	wl_list_remove(&listener->link);
	wm->backend = NULL;
	if (!wm->cleaning) {
		stop(wm, "parent Wayland connection ended");
	}
}

static void on_output_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct nested *wm = wl_container_of(listener, wm, output_destroy);
	wl_list_remove(&wm->frame.link);
	wl_list_remove(&wm->resize.link);
	wl_list_remove(&listener->link);
	wm->output = NULL;
	wm->cursor = NULL; /* owned by the output */
	wm->scene_output = NULL; /* wlroots destroys it with the output. */
	if (!wm->stopping) {
		stop(wm, wm->ready ? NULL : "parent closed the output during startup");
	}
}

static void on_frame(struct wl_listener *listener, void *data) {
	(void)data;
	struct nested *wm = wl_container_of(listener, wm, frame);
	if (!wlr_scene_output_commit(wm->scene_output, NULL)) {
		stop(wm, "could not render the nested output");
		return;
	}
	struct timespec now;
	if (clock_gettime(CLOCK_MONOTONIC, &now) == 0) {
		wlr_scene_output_send_frame_done(wm->scene_output, &now);
	}
}

static bool named_view(struct nested *wm) {
	return wm->domain_active && !wm->home_active && !wm->overview_active;
}

static void place_view(struct xdg_view *view) {
	if (view == NULL || view->slot < 0 || view->tree == NULL || view->wm->output == NULL ||
			!view->frame->node.enabled) {
		return;
	}
	struct nested *wm = view->wm;
	int width, height;
	wlr_output_effective_resolution(wm->output, &width, &height);
	if (width <= 0 || height <= FLOAT_BAR + FLOAT_GRIP) {
		return;
	}
	bool named = named_view(wm), floating = named && wm->layout == FLOAT;
	struct rect r;
	if (!named) {
		int middle = width / 2;
		r = view->slot == 0 ? (struct rect){0, 0, middle, height} :
			(struct rect){middle, 0, width - middle, height};
	} else if (wm->layout == STACK) {
		r = (struct rect){0, 0, width, height};
	} else if (floating) {
		if (view->floating.width == 0) {
			view->floating = float_start(view->slot, width, height);
		}
		r = view->floating = float_rect(view->floating, width, height);
	} else {
		r = tile_rect(view->slot, wm->visible_count, width, height);
	}
	int bar = floating ? FLOAT_BAR : 0;
	struct wlr_box clip = {
		.x = view->toplevel->base->geometry.x,
		.y = view->toplevel->base->geometry.y,
		.width = r.width,
		.height = r.height - bar,
	};
	wlr_scene_node_set_position(&view->frame->node, r.x, r.y);
	wlr_scene_node_set_position(&view->tree->node, 0, bar);
	wlr_scene_subsurface_tree_set_clip(&view->tree->node, &clip);
	if (floating && view->bar == NULL) {
		const float title_color[4] = {0.19f, 0.32f, 0.46f, 1.0f};
		const float grip_color[4] = {0.42f, 0.68f, 0.87f, 1.0f};
		view->bar = wlr_scene_rect_create(view->frame, r.width, FLOAT_BAR, title_color);
		view->grip = wlr_scene_rect_create(view->frame, FLOAT_GRIP, FLOAT_GRIP, grip_color);
	}
	if (view->bar != NULL) {
		wlr_scene_node_set_enabled(&view->bar->node, floating);
		if (floating) {
			wlr_scene_rect_set_size(view->bar, r.width, FLOAT_BAR);
		}
	}
	if (view->grip != NULL) {
		wlr_scene_node_set_enabled(&view->grip->node, floating);
		if (floating) {
			wlr_scene_node_set_position(&view->grip->node, r.width - FLOAT_GRIP, r.height - FLOAT_GRIP);
		}
	}
	int request_w = named ? clip.width : 0, request_h = named ? clip.height : 0;
	if (request_w != view->configure_w || request_h != view->configure_h) {
		view->configure_w = request_w;
		view->configure_h = request_h;
		wlr_xdg_toplevel_set_size(view->toplevel, request_w, request_h);
	}
}

static struct nested_hit hit_view(void *data, double x, double y) {
	struct nested *wm = data;
	struct nested_hit hit = {0};
	if (wm->cleaning || wm->output == NULL) {
		return hit;
	}
	struct wlr_scene_node *node = wlr_scene_node_at(&wm->scene->tree.node,
		x, y, &hit.x, &hit.y);
	if (node == NULL) {
		return hit;
	}
	if (node->type == WLR_SCENE_NODE_RECT && named_view(wm) && wm->layout == FLOAT) {
		for (int i = 0; i < NODE_WINDOWS; ++i) {
			struct xdg_view *v = wm->slots[i];
			if (v != NULL && ((v->bar != NULL && node == &v->bar->node) ||
					(v->grip != NULL && node == &v->grip->node))) {
				hit.handle = v;
				hit.resize = v->grip != NULL && node == &v->grip->node;
				hit.toplevel = v->toplevel->base->surface;
				return hit;
			}
		}
	}
	if (node->type != WLR_SCENE_NODE_BUFFER) {
		return hit;
	}
	struct wlr_scene_surface *surface = wlr_scene_surface_try_from_buffer(
		wlr_scene_buffer_from_node(node));
	if (surface == NULL) {
		return hit;
	}
	for (struct wlr_scene_tree *tree = node->parent; tree != NULL; tree = tree->node.parent) {
		if (wm->panel != NULL && wm->panel->scene->tree == tree) {
			hit.surface = surface->surface;
			if (wm->panel->wants_keyboard) {
				hit.panel_keyboard = wm->panel->layer->surface;
			}
			return hit;
		}
		for (int i = 0; i < NODE_WINDOWS; ++i) {
			struct xdg_view *view = wm->slots[i];
			if (view != NULL && view->tree == tree) {
				hit.surface = surface->surface;
				hit.toplevel = view->toplevel->base->surface;
				return hit;
			}
		}
	}
	return hit;
}

static void start_view_move(void *data, struct nested_hit hit, double x, double y) {
	struct nested *wm = data;
	wm->drag = hit.handle;
	wm->drag_rect = wm->drag->floating;
	wm->drag_x = (int)x;
	wm->drag_y = (int)y;
	wm->drag_resize = hit.resize;
}

static void update_view_move(void *data, bool moving, double x, double y) {
	struct nested *wm = data;
	if (!moving) {
		wm->drag = NULL;
		return;
	}
	struct xdg_view *view = wm->drag;
	if (view == NULL || !named_view(wm) || wm->layout != FLOAT) {
		return;
	}
	struct rect r = wm->drag_rect;
	int dx = (int)x - wm->drag_x, dy = (int)y - wm->drag_y;
	if (wm->drag_resize) {
		int width, height;
		wlr_output_effective_resolution(wm->output, &width, &height);
		r.width = clamp_int(r.width + dx, 180, width - r.x);
		r.height = clamp_int(r.height + dy, 100, height - r.y);
	} else {
		r.x += dx;
		r.y += dy;
	}
	view->floating = r;
	place_view(view);
}

static void move_cursor(void *data, bool inside, double x, double y) {
	struct nested *wm = data;
	if (wm->cursor == NULL || wm->cursor_image == NULL || wm->cleaning) {
		return;
	}
	if (inside != wm->cursor_visible) {
		struct wlr_xcursor_image *image = wm->cursor_image;
		struct wlr_buffer *buffer = inside ? wlr_xcursor_image_get_buffer(image) : NULL;
		wm->cursor_visible = inside && buffer != NULL;
		wlr_output_cursor_set_buffer(wm->cursor, buffer,
			wm->cursor_visible ? image->hotspot_x : 0, wm->cursor_visible ? image->hotspot_y : 0);
	}
	if (inside) {
		wlr_output_cursor_move(wm->cursor, x, y);
	}
}

static void init_cursor(struct nested *wm) {
	// Use wlroots' nested output cursor: the parent owns its presentation and
	// applies it only while the pointer is over this outer Wayland surface.
	wm->cursor_theme = wlr_xcursor_theme_load(NULL, 24);
	if (wm->cursor_theme == NULL) {
		return;
	}
	struct wlr_xcursor *arrow = wlr_xcursor_theme_get_cursor(wm->cursor_theme, "left_ptr");
	if (arrow != NULL && arrow->image_count > 0) {
		wm->cursor_image = arrow->images[0];
		wm->cursor = wlr_output_cursor_create(wm->output);
	}
}

static void on_resize(struct wl_listener *listener, void *data) {
	struct nested *wm = wl_container_of(listener, wm, resize);
	const struct wlr_output_event_request_state *request = data;
	if (!wlr_output_commit_state(wm->output, request->state)) {
		stop(wm, "parent resize could not be applied to the nested output");
		return;
	}
	int width, height;
	wlr_output_effective_resolution(wm->output, &width, &height);
	wlr_scene_rect_set_size(wm->background, width, height);
	for (int i = 0; i < NODE_WINDOWS; ++i) {
		place_view(wm->slots[i]);
	}
	position_panel(wm->panel);
	nested_seat_refresh(wm->seat);
}

static void domain_disconnect(struct nested *wm) {
	if (wm->domain_source != NULL) {
		wl_event_source_remove(wm->domain_source);
		wm->domain_source = NULL;
	}
	if (wm->domain_fd >= 0) {
		shutdown(wm->domain_fd, SHUT_RDWR);
		wm->domain_fd = -1;
	}
}

static bool domain_send(struct nested *wm, const char *event, uint64_t id) {
	if (wm->domain_fd < 0) {
		return false;
	}
	char packet[64];
	int n = id == 0 ? snprintf(packet, sizeof(packet), "%s", event) :
		snprintf(packet, sizeof(packet), "%s %" PRIu64, event, id);
	ssize_t sent;
	do {
		sent = send(wm->domain_fd, packet, n, MSG_NOSIGNAL | MSG_DONTWAIT);
	} while (sent < 0 && errno == EINTR);
	if (sent != n) {
		domain_disconnect(wm);
		return false;
	}
	return true;
}

static struct xdg_view *find_view(struct nested *wm, uint64_t id) {
	struct xdg_view *view;
	wl_list_for_each(view, &wm->views, link) {
		if (view->id == id) {
			return view;
		}
	}
	return NULL;
}

static void remember_focus(void *data, struct wlr_surface *surface) {
	struct nested *wm = data;
	if (!wm->domain_active) {
		return;
	}
	for (int i = 0; i < NODE_WINDOWS; ++i) {
		struct xdg_view *view = wm->slots[i];
		if (view != NULL && view->toplevel->base->surface == surface) {
			if (wm->home_active) {
				view->home_focused_at = ++wm->focus_order;
			} else {
				view->focused_at = ++wm->focus_order;
				if (wm->layout == FLOAT) {
					wlr_scene_node_raise_to_top(&view->frame->node);
				}
			}
			return;
		}
	}
}

static struct xdg_view *best_view(struct nested *wm, bool visible_only) {
	if (wm->cleaning) {
		return NULL;
	}
	struct xdg_view *best = NULL;
	uint64_t best_at = 0;
	for (int i = 0; i < NODE_WINDOWS; ++i) {
		struct xdg_view *view = wm->slots[i];
		if (view == NULL || view->tree == NULL ||
				(visible_only && !view->frame->node.enabled)) {
			continue;
		}
		uint64_t at = wm->home_active ? view->home_focused_at : view->focused_at;
		if (at > best_at) {
			best = view;
			best_at = at;
		}
	}
	return best;
}

static struct wlr_surface *view_target(struct nested *wm) {
	struct xdg_view *best = best_view(wm, true);
	return best == NULL ? NULL : best->toplevel->base->surface;
}

static void clear_slots(struct nested *wm) {
	for (int i = 0; i < NODE_WINDOWS; ++i) {
		struct xdg_view *view = wm->slots[i];
		if (view != NULL) {
			wlr_scene_node_set_enabled(&view->frame->node, false);
			view->slot = -1;
		}
		wm->slots[i] = NULL;
	}
	wm->visible_count = 0;
}

static void project_home(struct nested *wm) {
	struct xdg_view *next[HOME_WINDOWS] = {0};
	int count = 0;
	struct xdg_view *view;
	/* Overflow remains live and joins one of the two existing Home slots. */
	wl_list_for_each(view, &wm->views, link) {
		if (view->mapped && !view->assigned && view->tree != NULL) {
			next[count++] = view;
			if (count == HOME_WINDOWS) {
				break;
			}
		}
	}
	if (next[0] == wm->slots[0] && next[1] == wm->slots[1] && wm->visible_count <= HOME_WINDOWS) {
		return;
	}
	clear_slots(wm);
	wm->visible_count = count;
	for (int i = 0; i < count; ++i) {
		wm->slots[i] = next[i];
		next[i]->slot = i;
		wlr_scene_node_set_enabled(&next[i]->frame->node, true);
		place_view(next[i]);
	}
}

static void refresh_home(struct nested *wm) {
	struct xdg_view *old[] = {wm->slots[0], wm->slots[1]};
	project_home(wm);
	for (int i = 0; i < HOME_WINDOWS; ++i) {
		if (old[i] != NULL && old[i]->slot < 0) {
			nested_seat_forget(wm->seat, old[i]->toplevel->base->surface,
				view_target(wm));
		}
	}
	nested_seat_refresh(wm->seat);
}

static uint64_t domain_number(char *token) {
	if (token == NULL || *token == '\0') {
		return 0;
	}
	for (char *p = token; *p != '\0'; ++p) {
		if (*p < '0' || *p > '9') {
			return 0;
		}
	}
	errno = 0;
	uintmax_t value = strtoumax(token, NULL, 10);
	return errno == 0 && value <= UINT64_MAX ? (uint64_t)value : 0;
}

static bool read_members(char **tail, struct domain_member members[NODE_WINDOWS]) {
	for (int i = 0; i < NODE_WINDOWS; ++i) {
		char *id = strtok_r(NULL, " ", tail);
		if (id == NULL) {
			return true;
		}
		members[i].id = domain_number(id);
		members[i].epoch = domain_number(strtok_r(NULL, " ", tail));
		if (members[i].id == 0 || members[i].epoch == 0) {
			return false;
		}
		for (int j = 0; j < i; ++j) {
			if (members[i].id == members[j].id) {
				return false;
			}
		}
	}
	return strtok_r(NULL, " ", tail) == NULL;
}

static bool read_layout(const char *value, enum node_layout *layout) {
	if (value == NULL) {
		return false;
	}
	if (strcmp(value, "tiling") == 0) {
		*layout = TILING;
	} else if (strcmp(value, "stack") == 0) {
		*layout = STACK;
	} else if (strcmp(value, "float") == 0) {
		*layout = FLOAT;
	} else {
		return false;
	}
	return true;
}

static void project_named(struct nested *wm, const struct domain_member members[NODE_WINDOWS]) {
	clear_slots(wm);
	if (members != wm->members) {
		memcpy(wm->members, members, sizeof(wm->members));
	}
	for (int i = 0; i < NODE_WINDOWS && members[i].id != 0; ++i) {
		struct xdg_view *view = find_view(wm, members[i].id);
		if (view == NULL) {
			continue;
		}
		if (view->epoch != members[i].epoch) {
			view->epoch = members[i].epoch;
			view->focused_at = 0;
			view->floating = (struct rect){0};
		}
		if (view->mapped && view->tree != NULL) {
			view->slot = wm->visible_count;
			wm->slots[wm->visible_count++] = view;
		}
	}
	struct xdg_view *selected = best_view(wm, false);
	struct xdg_view *top = selected != NULL ? selected : wm->slots[0];
	for (int i = 0; i < wm->visible_count; ++i) {
		struct xdg_view *view = wm->slots[i];
		wlr_scene_node_set_enabled(&view->frame->node, wm->layout != STACK || view == top);
		place_view(view);
	}
	if (wm->layout == FLOAT && selected != NULL) {
		wlr_scene_node_raise_to_top(&selected->frame->node);
	}
}

static void refresh_named(struct nested *wm, const struct domain_member members[NODE_WINDOWS]) {
	struct xdg_view *old[NODE_WINDOWS];
	memcpy(old, wm->slots, sizeof(old));
	project_named(wm, members);
	struct wlr_surface *fallback = view_target(wm);
	if (fallback == NULL && wm->slots[0] != NULL) {
		fallback = wm->slots[0]->toplevel->base->surface;
	}
	for (int i = 0; i < NODE_WINDOWS; ++i) {
		if (old[i] != NULL && (old[i]->slot < 0 || !old[i]->frame->node.enabled)) {
			nested_seat_forget(wm->seat, old[i]->toplevel->base->surface, fallback);
		}
	}
	if (wm->drag != NULL && wm->drag->slot < 0) {
		wm->drag = NULL;
	}
	nested_seat_refresh(wm->seat);
}

static void select_projection(struct nested *wm) {
	wm->drag = NULL;
	position_panel(wm->panel);
	nested_seat_switch(wm->seat, view_target(wm));
}

static int on_domain_fd(int fd, uint32_t mask, void *data) {
	struct nested *wm = data;
	if (!(mask & WL_EVENT_READABLE)) {
		domain_disconnect(wm);
		return 0;
	}
	char buf[WM_PACKET_MAX];
	ssize_t n = recv(fd, buf, sizeof(buf), MSG_DONTWAIT | MSG_TRUNC);
	if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
		return 0;
	}
	if (n <= 0 || n >= sizeof(buf) || memchr(buf, '\0', n) != NULL) {
		domain_disconnect(wm);
		return 0;
	}
	buf[n] = '\0';
	char *tail, *op = strtok_r(buf, " ", &tail);
	if (op == NULL) {
		domain_disconnect(wm);
		return 0;
	}
	uint64_t serial = domain_number(strtok_r(NULL, " ", &tail));
	if (serial == 0) {
		domain_disconnect(wm);
		return 0;
	}
	if (strcmp(op, "home") == 0) {
		if (strtok_r(NULL, " ", &tail) != NULL) {
			domain_disconnect(wm);
			return 0;
		}
		clear_slots(wm);
		wm->home_active = true;
		wm->overview_active = false;
		project_home(wm);
		select_projection(wm);
		domain_send(wm, "applied", serial);
		return 0;
	}
	if (strcmp(op, "assigned") == 0) {
		uint64_t id = domain_number(strtok_r(NULL, " ", &tail));
		char *operation = strtok_r(NULL, " ", &tail);
		struct domain_member members[NODE_WINDOWS] = {0};
		if (id == 0 || (operation != NULL &&
				(strcmp(operation, "project") != 0 || !named_view(wm) ||
				!read_members(&tail, members)))) {
			domain_disconnect(wm);
			return 0;
		}
		struct xdg_view *view = find_view(wm, id);
		if (view != NULL) {
			view->assigned = true;
			if (wm->home_active) {
				refresh_home(wm);
			}
		}
		if (operation != NULL) {
			refresh_named(wm, members);
		}
		domain_send(wm, "applied", serial);
		return 0;
	}
	if (strcmp(op, "overview") == 0 && strtok_r(NULL, " ", &tail) == NULL) {
		clear_slots(wm);
		wm->home_active = false;
		wm->overview_active = true;
	} else if (strcmp(op, "layout") == 0 && named_view(wm)) {
		if (!read_layout(strtok_r(NULL, " ", &tail), &wm->layout) ||
				strtok_r(NULL, " ", &tail) != NULL) {
			domain_disconnect(wm);
			return 0;
		}
		project_named(wm, wm->members);
	} else if (strcmp(op, "next") == 0 && named_view(wm) && wm->layout == STACK &&
			strtok_r(NULL, " ", &tail) == NULL) {
		if (wm->visible_count > 0) {
			struct xdg_view *top = best_view(wm, true);
			int next = top != NULL ? (top->slot + 1) % wm->visible_count : 1 % wm->visible_count;
			remember_focus(wm, wm->slots[next]->toplevel->base->surface);
			project_named(wm, wm->members);
		}
	} else if (strcmp(op, "node") == 0 || strcmp(op, "focus") == 0) {
		struct domain_member members[NODE_WINDOWS] = {0};
		enum node_layout mode = TILING;
		if ((strcmp(op, "node") == 0 && !read_layout(strtok_r(NULL, " ", &tail), &mode)) ||
				!read_members(&tail, members)) {
			domain_disconnect(wm);
			return 0;
		}
		wm->home_active = false;
		wm->overview_active = false;
		wm->domain_active = true;
		wm->layout = mode;
		project_named(wm, members);
	} else {
		domain_disconnect(wm);
		return 0;
	}
	select_projection(wm);
	domain_send(wm, "applied", serial);
	return 0;
}

static void on_view_map(struct wl_listener *listener, void *data) {
	struct xdg_view *view = wl_container_of(listener, view, map);
	struct nested *wm = view->wm;
	view->mapped = true;
	if (wm->home_active) {
		refresh_home(wm);
	} else if (named_view(wm)) {
		refresh_named(wm, wm->members);
	} else if (!wm->domain_active) {
		for (int i = 0; i < HOME_WINDOWS; ++i) {
			if (wm->slots[i] == NULL) {
				view->slot = i;
				wm->slots[i] = view;
				wlr_scene_node_set_enabled(&view->frame->node, true);
				place_view(view);
				nested_seat_refresh(wm->seat);
				fprintf(stderr, "WINDOW_MAPPED=%" PRIu64 " SLOT=%d\n", view->id, i);
				break;
			}
		}
	}
	domain_send(wm, "map", view->id);
}

static void remove_view(struct xdg_view *view) {
	struct nested *wm = view->wm;
	if (wm->drag == view) {
		wm->drag = NULL;
	}
	if (wm->home_active) {
		refresh_home(wm);
		return;
	}
	if (named_view(wm)) {
		refresh_named(wm, wm->members);
		return;
	}
	if (view->slot < 0) {
		return;
	}
	struct xdg_view *other = wm->slots[1 - view->slot];
	wm->slots[view->slot] = NULL;
	view->slot = -1;
	wlr_scene_node_set_enabled(&view->frame->node, false);
	struct wlr_surface *fallback = other != NULL && !wm->cleaning &&
		other->tree != NULL && other->frame->node.enabled ?
		other->toplevel->base->surface : NULL;
	nested_seat_forget(wm->seat, view->toplevel->base->surface, fallback);
}

static void on_view_unmap(struct wl_listener *listener, void *data) {
	struct xdg_view *view = wl_container_of(listener, view, unmap);
	view->mapped = false;
	remove_view(view);
	if (!view->wm->cleaning) {
		domain_send(view->wm, "unmap", view->id);
	}
}

static void on_view_tree_destroy(struct wl_listener *listener, void *data) {
	struct xdg_view *view = wl_container_of(listener, view, tree_destroy);
	wl_list_remove(&listener->link);
	view->tree = NULL;
}

static void on_view_commit(struct wl_listener *listener, void *data) {
	(void)data;
	struct xdg_view *view = wl_container_of(listener, view, commit);
	if (view->toplevel->base->initial_commit) {
		wlr_xdg_surface_schedule_configure(view->toplevel->base);
	}
	place_view(view);
}

static void on_view_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct xdg_view *view = wl_container_of(listener, view, destroy);
	view->mapped = false;
	remove_view(view);
	if (!view->wm->cleaning) {
		domain_send(view->wm, "close", view->id);
	}
	wl_list_remove(&view->link);
	wl_list_remove(&view->map.link);
	wl_list_remove(&view->unmap.link);
	wl_list_remove(&view->commit.link);
	wl_list_remove(&view->destroy.link);
	if (view->tree != NULL) {
		wl_list_remove(&view->tree_destroy.link);
	}
	wlr_scene_node_destroy(&view->frame->node);
	free(view);
}

static void on_toplevel(struct wl_listener *listener, void *data) {
	struct nested *wm = wl_container_of(listener, wm, toplevel);
	struct wlr_xdg_toplevel *toplevel = data;
	struct xdg_view *view = calloc(1, sizeof(*view));
	if (view == NULL) {
		wl_resource_post_no_memory(toplevel->resource);
		return;
	}
	view->frame = wlr_scene_tree_create(wm->app_tree);
	if (view->frame != NULL) {
		view->tree = wlr_scene_xdg_surface_create(view->frame, toplevel->base);
	}
	if (view->tree == NULL) {
		if (view->frame != NULL) {
			wlr_scene_node_destroy(&view->frame->node);
		}
		free(view);
		wl_resource_post_no_memory(toplevel->resource);
		return;
	}
	view->wm = wm;
	view->toplevel = toplevel;
	view->id = ++wm->next_window_id;
	view->slot = -1;
	wlr_scene_node_set_enabled(&view->frame->node, false);
	view->tree_destroy.notify = on_view_tree_destroy;
	wl_signal_add(&view->tree->node.events.destroy, &view->tree_destroy);
	struct wlr_surface *surface = toplevel->base->surface;
	view->map.notify = on_view_map;
	wl_signal_add(&surface->events.map, &view->map);
	view->unmap.notify = on_view_unmap;
	wl_signal_add(&surface->events.unmap, &view->unmap);
	view->commit.notify = on_view_commit;
	wl_signal_add(&surface->events.commit, &view->commit);
	view->destroy.notify = on_view_destroy;
	wl_signal_add(&toplevel->events.destroy, &view->destroy);
	wl_list_insert(wm->views.prev, &view->link);
	domain_send(wm, "new", view->id);
}

int mothership_wm_run(struct mothership_control *control, int stop_read_fd, int domain_fd, const char **reason) {
	if (atomic_load(&control->phase) == STARTUP_INTERRUPTED) {
		*reason = NULL;
		return 0;
	}
	struct nested wm = { .domain_fd = domain_fd };
	wl_list_init(&wm.views);
	struct wl_display *parent = NULL;
	struct wlr_renderer *renderer = NULL;
	struct wlr_allocator *allocator = NULL;
	struct wl_event_source *stop_source = NULL;
	*reason = "could not create a local Wayland server";
	wm.server = wl_display_create();
	if (wm.server == NULL) {
		goto done;
	}
	struct wl_event_loop *loop = wl_display_get_event_loop(wm.server);
	*reason = "could not watch the shutdown signal";
	stop_source = wl_event_loop_add_fd(loop, stop_read_fd, WL_EVENT_READABLE, on_stop_fd, &wm);
	if (stop_source == NULL) {
		goto done;
	}
	*reason = "could not retain the selected parent Wayland display";
	const int fd = fcntl(control->parent_fd, F_DUPFD_CLOEXEC, 0);
	if (fd < 0 || (parent = wl_display_connect_to_fd(fd)) == NULL) {
		goto done;
	}

	*reason = "parent display cannot provide the wlroots Wayland backend";
	wm.backend = wlr_wl_backend_create(loop, parent);
	if (wm.backend == NULL) {
		goto done;
	}
	wm.backend_destroy.notify = on_backend_destroy;
	wl_signal_add(&wm.backend->events.destroy, &wm.backend_destroy);
	if (wl_display_get_error(parent) != 0) {
		goto done;
	}

	*reason = "could not initialize a renderer for the Wayland backend";
	renderer = wlr_renderer_autocreate(wm.backend);
	if (renderer == NULL || !wlr_renderer_init_wl_display(renderer, wm.server)) {
		goto done;
	}
	*reason = "could not allocate buffers for the nested Wayland output";
	allocator = wlr_allocator_autocreate(wm.backend, renderer);
	if (allocator == NULL) {
		goto done;
	}

	*reason = "could not create the nested Wayland globals or scene";
	wm.scene = wlr_scene_create();
	if (wm.scene == NULL ||
			wlr_compositor_create(wm.server, 5, renderer) == NULL ||
			wlr_subcompositor_create(wm.server) == NULL) {
		goto done;
	}
	struct wlr_xdg_shell *xdg_shell = wlr_xdg_shell_create(wm.server, 3);
	if (xdg_shell == NULL) {
		goto done;
	}
	wm.toplevel.notify = on_toplevel;
	wl_signal_add(&xdg_shell->events.new_toplevel, &wm.toplevel);

	*reason = "could not start the Wayland backend";
	if (!wlr_backend_start(wm.backend)) {
		goto done;
	}
	*reason = "could not create or initialize the nested output";
	wm.output = wlr_wl_output_create(wm.backend);
	if (wm.output == NULL || !wlr_output_init_render(wm.output, allocator, renderer)) {
		goto done;
	}
	wlr_wl_output_set_title(wm.output, "Mothership WM (nested)");
	wlr_wl_output_set_app_id(wm.output, "mothership-wm");
	wm.scene_output = wlr_scene_output_create(wm.scene, wm.output);
	const float color[4] = {0.07f, 0.10f, 0.14f, 1.0f};
	wm.background = wlr_scene_rect_create(&wm.scene->tree, 800, 600, color);
	if (wm.scene_output == NULL || wm.background == NULL) {
		goto done;
	}
	wm.app_tree = wlr_scene_tree_create(&wm.scene->tree);
	wm.shell_tree = wlr_scene_tree_create(&wm.scene->tree);
	if (wm.app_tree == NULL || wm.shell_tree == NULL) {
		goto done;
	}
	if (wm.domain_fd >= 0) {
		struct wlr_layer_shell_v1 *layer = wlr_layer_shell_v1_create(wm.server, 4);
		/* GTK needs the core data-device global to open Wayland. Do not
		 * handle selection/drag requests or bridge clipboard to the host. */
		if (layer == NULL || wlr_data_device_manager_create(wm.server) == NULL) {
			goto done;
		}
		wm.new_panel.notify = on_new_panel;
		wl_signal_add(&layer->events.new_surface, &wm.new_panel);
	}


	wm.frame.notify = on_frame;
	wl_signal_add(&wm.output->events.frame, &wm.frame);
	wm.resize.notify = on_resize;
	wl_signal_add(&wm.output->events.request_state, &wm.resize);
	wm.output_destroy.notify = on_output_destroy;
	wl_signal_add(&wm.output->events.destroy, &wm.output_destroy);
	struct wlr_output_state initial;
	wlr_output_state_init(&initial);
	wlr_output_state_set_enabled(&initial, true);
	wlr_output_state_set_custom_mode(&initial, 800, 600, 0);
	*reason = "could not enable the nested output and submit its first frame";
	const bool presented =
		wlr_scene_output_build_state(wm.scene_output, &initial, NULL) &&
		initial.buffer != NULL &&
		wlr_output_commit_state(wm.output, &initial);
	wlr_output_state_finish(&initial);
	if (!presented || wm.stopping || wl_display_get_error(parent) != 0) {
		goto done;
	}
	wlr_output_create_global(wm.output, wm.server);
	init_cursor(&wm);

	*reason = "could not create the nested input seat";
	wm.seat = nested_seat_create(wm.server, parent,
		wlr_wl_output_get_surface(wm.output), hit_view, remember_focus, move_cursor,
		start_view_move, update_view_move, &wm);
	if (wm.seat == NULL) {
		goto done;
	}

	*reason = "could not bind a distinct child Wayland socket in XDG_RUNTIME_DIR";
	const char *socket = wl_display_add_socket_auto(wm.server);
	if (socket == NULL) {
		goto done;
	}
	if (wm.domain_fd >= 0) {
		*reason = "could not watch the desktopd control connection";
		wm.domain_source = wl_event_loop_add_fd(loop, wm.domain_fd,
			WL_EVENT_READABLE, on_domain_fd, &wm);
		wm.domain_active = wm.home_active = true;
		nested_seat_switch(wm.seat, NULL);
		if (wm.domain_source == NULL || !domain_send(&wm, "ready_views", 0)) {
			goto done;
		}
	}
	/* Only the winner of this transition may announce readiness. A later
	 * cancellation is handled by the stop source in the event loop. */
	if (!mothership_control_serve(control)) {
		goto done;
	}
	*reason = "could not report the child Wayland socket";
	if (printf("WAYLAND_DISPLAY=%s\n", socket) < 0 || fflush(stdout) != 0) {
		goto done;
	}
	wm.ready = true;
	wl_display_run(wm.server);
	*reason = wm.failure;

done:
	if (wm.ready && wm.failure == NULL && *reason == NULL) {
		puts("WM_STOPPING");
		fflush(stdout);
	}
	wm.stopping = true;
	wm.cleaning = true;
	domain_disconnect(&wm);
	if (wm.server != NULL) {
		wl_display_destroy_clients(wm.server);
	}
	nested_seat_destroy(wm.seat);
	if (wm.new_panel.notify != NULL) {
		wl_list_remove(&wm.new_panel.link);
	}
	if (wm.toplevel.notify != NULL) {
		wl_list_remove(&wm.toplevel.link);
	}
	if (wm.backend != NULL) {
		wlr_backend_destroy(wm.backend);
	}
	if (wm.cursor_theme != NULL) {
		wlr_xcursor_theme_destroy(wm.cursor_theme);
	}
	if (wm.scene != NULL) {
		wlr_scene_node_destroy(&wm.scene->tree.node);
	}
	if (allocator != NULL) {
		wlr_allocator_destroy(allocator);
	}
	if (renderer != NULL) {
		wlr_renderer_destroy(renderer);
	}
	if (parent != NULL) {
		wl_display_disconnect(parent);
	}
	if (stop_source != NULL) {
		wl_event_source_remove(stop_source);
	}
	if (wm.server != NULL) {
		wl_display_destroy(wm.server);
	}
	if (atomic_load(&control->phase) == STARTUP_INTERRUPTED) {
		*reason = NULL;
	}
	return *reason != NULL;
}
