//go:build linux && cgo && wlroots

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <wayland-client-core.h>
#include <wayland-server-core.h>
#include <wlr/backend/wayland.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_output.h>
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

struct nested {
	struct wl_display *server;
	struct wlr_backend *backend;
	struct wlr_output *output;
	struct wlr_scene_output *scene_output;
	struct wlr_scene_rect *background;
	struct wlr_scene *scene;
	struct nested_seat *seat;
	struct xdg_view *slots[2];
	uint64_t next_window_id;
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
	int slot;
	struct wl_listener map, unmap, commit, destroy, tree_destroy;
};

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
	nested_seat_refresh(wm->seat);
}

static void on_view_map(struct wl_listener *listener, void *data) {
	struct xdg_view *view = wl_container_of(listener, view, map);
	for (int i = 0; i < 2; ++i) {
		if (view->wm->slots[i] == NULL) {
			view->slot = i;
			view->wm->slots[i] = view;
			place_view(view);
			nested_seat_refresh(view->wm->seat);
			fprintf(stderr, "WINDOW_MAPPED=%" PRIu64 " SLOT=%d\n", view->id, i);
			return;
		}
	}
}

static void remove_view(struct xdg_view *view) {
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
	nested_seat_forget(wm->seat, view->toplevel->base->surface, fallback);
}

static void on_view_unmap(struct wl_listener *listener, void *data) {
	struct xdg_view *view = wl_container_of(listener, view, unmap);
	remove_view(view);
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
	remove_view(view);
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
	view->tree = wlr_scene_xdg_surface_create(&wm->scene->tree, toplevel->base);
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
}

int mothership_wm_run(struct mothership_control *control, int stop_read_fd, const char **reason) {
	if (atomic_load(&control->phase) == STARTUP_INTERRUPTED) {
		*reason = NULL;
		return 0;
	}
	struct nested wm = {0};
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
		wlr_wl_output_get_surface(wm.output), hit_view, &wm);
	if (wm.seat == NULL) {
		goto done;
	}

	*reason = "could not bind a distinct child Wayland socket in XDG_RUNTIME_DIR";
	const char *socket = wl_display_add_socket_auto(wm.server);
	if (socket == NULL) {
		goto done;
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
	if (wm.server != NULL) {
		wl_display_destroy_clients(wm.server);
	}
	nested_seat_destroy(wm.seat);
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
