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

#include "nested-seat.h"

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
	struct wlr_scene_output *scene_output;
	struct wlr_scene_rect *background;
	struct wlr_scene *scene;
	struct wlr_scene_tree *app_tree, *shell_tree;
	struct shell_panel *panel;
	struct wl_listener new_panel;
	struct nested_seat *seat;
	struct xdg_view *slots[2];
	uint64_t next_window_id;
	struct wl_list views;
	struct domain_member members[2];
	uint64_t focus_order;
	bool domain_active, home_active;
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
	struct wlr_scene_tree *tree;
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
	struct wl_listener commit, unmap, destroy;
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
	int ph = height > 266 ? 254 : height - 12;
	wlr_scene_node_set_position(&panel->scene->tree->node, width - pw - 12, height - ph - 12);
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

static void place_view(struct xdg_view *view) {
	if (view == NULL || view->slot < 0 || view->tree == NULL || view->wm->output == NULL) {
		return;
	}
	int width, height;
	wlr_output_effective_resolution(view->wm->output, &width, &height);
	int middle = width / 2;
	int left = view->slot == 0 ? 0 : middle;
	int right = view->slot == 0 ? middle : width;
	if (right <= left || height <= 0) {
		wlr_scene_node_set_enabled(&view->tree->node, false);
		return;
	}
	/* Fixed two slots for this slice, not a general layout. Respect logical
	 * output coordinates and clip even clients that ignore requested size. */
	struct wlr_box clip = {
		.x = view->toplevel->base->geometry.x,
		.y = view->toplevel->base->geometry.y,
		.width = right - left,
		.height = height,
	};
	wlr_scene_node_set_position(&view->tree->node, left, 0);
	wlr_scene_subsurface_tree_set_clip(&view->tree->node, &clip);
	wlr_scene_node_set_enabled(&view->tree->node, true);
}

static struct nested_hit hit_view(void *data, double x, double y) {
	struct nested *wm = data;
	struct nested_hit hit = {0};
	if (wm->cleaning || wm->output == NULL) {
		return hit;
	}
	struct wlr_scene_node *node = wlr_scene_node_at(&wm->scene->tree.node,
		x, y, &hit.x, &hit.y);
	if (node == NULL || node->type != WLR_SCENE_NODE_BUFFER) {
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
		for (int i = 0; i < 2; ++i) {
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
	place_view(wm->slots[0]);
	place_view(wm->slots[1]);
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
	for (int i = 0; i < 2; ++i) {
		struct xdg_view *view = wm->slots[i];
		if (view != NULL && view->toplevel->base->surface == surface) {
			if (wm->home_active) {
				view->home_focused_at = ++wm->focus_order;
			} else {
				view->focused_at = ++wm->focus_order;
			}
			return;
		}
	}
}

static struct wlr_surface *view_target(struct nested *wm) {
	if (wm->cleaning) {
		return NULL;
	}
	struct xdg_view *best = NULL;
	uint64_t best_at = 0;
	for (int i = 0; i < 2; ++i) {
		struct xdg_view *view = wm->slots[i];
		if (view == NULL || view->tree == NULL || !view->tree->node.enabled) {
			continue;
		}
		uint64_t at = wm->home_active ? view->home_focused_at : view->focused_at;
		if (at > best_at) {
			best = view;
			best_at = at;
		}
	}
	return best == NULL ? NULL : best->toplevel->base->surface;
}

static void clear_slots(struct nested *wm) {
	for (int i = 0; i < 2; ++i) {
		struct xdg_view *view = wm->slots[i];
		if (view != NULL) {
			if (view->tree != NULL) {
				wlr_scene_node_set_enabled(&view->tree->node, false);
			}
			view->slot = -1;
		}
		wm->slots[i] = NULL;
	}
}

static void project_home(struct nested *wm) {
	struct xdg_view *next[2] = {0};
	int count = 0;
	struct xdg_view *view;
	/* The live list is in ID order. Overflow stays live in the catalog and
	 * joins the projection when one of the two fixed slots opens. */
	wl_list_for_each(view, &wm->views, link) {
		if (view->mapped && !view->assigned && view->tree != NULL) {
			next[count++] = view;
			if (count == 2) {
				break;
			}
		}
	}
	if (next[0] == wm->slots[0] && next[1] == wm->slots[1]) {
		return;
	}
	clear_slots(wm);
	for (int i = 0; i < count; ++i) {
		wm->slots[i] = next[i];
		next[i]->slot = i;
		place_view(next[i]);
	}
}

static void refresh_home(struct nested *wm) {
	struct xdg_view *old[] = {wm->slots[0], wm->slots[1]};
	project_home(wm);
	for (int i = 0; i < 2; ++i) {
		if (old[i] != NULL && old[i]->slot < 0) {
			nested_seat_forget(wm->seat, old[i]->toplevel->base->surface,
				view_target(wm));
		}
	}
	nested_seat_refresh(wm->seat);
}

static bool show_domain_member(struct xdg_view *view) {
	for (int i = 0; i < 2; ++i) {
		if (view->id != view->wm->members[i].id) {
			continue;
		}
		if (view->epoch != view->wm->members[i].epoch) {
			view->epoch = view->wm->members[i].epoch;
			view->focused_at = 0;
		}
		if (view->mapped && view->tree != NULL) {
			view->slot = i;
			view->wm->slots[i] = view;
			place_view(view);
			return true;
		}
	}
	return false;
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

static bool read_members(char **tail, struct domain_member members[2]) {
	for (int i = 0; i < 2; ++i) {
		char *id = strtok_r(NULL, " ", tail);
		if (id == NULL) {
			return true;
		}
		members[i].id = domain_number(id);
		members[i].epoch = domain_number(strtok_r(NULL, " ", tail));
		if (members[i].id == 0 || members[i].epoch == 0 ||
				(i > 0 && members[i].id == members[0].id)) {
			return false;
		}
	}
	return strtok_r(NULL, " ", tail) == NULL;
}

static void project_named(struct nested *wm, const struct domain_member members[2]) {
	clear_slots(wm);
	memcpy(wm->members, members, sizeof(wm->members));
	for (int i = 0; i < 2; ++i) {
		struct xdg_view *view = find_view(wm, members[i].id);
		if (view != NULL) {
			show_domain_member(view);
		}
	}
}

static void refresh_named(struct nested *wm, const struct domain_member members[2]) {
	struct xdg_view *old[] = {wm->slots[0], wm->slots[1]};
	project_named(wm, members);
	struct wlr_surface *fallback = view_target(wm);
	for (int i = 0; i < 2 && fallback == NULL; ++i) {
		struct xdg_view *v = wm->slots[i];
		if (v != NULL && v->tree != NULL && v->tree->node.enabled) {
			fallback = v->toplevel->base->surface;
		}
	}
	for (int i = 0; i < 2; ++i) {
		if (old[i] != NULL && old[i]->slot < 0) {
			nested_seat_forget(wm->seat, old[i]->toplevel->base->surface, fallback);
		}
	}
	nested_seat_refresh(wm->seat);
}

static int on_domain_fd(int fd, uint32_t mask, void *data) {
	struct nested *wm = data;
	if (!(mask & WL_EVENT_READABLE)) {
		domain_disconnect(wm);
		return 0;
	}
	char buf[160];
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
		wm->home_active = true;
		project_home(wm);
		nested_seat_switch(wm->seat, view_target(wm));
		domain_send(wm, "applied", serial);
		return 0;
	}
	if (strcmp(op, "assigned") == 0) {
		uint64_t id = domain_number(strtok_r(NULL, " ", &tail));
		char *operation = strtok_r(NULL, " ", &tail);
		struct domain_member members[2] = {0};
		if (id == 0 || (operation != NULL &&
				(strcmp(operation, "project") != 0 || wm->home_active ||
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
	if (strcmp(op, "focus") != 0) {
		domain_disconnect(wm);
		return 0;
	}
	struct domain_member members[2] = {0};
	if (!read_members(&tail, members)) {
		domain_disconnect(wm);
		return 0;
	}
	/* Visibility and keyboard focus belong to this one WM event loop. */
	wm->home_active = false;
	wm->domain_active = true;
	project_named(wm, members);
	nested_seat_switch(wm->seat, view_target(wm));
	domain_send(wm, "applied", serial);
	return 0;
}

static void on_view_map(struct wl_listener *listener, void *data) {
	struct xdg_view *view = wl_container_of(listener, view, map);
	struct nested *wm = view->wm;
	view->mapped = true;
	if (wm->home_active) {
		refresh_home(wm);
	} else if (wm->domain_active) {
		show_domain_member(view);
		nested_seat_refresh(wm->seat);
	} else {
		for (int i = 0; i < 2; ++i) {
			if (wm->slots[i] == NULL) {
				view->slot = i;
				wm->slots[i] = view;
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
	if (view->wm->home_active) {
		refresh_home(view->wm);
		return;
	}
	if (view->slot < 0) {
		return;
	}
	struct nested *wm = view->wm;
	struct xdg_view *other = wm->slots[1 - view->slot];
	wm->slots[view->slot] = NULL;
	view->slot = -1;
	if (view->tree != NULL) {
		wlr_scene_node_set_enabled(&view->tree->node, false);
	}
	struct wlr_surface *fallback = other != NULL && !wm->cleaning &&
		other->tree != NULL && other->tree->node.enabled ?
		other->toplevel->base->surface : NULL;
	if (wm->domain_active) {
		fallback = wm->cleaning ? NULL : view_target(wm);
	}
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
		wlr_scene_node_destroy(&view->tree->node);
	}
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
	view->tree = wlr_scene_xdg_surface_create(wm->app_tree, toplevel->base);
	if (view->tree == NULL) {
		free(view);
		wl_resource_post_no_memory(toplevel->resource);
		return;
	}
	view->wm = wm;
	view->toplevel = toplevel;
	view->id = ++wm->next_window_id;
	view->slot = -1;
	wlr_scene_node_set_enabled(&view->tree->node, false);
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

	*reason = "could not create the nested input seat";
	wm.seat = nested_seat_create(wm.server, parent,
		wlr_wl_output_get_surface(wm.output), hit_view, remember_focus, &wm);
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
		if (wm.domain_source == NULL || !domain_send(&wm, "ready_home", 0)) {
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
