//go:build linux && cgo && wlroots

#include <errno.h>
#include <linux/input-event-codes.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
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
#include <wlr/backend/drm.h>
#include <wlr/backend/headless.h>
#include <wlr/backend/session.h>
#include <wlr/render/allocator.h>
#include <wlr/render/gles2.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_damage_ring.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_linux_drm_syncobj_v1.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/transform.h>
#include <wlr/util/edges.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_xdg_foreign_v1.h>
#include <wlr/types/wlr_xdg_foreign_v2.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/xcursor.h>

#include "nested-seat.h"
#include "nested-parent.h"
#include "generated-map-pointer.h"
#include "../../internal/wayland/map-frame.h"
#include "nested-layout.h"
#include "flow-group.h"
#include "flow-order.h"
#include "group-chrome.h"

enum { WINDOW_CHROME_GROUP_LAYOUT=100,WINDOW_CHROME_GROUP_MINIMIZE };
#include "window-chrome.h"
#include "video-frost.h"
#include "scene-glass.h"
#include "render-debug.h"
#include "preview-projection.h"
#include "stage-scale.h"
#include "surface-sync.h"
#include "owned-preview.h"
#include "screenshot-capture.h"

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
		if(control->parent_fd>=0) {
			while (shutdown(control->parent_fd, SHUT_RDWR) < 0 && errno == EINTR) {}
			return;
		}
	}
	if (expected == SERVING || control->parent_fd<0) {
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

enum {
	PIN_LIVE = 3, PIN_ROWS = 5, PIN_WIDTH = 320, PIN_MAX_HEIGHT = 240,
	PIN_HEADER = 24, PIN_ROW = 32, PIN_EDGE = 18, PIN_RAIL_GAP = 16,
	PIN_GAP = 8, PIN_DRAG = 8, PIN_CLOSE = 24, PIN_TOAST_CLEAR = 138,
	PIN_IMAGE_PAD = 3,
};

struct pin_face {
	struct wlr_scene_buffer *scene;
	int width, height;
	float scale;
	bool highlight;
};

struct pin_item {
	uint64_t id;
	struct wlr_box box;
};

enum pin_action { PIN_NONE, PIN_OPEN, PIN_HEADER_PRESS, PIN_REMOVE, PIN_MORE };

struct nested {
	struct wl_display *server;
	struct nested_parent parent_window;
	struct wlr_backend *backend;
	struct wlr_session *hw_session;
	bool native_output,headless_test;
	struct wl_listener native_added,session_active,session_destroy;
	struct wlr_output *output;
	struct wlr_output_cursor *cursor;
	struct wlr_xcursor_theme *cursor_theme;
	struct wlr_xcursor_image *cursor_image;
	bool cursor_visible;
	int cursor_size,cursor_pixels;
	struct wlr_scene_output *scene_output;
	struct render_debug *render_debug;
	struct {bool pending;int w,h,hz,scale;} display_request;
	uint64_t reveal_ack;
	int64_t reveal_start;
	bool revealing;
	struct wlr_box aperture;
	struct wlr_scene_rect *background;
	struct wlr_scene *scene;
	struct wlr_scene_tree *wallpaper_tree, *app_tree, *hud_tree, *shell_tree, *node_strip_tree;
	struct wlr_scene_tree *dialog_tree;
	struct wl_list dialogs;
	struct wlr_scene_tree *node_switcher_tree;
	struct wlr_box node_strip;
	struct {
		struct wlr_scene_rect *background,*ring,*placeholder,*focus_marks[9];
		struct wlr_scene_buffer *image;
		uint64_t id;
		int painted_left,painted_right,painted_top,painted_height;
		struct wlr_box painted, reorder_from;
		double lift, lift_from, lift_to;
		double focus_ink,focus_from,focus_to;
		int64_t lift_start_ms,focus_start_ms;
	} preview[NODE_WINDOWS];
	uint64_t strip_hover;
	double strip_cursor_y;
	double strip_offset;
	int last_window_count,last_window_overflow;
	uint64_t picker_mark_id;
	uint64_t cycle_ids[NODE_WINDOWS],picker_ids[NODE_WINDOWS];
	int cycle_count,picker_count;
	bool cycle_has_origin;
	struct wlr_scene_tree *picker_tree;
	struct wlr_box picker_tiles[NODE_WINDOWS];
	struct wlr_scene_buffer *picker_preview[NODE_WINDOWS];
	struct wlr_scene_tree *picker_focus_tree;
	struct wlr_scene_rect *picker_focus_face,*picker_focus_marks[9];
	struct wlr_scene_buffer *picker_focus_image[FLOW_GROUP_MAX];
	struct wlr_box picker_focus_from,picker_focus_now;
	struct preview_projection picker_beam;
	double picker_rail_before,picker_rail_from,picker_rail_to;
	int64_t picker_rail_started,picker_opened_at;
	bool picker_rail_sliding;
	uint64_t picker_focus_id;
	int picker_scale,strip_hover_scale;
	int64_t picker_focus_started;
	uint32_t picker_serial;
	bool picker_keyboard;
	bool strip_motion;
	struct shell_panel *wallpaper, *panel, *map_hud, *node_telemetry;
	struct shell_panel *node_switcher, *pin_label;
	struct wl_resource *map_pointer;
	struct {
		struct wl_event_source *pid_source;
		int pid_fd;
		pid_t panel_pid;
		struct wl_resource *resource;
		bool opening, pending, active;
		uint64_t window, targets[NODE_WINDOWS];
		size_t target_count;
	} screenshot;
	bool map_aim_enabled,map_aim_valid;
	double map_aim_x,map_aim_y;
	bool map_zoom_enabled, video_frost_requested;
	int video_frost_margin;
	struct video_frost *video_frost;
	struct {
		struct scene_glass *glass;
		struct wlr_box box;
		unsigned alpha;
	} scene_glass[SCENE_GLASS_COUNT];
	int map_zoom_margin;
	struct wl_listener new_panel;
	struct nested_seat *seat;
	struct xdg_view *slots[NODE_WINDOWS], *drag, *focused;
	int flow_offset, flow_low, flow_high;
	int flow_slide_from, flow_slide_to, flow_slide_duration;
	int64_t flow_slide_start;
	bool flow_sliding;
	struct {
		bool active;
		int64_t start;
		double remaining, flow_from, strip_from;
	} reorder;
	uint64_t next_layout_order;
	struct xdg_view *stage_painted;
	struct wlr_scene_tree *stage_overlay;
	struct {
		struct stage_scale sprite;
		struct wlr_box anchor;
	} stage_flight[FLOW_GROUP_MAX];
	int stage_flight_count;
	struct wlr_scene_tree *maximize_overlay;
	struct {
		struct stage_scale sprite;
		struct xdg_view *view;
		struct wlr_box from, painted, button;
		int64_t start;
	} maximize;
	struct {
		struct stage_scale sprite;
		struct wlr_box anchor;
		uint64_t id;
		int64_t started;
		bool opening;
	} tray_flight[FLOW_GROUP_MAX];
	double stage_amount, stage_rail_from, stage_rail_to;
	int64_t stage_tick;
	struct xdg_view *mode_slide_view;
	int mode_slide_x,mode_slide_y,mode_slide_from_x,mode_slide_from_y;
	int64_t mode_slide_start;
	struct rect drag_rect;
	int drag_x, drag_y;
	bool drag_resize;
	struct {
		struct xdg_view *view;
		struct window_resize_preview *preview;
		struct rect start;
		double x, y;
	} resize;
	enum node_layout layout;
	int visible_count;
	uint64_t next_window_id,next_group_id,next_group_order;
	struct {
		uint64_t source,target;
		int64_t since;
		double x,y,from_x,from_y;
		bool tray,moved,beam;
	} group_grab;
	struct {
		uint64_t id;
		int64_t started;
		struct rect from;
	} group_join;
	struct wlr_scene_buffer *group_ghost;
	struct wlr_scene_rect *group_marks[9],*group_tray_ring[NODE_WINDOWS];
	struct {
		struct group_chrome *chrome;
		struct wlr_box box;
		uint64_t leader;
	} group_frame[NODE_WINDOWS/2];
	bool frame_glass_enabled;
	struct wl_list views;
	struct domain_member members[NODE_WINDOWS];
	uint64_t focus_order;
	bool domain_active, home_active, overview_active, default_node_active;
	int domain_fd;
	struct wl_event_source *domain_source;
	// Keep membership independent of the bounded scene projection.
	struct {
		struct wl_list windows; // newest first
		int corner; // 0 bottom-right, 1 bottom-left, 2 top-left, 3 top-right
		bool expanded,moved;
		enum pin_action grab;
		uint64_t grabbed_id;
		int count,live,scroll_row,scroll_max;
		double scroll_delta,grab_x,grab_y;
		int drag_x,drag_y;
		struct wlr_box box,grab_box,control_box,sent_control;
		int sent_remaining;
		bool sent_expanded;
		struct pin_item items[PIN_ROWS];
		struct wlr_scene_tree *tree;
		struct pin_face faces[PIN_LIVE],menu,control;
		struct wlr_scene_buffer *images[PIN_LIVE];
		struct pin_face highlight;
		bool pointer_inside,hovered;
		double pointer_x,pointer_y,hover_opacity,hover_from;
		uint64_t hover_id;
		int64_t hover_started;
	} pin;
	// Coarse idle for the Mission Log: no input for IDLE_SECONDS.
	struct wl_event_source *idle_timer;
	int64_t last_input_ms;
	bool idle_reported;
	struct wl_listener backend_destroy;
	struct wl_listener output_destroy;
	struct wl_listener frame;
	struct wl_listener toplevel, new_popup, new_decoration;
	bool stopping;
	bool cleaning;
	bool ready;
	const char *failure;
};

struct xdg_view {
	struct nested *wm;
	struct wlr_xdg_toplevel *toplevel;
	struct wlr_xdg_toplevel_decoration_v1 *decoration;
	struct wl_listener decoration_request, decoration_destroy;
	struct wlr_scene_tree *frame, *content, *tree;
	struct wlr_scene_rect *bar, *grip;
	struct rect floating, placement, flow_base, flow_unit;
	int stage_width, stage_height;
	uint64_t group_id,group_order;
	int group_sx,group_sy,group_gap;
	struct window_chrome *chrome;
	struct scene_glass *frame_glass;
	struct owned_preview preview_copy;
	uint64_t preview_generation;
	int content_top, content_bottom, content_edge, flow_width, flow_height;
	bool flowed_out, maximized, configured_maximized, configured_stage;
	int configure_w, configure_h;
	uint64_t id;
	uint64_t epoch, focused_at, home_focused_at;
	uint64_t node_order, home_order;
	int reorder_x, reorder_y;
	bool mapped, assigned, minimized, focus_on_show;
	bool catalogued, dialog;
	struct xdg_view *dialog_parent;
	struct wl_list dialog_link;
	int slot;
	struct wl_list link, pin_link;
	struct wlr_box pin_label_sent;
	bool pin_label_dirty;
	struct wl_listener map, unmap, commit, configure, destroy, tree_destroy, app_id, title;
	struct wl_listener request_maximize, request_minimize;
	struct wl_listener set_parent;
};

struct shell_panel {
	struct nested *wm;
	struct shell_panel **slot;
	struct wlr_layer_surface_v1 *layer;
	struct wlr_scene_layer_surface_v1 *scene;
	bool wants_keyboard, paint_only, compact;
	int compact_width,compact_height;
	struct wl_listener commit, map, unmap, destroy, new_popup;
};

struct shell_popup {
	struct nested *wm;
	struct wlr_xdg_popup *popup;
	struct wl_listener commit, destroy;
};

struct app_popup {
	struct nested *wm;
	struct wlr_xdg_popup *popup;
	struct wlr_xdg_surface *toplevel;
	struct wlr_scene_tree *scene;
	struct wl_listener commit, reposition, destroy, scene_destroy;
};

static void constrain_app_popup(struct app_popup *entry) {
	int x, y;
	struct wlr_scene_tree *content = entry->toplevel->data;
	wlr_scene_node_coords(&content->node, &x, &y);
	// The content origin is the window geometry, but wlroots expects
	// the constraint in root surface coordinates.
	struct wlr_box viewport = {
		.x = entry->toplevel->geometry.x - x,
		.y = entry->toplevel->geometry.y - y,
		.width = entry->wm->parent_window.width,
		.height = entry->wm->parent_window.height,
	};
	wlr_xdg_popup_unconstrain_from_box(entry->popup, &viewport);
}

static void app_popup_commit(struct wl_listener *listener, void *data) {
	(void)data;
	struct app_popup *entry = wl_container_of(listener, entry, commit);
	if (entry->popup->base->initial_commit) constrain_app_popup(entry);
}

static void app_popup_reposition(struct wl_listener *listener, void *data) {
	(void)data;
	struct app_popup *entry = wl_container_of(listener, entry, reposition);
	constrain_app_popup(entry);
}

static void app_popup_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct app_popup *entry = wl_container_of(listener, entry, destroy);
	wlr_scene_node_destroy(&entry->scene->node);
}

static void app_popup_scene_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct app_popup *entry = wl_container_of(listener, entry, scene_destroy);
	entry->popup->base->data = NULL;
	wl_list_remove(&entry->commit.link);
	wl_list_remove(&entry->reposition.link);
	wl_list_remove(&entry->destroy.link);
	wl_list_remove(&entry->scene_destroy.link);
	free(entry);
}

static void on_app_popup(struct wl_listener *listener, void *data) {
	struct nested *wm = wl_container_of(listener, wm, new_popup);
	struct wlr_xdg_popup *popup = data;
	if (popup->parent == NULL) return; // Layer-surface popups use their own path.
	struct wlr_xdg_surface *parent = wlr_xdg_surface_try_from_wlr_surface(popup->parent);
	if (parent == NULL || parent->data == NULL) return;
	struct wlr_scene_tree *parent_scene = parent->data;
	while (parent->role == WLR_XDG_SURFACE_ROLE_POPUP) {
		parent = wlr_xdg_surface_try_from_wlr_surface(parent->popup->parent);
	}
	struct app_popup *entry = calloc(1, sizeof(*entry));
	if (entry == NULL || (entry->scene = wlr_scene_xdg_surface_create(parent_scene, popup->base)) == NULL) {
		free(entry);
		wl_resource_post_no_memory(popup->resource);
		return;
	}
	entry->wm = wm;
	entry->popup = popup;
	entry->toplevel = parent;
	popup->base->data = entry->scene;
	entry->commit.notify = app_popup_commit;
	wl_signal_add(&popup->base->surface->events.commit, &entry->commit);
	entry->reposition.notify = app_popup_reposition;
	wl_signal_add(&popup->events.reposition, &entry->reposition);
	entry->destroy.notify = app_popup_destroy;
	wl_signal_add(&popup->events.destroy, &entry->destroy);
	entry->scene_destroy.notify = app_popup_scene_destroy;
	wl_signal_add(&entry->scene->node.events.destroy, &entry->scene_destroy);
}

static void screenshot_reset(struct nested *);
static void screenshot_problem(struct nested *, const char *, bool);

static void shell_popup_commit(struct wl_listener *listener, void *data) {
	(void)data;
	struct shell_popup *entry = wl_container_of(listener, entry, commit);
	if (!entry->popup->base->initial_commit) return;
	// The layer surface spans the output and is anchored at (0, 0).
	struct wlr_box viewport = {
		.width = entry->wm->parent_window.width,
		.height = entry->wm->parent_window.height,
	};
	wlr_xdg_popup_unconstrain_from_box(entry->popup, &viewport);
	wlr_xdg_surface_schedule_configure(entry->popup->base);
}

static void shell_popup_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct shell_popup *entry = wl_container_of(listener, entry, destroy);
	wl_list_remove(&entry->commit.link);
	wl_list_remove(&entry->destroy.link);
	free(entry);
}

static void panel_new_popup(struct wl_listener *listener, void *data) {
	struct shell_panel *panel = wl_container_of(listener, panel, new_popup);
	struct wlr_xdg_popup *popup = data;
	if (panel->paint_only) return;
	struct shell_popup *entry = calloc(1, sizeof(*entry));
	if (entry == NULL || wlr_scene_xdg_surface_create(panel->scene->tree, popup->base) == NULL) {
		free(entry);
		wl_resource_post_no_memory(popup->resource);
		return;
	}
	entry->wm = panel->wm;
	entry->popup = popup;
	entry->commit.notify = shell_popup_commit;
	wl_signal_add(&popup->base->surface->events.commit, &entry->commit);
	entry->destroy.notify = shell_popup_destroy;
	wl_signal_add(&popup->events.destroy, &entry->destroy);
}

static void position_panel(struct shell_panel *panel) {
	if (panel == NULL || panel->wm->output == NULL || !panel->layer->initialized) {
		return;
	}
	int width = panel->wm->parent_window.width;
	int height = panel->wm->parent_window.height;
	if (width <= 12 || height <= 12) {
		return;
	}
	if (panel->compact) {
		const struct wlr_layer_surface_v1_state *state=&panel->layer->current;
		int x=(int)fmax(0,fmin(width-1,state->margin.left));
		int y=(int)fmax(0,fmin(height-1,state->margin.top));
		int w=(int)fmax(1,fmin(width-x,state->desired_width));
		int h=(int)fmax(1,fmin(height-y,state->desired_height));
		wlr_scene_node_set_position(&panel->scene->tree->node,x,y);
		if (panel->compact_width!=w || panel->compact_height!=h) {
			panel->compact_width=w;panel->compact_height=h;
			wlr_layer_surface_v1_configure(panel->layer,w,h);
		}
		return;
	}
	// One GTK window owns the home dock, keyboard mode and its menus.
	// The shell restricts hits to its actual visible controls using a
	// Wayland input region; the rest of this viewport is transparent.
	wlr_scene_node_set_position(&panel->scene->tree->node, 0, 0);
	wlr_layer_surface_v1_configure(panel->layer, width, height);
}

static void refresh_blank_shell(struct nested *wm) {
	bool blank=wm->overview_active || (wm->home_active && wm->visible_count==0);
	nested_seat_blank_shell(wm->seat,blank && wm->panel && wm->panel->layer->surface->mapped
		? wm->panel->layer->surface : NULL);
}

static void panel_commit(struct wl_listener *listener, void *data) {
	(void)data;
	struct shell_panel *panel = wl_container_of(listener, panel, commit);
	struct wlr_layer_surface_v1 *layer = panel->layer;
	if(layer->surface->mapped)
		render_debug_surface(panel->wm->render_debug,panel==panel->wm->wallpaper?RENDER_WALLPAPER:
			panel==panel->wm->map_hud?RENDER_MAP:RENDER_PANEL,0,layer->surface);
	if (layer->initial_commit || (panel->compact && (layer->current.committed &
			(WLR_LAYER_SURFACE_V1_STATE_DESIRED_SIZE|WLR_LAYER_SURFACE_V1_STATE_MARGIN)))) {
		position_panel(panel);
	}
	bool wants = !panel->paint_only && layer->surface->mapped && layer->current.keyboard_interactive ==
		ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND;
	if (wants != panel->wants_keyboard) {
		panel->wants_keyboard = wants;
		nested_seat_panel(panel->wm->seat, layer->surface, wants);
	}
}

static void panel_map(struct wl_listener *listener, void *data) {
	(void)data;
	struct shell_panel *panel = wl_container_of(listener, panel, map);
	if (panel == panel->wm->map_hud && !wl_list_empty(&panel->wm->pin.windows) && panel->wm->output)
		wlr_output_schedule_frame(panel->wm->output);
	if (panel->paint_only) return;
	refresh_blank_shell(panel->wm);
	puts("SHELL_READY");
	fflush(stdout);
}

static void panel_unmap(struct wl_listener *listener, void *data) {
	(void)data;
	struct shell_panel *panel = wl_container_of(listener, panel, unmap);
	if (panel == panel->wm->map_hud && !wl_list_empty(&panel->wm->pin.windows) && panel->wm->output)
		wlr_output_schedule_frame(panel->wm->output);
	if (panel->compact) panel->compact_width=panel->compact_height=0;
	if (!panel->paint_only) {
		if (panel == panel->wm->panel)
			screenshot_problem(panel->wm, "The Panel closed.", true);
		nested_seat_cancel_node_switch(panel->wm->seat);
		nested_seat_cancel_map_pointer(panel->wm->seat);
		nested_seat_blank_shell(panel->wm->seat,NULL);
		panel->wants_keyboard = false;
		nested_seat_panel(panel->wm->seat, panel->layer->surface, false);
	}
}

static void panel_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct shell_panel *panel = wl_container_of(listener, panel, destroy);
	if (!panel->paint_only) {
		if (panel == panel->wm->panel) screenshot_reset(panel->wm);
		nested_seat_cancel_node_switch(panel->wm->seat);
		nested_seat_cancel_map_pointer(panel->wm->seat);
		nested_seat_blank_shell(panel->wm->seat,NULL);
		nested_seat_panel(panel->wm->seat, panel->layer->surface, false);
		panel->wm->map_zoom_enabled=false;
		nested_seat_map_home(panel->wm->seat,false);
		panel->wm->map_pointer=NULL;
		for(unsigned i=0;i<SCENE_GLASS_COUNT;i++) panel->wm->scene_glass[i].alpha=0;
		panel->wm->display_request.pending=false;
		panel->wm->video_frost_requested=false;
		video_frost_hide(panel->wm->video_frost);
		panel->wm->map_aim_enabled=panel->wm->map_aim_valid=false;
	}
	*panel->slot = NULL;
	wl_list_remove(&panel->commit.link);
	wl_list_remove(&panel->map.link);
	wl_list_remove(&panel->unmap.link);
	wl_list_remove(&panel->destroy.link);
	wl_list_remove(&panel->new_popup.link);
	free(panel);
}

static void on_new_panel(struct wl_listener *listener, void *data) {
	struct nested *wm = wl_container_of(listener, wm, new_panel);
	struct wlr_layer_surface_v1 *layer = data;
	struct shell_panel **slot;
	struct wlr_scene_tree *tree;
	if (strcmp(layer->namespace, "mothership-wallpaper") == 0) {
		slot = &wm->wallpaper;
		tree = wm->wallpaper_tree;
	} else if (strcmp(layer->namespace, "mothership-center-map") == 0) {
		slot = &wm->map_hud;
		tree = wm->hud_tree;
	} else if (strcmp(layer->namespace, "mothership-command-center") == 0) {
		slot = &wm->panel;
		tree = wm->shell_tree;
	} else if (strcmp(layer->namespace,"mothership-node-telemetry")==0 && wm->panel &&
			wl_resource_get_client(layer->resource)==wl_resource_get_client(wm->panel->layer->resource)) {
		slot=&wm->node_telemetry;
		tree=wm->shell_tree;
	} else if (strcmp(layer->namespace,"mothership-pin-label")==0 && wm->panel &&
			wl_resource_get_client(layer->resource)==wl_resource_get_client(wm->panel->layer->resource)) {
		// Labels stay above card pixels and below the interactive shell.
		slot=&wm->pin_label;
		tree=wm->pin.tree;
	} else if (strcmp(layer->namespace, "mothership-node-switcher") == 0 && wm->panel &&
			wl_resource_get_client(layer->resource) == wl_resource_get_client(wm->panel->layer->resource)) {
		slot = &wm->node_switcher;
		tree = wm->node_switcher_tree;
	} else {
		wlr_layer_surface_v1_destroy(layer);
		return;
	}
	if (*slot != NULL || wm->output == NULL ||
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
	panel->scene = wlr_scene_layer_surface_v1_create(tree, layer);
	if (slot==&wm->wallpaper) video_frost_raise(wm->video_frost);
	if (panel->scene == NULL) {
		free(panel);
		wlr_layer_surface_v1_destroy(layer);
		return;
	}
	panel->paint_only = slot!=&wm->panel;
	panel->compact = slot==&wm->node_telemetry || slot==&wm->pin_label;
	panel->slot = slot;
	*slot = panel;
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
	panel->new_popup.notify = panel_new_popup;
	wl_signal_add(&layer->events.new_popup, &panel->new_popup);
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
	nested_seat_detach_local(wm->seat);
	if(wm->native_added.notify) {
		wl_list_remove(&wm->native_added.link);
		wm->native_added.notify=NULL;
	}
	wm->backend = NULL;
	if (!wm->cleaning) {
		stop(wm, wm->native_output?"native display backend ended":"parent Wayland connection ended");
	}
}

static void on_output_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct nested *wm = wl_container_of(listener, wm, output_destroy);
	screenshot_problem(wm, "The display disconnected.", true);
	wl_list_remove(&wm->frame.link);
	wl_list_remove(&listener->link);
	wm->output = NULL;
	wm->cursor = NULL; /* owned by the output */
	wm->scene_output = NULL; /* wlroots destroys it with the output. */
	if (!wm->stopping) {
		stop(wm, wm->native_output?"native display disconnected":wm->ready?NULL:"parent closed the output during startup");
	}
}

enum { WORKSPACE_REVEAL_US = 200000 };
static bool domain_send(struct nested *,const char *,uint64_t);
static void pin_draw(struct nested *,struct timespec);
static void pin_forget(struct nested *,struct xdg_view *);
static bool pin_ready(const struct nested *);
static void pin_replay(struct nested *);
static void pin_metadata_changed(struct xdg_view *);
static bool pin_hit(struct nested *,double,double);
static enum pin_action pin_action_at(struct nested *,double,double,uint64_t *);
static uint64_t pin_hover_target(struct nested *);
static void pin_grab(struct nested *,enum nested_move_phase,double,double);
static void select_projection(struct nested *,bool);
static void apply_view_clip(struct xdg_view *);
static bool is_flow(struct nested *);
static bool is_stage(struct nested *);
static struct xdg_view *find_view(struct nested *,uint64_t);
static void strip_focus_view(struct nested *,struct xdg_view *);
static void focus_shown_view(struct nested *);
static void strip_reveal_window(struct nested *,struct xdg_view *);
static void group_frame(struct nested *,struct timespec);
static void flow_reorder_finish(struct nested *);
static unsigned decorate_frames(struct nested *);
static void resize_cancel(struct nested *);
static bool group_large(struct nested *wm) {
	return (is_flow(wm) || is_stage(wm)) && wm->parent_window.width>=FLOW_GROUP_MIN_WIDTH &&
		wm->parent_window.height>=FLOW_GROUP_MIN_HEIGHT;
}
static bool group_visible(struct xdg_view *v) {
	return v && v->mapped && v->tree && !v->minimized &&
		(is_stage(v->wm) || (!v->flowed_out && !v->maximized));
}
static int group_members(struct nested *wm,struct xdg_view *v,
		struct xdg_view *out[FLOW_GROUP_MAX],bool visible) {
	int n=0;
	if(!v || !v->group_id || !group_large(wm) || (visible && !group_visible(v)))return 0;
	for(int i=0;i<wm->visible_count;i++) {
		struct xdg_view *part=wm->slots[i];
		if(!part || part->group_id!=v->group_id || (visible && !group_visible(part)))continue;
		if(n>=FLOW_GROUP_MAX) return 0;
		int j=n++;
		while(j>0 && out[j-1]->group_order>part->group_order) {
			out[j]=out[j-1];j--;
		}
		out[j]=part;
	}
	return n;
}

static struct rect stage_bounds(struct nested *wm) {
	int height=wm->parent_window.height;
	int work=shell_node_dock_top(height)-SHELL_NODE_GUIDE_HEIGHT-SHELL_NODE_GUIDE_GAP;
	return stage_rect(wm->parent_window.width,work>120?work:height);
}

static void group_window_tiles(struct xdg_view *view,struct rect area,int count,
		struct rect tiles[FLOW_GROUP_MAX]) {
	struct rect inner={area.x+GROUP_FRAME_PAD,area.y+GROUP_FRAME_TOP,
		area.width-2*GROUP_FRAME_PAD,area.height-GROUP_FRAME_TOP-GROUP_FRAME_PAD};
	flow_group_tiles(inner,count,view->group_sx,view->group_sy,view->group_gap,220,140,tiles);
}

static int stage_members(struct nested *wm,struct xdg_view *view,struct xdg_view *members[FLOW_GROUP_MAX]) {
	if(!group_visible(view) || view->slot<0)return 0;
	int count=group_members(wm,view,members,true);
	if(count>=2)return count;
	members[0]=view;
	return 1;
}

static bool stage_same_unit(struct nested *wm,struct xdg_view *a,struct xdg_view *b) {
	return a==b || (a && b && a->group_id && a->group_id==b->group_id && group_large(wm));
}
static int64_t group_ms(void) {
	struct timespec t;
	if(clock_gettime(CLOCK_MONOTONIC,&t)!=0)return 0;
	return t.tv_sec*1000LL+t.tv_nsec/1000000;
}

static int64_t monotonic_us(struct timespec now) {
	return (int64_t)now.tv_sec*1000000 + now.tv_nsec/1000;
}

static void reveal_frame(struct nested *wm,struct timespec now) {
	if (!wm->revealing) return;
	int64_t us=monotonic_us(now);
	if (!wm->reveal_start) wm->reveal_start=us;
	double t=(us-wm->reveal_start)/(double)WORKSPACE_REVEAL_US;
	if(t>=1) {
		wm->revealing=false;
		for(int i=0;i<wm->visible_count;i++) apply_view_clip(wm->slots[i]);
		select_projection(wm,false);
		return;
	}
	// Grow one centered viewport clip in logical coords. With fractional
	// scale the same center maps to the physical buffer center.
	double e=t*t*(3-2*t);
	int w=wm->parent_window.width,h=wm->parent_window.height;
	int bw=1+(int)((w-1)*e),bh=1+(int)((h-1)*e);
	wm->aperture=(struct wlr_box){(w-bw)/2,(h-bh)/2,bw,bh};
	for(int i=0;i<wm->visible_count;i++) apply_view_clip(wm->slots[i]);
}

static bool picker_protocol(struct nested *wm) {
	return wm->map_pointer && wl_resource_get_version(wm->map_pointer)>=11;
}
static bool picker_scope(struct nested *wm) {
	return wm->domain_active && !wm->revealing && !wm->overview_active &&
		(!wm->home_active || wm->default_node_active);
}
static void notify_window_count(struct nested *wm) {
	int count=picker_scope(wm)?wm->visible_count:0;
	if(picker_protocol(wm) && count!=wm->last_window_count) {
		wm->last_window_count=count;
		mothership_map_pointer_v1_send_window_count(wm->map_pointer,count);
	}
}

static bool node_strip_active(struct nested *wm) {
	return wm->node_strip.width>0 && wm->panel && wm->panel->layer->surface->mapped &&
		wm->domain_active && !wm->revealing && !wm->overview_active &&
		(!wm->home_active || wm->default_node_active);
}

static bool preview_on_panel(struct nested *wm,struct xdg_view *view) {
	int i=view->slot;
	return node_strip_active(wm) && i>=0 && i<wm->visible_count && wm->preview[i].id==view->id &&
		wm->preview[i].painted_right>wm->preview[i].painted_left;
}
static void notify_window_overflow(struct nested *wm) {
	if(!wm->map_pointer || wl_resource_get_version(wm->map_pointer)<12) return;
	int hidden=0;
	if(picker_scope(wm))
		for(int i=0;i<wm->visible_count;i++)
			if(wm->slots[i] && !preview_on_panel(wm,wm->slots[i])) hidden++;
	if(hidden!=wm->last_window_overflow) {
		wm->last_window_overflow=hidden;
		mothership_map_pointer_v1_send_window_overflow(wm->map_pointer,hidden);
	}
}
enum { STRIP_PITCH=SHELL_PREVIEW_PITCH, STRIP_LIFT_RESERVE=30, STRIP_HOVER_GROW=7, STRIP_HOVER_SPREAD=9 };

// Hover zoom scales the fully lifted card. At 100% it reaches the lane
// height; larger values rise above the rail from the same baseline.
static double strip_hover_scale(struct nested *wm) {
	return wm->strip_hover_scale>100?wm->strip_hover_scale*.01:1.;
}
// Center spacing added per unit of lift. Grow it by half the extra width
// so the visible gap next to a zoomed card matches the 100% gap.
static double strip_hover_spread(struct nested *wm) {
	double rest=STRIP_PITCH-12,hover=rest+STRIP_HOVER_GROW;
	return STRIP_HOVER_SPREAD+(hover*strip_hover_scale(wm)-hover)*.5;
}

static double strip_limit_for(struct nested *wm,double count) {
	double reserve=STRIP_LIFT_RESERVE*strip_hover_spread(wm)/STRIP_HOVER_SPREAD;
	return fmax(0,count*STRIP_PITCH+(count>1?reserve:0)-wm->node_strip.width);
}

static double stage_ease(double t) {
	return t*t*(3.-2.*t);
}

#define GROUP_PITCH 1.65
static double strip_group_slot(struct nested *wm,int slot,int *part,int *size) {
	*part=0;*size=1;
	double cursor=0;
	bool placed[NODE_WINDOWS]={0};
	for(int i=0;i<wm->visible_count;i++) {
		if(placed[i])continue;
		struct xdg_view *group[FLOW_GROUP_MAX];
		int n=group_members(wm,wm->slots[i],group,false);
		if(n<2) {
			if(i==slot)return cursor;
			cursor+=1;
		} else {
			for(int j=0;j<n;j++) {
				placed[group[j]->slot]=true;
				if(group[j]->slot==slot) {*part=j;*size=n;return cursor;}
			}
			cursor+=GROUP_PITCH;
		}
	}
	return cursor;
}
static struct xdg_view *strip_hit(struct nested *wm,double x,double y) {
	if(!node_strip_active(wm))return NULL;
	bool horizontal_only=isnan(y);
	// Preview tiles paint in slot order, including where they cross in a swap.
	for(int i=wm->visible_count-1;i>=0;i--) {
		struct xdg_view *v=wm->slots[i];
		if(wm->preview[i].id==v->id && x>=wm->preview[i].painted_left && x<wm->preview[i].painted_right &&
			(horizontal_only || (y>=wm->preview[i].painted_top &&
			 y<wm->preview[i].painted_top+wm->preview[i].painted_height)))return v;
	}
	return NULL;
}
static bool strip_has_group(struct nested *wm) {
	if(!group_large(wm))return false;
	for(int i=0;i<wm->visible_count;i++) {
		struct xdg_view *v=wm->slots[i];
		struct xdg_view *members[FLOW_GROUP_MAX];
		if(v->group_id && group_members(wm,v,members,false)>=2)return true;
	}
	return false;
}
static double strip_slot(struct nested *wm,int slot) {
	int part,n;
	return group_large(wm)?strip_group_slot(wm,slot,&part,&n):slot;
}

static double strip_limit(struct nested *wm) {
	if(group_large(wm)) {
		int part,n;
		return strip_limit_for(wm,strip_group_slot(wm,-1,&part,&n));
	}
	return strip_limit_for(wm,wm->visible_count);
}

static double strip_reveal_at(struct nested *wm,double left,double right,double offset) {
	if(left<offset) offset=left;
	else if(right>offset+wm->node_strip.width) offset=right-wm->node_strip.width;
	return fmax(0,fmin(offset,strip_limit(wm)));
}

static struct wlr_box strip_rest_tile(struct nested *wm,struct xdg_view *v,double offset) {
	struct wlr_box lane=wm->node_strip;
	int part=0,count=1;
	double position=group_large(wm)?strip_group_slot(wm,v->slot,&part,&count):v->slot;
	int h=lane.height-SHELL_PREVIEW_REST_INSET;
	if(h<1 || lane.width<24 || v->slot<0)return (struct wlr_box){0};
	int w=count>1?(int)round(GROUP_PITCH*STRIP_PITCH)-10:STRIP_PITCH-12;
	int x=lane.x-(int)round(offset)+(int)round(position*STRIP_PITCH)+(count>1?5:(STRIP_PITCH-w)/2);
	struct rect box={x,lane.y+lane.height-SHELL_PREVIEW_BOTTOM-h,w,h};
	struct rect tiles[FLOW_GROUP_MAX];
	flow_group_tiles(box,count,v->group_sx,v->group_sy,3,16,11,tiles);
	struct rect tile=tiles[part];
	return (struct wlr_box){tile.x,tile.y,tile.width,tile.height};
}

static double strip_goal(struct nested *wm,struct xdg_view *v,double offset) {
	if(!v || v->slot<0 || wm->node_strip.width<=0) return offset;
	int part=0,count=1;
	double slot=group_large(wm)?strip_group_slot(wm,v->slot,&part,&count):v->slot;
	double left=slot*STRIP_PITCH,right=left+(count>=2?GROUP_PITCH:1)*STRIP_PITCH;
	if(count>=2 && right-left>wm->node_strip.width) {
		struct wlr_box tile=strip_rest_tile(wm,v,0);
		if(tile.width>0) {
			left=tile.x-wm->node_strip.x;
			right=left+tile.width;
		}
	}
	return strip_reveal_at(wm,left,right,offset);
}

static void picker_rail_frame(struct nested *wm,int64_t ms) {
	if(!wm->picker_keyboard || !wm->picker_rail_sliding) return;
	if(wm->picker_rail_started<0) wm->picker_rail_started=ms;
	double t=wm->strip_motion?fmin(1,(ms-wm->picker_rail_started)/120.):1.;
	double eased=t*(2.-t);
	wm->strip_offset=wm->picker_rail_from+(wm->picker_rail_to-wm->picker_rail_from)*eased;
	wm->picker_rail_sliding=t<1.;
	if(wm->picker_rail_sliding) wlr_output_schedule_frame(wm->output);
}

static int strip_origin(struct nested *wm) {
	return wm->node_strip.x-(int)round(fmin(wm->strip_offset,strip_limit(wm)));
}

static struct xdg_view *strip_at(struct nested *wm,double x) {
	if(!node_strip_active(wm) || x<wm->node_strip.x ||
		x>=wm->node_strip.x+wm->node_strip.width) return NULL;
	for(int i=0;i<wm->visible_count;i++) {
		struct xdg_view *v=wm->slots[i];
		double left=strip_origin(wm)+strip_slot(wm,i)*STRIP_PITCH;
		if(x>=left && x<left+STRIP_PITCH) return v;
	}
	return NULL;
}

static bool scroll_node_strip(void *data,struct wlr_surface *surface,double x,double y,
		double delta,bool wheel) {
	struct nested *wm=data;
	const struct wlr_box *b=&wm->node_strip;
	if(!node_strip_active(wm) || wm->stage_flight_count || wm->picker_count || wm->picker_keyboard || wm->panel->wants_keyboard || surface!=wm->panel->layer->surface ||
		x<b->x || x>=b->x+b->width || y<b->y || y>=b->y+b->height) return false;
	flow_reorder_finish(wm);
	double next=fmax(0,fmin(strip_limit(wm),wm->strip_offset+delta*(wheel?5.:1.)));
	if(next!=wm->strip_offset) {
		wm->strip_offset=next;
		wm->strip_hover=0;
		wlr_output_schedule_frame(wm->output);
	}
	return true;
}

static void strip_clear(struct nested *wm) {
	flow_reorder_finish(wm);
	wm->strip_hover=0;
	for(int i=0;i<NODE_WINDOWS;i++) {
		wm->preview[i].id=0;wm->preview[i].lift=0;
		if(wm->preview[i].image && wm->preview[i].image->buffer)
			wlr_scene_buffer_set_buffer(wm->preview[i].image,NULL);
	}
	if(wm->node_strip_tree)wlr_scene_node_set_enabled(&wm->node_strip_tree->node,false);
}

static void strip_rect(struct wlr_scene_rect *r,const struct wlr_box *lane,int x,int y,int w,int h) {
	int left=x>lane->x?x:lane->x,right=x+w<lane->x+lane->width?x+w:lane->x+lane->width;
	wlr_scene_node_set_enabled(&r->node,right>left);
	if(right<=left)return;
	wlr_scene_node_set_position(&r->node,left,y);
	wlr_scene_rect_set_size(r,right-left,h);
}

static void strip_focus_marks(struct wlr_scene_rect *marks[9],const struct wlr_box *lane,
		int x,int y,int w,int h,double opacity) {
	const int short_edge=w<h?w:h;
	const int leg=clamp_int(short_edge/3,3,9),underline=clamp_int(w-6,4,18);
	const float ink[4]={.49f*opacity,.83f*opacity,.90f*opacity,opacity};
	const struct wlr_box pieces[]={
		{x-1,y-1,leg,1},{x-1,y,1,leg-1},
		{x+w+1-leg,y-1,leg,1},{x+w,y,1,leg-1},
		{x-1,y+h,leg,1},{x-1,y+h-leg+1,1,leg-1},
		{x+w+1-leg,y+h,leg,1},{x+w,y+h-leg+1,1,leg-1},
		{x+(w-underline)/2,y+h+2,underline,2},
	};
	for(int i=0;i<9;i++) {
		if(opacity<=0) {wlr_scene_node_set_enabled(&marks[i]->node,false);continue;}
		wlr_scene_rect_set_color(marks[i],ink);
		strip_rect(marks[i],lane,pieces[i].x,pieces[i].y,pieces[i].width,pieces[i].height);
	}
}

static bool strip_image(struct wlr_scene_buffer *image,struct xdg_view *view,
		const struct wlr_box *lane,int x,int y,int w,int h) {
	struct wlr_surface *surface=view->toplevel->base->surface;
	int left=x>lane->x?x:lane->x,right=x+w<lane->x+lane->width?x+w:lane->x+lane->width;
	struct wlr_buffer *buffer=surface->buffer?&surface->buffer->base:NULL;
	bool explicit=buffer && surface_has_explicit_sync(surface);
	if(explicit && right>left) {
		buffer=owned_preview_update(&view->preview_copy,view->wm->output,surface,view->preview_generation,
			wl_display_get_event_loop(view->wm->server));
	} else if(!explicit && view->preview_copy.timeline) {
		owned_preview_clear(&view->preview_copy);
	}
	if(!buffer || right<=left) {
		wlr_scene_node_set_enabled(&image->node,false);
		if(image->buffer) wlr_scene_buffer_set_buffer(image,NULL);
		return false;
	}
	struct wlr_fbox crop={(double)(left-x)/w,0,(double)(right-left)/w,1},source;
	enum wl_output_transform transform=explicit?WL_OUTPUT_TRANSFORM_NORMAL:surface->current.transform;
	wlr_fbox_transform(&crop,&crop,wlr_output_transform_invert(transform),1,1);
	if(explicit) {
		source=(struct wlr_fbox){0,0,buffer->width,buffer->height};
		owned_preview_bind(&view->preview_copy,image);
	} else {
		wlr_surface_get_buffer_source_box(surface,&source);
		if(image->buffer!=buffer) wlr_scene_buffer_set_buffer(image,buffer);
	}
	source=(struct wlr_fbox){source.x+crop.x*source.width,source.y+crop.y*source.height,
		crop.width*source.width,crop.height*source.height};
	wlr_scene_buffer_set_source_box(image,&source);
	wlr_scene_buffer_set_transform(image,transform);
	wlr_scene_buffer_set_dest_size(image,right-left,h);
	wlr_scene_node_set_position(&image->node,left,y);
	wlr_scene_node_set_enabled(&image->node,true);
	return true;
}

static int preview_mix(int from,int to,double progress) {
	return from+(int)round((to-from)*progress);
}

static void strip_draw(struct nested *wm,struct timespec now) {
	notify_window_count(wm);
	if(!node_strip_active(wm)) {strip_clear(wm);notify_window_overflow(wm);return;}
	int64_t ms=now.tv_sec*1000LL+now.tv_nsec/1000000;
	picker_rail_frame(wm,ms);
	wm->strip_offset=fmin(wm->strip_offset,strip_limit(wm));
	int count=wm->visible_count,pitch=STRIP_PITCH,start=strip_origin(wm);
	bool compact=strip_has_group(wm);
	int hot=-10;
	if(!wm->stage_flight_count && !wm->group_grab.source)
		for(int i=0;i<count;i++) if(wm->slots[i]->id==wm->strip_hover) {hot=i;break;}
	bool moving=false;
	double spread[NODE_WINDOWS]={0};
	const double zoom=strip_hover_scale(wm),lift_spread=strip_hover_spread(wm);
	for(int i=0;i<count;i++){
		__typeof__(wm->preview[0]) *p=&wm->preview[i];
		uint64_t id=wm->slots[i]->id;
		double focus=wm->picker_keyboard?
			(wm->slots[i]->id==wm->picker_mark_id):(wm->slots[i]==wm->focused);
		if(p->id!=id) {
			p->id=id;
			p->reorder_from=(struct wlr_box){0};
			p->lift=p->lift_from=p->lift_to=0;
			p->lift_start_ms=ms;
			p->focus_ink=p->focus_from=p->focus_to=focus;
			p->focus_start_ms=ms;
		}
		bool hovered=hot==i;
		if(compact && hot>=0 && wm->slots[i]->group_id &&
			wm->slots[hot]->group_id==wm->slots[i]->group_id)hovered=true;
		double goal=hovered?1:abs(hot-i)==1?.42:0;
		if(!wm->strip_motion || wm->stage_flight_count){p->lift=p->lift_from=p->lift_to=goal;}
		else {
			if(p->lift_to!=goal){
				p->lift_from=p->lift;
				p->lift_to=goal;
				p->lift_start_ms=ms;
			}
			double t=fmin(1,(ms-p->lift_start_ms)/165.);
			double eased=t*t*(3-2*t);
			p->lift=p->lift_from+(p->lift_to-p->lift_from)*eased;
			moving|=t<1 && p->lift_from!=p->lift_to;
		}
		if(!wm->strip_motion) p->focus_ink=p->focus_from=p->focus_to=focus;
		else {
			if(p->focus_to!=focus) {
				p->focus_from=p->focus_ink;
				p->focus_to=focus;
				p->focus_start_ms=ms;
			}
			double t=fmin(1,(ms-p->focus_start_ms)/150.);
			double eased=t*t*(3-2*t);
			p->focus_ink=p->focus_from+(p->focus_to-p->focus_from)*eased;
			moving|=t<1 && p->focus_from!=p->focus_to;
		}
		// Increase the center distances faster than the width grows,
		// so the visible gap increases in proportion to both neighbors.
		if(i>0)spread[i]=spread[i-1]+lift_spread*(wm->preview[i-1].lift+p->lift);
	}
	// Cards grow around their slot centers. When the first card zooms past
	// its slot, move the row right by that overflow so the lane's left edge
	// never clips it. At rest, and at 100% hover zoom, this stays 0.
	int lead=0;
	if(count>0) {
		int part=0,members=1;
		if(compact) strip_group_slot(wm,0,&part,&members);
		if(members>=2) {
			struct xdg_view *views[FLOW_GROUP_MAX];
			double lift=0;
			for(int k=0,num=group_members(wm,wm->slots[0],views,false);k<num;k++)
				lift=fmax(lift,wm->preview[views[k]->slot].lift);
			const int rest_gw=(int)round(GROUP_PITCH*pitch)-10;
			lead=(int)round(((rest_gw+6)*zoom-rest_gw)*lift)/2-5;
		} else {
			const int rest_w=pitch-12;
			int w=(int)fmax(1,round(rest_w+((rest_w+STRIP_HOVER_GROW)*zoom-rest_w)*wm->preview[0].lift));
			lead=-((pitch-w)/2);
		}
		if(lead<0) lead=0;
	}
	start+=lead;
	wlr_scene_node_set_enabled(&wm->node_strip_tree->node,true);
	const float outline[4]={.28f,.41f,.48f,1};
	for(int i=0;i<NODE_WINDOWS;i++) {
		__typeof__(wm->preview[0]) *p=&wm->preview[i];
		if(!p->image) continue;
		wlr_scene_node_set_enabled(&wm->group_tray_ring[i]->node,false);
		struct xdg_view *v=i<count?wm->slots[i]:NULL;
		if(!v) {
			p->id=0;p->lift=0;p->painted_left=p->painted_right=0;
			p->painted=(struct wlr_box){0};
			wlr_scene_node_set_enabled(&p->ring->node.parent->node,false);
			if(p->image->buffer) wlr_scene_buffer_set_buffer(p->image,NULL);
			continue;
		}
		struct wlr_surface *s=v->toplevel->base->surface;
		struct wlr_buffer *buffer=s->buffer?&s->buffer->base:NULL;
		// The outside card is stable regardless of app aspect ratio.
		// Fit the live contents inside it; a maximized app must not
		// shorten or lower the rim of its preview.
		const int rest_w=pitch-12,rest_h=wm->node_strip.height-SHELL_PREVIEW_REST_INSET;
		int w=(int)fmax(1,round(rest_w+((rest_w+STRIP_HOVER_GROW)*zoom-rest_w)*p->lift));
		int h=(int)fmax(1,round(rest_h+(wm->node_strip.height*zoom-rest_h)*p->lift));
		double ratio=buffer && s->current.width>0 && s->current.height>0?
			(double)s->current.width/s->current.height:1.60;
		int part=0,members=1;
		double position=compact?strip_group_slot(wm,i,&part,&members):strip_slot(wm,i);
		int x=start+(int)round(position*pitch+(compact?0:spread[i]))+(pitch-w)/2;
		int y=wm->node_strip.y+wm->node_strip.height-SHELL_PREVIEW_BOTTOM-h;
		const struct wlr_box *lane=&wm->node_strip;
		if(members>=2) {
			// Fly the shared outline as one tile: all four member hit boxes
			// use exactly the same lift, split and spacing.
			struct xdg_view *views[FLOW_GROUP_MAX];
			double lift=0;
			for(int k=0,num=group_members(wm,v,views,false);k<num;k++)
				lift=fmax(lift,wm->preview[views[k]->slot].lift);
			const int rest_gw=(int)round(GROUP_PITCH*pitch)-10,rest_gh=wm->node_strip.height-SHELL_PREVIEW_REST_INSET;
			int grow=(int)round(((rest_gw+6)*zoom-rest_gw)*lift);
			h=rest_gh+(int)round(((rest_gh+4)*zoom-rest_gh)*lift);
			y=wm->node_strip.y+wm->node_strip.height-SHELL_PREVIEW_BOTTOM-h;
			struct rect box={start+(int)round(position*pitch)+5-grow/2,y,
				(int)round(GROUP_PITCH*pitch)-10+grow,h};
			if(part==0) {
				int shift=wm->reorder.active?(int)round((wm->strip_offset-wm->reorder.strip_from)*wm->reorder.remaining):0;
				strip_rect(wm->group_tray_ring[i],lane,box.x-2+shift,box.y-2,box.width+4,box.height+4);
			}
			struct rect slots[FLOW_GROUP_MAX];
			flow_group_tiles(box,members,v->group_sx,v->group_sy,3,16,11,slots);
			struct rect tile=slots[part];x=tile.x;y=tile.y;w=tile.width;h=tile.height;
		}
		if(wm->reorder.active && p->reorder_from.width>0) {
			const struct wlr_box from=p->reorder_from;
			double t=1-wm->reorder.remaining;
			x=preview_mix(from.x,x,t);y=preview_mix(from.y,y,t);
			w=preview_mix(from.width,w,t);h=preview_mix(from.height,h,t);
		}
		p->painted=(struct wlr_box){x,y,w,h};
		p->painted_top=y;p->painted_height=h;
		p->painted_left=x>lane->x?x:lane->x;
		p->painted_right=x+w<lane->x+lane->width?x+w:lane->x+lane->width;
		bool visible=x-1<lane->x+lane->width && x+w+1>lane->x;
		wlr_scene_node_set_enabled(&p->ring->node.parent->node,visible);
		if(!visible)continue;
		// A zoomed card may overlap its neighbors; keep it in front.
		if(i==hot && zoom>1) wlr_scene_node_raise_to_top(&p->ring->node.parent->node);
		strip_rect(p->ring,lane,x-1,y-1,w+2,h+2);
		wlr_scene_rect_set_color(p->ring,outline);
		strip_focus_marks(p->focus_marks,lane,x,y,w,h,p->focus_ink);
		strip_rect(p->background,lane,x,y,w,h);
		if(buffer) {
			wlr_scene_node_set_enabled(&p->placeholder->node,false);
			int image_w=(int)fmax(1,fmin(w,h*ratio));
			int image_h=(int)fmax(1,fmin(h,w/ratio));
			if(!strip_image(p->image,v,lane,x+(w-image_w)/2,y+(h-image_h)/2,image_w,image_h))
				strip_rect(p->placeholder,lane,x+w/2-3,y+h/2-2,6,4);
		} else {
			wlr_scene_node_set_enabled(&p->image->node,false);
			if(p->image->buffer)wlr_scene_buffer_set_buffer(p->image,NULL);
			strip_rect(p->placeholder,lane,x+w/2-3,y+h/2-2,6,4);
		}
	}
	notify_window_overflow(wm);
	if(moving) wlr_output_schedule_frame(wm->output);
}

static struct xdg_view *picker_view(struct nested *wm,uint64_t id);
static void picker_draw(struct nested *wm) {
	if(!wm->picker_tree) return;
	bool active=wm->picker_count>0 && picker_scope(wm) && node_strip_active(wm);
	if(!active && !wm->picker_tree->node.enabled) return;
	wlr_scene_node_set_enabled(&wm->picker_tree->node,active);
	for(int i=0;i<NODE_WINDOWS;i++) {
		struct wlr_scene_buffer *image=wm->picker_preview[i];
		if(!image) continue;
		struct xdg_view *v=active && i<wm->picker_count?picker_view(wm,wm->picker_ids[i]):NULL;
		const struct wlr_box *box=&wm->picker_tiles[i];
		struct wlr_surface *surface=v?v->toplevel->base->surface:NULL;
		if(!surface || !surface->buffer ||
			box->width<=0 || box->height<=0 ||
			surface->current.width<=0 || surface->current.height<=0 || preview_on_panel(wm,v)) {
			wlr_scene_node_set_enabled(&image->node,false);
			if(image->buffer) wlr_scene_buffer_set_buffer(image,NULL);
			continue;
		}
		double ratio=(double)surface->current.width/surface->current.height;
		int w=(int)fmax(1,fmin(box->width,box->height*ratio));
		int h=(int)fmax(1,fmin(box->height,box->width/ratio));
		strip_image(image,v,box,
			box->x+(box->width-w)/2,box->y+(box->height-h)/2,w,h);
	}
}

static void picker_focus_clear(struct nested *wm) {
	wm->picker_focus_id=0;
	wm->picker_opened_at=-1;
	if(wm->picker_focus_tree) wlr_scene_node_set_enabled(&wm->picker_focus_tree->node,false);
	preview_projection_clear(&wm->picker_beam);
	for(int i=0;i<FLOW_GROUP_MAX;i++) if(wm->picker_focus_image[i] && wm->picker_focus_image[i]->buffer)
		wlr_scene_buffer_set_buffer(wm->picker_focus_image[i],NULL);
}
static void picker_focus_draw(struct nested *wm,struct timespec now) {
	if(!wm->picker_focus_tree || !wm->picker_keyboard || wm->picker_scale<=100 || !node_strip_active(wm)) {
		if(wm->picker_focus_id) picker_focus_clear(wm);
		return;
	}
	struct xdg_view *v=picker_view(wm,wm->picker_mark_id);
	struct wlr_surface *surface=v?v->toplevel->base->surface:NULL;
	if(!surface || !surface->buffer ||
			surface->current.width<=0 || surface->current.height<=0) {
		picker_focus_clear(wm);return;
	}
	if(!preview_on_panel(wm,v)) {
		// The selected item is moving in from offscreen. Never cast light
		// from an unpainted thumbnail or show the previously selected app.
		wlr_scene_node_set_enabled(&wm->picker_focus_tree->node,false);
		preview_projection_clear(&wm->picker_beam);
		return;
	}
	int i=v->slot;
	struct wlr_box source={wm->preview[i].painted_left,wm->preview[i].painted_top,
		wm->preview[i].painted_right-wm->preview[i].painted_left,wm->preview[i].painted_height};
	const int margin=8,gap=44;
	int low=wm->node_strip.y-gap;
	int max_w=wm->parent_window.width-2*margin,max_h=low-margin;
	if(max_w<36 || max_h<36) {picker_focus_clear(wm);return;}
	const double scale=wm->picker_scale*.01;
	struct xdg_view *members[FLOW_GROUP_MAX]={0};
	int count=group_members(wm,v,members,false);
	if(count<2) {count=1;members[0]=v;}
	for(int j=0;j<count;j++) {
		int k=members[j]->slot;
		if(!preview_on_panel(wm,members[j]))continue;
		int right=source.x+source.width,bottom=source.y+source.height;
		source.x=(int)fmin(source.x,wm->preview[k].painted_left);
		source.y=(int)fmin(source.y,wm->preview[k].painted_top);
		source.width=(int)fmax(right,wm->preview[k].painted_right)-source.x;
		source.height=(int)fmax(bottom,wm->preview[k].painted_top+wm->preview[k].painted_height)-source.y;
	}
	int w=(int)fmin(max_w,scale*(count>1?336:164)),h=(int)fmin(max_h,scale*(count>1?180:100));
	int x=source.x+(source.width-w)/2;
	struct wlr_box goal={(int)fmax(margin,fmin(x,wm->parent_window.width-margin-w)),low-h,w,h};
	int64_t ms=now.tv_sec*1000LL+now.tv_nsec/1000000;
	if(wm->picker_opened_at<0) wm->picker_opened_at=ms;
	if(wm->picker_focus_id!=v->id) {
		wm->picker_focus_from=wm->picker_focus_id?wm->picker_focus_now:goal;
		wm->picker_focus_id=v->id;
		wm->picker_focus_started=ms;
	}
	double pan=wm->strip_motion?fmin(1,(ms-wm->picker_focus_started)/120.):1.;
	double ease=pan*(2.-pan);
	double opened=wm->strip_motion?fmin(1,(ms-wm->picker_opened_at)/150.):1.;
	double opacity=opened*(2.-opened);
	struct wlr_box from=wm->picker_focus_from;
	struct wlr_box box={preview_mix(from.x,goal.x,ease),goal.y,goal.width,goal.height};
	wm->picker_focus_now=box;
	wlr_scene_node_set_enabled(&wm->picker_focus_tree->node,true);
	struct wlr_box lane={0,0,wm->parent_window.width,wm->parent_window.height};
	preview_projection_update(&wm->picker_beam,source,box,opened);
	const float glass[4]={.022*opacity,.052*opacity,.072*opacity,.92*opacity};
	wlr_scene_rect_set_color(wm->picker_focus_face,glass);
	strip_rect(wm->picker_focus_face,&lane,box.x,box.y,box.width,box.height);

	struct rect boxes[FLOW_GROUP_MAX];
	flow_group_tiles((struct rect){box.x+4,box.y+4,box.width-8,box.height-8},count,v->group_sx,v->group_sy,
		clamp_int(v->group_gap*box.width/wm->parent_window.width,0,24),12,12,boxes);
	for(int j=0;j<FLOW_GROUP_MAX;j++) {
		struct wlr_scene_buffer *image=wm->picker_focus_image[j];
		if(j>=count) {
			wlr_scene_node_set_enabled(&image->node,false);
			if(image->buffer)wlr_scene_buffer_set_buffer(image,NULL);
			continue;
		}
		struct wlr_surface *s=members[j]->toplevel->base->surface;
		struct rect part=boxes[j];
		double ratio=s->current.height>0 && s->current.width>0?(double)s->current.width/s->current.height:1.;
		int bw=(int)fmax(1,fmin(part.width,part.height*ratio));
		int bh=(int)fmax(1,fmin(part.height,part.width/ratio));
		strip_image(image,members[j],&lane,part.x+(part.width-bw)/2,part.y+(part.height-bh)/2,bw,bh);
		wlr_scene_buffer_set_opacity(image,.965*opacity);
		if(members[j]==v)
			strip_focus_marks(wm->picker_focus_marks,&lane,part.x,part.y,part.width,part.height,.82*opacity);
	}
	wlr_scene_node_set_enabled(&wm->picker_focus_marks[8]->node,false);
}

static void flow_slide_frame(struct nested *,struct timespec);
static void flow_reorder_frame(struct nested *,struct timespec);
static void mode_slide_frame(struct nested *,struct timespec);
static void mode_slide_finish(struct nested *);
static void maximize_finish(struct nested *);
static void maximize_frame(struct nested *,struct timespec);
static void stage_scale_frame(struct nested *,struct timespec);
static void tray_flights_frame(struct nested *,struct timespec);
static void display_commit(struct nested *,struct wl_resource *,int,int,int,int);
static bool screenshot_frame(struct nested *, const struct wlr_scene_output_state_options *);
static void screenshot_window_frame(struct nested *);
static bool place_dialogs(struct nested *);

static void on_frame(struct wl_listener *listener, void *data) {
	(void)data;
	struct nested *wm = wl_container_of(listener, wm, frame);
	if(wm->hw_session && !wm->hw_session->active) return;
	if(wm->display_request.pending && wm->map_pointer && !wm->output->frame_pending) {
		wm->display_request.pending=false;
		display_commit(wm,wm->map_pointer,wm->display_request.w,wm->display_request.h,
			wm->display_request.hz,wm->display_request.scale);
		// A successful modeset submits a buffer of its own.
		wlr_output_schedule_frame(wm->output);
		return;
	}
	struct timespec now;
	bool timed=clock_gettime(CLOCK_MONOTONIC,&now)==0;
	bool tracing=timed && render_debug_active(wm->render_debug);
	if(tracing && (wm->revealing || wm->flow_sliding || wm->reorder.active || wm->mode_slide_view ||
			wm->stage_flight_count || wm->maximize.view || wm->picker_rail_sliding))
		render_debug_source(wm->render_debug,RENDER_MOTION);
	if(timed) flow_reorder_frame(wm,now);
	if (timed) {reveal_frame(wm,now);flow_slide_frame(wm,now);mode_slide_frame(wm,now);maximize_frame(wm,now);stage_scale_frame(wm,now);strip_draw(wm,now);pin_draw(wm,now);picker_draw(wm);picker_focus_draw(wm,now);group_frame(wm,now);tray_flights_frame(wm,now);}
	enum video_frost_result frost=FROST_OFF;
	if (wm->video_frost_requested && wm->overview_active && wm->map_hud &&
			wm->map_hud->layer->surface->mapped && wm->wallpaper)
		frost=video_frost_update(wm->video_frost,wm->wallpaper->layer->surface,
			wm->parent_window.width,wm->parent_window.height,wm->video_frost_margin);
	else video_frost_hide(wm->video_frost);
	unsigned scene_glass_trace=decorate_frames(wm);
	if(place_dialogs(wm)) nested_seat_refresh_dialog(wm->seat);
	bool shell_drawn=wm->panel && wm->panel->layer->surface->mapped;
	struct wlr_scene_node *below_shell[]={
		&wm->background->node,&wm->wallpaper_tree->node,&wm->app_tree->node,
	};
	for(unsigned i=0;i<SCENE_GLASS_COUNT;i++) {
		unsigned alpha=shell_drawn?wm->scene_glass[i].alpha:0;
		if(i==SCENE_GLASS_GRID && (wm->overview_active ||
				(!wm->default_node_active && (!wm->domain_active || wm->home_active)))) alpha=0;
		scene_glass_trace|=scene_glass_update(wm->scene_glass[i].glass,wm->scene_glass[i].box,alpha,
			wm->drag!=NULL,wl_display_get_event_loop(wm->server),
			below_shell,sizeof below_shell/sizeof below_shell[0]);
	}
	struct timespec submit;
	const struct wlr_scene_output_state_options *options=NULL;
	if(tracing) {
		if(frost==FROST_REBUILT)render_debug_source(wm->render_debug,RENDER_FROST);
		if(scene_glass_trace) render_debug_source(wm->render_debug,scene_glass_trace);
		options=render_debug_scene_begin(wm->render_debug,wm->scene_output);
		clock_gettime(CLOCK_MONOTONIC,&submit);
	}
	bool submitted = wm->screenshot.pending && screenshot_frame(wm, options);
	if (!submitted && !wlr_scene_output_commit(wm->scene_output, options)) {
		stop(wm, "could not render the nested output");
		return;
	}
	if(tracing) {
		render_debug_frame(wm->render_debug,now,submit,frost);
		render_debug_scene_end(wm->render_debug);
	}
	if (wm->reveal_ack) {
		uint64_t ack=wm->reveal_ack;
		wm->reveal_ack=0;
		domain_send(wm,"applied",ack);
	}
	if (timed) wlr_scene_output_send_frame_done(wm->scene_output,&now);
	if (wm->screenshot.window) screenshot_window_frame(wm);
	if (wm->revealing) wlr_output_schedule_frame(wm->output);
}

static bool named_view(struct nested *wm) {
	return wm->domain_active && !wm->home_active && !wm->overview_active;
}

static bool node_view(struct nested *wm) {
	return wm->default_node_active || named_view(wm);
}
static bool is_flow(struct nested *wm) {
	return node_view(wm) && flow_layout(wm->layout);
}

static bool is_stage(struct nested *wm) {
	return node_view(wm) && wm->layout==STAGE;
}
static bool view_has_frame(struct nested *wm) {return is_flow(wm) || is_stage(wm);}
static void set_view_decoration(struct xdg_view *view,bool framed) {
	struct wlr_xdg_toplevel_decoration_v1 *d=view->decoration;
	// Clients commonly create decorations before xdg_shell's first commit.
	// wlroots cannot schedule a configure until that surface is initialized.
	if(!d || !d->toplevel->base->initialized)return;
	enum wlr_xdg_toplevel_decoration_v1_mode mode=framed?
		WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE:WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE;
	if(d->scheduled_mode!=mode)wlr_xdg_toplevel_decoration_v1_set_mode(d,mode);
}

static void view_decoration_requested(struct wl_listener *listener,void *data) {
	(void)data;
	struct xdg_view *view=wl_container_of(listener,view,decoration_request);
	set_view_decoration(view,view->slot>=0 && view_has_frame(view->wm) &&
		view->chrome && view->content_top==WINDOW_CONTENT_TOP);
}

static void detach_view_decoration(struct xdg_view *view) {
	if(!view->decoration)return;
	wl_list_remove(&view->decoration_request.link);
	wl_list_remove(&view->decoration_destroy.link);
	view->decoration=NULL;
}

static void view_decoration_destroyed(struct wl_listener *listener,void *data) {
	(void)data;
	struct xdg_view *view=wl_container_of(listener,view,decoration_destroy);
	detach_view_decoration(view);
}

static void on_new_decoration(struct wl_listener *listener,void *data) {
	struct nested *wm=wl_container_of(listener,wm,new_decoration);
	struct wlr_xdg_toplevel_decoration_v1 *d=data;
	struct xdg_view *view;
	wl_list_for_each(view,&wm->views,link) {
		if(view->toplevel!=d->toplevel)continue;
		view->decoration=d;
		view->decoration_request.notify=view_decoration_requested;
		view->decoration_destroy.notify=view_decoration_destroyed;
		wl_signal_add(&d->events.request_mode,&view->decoration_request);
		wl_signal_add(&d->events.destroy,&view->decoration_destroy);
		view_decoration_requested(&view->decoration_request,NULL);
		return;
	}
	// A toplevel we cannot manage must keep its own window controls.
	if(d->toplevel->base->initialized)
		wlr_xdg_toplevel_decoration_v1_set_mode(d,WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);
}

static bool view_maximized(struct xdg_view *v) {
	return is_flow(v->wm) && v->maximized;
}

static bool view_floats(struct xdg_view *view) {
	return view->wm->layout==FLOAT || (is_flow(view->wm) && view->flowed_out);
}

static bool view_resizes_in_layout(struct xdg_view *view) {
	struct nested *wm=view->wm;
	if ((!is_flow(wm) && !is_stage(wm)) || view_floats(view) || view_maximized(view))
		return false;
	struct xdg_view *members[FLOW_GROUP_MAX];
	return group_members(wm,view,members,true)<2;
}

static int flow_rect_center(bool vertical,int w,int h,struct rect unit,bool grouped) {
	if (vertical) return (grouped?12:(h-unit.height)/2)-unit.y;
	return (w-unit.width)/2-unit.x;
}

static int flow_center(struct nested *wm, struct xdg_view *view) {
	int w=wm->parent_window.width,h=wm->parent_window.height;
	struct rect unit=view->flow_unit;
	bool grouped=unit.width!=view->flow_base.width || unit.height!=view->flow_base.height;
	return flow_rect_center(flow_vertical(wm->layout,w,h),w,h,unit,grouped);
}

// A resize preview uses the same slot and scroll bounds as the committed layout,
// while leaving all live view geometry unchanged.
static struct rect layout_flow(struct nested *wm,struct xdg_view *center,
		struct xdg_view *preview,struct rect proposed) {
	int w=wm->parent_window.width,h=wm->parent_window.height;
	struct rect preview_box={0};
	if (w<1 || h<1) return preview_box;
	bool vertical=flow_vertical(wm->layout,w,h);
	int cursor=0,high=0,low=0;
	bool first=true;
	bool placed[NODE_WINDOWS]={0};
	for (int i=0;i<wm->visible_count;i++) {
		struct xdg_view *v=wm->slots[i];
		if (placed[i] || v->flowed_out || v->minimized) continue;
		struct xdg_view *group[FLOW_GROUP_MAX];
		int n=group_members(wm,v,group,true);
		struct rect unit;
		if(n>=2) {
			const int pad=12;
			int bottom=shell_node_dock_top(h)-SHELL_NODE_GUIDE_HEIGHT-SHELL_NODE_GUIDE_GAP;
			int gh=clamp_int(bottom-pad*2,240,h-pad*2);
			struct rect area={vertical?pad:cursor,vertical?cursor:(h-gh)/2,w-2*pad,gh};
			if(!vertical) area.y=pad; // Keep the bottom flush with work-area limits.
			struct rect tiles[FLOW_GROUP_MAX];
			if(!preview) group_window_tiles(v,area,n,tiles);
			for(int j=0;j<n;j++) {
				if(!preview) {
					group[j]->flow_base=tiles[j];
					group[j]->flow_unit=area;
				}
				placed[group[j]->slot]=true;
			}
			unit=area;
		} else {
			int requested_width=v->flow_width,requested_height=v->flow_height;
			if (!requested_width) {
				struct wlr_box g=v->toplevel->base->geometry;
				requested_width=g.width>0 && g.width<w ? g.width+2*WINDOW_CONTENT_EDGE : 720+2*WINDOW_CONTENT_EDGE;
				requested_height=g.height>0 && g.height<h ? g.height+WINDOW_CONTENT_TOP+WINDOW_CONTENT_BOTTOM : 520+WINDOW_CONTENT_TOP+WINDOW_CONTENT_BOTTOM;
			}
			int vw=clamp_int(v==preview?proposed.width:requested_width,w<180?w:180,w);
			int vh=clamp_int(v==preview?proposed.height:requested_height,h<100?h:100,h);
			unit=(struct rect){vertical?(w-vw)/2:cursor,vertical?cursor:(h-vh)/2,vw,vh};
			if(!preview) {
				v->flow_width=requested_width;
				v->flow_height=requested_height;
				v->flow_base=v->flow_unit=unit;
			} else if(v==preview) preview_box=unit;
		}
		low=flow_rect_center(vertical,w,h,unit,n>=2);
		if(first) high=low;
		first=false;
		cursor+=(vertical?unit.height:unit.width)+FLOW_GAP;
	}
	if(preview) {
		int offset=clamp_int(wm->flow_offset,low,high);
		if(vertical) preview_box.y+=offset;
		else preview_box.x+=offset;
		return preview_box;
	}
	wm->flow_high=high;
	wm->flow_low=low;
	if (center && center->slot>=0 && !center->minimized && !center->flowed_out)
		wm->flow_offset=flow_center(wm,center);
	wm->flow_offset=clamp_int(wm->flow_offset,low,high);
	return preview_box;
}

static void arrange_flow(struct nested *wm, struct xdg_view *center) {
	resize_cancel(wm);
	flow_reorder_finish(wm);
	wm->flow_sliding=false;
	if (is_flow(wm)) layout_flow(wm,center,NULL,(struct rect){0});
}

static void clip_chrome(struct xdg_view *view,struct wlr_scene_rect *scene,struct wlr_box rect,bool shown) {
	if(!scene) return;
	struct wlr_box visible=rect;
	bool paint=shown && (!view->wm->revealing || wlr_box_intersection(&visible,&rect,&view->wm->aperture));
	wlr_scene_node_set_enabled(&scene->node,paint);
	if(!paint) return;
	wlr_scene_node_set_position(&scene->node,visible.x-view->placement.x,visible.y-view->placement.y);
	wlr_scene_rect_set_size(scene,visible.width,visible.height);
}

static void apply_view_clip(struct xdg_view *view) {
	if(!view || !view->tree || (!view->frame->node.enabled && view->wm->maximize.view!=view)) return;
	struct rect r=view->placement;
	int bar=view->content_top,bottom=view->content_bottom,edge=view->content_edge;
	struct wlr_box target={r.x+edge,r.y+bar,r.width-2*edge,r.height-bar-bottom},visible=target;
	bool shown=!view->wm->revealing || wlr_box_intersection(&visible,&target,&view->wm->aperture);
	wlr_scene_node_set_enabled(&view->content->node,shown);
	if(shown) {
		struct wlr_box clip={
			view->toplevel->base->geometry.x + visible.x-target.x,
			view->toplevel->base->geometry.y + visible.y-target.y,
			visible.width,visible.height,
		};
		wlr_scene_subsurface_tree_set_clip(&view->tree->node,&clip);
	}
	bool legacy=!view_has_frame(view->wm) && bar>0;
	clip_chrome(view,view->bar,(struct wlr_box){r.x,r.y,r.width,bar},legacy);
	clip_chrome(view,view->grip,(struct wlr_box){
		r.x+r.width-FLOAT_GRIP,r.y+r.height-FLOAT_GRIP,FLOAT_GRIP,FLOAT_GRIP},legacy);
	if (view->chrome && view_has_frame(view->wm) && bar>0) {
		struct wlr_box frame={r.x,r.y,r.width,r.height},crop;
		bool visible_frame=!view->wm->revealing || wlr_box_intersection(&crop,&frame,&view->wm->aperture);
		window_chrome_show(view->chrome,visible_frame);
		if (visible_frame && view->wm->revealing) {
			crop.x-=r.x;crop.y-=r.y;
			window_chrome_clip(view->chrome,&crop);
		} else if (visible_frame) window_chrome_clip(view->chrome,NULL);
	}
}

// Only the keyboard-focused frame lists the window chords it accepts.
// Every frame keeps the same footer height so focus never resizes an app.
static const char *frame_hints(struct nested *wm,struct xdg_view *view,bool movable,char *out,size_t size) {
	const char *close=nested_seat_close_key_name(wm->seat);
	if(wm->focused!=view || !close) return NULL;
	const char *pin=pin_ready(wm)?(wl_list_empty(&view->pin_link)?"|Win + P Pin":"|Win + P Unpin"):"";
	snprintf(out,size,"Win + %s Close%s%s",close,pin,
		movable?"|Win + Shift + Arrows Move":"");
	return out;
}

static void place_view(struct xdg_view *view) {
	if (view == NULL || view->slot < 0 || view->tree == NULL || view->wm->output == NULL ||
			view->minimized || (!view->frame->node.enabled && view->wm->maximize.view!=view)) {
		return;
	}
	struct nested *wm = view->wm;
	int width = wm->parent_window.width;
	int height = wm->parent_window.height;
	if (width <= 0 || height <= FLOAT_BAR + FLOAT_GRIP) {
		return;
	}
	bool node=node_view(wm),flow=is_flow(wm),stage=is_stage(wm);
	bool floating=node && view_floats(view),maximized=view_maximized(view);
	struct xdg_view *layout_group[FLOW_GROUP_MAX];
	int count=group_members(wm,view,layout_group,stage);
	bool grouped=count>=2;
	struct rect r;
	if (maximized || (node && wm->layout==STACK)) {
		// App frame ends against the fixed rail, never on top of its controls.
		int work=shell_node_dock_top(height);
		r = (struct rect){0, 0, width, work>0?work:height};
	} else if (!node) {
		int middle = width / 2;
		r = view->slot == 0 ? (struct rect){0, 0, middle, height} :
			(struct rect){middle, 0, width - middle, height};
	} else if (stage) {
		r=stage_bounds(wm);
		if(grouped) {
			struct rect tiles[FLOW_GROUP_MAX];
			group_window_tiles(view,r,count,tiles);
			for(int i=0;i<count;i++) if(layout_group[i]==view) {r=tiles[i];break;}
		} else {
			if (view->stage_width && view->stage_width<r.width) r.width=view->stage_width;
			if (view->stage_height && view->stage_height<r.height) r.height=view->stage_height;
		}
	} else if (floating) {
		if (view->floating.width == 0) {
			view->floating = float_start(view->slot, width, height);
		}
		r = view->floating = float_rect(view->floating, width, height);
	} else if (flow) {
		r=view->flow_base;
		if (flow_vertical(wm->layout,width,height)) r.y+=wm->flow_offset;
		else r.x+=wm->flow_offset;
	} else {
		r = tile_rect(view->slot, wm->visible_count, width, height);
	}
	bool framed=(flow||stage) && r.width>2*WINDOW_CONTENT_EDGE && r.height>WINDOW_CONTENT_TOP+WINDOW_CONTENT_BOTTOM;
	int bar=framed?WINDOW_CONTENT_TOP:(!flow && floating)?FLOAT_BAR:0;
	int edge=framed?WINDOW_CONTENT_EDGE:0,bottom=framed?WINDOW_CONTENT_BOTTOM:0;
	if(wm->mode_slide_view==view){r.x+=wm->mode_slide_x;r.y+=wm->mode_slide_y;}
	if(wm->reorder.active) {
		r.x+=(int)round(view->reorder_x*wm->reorder.remaining);
		r.y+=(int)round(view->reorder_y*wm->reorder.remaining);
	}
	view->placement=r;
	view->content_top=bar;
	view->content_bottom=bottom;
	view->content_edge=edge;
	wlr_scene_node_set_position(&view->frame->node,r.x,r.y);
	wlr_scene_node_set_position(&view->content->node,edge,bar);
	if (framed && !view->chrome) view->chrome=window_chrome_create(view->frame);
	if (view->chrome) {
		char hints[96];
		if (framed) window_chrome_update(view->chrome,r.width,r.height,(double)wm->parent_window.scale_120/120,
			view->toplevel->title,wm->focused==view,!wl_list_empty(&view->pin_link),flow && !grouped,floating,maximized,
			stage || grouped,!grouped,!grouped && !maximized,
			frame_hints(wm,view,flow && !floating && !maximized,hints,sizeof(hints)));
		else window_chrome_show(view->chrome,false);
	}
	if (floating && !flow && view->bar == NULL) {
		const float title_color[4] = {0.19f, 0.32f, 0.46f, 1.0f};
		const float grip_color[4] = {0.42f, 0.68f, 0.87f, 1.0f};
		view->bar = wlr_scene_rect_create(view->frame, r.width, FLOAT_BAR, title_color);
		view->grip = wlr_scene_rect_create(view->frame, FLOAT_GRIP, FLOAT_GRIP, grip_color);
	}
	set_view_decoration(view,framed && view->chrome);
	apply_view_clip(view);
	if (view->configured_maximized!=maximized) {
		view->configured_maximized=maximized;
		wlr_xdg_toplevel_set_maximized(view->toplevel,maximized);
	}
	// WM owns the pane bounds. Identify Stage windows as tiled so clients
	// do not apply their floating-window size clamp to the assigned size.
	bool tiled=stage || (flow && !floating && !maximized &&
		(view->flow_unit.width!=view->flow_base.width || view->flow_unit.height!=view->flow_base.height));
	if(view->configured_stage!=tiled) {
		view->configured_stage=tiled;
		wlr_xdg_toplevel_set_tiled(view->toplevel,tiled?
			WLR_EDGE_TOP|WLR_EDGE_BOTTOM|WLR_EDGE_LEFT|WLR_EDGE_RIGHT:0);
	}
	int request_w=node?r.width-2*edge:0, request_h=node?r.height-bar-bottom:0;
	if (request_w != view->configure_w || request_h != view->configure_h) {
		view->configure_w = request_w;
		view->configure_h = request_h;
		wlr_xdg_toplevel_set_size(view->toplevel, request_w, request_h);
	}
}

static struct xdg_view *dialog_owner(struct xdg_view *view) {
	while (view && view->mapped && view->dialog) view=view->dialog_parent;
	return view && view->mapped ? view : NULL;
}

static bool place_dialogs(struct nested *wm) {
	if (wm->cleaning || wl_list_empty(&wm->dialogs)) return false;
	int width=wm->parent_window.width;
	int height=node_view(wm) ? shell_node_dock_top(wm->parent_window.height) : wm->parent_window.height;
	struct wlr_box output={0,0,width,height};
	bool changed=false, any_visible=false;
	struct xdg_view *view;
	wl_list_for_each_reverse(view,&wm->dialogs,dialog_link) {
		struct xdg_view *owner=dialog_owner(view);
		struct wlr_box bounds, visible;
		if (owner) bounds=(struct wlr_box){owner->placement.x,owner->placement.y,
			owner->placement.width,owner->placement.height};
		bool show=!wm->revealing && owner && owner->slot>=0 && owner->frame->node.enabled &&
			wlr_box_intersection(&visible,&bounds,&output);
		changed|=view->frame->node.enabled!=show;
		wlr_scene_node_set_enabled(&view->frame->node,show);
		if (!show) continue;
		any_visible=true;
		struct wlr_box geometry=view->toplevel->base->geometry;
		if (geometry.width>width || geometry.height>height) {
			int request_w=geometry.width>width ? width : geometry.width;
			int request_h=geometry.height>height ? height : geometry.height;
			if (request_w!=view->configure_w || request_h!=view->configure_h) {
				// GTK may keep its natural size if this configure looks
				// like a suggestion for an unconstrained floating window.
				wlr_xdg_toplevel_set_tiled(view->toplevel,
					WLR_EDGE_TOP|WLR_EDGE_BOTTOM|WLR_EDGE_LEFT|WLR_EDGE_RIGHT);
				view->configured_stage=true;
				wlr_xdg_toplevel_set_size(view->toplevel,request_w,request_h);
			}
			view->configure_w=request_w;
			view->configure_h=request_h;
		}
		int x=visible.x+(visible.width-geometry.width)/2;
		int y=visible.y+(visible.height-geometry.height)/2;
		x=fmax(0,fmin(x,width-geometry.width));
		y=fmax(0,fmin(y,height-geometry.height));
		wlr_scene_node_set_position(&view->frame->node,x,y);
	}
	if (any_visible && wm->app_tree->children.prev!=&wm->dialog_tree->node.link)
		wlr_scene_node_raise_to_top(&wm->dialog_tree->node);
	return changed;
}

static struct wlr_surface *dialog_keyboard_target(void *data,struct wlr_surface *parent) {
	struct nested *wm=data;
	struct xdg_view *view;
	wl_list_for_each(view,&wm->dialogs,dialog_link) {
		struct xdg_view *owner=dialog_owner(view);
		if (!wm->revealing && owner && owner->slot>=0 && owner->frame->node.enabled &&
				owner->toplevel->base->surface==parent && view->frame->node.enabled)
			return view->toplevel->base->surface;
	}
	return parent;
}

static struct nested_hit hit_view(void *data, double x, double y) {
	struct nested *wm = data;
	struct nested_hit hit = {0};
	if (wm->cleaning || wm->output == NULL) {
		return hit;
	}
	// Wallpaper, HUD and the Node switcher are paint-only. Never hit-test their trees, even
	// before a client can commit its empty Wayland input region.
	struct wlr_scene_node *node = wlr_scene_node_at(&wm->shell_tree->node,
		x, y, &hit.x, &hit.y);
	if (node == NULL) {
		if (wm->revealing) return hit;
		if (pin_hit(wm,x,y)) {
			// The pin never forwards input to its app and never takes focus.
			hit.handle=&wm->pin;
			hit.pin=hit.defer_move_focus=true;
			return hit;
		}
		struct xdg_view *moving=wm->maximize.view;
		const struct wlr_box box=wm->maximize.painted;
		if(moving && wlr_box_contains_point(&box,x,y)) {
			const struct wlr_box source=wm->maximize.sprite.full;
			double bx=(x-box.x)*source.width/box.width;
			double by=(y-box.y)*source.height/box.height;
			if(wlr_box_contains_point(&wm->maximize.button,bx,by)) {
				hit.decoration=moving;
				hit.toplevel=moving->toplevel->base->surface;
				hit.chrome_action=WINDOW_CHROME_MAXIMIZE;
			}
			return hit;
		}
		node = wlr_scene_node_at(&wm->app_tree->node, x, y, &hit.x, &hit.y);
	}
	if (node == NULL) {
		return hit;
	}
	if(group_large(wm)) for(int i=0;i<NODE_WINDOWS/2;i++) {
		__typeof__(wm->group_frame[0]) *g=&wm->group_frame[i];
		if(!g->leader || !group_chrome_owns_node(g->chrome,node))continue;
		enum group_control action=group_chrome_control(g->chrome,x-g->box.x,y-g->box.y);
		if(action!=GROUP_CONTROL_NONE && !wm->stage_flight_count) {
			hit.decoration=find_view(wm,g->leader);
			hit.chrome_action=action==GROUP_CONTROL_LAYOUT?WINDOW_CHROME_GROUP_LAYOUT:WINDOW_CHROME_GROUP_MINIMIZE;
		}
		return hit;
	}
	if (view_has_frame(wm)) {
		for (int i=0;i<wm->visible_count;i++) {
			struct xdg_view *v=wm->slots[i];
			if (!v || !v->chrome || !window_chrome_owns_node(v->chrome,node)) continue;
			enum window_chrome_hit action=window_chrome_hit_at(v->chrome,x-v->placement.x,y-v->placement.y);
			if (action==WINDOW_CHROME_NONE) return hit;
			hit.toplevel=v->toplevel->base->surface;
			hit.decoration=v;
			hit.chrome_action=action;
			if (action==WINDOW_CHROME_RESIZE && wm->mode_slide_view!=v &&
					view_resizes_in_layout(v)) {
				hit.handle=v;
				hit.frame_resize=true;
			} else if (view_floats(v) && !view_maximized(v) &&
					(action==WINDOW_CHROME_TITLE || action==WINDOW_CHROME_RESIZE)) {
				hit.handle=v;hit.resize=action==WINDOW_CHROME_RESIZE;
			} else if(action==WINDOW_CHROME_TITLE && is_flow(wm) && group_large(wm) && !v->maximized) {
				hit.handle=v;hit.defer_move_focus=true;
			}
			return hit;
		}
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
		if (tree->node.parent==wm->dialog_tree) {
			struct xdg_view *dialog;
			wl_list_for_each(dialog,&wm->dialogs,dialog_link) {
				if (dialog->frame!=tree) continue;
				struct xdg_view *owner=dialog_owner(dialog);
				if (owner && owner->slot>=0 && owner->frame->node.enabled) {
					hit.surface=surface->surface;
					hit.toplevel=owner->toplevel->base->surface;
				}
				return hit;
			}
		}
		if (wm->panel != NULL && wm->panel->scene->tree == tree) {
			hit.surface = surface->surface;
			// Any Node tray card can be dragged: up out of the tray it becomes
			// a tractor beam; on wide outputs it can also join a group.
			if(!wm->panel->wants_keyboard && !wm->picker_count &&
					!wm->picker_keyboard && node_strip_active(wm) &&
					y>=wm->node_strip.y && y<wm->node_strip.y+wm->node_strip.height) {
				struct xdg_view *v=strip_hit(wm,x,y);
				if(v) {hit.handle=v;hit.tray=true;hit.defer_move_focus=true;}
			}
			if (wm->panel->wants_keyboard) {
				hit.panel_keyboard = wm->panel->layer->surface;
			}
			return hit;
		}
		for (int i = 0; i < NODE_WINDOWS; ++i) {
			struct xdg_view *view = wm->slots[i];
			if (view != NULL && view->content == tree) {
				hit.surface = surface->surface;
				hit.toplevel = view->toplevel->base->surface;
				return hit;
			}
		}
	}
	return hit;
}

static void group_drag_update(struct nested *,enum nested_move_phase,double,double);
static void present_flow(struct nested *);

static void resize_cancel(struct nested *wm) {
	if (!wm->resize.view) return;
	wm->resize.view=NULL;
	window_resize_preview_hide(wm->resize.preview);
	if (wm->output) wlr_output_schedule_frame(wm->output);
}

static int resized_dimension(int start,int delta,int room,int minimum,int maximum,int chrome) {
	if (!delta) return start;
	if (room<=chrome+1) return room;
	int min=clamp_int(minimum,1,room-chrome)+chrome;
	int max=maximum>0 ? clamp_int(maximum,min-chrome,room-chrome)+chrome : room;
	return clamp_int(start+delta,min,max);
}

static void resize_update(struct nested *wm,enum nested_move_phase phase,double x,double y) {
	struct xdg_view *view=wm->resize.view;
	if (phase==MOVE_CANCEL || view->slot<0 || !view->frame->node.enabled ||
			!view_resizes_in_layout(view)) {
		resize_cancel(wm);
		return;
	}
	bool stage=is_stage(wm);
	struct rect limit=stage ? stage_bounds(wm) :
		(struct rect){0,0,wm->parent_window.width,wm->parent_window.height};
	struct rect start=wm->resize.start;
	struct rect proposed=start;
	if (!stage) {
		// A centered Flow window must keep its lower resize corner above the rail.
		int rail_safe_height=2*shell_node_dock_top(limit.height)-limit.height;
		if (rail_safe_height>=100)
			limit.height=rail_safe_height>start.height ? rail_safe_height : start.height;
	}
	const struct wlr_xdg_toplevel_state *client=&view->toplevel->current;
	int min_width=client->min_width, min_height=client->min_height;
	if (min_width<180-2*WINDOW_CONTENT_EDGE) min_width=180-2*WINDOW_CONTENT_EDGE;
	if (min_height<100-WINDOW_CONTENT_TOP-WINDOW_CONTENT_BOTTOM)
		min_height=100-WINDOW_CONTENT_TOP-WINDOW_CONTENT_BOTTOM;
	proposed.width=resized_dimension(start.width,(int)round(x-wm->resize.x),limit.width,
		min_width,client->max_width,2*WINDOW_CONTENT_EDGE);
	proposed.height=resized_dimension(start.height,(int)round(y-wm->resize.y),limit.height,
		min_height,client->max_height,WINDOW_CONTENT_TOP+WINDOW_CONTENT_BOTTOM);
	bool width_changed=proposed.width!=start.width;
	bool height_changed=proposed.height!=start.height;
	if (phase==MOVE_UPDATE) {
		if (width_changed || height_changed) {
			if (!stage) proposed=layout_flow(wm,NULL,view,proposed);
			window_resize_preview_place(wm->resize.preview,
				&(struct wlr_box){start.x,start.y,start.width,start.height},
				&(struct wlr_box){proposed.x,proposed.y,proposed.width,proposed.height},
				wm->parent_window.width,wm->parent_window.height);
		} else window_resize_preview_hide(wm->resize.preview);
		wlr_output_schedule_frame(wm->output);
		return;
	}
	resize_cancel(wm);
	if (!width_changed && !height_changed) return;
	if (stage) {
		if (width_changed) view->stage_width=proposed.width;
		if (height_changed) view->stage_height=proposed.height;
		place_view(view);
	} else {
		if (width_changed) view->flow_width=proposed.width;
		if (height_changed) view->flow_height=proposed.height;
		arrange_flow(wm,NULL);
		present_flow(wm);
	}
}

static void start_view_move(void *data, struct nested_hit hit, double x, double y) {
	struct nested *wm = data;
	if(hit.pin) {
		wm->pin.grab=pin_action_at(wm,x,y,&wm->pin.grabbed_id);
		wm->pin.moved=false;
		wm->pin.grab_box=wm->pin.box;
		wm->pin.grab_x=x;wm->pin.grab_y=y;
		return;
	}
	flow_reorder_finish(wm);
	if (hit.frame_resize) {
		struct xdg_view *view=hit.handle;
		wm->flow_sliding=false;
		wm->resize.view=view;
		wm->resize.start=view->placement;
		wm->resize.x=x;
		wm->resize.y=y;
		return;
	}
	wm->drag = hit.handle;
	if(hit.defer_move_focus) {
		struct xdg_view *v=hit.handle;
		wm->group_grab=(__typeof__(wm->group_grab)){.source=v->id,.x=x,.y=y,
			.from_x=x,.from_y=y,.tray=hit.tray};
		return;
	}
	if(wm->drag && wm->mode_slide_view==wm->drag){
		wm->drag->floating=wm->drag->placement;
		wm->mode_slide_view=NULL;
	}
	wm->drag_rect = wm->drag->floating;
	wm->drag_x = (int)x;
	wm->drag_y = (int)y;
	wm->drag_resize = hit.resize;
	if(wm->output) wlr_output_schedule_frame(wm->output);
}

static void update_view_move(void *data, enum nested_move_phase phase, double x, double y) {
	struct nested *wm = data;
	if (wm->resize.view) {resize_update(wm,phase,x,y);return;}
	if(wm->pin.grab!=PIN_NONE) {pin_grab(wm,phase,x,y);return;}
	if(wm->group_grab.source) {group_drag_update(wm,phase,x,y);return;}
	if (phase!=MOVE_UPDATE) {
		if(wm->drag && wm->output) wlr_output_schedule_frame(wm->output);
		wm->drag = NULL;
		return;
	}
	struct xdg_view *view = wm->drag;
	if (view == NULL || !node_view(wm) || !view_floats(view) || view_maximized(view)) {
		return;
	}
	struct rect r = wm->drag_rect;
	int dx = (int)x - wm->drag_x, dy = (int)y - wm->drag_y;
	if (wm->drag_resize) {
		int width = wm->parent_window.width;
		int height = wm->parent_window.height;
		r.width = clamp_int(r.width + dx, 180, width - r.x);
		r.height = clamp_int(r.height + dy, 100, height - r.y);
	} else {
		r.x += dx;
		r.y += dy;
	}
	view->floating=r;
	place_view(view);
}

static void map_pointer_destroy(struct wl_client *client, struct wl_resource *resource) {
	(void)client; wl_resource_destroy(resource);
}

static void display_query(struct wl_client *,struct wl_resource *);
static void display_apply(struct wl_client *,struct wl_resource *,int32_t,int32_t,int32_t,int32_t);

static bool screenshot_allowed(struct nested *wm, struct wl_resource *resource) {
	if (!resource || resource != wm->map_pointer || resource != wm->screenshot.resource ||
			wm->screenshot.panel_pid <= 0) return false;
	pid_t pid;
	uid_t uid;
	gid_t gid;
	wl_client_get_credentials(wl_resource_get_client(resource), &pid, &uid, &gid);
	return pid == wm->screenshot.panel_pid && uid == getuid();
}

static void screenshot_reset(struct nested *wm) {
	wm->screenshot.opening = false;
	wm->screenshot.pending = false;
	wm->screenshot.window = 0;
	wm->screenshot.target_count = 0;
	if (wm->screenshot.active) {
		wm->screenshot.active = false;
		wlr_scene_node_place_below(&wm->shell_tree->node, &wm->node_strip_tree->node);
		nested_seat_screenshot_modal(wm->seat, NULL);
	}
}

static void screenshot_problem(struct nested *wm, const char *message, bool terminal) {
	bool busy = wm->screenshot.opening || wm->screenshot.pending || wm->screenshot.active;
	if (busy && screenshot_allowed(wm, wm->screenshot.resource))
		mothership_map_pointer_v1_send_screenshot_problem(wm->screenshot.resource, message, terminal);
	if (terminal) screenshot_reset(wm);
}

static void screenshot_abort(void *data) {
	struct nested *wm = data;
	screenshot_problem(wm, "The session lost keyboard focus.", true);
}

static bool screenshot_key(void *data) {
	struct nested *wm = data;
	if (!screenshot_allowed(wm, wm->screenshot.resource) || !wm->panel ||
			!wm->panel->layer->surface->mapped || !wm->output || !wm->output->enabled)
		return false;
	if (!wm->screenshot.opening && !wm->screenshot.pending && !wm->screenshot.active) {
		wm->screenshot.opening = true;
		mothership_map_pointer_v1_send_screenshot_open(wm->screenshot.resource);
	}
	return true;
}

static void screenshot_auth_result(struct nested *wm) {
	struct wl_resource *resource = wm->screenshot.resource;
	if (resource)
		mothership_map_pointer_v1_send_screenshot_available(resource, screenshot_allowed(wm, resource));
}

static void screenshot_pid_cleanup(struct nested *wm) {
	if (wm->screenshot.pid_source) {
		wl_event_source_remove(wm->screenshot.pid_source);
		wm->screenshot.pid_source = NULL;
	}
	if (wm->screenshot.pid_fd >= 0) {
		close(wm->screenshot.pid_fd);
		wm->screenshot.pid_fd = -1;
	}
}

static int screenshot_read_pid(int fd, uint32_t mask, void *data) {
	(void)mask;
	struct nested *wm = data;
	char value[32];
	ssize_t n;
	do n = read(fd, value, sizeof(value) - 1); while (n < 0 && errno == EINTR);
	if (n < 0 && errno == EAGAIN) return 0;
	if (n > 0) {
		value[n] = '\0';
		char *end;
		long pid = strtol(value, &end, 10);
		if (pid > 0 && pid <= INT_MAX && end == value + n - 1 && *end == '\n')
			wm->screenshot.panel_pid = pid;
	}
	screenshot_pid_cleanup(wm);
	screenshot_auth_result(wm);
	return 0;
}

static void screenshot_watch_pid(struct nested *wm, struct wl_event_loop *loop) {
	const char *value = getenv("MOTHERSHIP_SCREENSHOT_PANEL_FD");
	if (!value || !*value) return;
	char *end;
	long fd = strtol(value, &end, 10);
	if (*end || fd < 3 || fd > INT_MAX) return;
	wm->screenshot.pid_fd = fd;
	int flags = fcntl(fd, F_GETFL);
	if (flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) >= 0) {
		wm->screenshot.pid_source = wl_event_loop_add_fd(loop, fd,
			WL_EVENT_READABLE, screenshot_read_pid, wm);
	}
	if (!wm->screenshot.pid_source) screenshot_pid_cleanup(wm);
}

static void screenshot_auth(struct wl_client *client, struct wl_resource *resource) {
	(void)client;
	struct nested *wm = wl_resource_get_user_data(resource);
	if (resource != wm->map_pointer) {
		mothership_map_pointer_v1_send_screenshot_available(resource, 0);
		return;
	}
	wm->screenshot.resource = resource;
	if (!wm->screenshot.pid_source) screenshot_auth_result(wm);
}

static void screenshot_begin(struct wl_client *client, struct wl_resource *resource) {
	(void)client;
	struct nested *wm = wl_resource_get_user_data(resource);
	if (!screenshot_allowed(wm, resource) || wm->screenshot.pending || wm->screenshot.active) return;
	nested_seat_cancel_node_switch(wm->seat);
	wm->screenshot.opening = false;
	wm->screenshot.pending = true;
	if (!wm->panel || !wm->panel->layer->surface->mapped || !wm->output || !wm->output->enabled ||
			(wm->hw_session && !wm->hw_session->active)) {
		screenshot_problem(wm, "The display is unavailable.", true);
		return;
	}
	wlr_output_schedule_frame(wm->output);
}

static void screenshot_cancel(struct wl_client *client, struct wl_resource *resource) {
	(void)client;
	struct nested *wm = wl_resource_get_user_data(resource);
	if (screenshot_allowed(wm, resource)) screenshot_reset(wm);
}

static bool screenshot_has_target(struct nested *wm, uint64_t id) {
	if (!id) return false;
	for (size_t i = 0; i < wm->screenshot.target_count; ++i)
		if (wm->screenshot.targets[i] == id) return true;
	return false;
}

static void screenshot_window(struct wl_client *client, struct wl_resource *resource, uint32_t hi, uint32_t lo) {
	(void)client;
	struct nested *wm = wl_resource_get_user_data(resource);
	if (!screenshot_allowed(wm, resource) || !wm->screenshot.active || !wm->output || wm->screenshot.window) return;
	uint64_t id = ((uint64_t)hi << 32) | lo;
	if (!screenshot_has_target(wm, id)) {
		screenshot_problem(wm, "The selected window is unavailable.", false);
		return;
	}
	wm->screenshot.window = id;
	wlr_output_schedule_frame(wm->output);
}

static void screenshot_send_targets(struct nested *wm) {
	struct wlr_box output = {0, 0, wm->parent_window.width, wm->parent_window.height};
	struct wlr_scene_node *node;
	wl_list_for_each(node, &wm->app_tree->children, link) {
		struct xdg_view *view = NULL;
		for (int i = 0; i < NODE_WINDOWS; ++i) {
			if (wm->slots[i] && node == &wm->slots[i]->frame->node) {
				view = wm->slots[i];
				break;
			}
		}
		struct wlr_box box, visible;
		if (!view || !view->mapped || view->minimized || !screenshot_window_bounds(node, &box) ||
				!wlr_box_intersection(&visible, &box, &output)) continue;
		wm->screenshot.targets[wm->screenshot.target_count++] = view->id;
		const char *title = view->toplevel->title;
		if (!title || !*title) title = view->toplevel->app_id;
		mothership_map_pointer_v1_send_screenshot_target(wm->screenshot.resource, view->id >> 32, view->id,
			visible.x, visible.y, visible.width, visible.height, title && *title ? title : "Window");
	}
}

static void screenshot_remove_target(struct nested *wm, uint64_t id) {
	if (wm->cleaning || !wm->screenshot.active || !screenshot_allowed(wm, wm->screenshot.resource)) return;
	for (size_t i = 0; i < wm->screenshot.target_count; ++i) {
		if (wm->screenshot.targets[i] != id) continue;
		wm->screenshot.targets[i] = 0;
		mothership_map_pointer_v1_send_screenshot_target(wm->screenshot.resource, id >> 32, id,
			0, 0, 0, 0, "");
		return;
	}
}

static bool screenshot_frame(struct nested *wm, const struct wlr_scene_output_state_options *options) {
	// Use the same composition, damage and sync path as ordinary frames.
	if (!screenshot_allowed(wm, wm->screenshot.resource) || !wm->panel ||
			!wm->panel->layer->surface->mapped || wm->output->transform != WL_OUTPUT_TRANSFORM_NORMAL ||
			!screenshot_buffer_ready(&wm->scene->tree.node)) {
		screenshot_problem(wm, "The display frame is unavailable. Try again.", true);
		return false;
	}
	struct wlr_output *output = wm->output;
	struct wlr_output_state state;
	wlr_output_state_init(&state);
	wlr_output_lock_attach_render(output, true);
	wlr_damage_ring_add_whole(&wm->scene_output->damage_ring);
	bool cursor_enabled = wm->cursor && wm->cursor->enabled;
	// A hardware cursor is separate; suppress the software cursor from the
	// sampled composition and repaint the cursor normally on the next frame.
	if (wm->cursor) wm->cursor->enabled = false;
	bool built = wlr_scene_output_build_state(wm->scene_output, &state, options);
	if (wm->cursor) wm->cursor->enabled = cursor_enabled;
	wlr_output_lock_attach_render(output, false);
	bool committed = built && state.buffer && wlr_output_commit_state(output, &state);
	struct screenshot_pixels pixels;
	bool captured = committed && screenshot_read_buffer(output->renderer, state.buffer, &pixels);
	wlr_output_state_finish(&state);
	if (cursor_enabled || !committed) {
		wlr_damage_ring_add_whole(&wm->scene_output->damage_ring);
		wlr_output_schedule_frame(output);
	}
	if (!captured) {
		screenshot_problem(wm, "The display frame could not be captured.", true);
		return committed;
	}

	wm->screenshot.pending = false;
	wm->screenshot.active = true;
	screenshot_send_targets(wm);
	mothership_map_pointer_v1_send_screenshot_frame(wm->screenshot.resource, pixels.fd,
		pixels.width, pixels.height, pixels.stride, wm->parent_window.width, wm->parent_window.height);
	close(pixels.fd);
	wlr_scene_node_raise_to_top(&wm->shell_tree->node);
	nested_seat_screenshot_modal(wm->seat, wm->panel->layer->surface);
	return true;
}

static void screenshot_window_frame(struct nested *wm) {
	uint64_t id = wm->screenshot.window;
	wm->screenshot.window = 0;
	if (!screenshot_allowed(wm, wm->screenshot.resource) || !wm->screenshot.active) return;
	struct xdg_view *view = find_view(wm, id);
	struct wlr_box box;
	if (!screenshot_has_target(wm, id) || !view || !view->mapped || view->minimized || !view->frame ||
			!screenshot_window_bounds(&view->frame->node, &box)) {
		screenshot_remove_target(wm, id);
		screenshot_problem(wm, "The selected window is unavailable.", false);
		return;
	}
	struct screenshot_pixels pixels;
	if (!screenshot_render_window(wm->output, &view->frame->node, &box,
			wl_display_get_event_loop(wm->server), &pixels)) {
		screenshot_problem(wm, "The selected window could not be captured. Try again.", false);
		return;
	}
	mothership_map_pointer_v1_send_screenshot_image(wm->screenshot.resource, pixels.fd,
		pixels.width, pixels.height, pixels.stride);
	close(pixels.fd);
}

static void map_pointer_watch(struct wl_client *client, struct wl_resource *resource, struct wl_resource *surface) {
	(void)client;
	struct nested *wm=wl_resource_get_user_data(resource);
	if (!wm->map_pointer && wm->panel && wm->panel->layer->surface->resource==surface) {
		wm->map_pointer=resource;
		pin_replay(wm);
		if(wm->focused) place_view(wm->focused);
		wm->last_window_count=wm->last_window_overflow=-1;
		notify_window_count(wm);
		if(wl_resource_get_version(resource)>=MOTHERSHIP_MAP_POINTER_V1_DISPLAY_AVAILABLE_SINCE_VERSION)
			mothership_map_pointer_v1_send_display_available(resource,wm->native_output && wm->output);
		if(wl_resource_get_version(resource)>=MOTHERSHIP_MAP_POINTER_V1_SCENE_GLASS_AVAILABLE_SINCE_VERSION) {
			unsigned slots=0;
			for(unsigned i=0;i<SCENE_GLASS_COUNT;i++)
				if(wm->scene_glass[i].glass) slots|=1u<<i;
			mothership_map_pointer_v1_send_scene_glass_available(resource,slots);
		}
	}
}

static void map_pointer_set_zoom(struct wl_client *client,struct wl_resource *resource,uint32_t enabled,uint32_t margin) {
	(void)client;
	struct nested *wm=wl_resource_get_user_data(resource);
	if (wm->map_pointer!=resource || margin>96) return;
	if (!enabled) nested_seat_cancel_map_pointer(wm->seat);
	wm->map_zoom_enabled=enabled!=0;
	nested_seat_map_home(wm->seat,wm->map_zoom_enabled);
	wm->map_zoom_margin=(int)margin;
}

static bool map_target(struct nested *wm,struct wlr_surface *panel) {
	return wm->map_zoom_enabled && wm->overview_active && wm->map_pointer &&
		wm->panel && panel==wm->panel->layer->surface && wm->map_hud &&
		wm->map_hud->layer->surface->mapped;
}

static bool map_scroll(void *data,struct wlr_surface *panel,double x,double y,double delta) {
	struct nested *wm=data;
	if (!map_target(wm,panel) ||
			!map_frame_contains(wm->parent_window.width,wm->parent_window.height,wm->map_zoom_margin,x,y) ||
			hit_view(wm,x,y).panel_keyboard==panel) return false;
	if (delta) mothership_map_pointer_v1_send_zoom(wm->map_pointer,
		wl_fixed_from_double(x),wl_fixed_from_double(y),wl_fixed_from_double(delta));
	return true;
}

static bool map_contact(void *data,struct wlr_surface *panel,double x,double y,uint32_t phase) {
	struct nested *wm=data;
	if (phase!=MOTHERSHIP_MAP_POINTER_V1_CONTACT_PHASE_CANCEL && !map_target(wm,panel)) return false;
	if (!wm->map_pointer) return false;
	uint32_t version=wl_resource_get_version(wm->map_pointer);
	if (version>=MOTHERSHIP_MAP_POINTER_V1_CONTACT_SINCE_VERSION) {
		if (phase!=NESTED_MAP_LEGACY_CLICK)
			mothership_map_pointer_v1_send_contact(wm->map_pointer,phase,wl_fixed_from_double(x),wl_fixed_from_double(y));
	} else if (version>=MOTHERSHIP_MAP_POINTER_V1_CLICK_SINCE_VERSION) {
		if (phase==NESTED_MAP_LEGACY_CLICK)
			mothership_map_pointer_v1_send_click(wm->map_pointer,wl_fixed_from_double(x),wl_fixed_from_double(y));
	} else return false;
	return true;
}

static void map_aim(struct nested *wm,bool inside,double x,double y) {
	if(!wm->map_pointer || !wm->map_aim_enabled) return;
	struct wlr_surface *panel=inside?nested_seat_map_position(wm->seat,&x,&y):NULL;
	bool valid=inside && map_target(wm,panel) &&
		map_arcade_contains(wm->parent_window.width,wm->parent_window.height,wm->map_zoom_margin,x,y) &&
		!hit_view(wm,x,y).surface;
	if(valid==wm->map_aim_valid && (!valid || (wm->map_aim_x==x && wm->map_aim_y==y))) return;
	wm->map_aim_valid=valid;wm->map_aim_x=x;wm->map_aim_y=y;
	mothership_map_pointer_v1_send_aim(wm->map_pointer,valid,wl_fixed_from_double(x),wl_fixed_from_double(y));
}

static bool map_weapon(void *data,uint32_t weapon,bool pressed) {
	struct nested *wm=data;
	if(!wm->map_pointer || !wm->map_aim_enabled ||
	   wl_resource_get_version(wm->map_pointer)<MOTHERSHIP_MAP_POINTER_V1_WEAPON_BUTTON_SINCE_VERSION)
		return false;
	if(pressed) {
		// Recheck real bounds and owner on the button, not a stale move
		// sample (for example immediately after a resize).
		map_aim(wm,true,0,0);
		if(!wm->map_aim_valid)return false;
	}
	mothership_map_pointer_v1_send_weapon_button(wm->map_pointer,weapon,pressed);
	return true;
}

static void map_pointer_set_aim(struct wl_client *client,struct wl_resource *resource,uint32_t enabled) {
	(void)client;
	struct nested *wm=wl_resource_get_user_data(resource);
	if(wm->map_pointer!=resource) return;
	wm->map_aim_enabled=enabled!=0;
	wm->map_aim_valid=false;
	if(enabled) map_aim(wm,true,0,0);
}

static void map_pointer_set_video_frost(struct wl_client *client,struct wl_resource *resource,uint32_t enabled,uint32_t margin) {
	(void)client;
	struct nested *wm=wl_resource_get_user_data(resource);
	if (wm->map_pointer!=resource || margin>96) return;
	if (wm->video_frost_requested==(enabled!=0) && wm->video_frost_margin==(int)margin) return;
	wm->video_frost_requested=enabled!=0;wm->video_frost_margin=(int)margin;
	if (wm->output) wlr_output_schedule_frame(wm->output);
}

static void strip_focus_view(struct nested *wm,struct xdg_view *v);
static void map_pointer_set_node_strip(struct wl_client *client,struct wl_resource *resource,
		int32_t x,int32_t y,int32_t w,int32_t h,uint32_t motion) {
	(void)client;
	struct nested *wm=wl_resource_get_user_data(resource);
	if(wm->map_pointer!=resource) return;
	wm->strip_motion=motion!=0;
	if(w<=0 || h<18 || h>62 || x<0 || y<0 ||
			w>wm->parent_window.width-x || h>wm->parent_window.height-y) {
		wm->node_strip=(struct wlr_box){0};
		strip_clear(wm);
	} else {
		wm->node_strip=(struct wlr_box){x,y,w,h};
	}
	if(!wm->strip_motion) {
		maximize_finish(wm);
		flow_reorder_finish(wm);
	}
	if(wm->output)wlr_output_schedule_frame(wm->output);
}
static void map_pointer_strip_hover(struct wl_client *client,struct wl_resource *resource,wl_fixed_t x) {
	(void)client;
	struct nested *wm=wl_resource_get_user_data(resource);
	if(wm->map_pointer!=resource || wm->picker_count || wm->picker_keyboard) return;
	double px=wl_fixed_to_double(x);
	struct xdg_view *v=strip_has_group(wm) || wm->reorder.active?
		strip_hit(wm,px,wm->strip_cursor_y):strip_at(wm,px);
	uint64_t id=v?v->id:0;
	if(id==wm->strip_hover) return;
	wm->strip_hover=id;
	if(wm->output)wlr_output_schedule_frame(wm->output);
}
static void map_pointer_strip_focus(struct wl_client *client,struct wl_resource *resource,wl_fixed_t x) {
	(void)client;
	struct nested *wm=wl_resource_get_user_data(resource);
	if(wm->map_pointer==resource) {
		if(wm->picker_keyboard || !node_strip_active(wm)) return;
		// The panel reports the saved click X. A newer pointer Y can belong
		// to a different surface by the time this request arrives.
		struct xdg_view *v=strip_hit(wm,wl_fixed_to_double(x),NAN);
		// Clicking an already selected thumbnail preserves a manual Flow pan.
		if(v && v!=wm->focused) strip_focus_view(wm,v);
	}
}

static void map_pointer_window_picker(struct wl_client *,struct wl_resource *);
static void map_pointer_window_pick(struct wl_client *,struct wl_resource *,uint32_t,uint32_t,uint32_t);

static void map_pointer_window_preview(struct wl_client *client,struct wl_resource *resource,
		uint32_t serial,uint32_t hi,uint32_t lo,int32_t x,int32_t y,int32_t w,int32_t h) {
	(void)client;
	struct nested *wm=wl_resource_get_user_data(resource);
	if(resource!=wm->map_pointer || serial!=wm->picker_serial || !picker_scope(wm) ||
		x<0 || y<0 || w<1 || h<1 || w>wm->parent_window.width-x || h>wm->parent_window.height-y)
		return;
	uint64_t id=((uint64_t)hi<<32)|lo;
	for(int i=0;i<wm->picker_count;i++) if(wm->picker_ids[i]==id) {
		struct wlr_box box={x,y,w,h};
		struct wlr_box old=wm->picker_tiles[i];
		if(old.x!=box.x || old.y!=box.y || old.width!=box.width || old.height!=box.height) {
			wm->picker_tiles[i]=box;
			wlr_output_schedule_frame(wm->output);
		}
		return;
	}
}

static void map_pointer_set_picker_scale(struct wl_client *client,struct wl_resource *resource,uint32_t percent) {
	(void)client;
	struct nested *wm=wl_resource_get_user_data(resource);
	if(resource!=wm->map_pointer || percent<100 || percent>300 || wm->picker_scale==(int)percent) return;
	wm->picker_scale=percent;
	if(wm->picker_keyboard) wlr_output_schedule_frame(wm->output);
}

static void map_pointer_set_strip_hover_scale(struct wl_client *client,struct wl_resource *resource,uint32_t percent) {
	(void)client;
	struct nested *wm=wl_resource_get_user_data(resource);
	if(resource!=wm->map_pointer || percent<100 || percent>300 || wm->strip_hover_scale==(int)percent) return;
	wm->strip_hover_scale=percent;
	if(wm->output) wlr_output_schedule_frame(wm->output);
}

static void map_pointer_highlight_redraw(struct wl_client *client,struct wl_resource *resource,uint32_t enabled) {
	// Retired request: keep the wire opcode compatible with older panels.
	(void)client;(void)resource;(void)enabled;
}

static bool reload_cursor(struct nested *wm,int size);

static void map_pointer_cursor_size(struct wl_client *client,struct wl_resource *resource,uint32_t size) {
	(void)client;
	struct nested *wm=wl_resource_get_user_data(resource);
	if(wm->map_pointer==resource && size>=16 && size<=64 && !reload_cursor(wm,(int)size))
		fputs("mothership-wm: could not load the selected cursor size\n",stderr);
}

static void map_pointer_scene_glass(struct wl_client *client,struct wl_resource *resource,
		uint32_t slot,int32_t x,int32_t y,int32_t w,int32_t h,uint32_t opacity) {
	(void)client;
	struct nested *wm=wl_resource_get_user_data(resource);
	if(wm->map_pointer!=resource || slot>=SCENE_GLASS_COUNT ||
			!wm->scene_glass[slot].glass || !wm->output) return;
	if(opacity>1000 || x<0 || y<0 || w<24 || h<24 ||
			(int64_t)x+w>wm->parent_window.width || (int64_t)y+h>wm->parent_window.height)
		opacity=0;
	struct wlr_box box=opacity?(struct wlr_box){x,y,w,h}:(struct wlr_box){0};
	if(wm->scene_glass[slot].alpha==opacity && wlr_box_equal(&wm->scene_glass[slot].box,&box)) return;
	wm->scene_glass[slot].box=box;
	wm->scene_glass[slot].alpha=opacity;
	wlr_output_schedule_frame(wm->output);
}

static void map_pointer_group_query(struct wl_client *,struct wl_resource *,wl_fixed_t,wl_fixed_t);
static void map_pointer_window_list(struct wl_client *,struct wl_resource *,uint32_t);
static void map_pointer_group_action(struct wl_client *,struct wl_resource *,
	uint32_t,uint32_t,uint32_t,int32_t,int32_t,int32_t);
static const struct mothership_map_pointer_v1_interface map_pointer_requests={
	.destroy=map_pointer_destroy,
	.display_query=display_query,.display_apply=display_apply,
	.watch=map_pointer_watch,
	.set_zoom=map_pointer_set_zoom,
	.set_aim=map_pointer_set_aim,
	.set_video_frost=map_pointer_set_video_frost,
	.set_node_strip=map_pointer_set_node_strip,
	.node_strip_hover=map_pointer_strip_hover,
	.node_strip_focus=map_pointer_strip_focus,
	.window_picker=map_pointer_window_picker,.window_pick=map_pointer_window_pick,
	.window_preview=map_pointer_window_preview,
	.set_picker_scale=map_pointer_set_picker_scale,
	.highlight_redraw=map_pointer_highlight_redraw,
	.set_cursor_size=map_pointer_cursor_size,
	.set_scene_glass=map_pointer_scene_glass,
	.group_query=map_pointer_group_query,.group_action=map_pointer_group_action,
	.screenshot_auth=screenshot_auth,.screenshot_begin=screenshot_begin,
	.screenshot_cancel=screenshot_cancel,.screenshot_window=screenshot_window,
	.set_strip_hover_scale=map_pointer_set_strip_hover_scale,
	.window_list=map_pointer_window_list,
};

static void map_pointer_gone(struct wl_resource *resource) {
	struct nested *wm=wl_resource_get_user_data(resource);
	if (wm->screenshot.resource == resource) {
		wm->screenshot.resource = NULL;
		screenshot_reset(wm);
	}
	if(wm->map_pointer==resource) {
		wm->map_pointer=NULL;
		if(wm->output && !wl_list_empty(&wm->pin.windows)) wlr_output_schedule_frame(wm->output);
		nested_seat_cancel_node_switch(wm->seat);
		bool glass_was_active=false;
		for(unsigned i=0;i<SCENE_GLASS_COUNT;i++) {
			glass_was_active|=wm->scene_glass[i].alpha>0;
			wm->scene_glass[i].alpha=0;
		}
		if(glass_was_active && wm->output) wlr_output_schedule_frame(wm->output);
		wm->display_request.pending=false;
		wm->picker_count=wm->cycle_count=0;
		wm->picker_keyboard=false;
		picker_focus_clear(wm);
		picker_draw(wm);
		wm->video_frost_requested=false;
		wm->node_strip=(struct wlr_box){0};
		strip_clear(wm);
		video_frost_hide(wm->video_frost);
		wm->map_aim_enabled=wm->map_aim_valid=false;
		wm->map_zoom_enabled=false;
		nested_seat_map_home(wm->seat,false);
		nested_seat_cancel_map_pointer(wm->seat);
	}
}

static void map_pointer_bind(struct wl_client *client,void *data,uint32_t version,uint32_t id) {
	struct wl_resource *resource=wl_resource_create(client,&mothership_map_pointer_v1_interface,version<(uint32_t)mothership_map_pointer_v1_interface.version?version:mothership_map_pointer_v1_interface.version,id);
	if(!resource) {wl_client_post_no_memory(client);return;}
	wl_resource_set_implementation(resource,&map_pointer_requests,data,map_pointer_gone);
}

static void move_cursor(void *data, bool inside, double x, double y) {
	struct nested *wm = data;
	wm->strip_cursor_y=inside?y:-1;
	wm->pin.pointer_inside=inside;
	wm->pin.pointer_x=x;
	wm->pin.pointer_y=y;
	if(!wm->cleaning && wm->output &&
			pin_hover_target(wm)!=(wm->pin.hovered?wm->pin.hover_id:0))
		wlr_output_schedule_frame(wm->output);
	if(!wm->cleaning) map_aim(wm,inside,x,y);
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

extern int wmLoadCursorSize(void);

static bool reload_cursor(struct nested *wm,int size) {
	if(!wm->output || wm->cleaning)return false;
	int pixels=(int)lround(size*wm->output->scale);
	if(wm->cursor_image && wm->cursor_pixels==pixels) {
		wm->cursor_size=size;
		return true;
	}
	struct wlr_xcursor_theme *theme=wlr_xcursor_theme_load(NULL,pixels);
	if(!theme)return false;
	struct wlr_xcursor *arrow=wlr_xcursor_theme_get_cursor(theme,"left_ptr");
	if(!arrow || !arrow->image_count) {
		wlr_xcursor_theme_destroy(theme);
		return false;
	}
	if(!wm->cursor)wm->cursor=wlr_output_cursor_create(wm->output);
	struct wlr_xcursor_image *image=arrow->images[0];
	bool applied=wm->cursor!=NULL;
	if(applied && wm->cursor_visible) {
		struct wlr_buffer *buffer=wlr_xcursor_image_get_buffer(image);
		applied=buffer && wlr_output_cursor_set_buffer(wm->cursor,buffer,image->hotspot_x,image->hotspot_y);
		if(!applied && wm->cursor_image) {
			struct wlr_xcursor_image *old=wm->cursor_image;
			wlr_output_cursor_set_buffer(wm->cursor,wlr_xcursor_image_get_buffer(old),old->hotspot_x,old->hotspot_y);
		}
	}
	if(!applied){wlr_xcursor_theme_destroy(theme);return false;}
	if(wm->cursor_theme)wlr_xcursor_theme_destroy(wm->cursor_theme);
	wm->cursor_theme=theme;
	wm->cursor_image=image;
	wm->cursor_size=size;
	wm->cursor_pixels=pixels;
	return true;
}

static bool output_geometry(struct nested *wm, struct wlr_output_state *state) {
	struct nested_parent *p = &wm->parent_window;
	int width, height;
	if (!nested_parent_dimensions(p, &width, &height)) return false;
	wlr_output_state_set_custom_mode(state, width, height, 0);
	wlr_output_state_set_scale(state, (double)p->scale_120 / 120);
	nested_parent_viewport(p);
	wlr_scene_rect_set_size(wm->background, p->width, p->height);
	return true;
}

static void present_stage(struct nested *wm);

static void parent_geometry_changed(void *data) {
	struct nested *wm = data;
	if (wm->output == NULL || !wm->output->enabled || wm->stopping) return;
	resize_cancel(wm);
	screenshot_problem(wm, "The display size or scale changed.", true);
	bool resized = wm->background->width != wm->parent_window.width ||
		wm->background->height != wm->parent_window.height;
	struct wlr_output_state state;
	wlr_output_state_init(&state);
	bool applied = output_geometry(wm, &state) && wlr_output_commit_state(wm->output, &state);
	wlr_output_state_finish(&state);
	if (!applied) {
		stop(wm, "parent size or scale could not be applied to the nested output");
		return;
	}
	maximize_finish(wm);
	if(wm->cursor_size)reload_cursor(wm,wm->cursor_size);
	if (resized) {mode_slide_finish(wm);arrange_flow(wm,wm->focused);}
	// A scale-only change refreshes cached chrome without resizing clients.
	if (is_stage(wm)) present_stage(wm);
	else for (int i=0;i<NODE_WINDOWS;i++) place_view(wm->slots[i]);
	if (resized) {
		position_panel(wm->panel);
		position_panel(wm->map_hud);
		position_panel(wm->wallpaper);
		position_panel(wm->node_telemetry);
		position_panel(wm->node_switcher);
		if (wm->seat != NULL) nested_seat_refresh(wm->seat);
	}
	wlr_output_schedule_frame(wm->output);
}

static void parent_closed(void *data) {
	struct nested *wm = data;
	stop(wm, wm->ready ? NULL : "parent closed the output during startup");
}

static void domain_disconnect(struct nested *wm) {
	if (wm->domain_source != NULL) {
		wl_event_source_remove(wm->domain_source);
		wm->domain_source = NULL;
	}
	if (wm->domain_fd >= 0) {
		shutdown(wm->domain_fd, SHUT_RDWR);
		wm->domain_fd = -1;
		nested_seat_cancel_node_switch(wm->seat);
	}
}

static bool domain_write(struct nested *wm, const char *packet, size_t n) {
	if (wm->domain_fd < 0) return false;
	ssize_t sent;
	do {
		sent = send(wm->domain_fd, packet, n, MSG_NOSIGNAL | MSG_DONTWAIT);
	} while (sent < 0 && errno == EINTR);
	if (sent < 0 || (size_t)sent != n) {
		domain_disconnect(wm);
		return false;
	}
	return true;
}

static bool domain_send(struct nested *wm, const char *event, uint64_t id) {
	char packet[64];
	int n = id == 0 ? snprintf(packet, sizeof(packet), "%s", event) :
		snprintf(packet, sizeof(packet), "%s %" PRIu64, event, id);
	return domain_write(wm, packet, n);
}

static bool valid_app_id(const char *s) {
	if (!s || !*s || strnlen(s, 129) > 128) return false;
	for (; *s; ++s) {
		unsigned char b = *s;
		if (!((b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z') ||
				(b >= '0' && b <= '9') || b == '_' || b == '-' || b == '.')) return false;
	}
	return true;
}

static void publish_app_id(struct xdg_view *view) {
	const char *value = view->toplevel->app_id;
	char packet[160];
	if (!valid_app_id(value)) value = NULL;
	int n = value ? snprintf(packet, sizeof(packet), "app %" PRIu64 " %s", view->id, value) :
		snprintf(packet,sizeof(packet), "app %" PRIu64, view->id);
	domain_write(view->wm, packet, n);
}

static void on_view_app_id(struct wl_listener *listener, void *data) {
	(void)data;
	struct xdg_view *view = wl_container_of(listener, view, app_id);
	if (!view->wm->cleaning) {
		if (view->catalogued) publish_app_id(view);
		pin_metadata_changed(view);
	}
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

static void stage_send_frame(struct wlr_surface *surface,int sx,int sy,void *data) {
	(void)sx;(void)sy;
	wlr_surface_send_frame_done(surface,data);
}

static void stage_prime(struct xdg_view *view) {
	// A hidden client may still be waiting on a frame callback from before
	// the switch. Let the incoming client paint while the old one docks.
	struct timespec now;
	if(clock_gettime(CLOCK_MONOTONIC,&now)==0)
		wlr_surface_for_each_surface(view->toplevel->base->surface,stage_send_frame,&now);
}

// --- Pinned windows ---------------------------------------------------
// Membership is session-local. Only the visible projection has buffers,
// hit targets or labels, and the WM keeps all client pixels and input here.
static bool pin_ready(const struct nested *wm) {
	return wm->map_pointer && wl_resource_get_version(wm->map_pointer)>=
		MOTHERSHIP_MAP_POINTER_V1_PIN_STACK_LABEL_SINCE_VERSION;
}

static bool pin_scene_available(const struct nested *wm) {
	// Blank Home can keep the Overview projection after its HUD unmaps.
	bool map_visible=wm->map_hud && wm->map_hud->layer->surface->mapped;
	return pin_ready(wm) && !map_visible && !wm->revealing &&
		!wm->screenshot.active && !wm->screenshot.opening;
}

static bool same_box(struct wlr_box a,struct wlr_box b) {
	return a.x==b.x && a.y==b.y && a.width==b.width && a.height==b.height;
}

static void pin_send_label(struct xdg_view *view,struct wlr_box box) {
	struct nested *wm=view->wm;
	if(!pin_ready(wm) || (same_box(view->pin_label_sent,box) &&
			(!box.width || !view->pin_label_dirty))) return;
	view->pin_label_sent=box;
	view->pin_label_dirty=false;
	mothership_map_pointer_v1_send_pin_stack_label(wm->map_pointer,view->id>>32,(uint32_t)view->id,
		box.x,box.y,box.width,box.height,
		box.width && view->toplevel->title?view->toplevel->title:"",
		box.width && view->toplevel->app_id?view->toplevel->app_id:"");
}

static void pin_send_control(struct nested *wm,int remaining) {
	struct wlr_box box=wm->pin.control_box;
	bool expanded=box.width && wm->pin.expanded;
	if(!pin_ready(wm) || (same_box(wm->pin.sent_control,box) &&
			wm->pin.sent_remaining==remaining && wm->pin.sent_expanded==expanded)) return;
	wm->pin.sent_control=box;
	wm->pin.sent_remaining=remaining;
	wm->pin.sent_expanded=expanded;
	mothership_map_pointer_v1_send_pin_stack_control(wm->map_pointer,box.x,box.y,box.width,box.height,
		remaining,expanded);
}

static void pin_replay(struct nested *wm) {
	struct xdg_view *view;
	wl_list_for_each(view,&wm->pin.windows,pin_link)
		view->pin_label_sent=(struct wlr_box){0};
	wm->pin.sent_control=(struct wlr_box){0};
	wm->pin.sent_remaining=0;
	wm->pin.sent_expanded=false;
	if(wm->output) wlr_output_schedule_frame(wm->output);
}

static void pin_face_place(struct pin_face *face,struct wlr_box box,float scale,int header,int row) {
	wlr_scene_node_set_enabled(&face->scene->node,box.width>0);
	if(!box.width) return;
	wlr_scene_node_set_position(&face->scene->node,box.x,box.y);
	if(face->width==box.width && face->height==box.height && face->scale==scale) return;
	if(!window_chrome_pin_face(face->scene,box.width,box.height,scale,header,row,face->highlight)) return;
	face->width=box.width;
	face->height=box.height;
	face->scale=scale;
}

static bool pin_view_eligible(const struct xdg_view *view) {
	// The Default Node may have more Inbox windows than projection slots.
	return view && view->mapped && view->tree && view->slot<0 &&
		!(view->wm->default_node_active && !view->assigned) && !wl_list_empty(&view->pin_link);
}

static double pin_aspect(const struct xdg_view *view) {
	const struct wlr_surface_state *surface=&view->toplevel->base->surface->current;
	return surface->width>0 && surface->height>0?(double)surface->width/surface->height:16./9.;
}

static void pin_clear_images(struct nested *wm,int first) {
	for(int i=first;i<PIN_LIVE;i++) {
		wlr_scene_node_set_enabled(&wm->pin.images[i]->node,false);
		if(wm->pin.images[i]->buffer) wlr_scene_buffer_set_buffer(wm->pin.images[i],NULL);
		pin_face_place(&wm->pin.faces[i],(struct wlr_box){0},0,0,0);
	}
}

static void pin_clear_labels(struct nested *wm,const struct pin_item next[],int count) {
	for(int i=0;i<wm->pin.count;i++) {
		uint64_t id=wm->pin.items[i].id;
		bool retained=false;
		for(int j=0;j<count;j++) retained|=next[j].id==id;
		if(!retained) {
			struct xdg_view *view=find_view(wm,id);
			if(view) pin_send_label(view,(struct wlr_box){0});
		}
	}
}

static void pin_collapse(struct nested *wm) {
	wm->pin.expanded=false;
	wm->pin.scroll_row=0;
	wm->pin.scroll_delta=0;
}

static void pin_hide(struct nested *wm) {
	if(wm->pin.count || wm->pin.control_box.width) {
		pin_clear_labels(wm,NULL,0);
		pin_clear_images(wm,0);
	}
	wm->pin.count=wm->pin.live=0;
	wm->pin.box=wm->pin.control_box=(struct wlr_box){0};
	pin_send_control(wm,0);
	pin_collapse(wm);
	wm->pin.hover_id=0;
	wm->pin.hovered=false;
	wm->pin.hover_opacity=0;
	pin_face_place(&wm->pin.highlight,(struct wlr_box){0},0,0,0);
	wlr_scene_node_set_enabled(&wm->pin.tree->node,false);
}

static uint64_t pin_hover_target(struct nested *wm) {
	if(!wm->pin.pointer_inside || !wm->pin.count || wm->pin.moved) return 0;
	double x=wm->pin.pointer_x,y=wm->pin.pointer_y,sx,sy;
	if(!wlr_box_contains_point(&wm->pin.box,x,y)) return 0;
	if(wlr_scene_node_at(&wm->shell_tree->node,x,y,&sx,&sy)) return 0;
	uint64_t id;
	pin_action_at(wm,x,y,&id);
	return id;
}

static void pin_draw_hover(struct nested *wm,struct timespec now) {
	uint64_t target=pin_hover_target(wm);
	int64_t ms=now.tv_sec*1000LL+now.tv_nsec/1000000;
	if(target && target!=wm->pin.hover_id) {
		wm->pin.hover_id=target;
		wm->pin.hovered=false;
		wm->pin.hover_opacity=0;
	}
	if(wm->pin.hovered!=(target!=0)) {
		wm->pin.hovered=target!=0;
		wm->pin.hover_from=wm->pin.hover_opacity;
		wm->pin.hover_started=ms;
	}

	struct wlr_box box={0};
	for(int i=0;i<wm->pin.count && wm->pin.hover_id;i++) {
		if(wm->pin.items[i].id!=wm->pin.hover_id) continue;
		box=wm->pin.items[i].box;
		if(wm->pin.expanded) {
			box.x+=2;
			box.y+=2;
			box.width-=4;
			box.height-=4;
		}
		break;
	}
	double t=wm->strip_motion?fmin(1,(ms-wm->pin.hover_started)/165.):1.;
	double eased=t*t*(3-2*t);
	wm->pin.hover_opacity=wm->pin.hover_from+(wm->pin.hovered-wm->pin.hover_from)*eased;
	if(!box.width || (!target && t>=1)) {
		wm->pin.hover_id=0;
		box=(struct wlr_box){0};
	}
	pin_face_place(&wm->pin.highlight,box,wm->output->scale,0,0);
	if(box.width) {
		wlr_scene_buffer_set_opacity(wm->pin.highlight.scene,wm->pin.hover_opacity);
		if(t<1 && wm->pin.hover_from!=wm->pin.hovered)
			wlr_output_schedule_frame(wm->output);
	}
}

static void pin_draw(struct nested *wm,struct timespec now) {
	if(!wm->pin.tree) return;
	if(!pin_scene_available(wm) || wl_list_empty(&wm->pin.windows)) {
		pin_hide(wm);
		return;
	}

	int width=clamp_int(wm->parent_window.width-2*PIN_EDGE,0,PIN_WIDTH);
	int bottom=shell_node_dock_top(wm->parent_window.height)-PIN_RAIL_GAP;
	bool left=wm->pin.corner==1 || wm->pin.corner==2, from_top=wm->pin.corner>=2;
	int top=left?PIN_EDGE:PIN_TOAST_CLEAR, room=bottom-top;
	if(width<2*PIN_CLOSE || room<PIN_HEADER+2*PIN_IMAGE_PAD+1) {
		pin_hide(wm);
		return;
	}

	struct xdg_view *view, *newest[PIN_LIVE];
	int heights[PIN_LIVE], eligible=0;
	wl_list_for_each(view,&wm->pin.windows,pin_link) {
		if(!pin_view_eligible(view)) continue;
		if(eligible<PIN_LIVE) {
			newest[eligible]=view;
			int h=(int)fmin(PIN_MAX_HEIGHT,round((width-2*PIN_IMAGE_PAD)/pin_aspect(view)));
			heights[eligible]=PIN_HEADER+PIN_IMAGE_PAD+clamp_int(h,1,PIN_MAX_HEIGHT);
		}
		eligible++;
	}
	if(!eligible) {
		pin_hide(wm);
		return;
	}

	bool can_expand=room>=2*PIN_ROW+PIN_GAP;
	int live=0, height=0;
	while(live<eligible && live<PIN_LIVE) {
		int next=height+(live?PIN_GAP:0)+heights[live];
		int reserve=can_expand && live+1<eligible?PIN_GAP+PIN_ROW:0;
		if(next+reserve>room) break;
		height=next;
		live++;
	}
	if(!live) {
		int reserve=can_expand && eligible>1?PIN_GAP+PIN_ROW:0;
		heights[0]=clamp_int(heights[0],1,room-reserve);
		height=heights[0];
		live=1;
	}
	int remaining=can_expand?eligible-live:0;
	if(!remaining) pin_collapse(wm);

	struct xdg_view *shown[PIN_ROWS]={0};
	struct pin_item items[PIN_ROWS]={0};
	int rows=0, count=live;
	if(wm->pin.expanded) {
		rows=clamp_int((room-PIN_ROW-PIN_GAP)/PIN_ROW,1,PIN_ROWS);
		count=eligible<rows?eligible:rows;
		wm->pin.scroll_max=eligible-count;
		wm->pin.scroll_row=clamp_int(wm->pin.scroll_row,0,wm->pin.scroll_max);
		int skip=wm->pin.scroll_row, at=0;
		wl_list_for_each(view,&wm->pin.windows,pin_link) {
			if(!pin_view_eligible(view)) continue;
			if(skip>0) {skip--;continue;}
			shown[at++]=view;
			if(at==count) break;
		}
		height=count*PIN_ROW;
	} else {
		wm->pin.scroll_max=0;
		for(int i=0;i<count;i++) shown[i]=newest[i];
	}

	int total=height+(remaining?PIN_ROW+PIN_GAP:0);
	int x=left?PIN_EDGE:wm->parent_window.width-PIN_EDGE-width;
	int y=from_top?top:bottom-total;
	if(wm->pin.moved && wm->pin.grab==PIN_HEADER_PRESS) {
		x=clamp_int(wm->pin.grab_box.x+wm->pin.drag_x,0,wm->parent_window.width-width);
		y=clamp_int(wm->pin.grab_box.y+wm->pin.drag_y,PIN_EDGE,bottom-total);
	}
	wm->pin.box=(struct wlr_box){x,y,width,total};
	wm->pin.control_box=(struct wlr_box){0};
	if(remaining) wm->pin.control_box=(struct wlr_box){x,from_top?y+height+PIN_GAP:y,width,PIN_ROW};
	int content_y=y+(remaining && !from_top?PIN_ROW+PIN_GAP:0);
	struct wlr_box menu=wm->pin.expanded?(struct wlr_box){x,content_y,width,height}:(struct wlr_box){0};
	pin_face_place(&wm->pin.menu,menu,wm->output->scale,0,PIN_ROW);
	pin_face_place(&wm->pin.control,wm->pin.control_box,wm->output->scale,0,0);
	wm->pin.live=wm->pin.expanded?0:live;
	for(int i=0,offset=0;i<count;i++) {
		int index=from_top && !wm->pin.expanded?count-i-1:i;
		int h=wm->pin.expanded?PIN_ROW:heights[index];
		struct wlr_box box={x,content_y+offset,width,h};
		items[i]=(struct pin_item){shown[index]->id,box};
		if(!wm->pin.expanded) {
			struct wlr_scene_buffer *image=wm->pin.images[i];
			pin_face_place(&wm->pin.faces[i],box,wm->output->scale,PIN_HEADER,0);
			struct wlr_box lane={x+PIN_IMAGE_PAD,box.y+PIN_HEADER,width-2*PIN_IMAGE_PAD,
				h-PIN_HEADER-PIN_IMAGE_PAD};
			int image_w=clamp_int((int)round(lane.height*pin_aspect(shown[index])),1,lane.width);
			strip_image(image,shown[index],&lane,lane.x+(lane.width-image_w)/2,lane.y,image_w,lane.height);
			wlr_surface_for_each_surface(shown[index]->toplevel->base->surface,stage_send_frame,&now);
		}
		offset+=h+(wm->pin.expanded?0:PIN_GAP);
	}
	pin_clear_images(wm,wm->pin.live);
	pin_clear_labels(wm,items,count);
	memcpy(wm->pin.items,items,sizeof(items));
	wm->pin.count=count;
	for(int i=0;i<count;i++) {
		int index=from_top && !wm->pin.expanded?count-i-1:i;
		struct wlr_box label=items[i].box;
		if(!wm->pin.expanded) label.height=PIN_HEADER;
		pin_send_label(shown[index],label);
	}
	pin_send_control(wm,remaining);
	pin_draw_hover(wm,now);
	wlr_scene_node_set_enabled(&wm->pin.tree->node,true);
}

static void pin_forget(struct nested *wm,struct xdg_view *view) {
	if(wl_list_empty(&view->pin_link)) return;
	wl_list_remove(&view->pin_link);
	wl_list_init(&view->pin_link);
	if(wm->cleaning) return;
	pin_send_label(view,(struct wlr_box){0});
	if(wm->output) wlr_output_schedule_frame(wm->output);
}

static void pin_metadata_changed(struct xdg_view *view) {
	if(wl_list_empty(&view->pin_link)) return;
	view->pin_label_dirty=true;
	if(view->pin_label_sent.width && view->wm->output)
		wlr_output_schedule_frame(view->wm->output);
}

static bool pin_toggle(void *data,struct wlr_surface *surface) {
	struct nested *wm=data;
	struct xdg_view *view=wm->focused;
	if(!view || view->toplevel->base->surface!=surface || !node_view(wm) || !pin_ready(wm)) return false;
	if(!wl_list_empty(&view->pin_link)) {
		pin_forget(wm,view);
	} else {
		wl_list_insert(&wm->pin.windows,&view->pin_link);
		mothership_map_pointer_v1_send_pin_stack_added(wm->map_pointer,view->id>>32,(uint32_t)view->id,
			view->toplevel->title?view->toplevel->title:"",
			view->toplevel->app_id?view->toplevel->app_id:"");
	}
	place_view(view);
	wlr_output_schedule_frame(wm->output);
	return true;
}

static enum pin_action pin_action_at(struct nested *wm,double x,double y,uint64_t *id) {
	*id=0;
	if(!pin_scene_available(wm)) return PIN_NONE;
	if(wlr_box_contains_point(&wm->pin.control_box,x,y)) return PIN_MORE;
	for(int i=0;i<wm->pin.count;i++) {
		struct pin_item item=wm->pin.items[i];
		if(!wlr_box_contains_point(&item.box,x,y)) continue;
		if(!pin_view_eligible(find_view(wm,item.id))) return PIN_NONE;
		*id=item.id;
		if(!wm->pin.live || y<item.box.y+PIN_HEADER) {
			if(x>=item.box.x+item.box.width-PIN_CLOSE) return PIN_REMOVE;
			if(wm->pin.live) return PIN_HEADER_PRESS;
		}
		return PIN_OPEN;
	}
	return PIN_NONE;
}

static bool pin_hit(struct nested *wm,double x,double y) {
	uint64_t id;
	return pin_action_at(wm,x,y,&id)!=PIN_NONE;
}

static bool pin_dismiss(void *data) {
	struct nested *wm=data;
	if(!wm->pin.expanded) return false;
	pin_collapse(wm);
	if(wm->output) wlr_output_schedule_frame(wm->output);
	return true;
}

static void pin_grab(struct nested *wm,enum nested_move_phase phase,double x,double y) {
	if(phase==MOVE_UPDATE) {
		double dx=x-wm->pin.grab_x,dy=y-wm->pin.grab_y;
		if(!wm->pin.moved && hypot(dx,dy)<PIN_DRAG) return;
		wm->pin.moved=true;
		if(wm->pin.grab==PIN_HEADER_PRESS) {
			wm->pin.drag_x=(int)round(dx);
			wm->pin.drag_y=(int)round(dy);
			wlr_output_schedule_frame(wm->output);
		}
		return;
	}

	enum pin_action grab=wm->pin.grab;
	bool moved=wm->pin.moved;
	wm->pin.grab=PIN_NONE;
	wm->pin.moved=false;
	wlr_output_schedule_frame(wm->output);
	if(phase==MOVE_CANCEL) return;
	if(moved) {
		if(grab==PIN_HEADER_PRESS) {
			bool left=x<wm->parent_window.width/2.,top=y<wm->parent_window.height/2.;
			wm->pin.corner=top?(left?2:3):(left?1:0);
		}
		return;
	}
	uint64_t id;
	enum pin_action release=pin_action_at(wm,x,y,&id);
	if(id!=wm->pin.grabbed_id || release==PIN_NONE) return;
	if(grab==PIN_MORE && release==PIN_MORE) {
		if(!pin_dismiss(wm)) wm->pin.expanded=true;
	} else if(grab==PIN_REMOVE && release==PIN_REMOVE) {
		struct xdg_view *view=find_view(wm,id);
		pin_forget(wm,view);
		place_view(view);
	} else if((grab==PIN_OPEN || grab==PIN_HEADER_PRESS) &&
			(release==PIN_OPEN || release==PIN_HEADER_PRESS)) {
		pin_dismiss(wm);
		mothership_map_pointer_v1_send_pin_activate(wm->map_pointer,id>>32,(uint32_t)id);
	}
}

static bool pin_scroll(struct nested *wm,double x,double y,unsigned axis,double delta) {
	if(!pin_hit(wm,x,y)) return false;
	if(wm->pin.expanded && !wlr_box_contains_point(&wm->pin.control_box,x,y) &&
			axis==WL_POINTER_AXIS_VERTICAL_SCROLL && delta) {
		const double step=12;
		wm->pin.scroll_delta+=delta;
		int rows=(int)(wm->pin.scroll_delta/step);
		wm->pin.scroll_delta-=rows*step;
		int next=clamp_int(wm->pin.scroll_row+rows,0,wm->pin.scroll_max);
		if(next!=wm->pin.scroll_row) {
			wm->pin.scroll_row=next;
			wlr_output_schedule_frame(wm->output);
		}
	}
	return true;
}

static bool pin_scene_init(struct nested *wm) {
	for(int i=0;i<PIN_LIVE;i++) {
		wm->pin.faces[i].scene=wlr_scene_buffer_create(wm->pin.tree,NULL);
		wm->pin.images[i]=wlr_scene_buffer_create(wm->pin.tree,NULL);
		if(!wm->pin.faces[i].scene || !wm->pin.images[i]) return false;
	}
	wm->pin.menu.scene=wlr_scene_buffer_create(wm->pin.tree,NULL);
	wm->pin.control.scene=wlr_scene_buffer_create(wm->pin.tree,NULL);
	wm->pin.highlight.scene=wlr_scene_buffer_create(wm->pin.tree,NULL);
	wm->pin.highlight.highlight=true;
	wlr_scene_node_set_enabled(&wm->pin.tree->node,false);
	return wm->pin.menu.scene && wm->pin.control.scene && wm->pin.highlight.scene;
}

static bool tray_closing(struct nested *wm) {
	for(int i=0;i<FLOW_GROUP_MAX;i++)
		if(wm->tray_flight[i].id && !wm->tray_flight[i].opening)return true;
	return false;
}

static void stage_show_unit(struct nested *wm,struct xdg_view *view) {
	struct xdg_view *members[FLOW_GROUP_MAX];
	int count=stage_members(wm,view,members);
	// Keep the work area exclusive while the outgoing unit docks.
	bool closing=tray_closing(wm);
	if (closing || (wm->resize.view && wm->resize.view!=view)) resize_cancel(wm);
	for(int i=0;i<wm->visible_count;i++) {
		struct xdg_view *v=wm->slots[i];
		bool paint=false;
		for(int j=0;j<count;j++) paint|=v==members[j];
		wlr_scene_node_set_enabled(&v->frame->node,paint && !closing);
	}
	for(int i=0;i<count;i++) place_view(members[i]);
}

static void stage_flights_clear(struct nested *wm) {
	for(int i=0;i<wm->stage_flight_count;i++) stage_scale_clear(&wm->stage_flight[i].sprite);
	wm->stage_flight_count=0;
	wm->stage_tick=0;
}

static void present_stage(struct nested *wm) {
	stage_flights_clear(wm);
	wm->stage_painted=wm->focused;
	wm->stage_amount=1;
	if(!wm->picker_keyboard) strip_reveal_window(wm,wm->focused);
	stage_show_unit(wm,wm->focused);
}

static bool flight_capture(struct nested *wm,struct stage_scale *scale,struct xdg_view *view,struct wlr_scene_tree *overlay) {
	struct rect r=view->placement;
	struct wlr_surface *surface=view->toplevel->base->surface;
	struct owned_preview *owned=NULL;
	if(surface_has_explicit_sync(surface) &&
		owned_preview_update(&view->preview_copy,wm->output,surface,view->preview_generation,
			wl_display_get_event_loop(wm->server)))owned=&view->preview_copy;
	return stage_scale_capture(scale,overlay,view->frame,
		(struct wlr_box){r.x,r.y,r.width,r.height},surface,owned);
}
static bool tray_flight_enabled(struct nested *wm,struct xdg_view *view) {
	if(!view->mapped || view->minimized || view->slot<0)return false;
	if(is_stage(wm))return stage_same_unit(wm,wm->focused,view);
	return is_flow(wm) && (wm->focused && wm->focused->maximized?
		wm->focused==view:!view->maximized);
}
static void tray_flights_clear(struct nested *wm) {
	bool stage_closing=is_stage(wm) && tray_closing(wm);
	for(int i=0;i<FLOW_GROUP_MAX;i++) {
		__typeof__(wm->tray_flight[0]) *f=&wm->tray_flight[i];
		if(f->opening && f->id) {
			struct xdg_view *v=find_view(wm,f->id);
			if(v && v->frame)wlr_scene_node_set_enabled(&v->frame->node,tray_flight_enabled(wm,v));
		}
		stage_scale_clear(&f->sprite);
		f->id=0;
	}
	if(stage_closing)present_stage(wm);
}
static struct wlr_box tray_flight_anchor(struct nested *wm,struct xdg_view *v) {
	struct wlr_box tile=strip_rest_tile(wm,v,fmin(wm->strip_offset,strip_limit(wm)));
	int remaining=wm->node_strip.width-tile.width;
	tile.x=clamp_int(tile.x,wm->node_strip.x,wm->node_strip.x+(remaining>0?remaining:0));
	return tile;
}
static void tray_flight_start(struct nested *wm,struct xdg_view *v,int index,bool opening,struct wlr_box anchor) {
	if(!wm->strip_motion || !node_strip_active(wm) || v->slot<0 || !v->frame->node.enabled)return;
	__typeof__(wm->tray_flight[0]) *f=&wm->tray_flight[index];
	if(!flight_capture(wm,&f->sprite,v,wm->stage_overlay))return;
	f->id=v->id;f->started=-1;f->opening=opening;f->anchor=anchor;
	if(opening)wlr_scene_node_set_enabled(&v->frame->node,false);
	wlr_output_schedule_frame(wm->output);
}
static void tray_flights_frame(struct nested *wm,struct timespec now) {
	int64_t ms=now.tv_sec*1000LL+now.tv_nsec/1000000;
	bool changed=false;
	bool stage_closing=is_stage(wm) && tray_closing(wm);
	for(int i=0;i<FLOW_GROUP_MAX;i++) {
		__typeof__(wm->tray_flight[0]) *f=&wm->tray_flight[i];
		if(!f->id)continue;
		struct xdg_view *v=find_view(wm,f->id);
		if(f->started<0)f->started=ms;
		double t=fmin(1.,(ms-f->started)/215.);
		if(t>=1 || !v || v->slot<0 || !v->mapped || !wm->strip_motion ||
				!node_strip_active(wm) || (f->opening && !tray_flight_enabled(wm,v))) {
			if(v && f->opening)wlr_scene_node_set_enabled(&v->frame->node,tray_flight_enabled(wm,v));
			stage_scale_clear(&f->sprite);f->id=0;changed=true;
			continue;
		}
		if(!f->opening)f->anchor=tray_flight_anchor(wm,v);
		else wlr_scene_node_set_enabled(&v->frame->node,false);
		if(f->anchor.width>0 && f->anchor.height>0)
			stage_scale_draw(&f->sprite,f->anchor,stage_ease(f->opening?t:1.-t));
		wlr_output_schedule_frame(wm->output);
	}
	if(stage_closing && !tray_closing(wm))present_stage(wm);
	if(changed)nested_seat_refresh(wm->seat);
}
static bool stage_capture_unit(struct nested *wm,struct xdg_view *view) {
	const struct wlr_box *lane=&wm->node_strip;
	if(!node_strip_active(wm))return false;
	struct xdg_view *members[FLOW_GROUP_MAX];
	int count=stage_members(wm,view,members);
	wm->stage_rail_from=wm->strip_offset;
	wm->stage_rail_to=strip_goal(wm,view,wm->strip_offset);
	for(int i=0;i<count;i++) {
		struct wlr_box anchor=strip_rest_tile(wm,members[i],wm->stage_rail_to);
		if(anchor.width<=0 || anchor.x<lane->x || anchor.x+anchor.width>lane->x+lane->width ||
				!members[i]->frame->node.enabled ||
				!flight_capture(wm,&wm->stage_flight[i].sprite,members[i],wm->stage_overlay))return false;
		wm->stage_flight[i].anchor=anchor;
		wm->stage_flight_count++;
	}
	for(int i=0;i<count;i++) wlr_scene_node_set_enabled(&members[i]->frame->node,false);
	return count>0;
}

static void stage_begin_switch(struct nested *wm) {
	if(tray_closing(wm))return;
	tray_flights_clear(wm);
	struct xdg_view *painted=wm->stage_painted;
	if(!wm->strip_motion || !painted) {present_stage(wm);return;}
	wm->strip_hover=0;
	if(!wm->stage_flight_count && !stage_capture_unit(wm,painted)) {
		present_stage(wm);
		return;
	}
	wlr_output_schedule_frame(wm->output);
}

static void stage_scale_frame(struct nested *wm,struct timespec now) {
	if(!wm->stage_flight_count) return;
	if(!wm->strip_motion || !node_strip_active(wm) || !wm->focused) {
		present_stage(wm);
		nested_seat_refresh(wm->seat);
		return;
	}
	int64_t ms=now.tv_sec*1000LL+now.tv_nsec/1000000;
	double elapsed=wm->stage_tick?ms-wm->stage_tick:0;
	wm->stage_tick=ms;
	if(!stage_same_unit(wm,wm->stage_painted,wm->focused)) {
		wm->stage_amount=fmax(0,wm->stage_amount-elapsed/200.);
		if(wm->stage_amount<=0) {
			stage_flights_clear(wm);
			wm->stage_tick=ms;
			if(!wm->picker_keyboard) wm->strip_offset=wm->stage_rail_to;
			wm->stage_painted=wm->focused;
			stage_show_unit(wm,wm->focused);
			if(!stage_capture_unit(wm,wm->focused)) {
				present_stage(wm);
				nested_seat_refresh(wm->seat);
				return;
			}
			if(!wm->picker_keyboard) wm->strip_offset=wm->stage_rail_to;
			wm->stage_rail_from=wm->stage_rail_to;
		}
	} else {
		wm->stage_amount=fmin(1,wm->stage_amount+elapsed/240.);
		if(wm->stage_amount>=1) {
			present_stage(wm);
			nested_seat_refresh(wm->seat);
			return;
		}
	}
	const double eased=stage_ease(wm->stage_amount);
	if(!wm->picker_keyboard)
		wm->strip_offset=wm->stage_rail_to+(wm->stage_rail_from-wm->stage_rail_to)*eased;
	for(int i=0;i<wm->stage_flight_count;i++)
		stage_scale_draw(&wm->stage_flight[i].sprite,wm->stage_flight[i].anchor,eased);
	wlr_output_schedule_frame(wm->output);
}

static void present_flow(struct nested *wm) {
	// Recompose low-to-high: strip, independently raised floats, focused max.
	uint64_t float_order[NODE_WINDOWS]={0};
	// A maximized client may take time to commit its new buffer, or decline
	// the requested size. Do not expose other windows around its old buffer.
	bool exclusive=wm->focused && wm->focused->slot>=0 && wm->focused->maximized;
	for (int i=0;i<wm->visible_count;i++) {
		struct xdg_view *v=wm->slots[i];
		wlr_scene_node_set_enabled(&v->frame->node,!v->minimized &&
			(exclusive ? v==wm->focused : !v->maximized));
		if(v->minimized) continue;
		place_view(v);
		if(wm->maximize.view==v)wlr_scene_node_set_enabled(&v->frame->node,false);
		if (!v->maximized && !v->flowed_out) wlr_scene_node_raise_to_top(&v->frame->node);
		else if (!v->maximized) float_order[i]=v->focused_at+1;
	}
	for (int n=0;n<wm->visible_count;n++) {
		int first=-1;
		for (int i=0;i<wm->visible_count;i++)
			if (float_order[i] && (first<0 || float_order[i]<float_order[first])) first=i;
		if (first<0) break;
		wlr_scene_node_raise_to_top(&wm->slots[first]->frame->node);
		float_order[first]=0;
	}
	if (wm->focused && wm->focused->slot>=0 && wm->focused->maximized)
		wlr_scene_node_raise_to_top(&wm->focused->frame->node);
}


static size_t glass_below(struct nested *wm,struct wlr_scene_node *anchor,
		struct wlr_scene_node *list[NODE_WINDOWS+2]) {
	size_t n=0;
	list[n++]=&wm->background->node;
	list[n++]=&wm->wallpaper_tree->node;
	struct wlr_scene_node *node;
	wl_list_for_each(node,&wm->app_tree->children,link) {
		if(node==anchor)break;
		for(int i=0;i<wm->visible_count;i++)
			if(node==&wm->slots[i]->frame->node && node->enabled) {
				list[n++]=node;break;
			}
	}
	return n;
}
static struct wlr_box client_aperture(struct xdg_view *v) {
	struct rect a=v->placement;
	return (struct wlr_box){a.x+v->content_edge,a.y+v->content_top,
		a.width-2*v->content_edge,a.height-v->content_top-v->content_bottom};
}
static unsigned decorate_frames(struct nested *wm) {
	const struct wlr_box output={0,0,wm->parent_window.width,wm->parent_window.height};
	if(output.width<=0 || output.height<=0)return 0;
	bool framed=view_has_frame(wm) && !wm->revealing;
	bool moving=wm->drag || wm->flow_sliding || wm->reorder.active || wm->mode_slide_view || wm->stage_flight_count || wm->maximize.view;
	bool assigned[NODE_WINDOWS]={0};
	unsigned trace=0;
	int g=0;
	for(int i=0;framed && group_large(wm) && i<wm->visible_count && g<NODE_WINDOWS/2;i++) {
		struct xdg_view *view=wm->slots[i];
		if(assigned[i] || !view->frame->node.enabled)continue;
		struct xdg_view *members[FLOW_GROUP_MAX];
		int count=group_members(wm,view,members,true);
		if(count<2)continue;
		struct wlr_scene_node *bottom=NULL,*node;
		wl_list_for_each(node,&wm->app_tree->children,link) {
			for(int m=0;m<count;m++) if(node==&members[m]->frame->node) {bottom=node;break;}
			if(bottom)break;
		}
		if(!bottom)continue;
		struct rect r=is_stage(wm)?stage_bounds(wm):view->flow_unit;
		if(is_flow(wm)) {
			int offset=wm->flow_offset;
			if(wm->reorder.active)
				offset+=(int)round((wm->reorder.flow_from-offset)*wm->reorder.remaining);
			if(flow_vertical(wm->layout,output.width,output.height))r.y+=offset;
			else r.x+=offset;
		}
		struct wlr_box box={r.x,r.y,r.width,r.height};
		if(!wlr_box_intersection(&(struct wlr_box){0},&box,&output))continue;
		__typeof__(wm->group_frame[0]) *slot=&wm->group_frame[g++];
		if(!slot->chrome)slot->chrome=group_chrome_create(wm->app_tree);
		struct wlr_scene_tree *tree=group_chrome_tree(slot->chrome);
		if(!tree)continue;
		bool focused=false;
		for(int j=0;j<count;j++) {
			assigned[members[j]->slot]=true;
			focused|=wm->focused==members[j];
		}
		slot->box=box;
		slot->leader=view->id;
		group_chrome_place(slot->chrome,r.x,r.y,r.width,r.height,wm->output->scale,
			(unsigned)view->group_id,focused);
		if(tree->node.link.next!=&bottom->link)wlr_scene_node_place_below(&tree->node,bottom);
	}
	for(;g<NODE_WINDOWS/2;g++) {
		__typeof__(wm->group_frame[0]) *slot=&wm->group_frame[g];
		if(!slot->leader)continue;
		slot->leader=0;
		wlr_scene_node_set_enabled(&group_chrome_tree(slot->chrome)->node,false);
	}
	struct xdg_view *v;
	wl_list_for_each(v,&wm->views,link) {
		struct rect r=v->placement;
		struct wlr_box box={r.x,r.y,r.width,r.height};
		bool show=framed && v->slot>=0 && !assigned[v->slot] && v->chrome &&
			!v->minimized && v->frame->node.enabled &&
			wlr_box_intersection(&(struct wlr_box){0},&box,&output);
		if(show && wm->frame_glass_enabled && !v->frame_glass)v->frame_glass=scene_glass_create(wm->output,v->frame);
		if(!v->frame_glass)continue;
		if(!show) {
			trace|=scene_glass_update(v->frame_glass,(struct wlr_box){0},0,false,NULL,NULL,0);
			continue;
		}
		scene_glass_place_below(v->frame_glass,&v->frame->node);
		struct wlr_scene_node *sources[NODE_WINDOWS+2];
		size_t n=glass_below(wm,&v->frame->node,sources);
		struct wlr_box hole=client_aperture(v);
		trace|=scene_glass_update_masked(v->frame_glass,box,850,moving,
			wl_display_get_event_loop(wm->server),sources,n,&hole,1);
	}
	return trace;
}
static void flow_focus(struct nested *wm,struct xdg_view *view) {
	flow_reorder_finish(wm);
	if(!is_flow(wm) || wm->revealing || view->flowed_out || view->maximized) {
		wm->flow_sliding=false;
		return;
	}
	int to=clamp_int(flow_center(wm,view),wm->flow_low,wm->flow_high);
	if(wm->flow_sliding && wm->flow_slide_to==to)return;
	wm->flow_sliding=false;
	if(to==wm->flow_offset)return;
	if(!wm->strip_motion) {wm->flow_offset=to;present_flow(wm);return;}
	wm->flow_slide_from=wm->flow_offset;wm->flow_slide_to=to;
	// Long jumps with the short-hop timing skip too much distance
	// per frame, even when presentation keeps up with refresh.
	wm->flow_slide_duration=clamp_int(145+abs(to-wm->flow_offset)/6,180,335);
	wm->flow_slide_start=-1;wm->flow_sliding=true;
	wlr_output_schedule_frame(wm->output);
}

static void flow_slide_frame(struct nested *wm,struct timespec now) {
	if(!wm->flow_sliding)return;
	if(!is_flow(wm) || wm->revealing) {wm->flow_sliding=false;return;}
	int64_t ms=now.tv_sec*1000LL+now.tv_nsec/1000000;
	if(wm->flow_slide_start<0)wm->flow_slide_start=ms;
	double t=fmin(1,(ms-wm->flow_slide_start)/(double)wm->flow_slide_duration);
	double e=t*t*(3-2*t);
	int next=(int)round(wm->flow_slide_from+(wm->flow_slide_to-wm->flow_slide_from)*e);
	int delta=next-wm->flow_offset;
	wm->flow_offset=next;
	bool vertical=flow_vertical(wm->layout,wm->parent_window.width,wm->parent_window.height);
	// Change only scene positions. No configure, resize, chrome render or
	// recapture is necessary for the focus transition.
	if(delta)for(int i=0;i<wm->visible_count;i++){
		struct xdg_view *v=wm->slots[i];
		if(v->flowed_out || v->maximized || (!v->frame->node.enabled && wm->group_join.id!=v->id)) continue;
		if(vertical)v->placement.y+=delta;else v->placement.x+=delta;
		wlr_scene_node_set_position(&v->frame->node,v->placement.x,v->placement.y);
	}
	wm->flow_sliding=t<1;
	if(wm->flow_sliding)wlr_output_schedule_frame(wm->output);
	else nested_seat_refresh(wm->seat);
}

static void flow_reorder_position(struct nested *wm,double remaining) {
	for(int i=0;i<wm->visible_count;i++) {
		struct xdg_view *v=wm->slots[i];
		if(!v->reorder_x && !v->reorder_y) continue;
		v->placement.x+=(int)round(v->reorder_x*remaining)-(int)round(v->reorder_x*wm->reorder.remaining);
		v->placement.y+=(int)round(v->reorder_y*remaining)-(int)round(v->reorder_y*wm->reorder.remaining);
		wlr_scene_node_set_position(&v->frame->node,v->placement.x,v->placement.y);
	}
	wm->reorder.remaining=remaining;
}

static void flow_reorder_finish(struct nested *wm) {
	if(!wm->reorder.active) return;
	flow_reorder_position(wm,0);
	wm->reorder.active=false;
	if(wm->output) wlr_output_schedule_frame(wm->output);
}

static void flow_reorder_frame(struct nested *wm,struct timespec now) {
	if(!wm->reorder.active) return;
	int64_t ms=monotonic_us(now)/1000;
	if(wm->reorder.start<0) wm->reorder.start=ms;
	double t=fmin(1,(ms-wm->reorder.start)/190.);
	flow_reorder_position(wm,1-stage_ease(t));
	wm->reorder.active=t<1;
	if(wm->reorder.active) wlr_output_schedule_frame(wm->output);
	else nested_seat_refresh(wm->seat);
}

static void strip_reveal_window(struct nested *wm,struct xdg_view *view) {
	if(!view || view->slot<0 || wm->node_strip.width<=0) return;
	double offset=strip_goal(wm,view,wm->strip_offset);
	// Focus is not a press: keep the hovered card lifted unless the rail
	// scrolls and moves a different card under the cursor.
	if(offset!=wm->strip_offset) wm->strip_hover=0;
	wm->strip_offset=offset;
}

static void remember_focus(void *data, struct wlr_surface *surface) {
	struct nested *wm = data;
	if (!wm->domain_active) {
		return;
	}
	for (int i = 0; i < NODE_WINDOWS; ++i) {
		struct xdg_view *view = wm->slots[i];
		if (view != NULL && !view->minimized && view->toplevel->base->surface == surface) {
			if (wm->resize.view && wm->resize.view!=view) resize_cancel(wm);
			if(wm->focused!=view && !is_stage(wm)) strip_reveal_window(wm,view);
			if(wm->home_active) view->home_focused_at=++wm->focus_order;
			else view->focused_at=++wm->focus_order;
			if(is_stage(wm)) {
				struct xdg_view *previous=wm->focused;
				wm->focused=view;
				if(previous!=view) {
					if(wm->stage_painted && stage_same_unit(wm,previous,view)) {
						if(!wm->stage_flight_count) {
							strip_reveal_window(wm,view);
							place_view(previous);
							place_view(view);
							wm->stage_painted=view;
						}
					} else {
						struct xdg_view *members[FLOW_GROUP_MAX];
						int count=stage_members(wm,view,members);
						for(int j=0;j<count;j++) stage_prime(members[j]);
						stage_begin_switch(wm);
					}
				}
			} else if(is_flow(wm)) {
				bool changed=wm->focused!=view;
				bool same_group=changed && view->group_id && wm->focused &&
					wm->focused->group_id==view->group_id && group_large(wm) &&
					group_visible(wm->focused) && group_visible(view);
				if(changed && wm->maximize.view!=view)maximize_finish(wm);
				wm->focused=view;
				present_flow(wm);
				if(changed && !same_group && wm->resize.view!=view)flow_focus(wm,view);
			} else if(wm->layout==FLOAT) {
				wlr_scene_node_raise_to_top(&view->frame->node);
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
		if (view == NULL || view->tree == NULL || view->minimized ||
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
	struct xdg_view *best = is_stage(wm)?wm->focused:best_view(wm,true);
	return best == NULL ? NULL : best->toplevel->base->surface;
}

static void clear_slots(struct nested *wm) {
	resize_cancel(wm);
	wm->reorder.active=false;
	maximize_finish(wm);
	tray_flights_clear(wm);
	wm->group_grab.source=wm->group_join.id=0;
	stage_flights_clear(wm);
	wm->stage_painted=NULL;
	wm->flow_sliding=false;
	wm->mode_slide_view=NULL;
	for (int i = 0; i < NODE_WINDOWS; ++i) {
		struct xdg_view *view = wm->slots[i];
		if (view != NULL) {
			wlr_scene_node_set_enabled(&view->frame->node, false);
			view->slot = -1;
		}
		wm->slots[i] = NULL;
	}
	wm->visible_count = 0;
	wm->focused = NULL;
}

static void present_node(struct nested *wm,bool recenter,uint64_t preferred_group) {
	struct xdg_view *selected = best_view(wm, false);
	struct xdg_view *top = selected;
	for(int i=0;!top && i<wm->visible_count;i++)
		if(!wm->slots[i]->minimized) top=wm->slots[i];
	wm->focused=selected;
	if (is_stage(wm)) {
		if(preferred_group) {
			struct xdg_view *survivor=NULL;
			uint64_t rank=0;
			for(int i=0;i<wm->visible_count;i++) {
				struct xdg_view *v=wm->slots[i];
				if(v->group_id!=preferred_group || v->minimized)continue;
				uint64_t at=wm->default_node_active?v->home_focused_at:v->focused_at;
				if(!survivor || at>rank) {survivor=v;rank=at;}
			}
			if(survivor) top=survivor;
		}
		wm->focused=top;
		if(top && top!=selected) {
			if(wm->default_node_active) top->home_focused_at=++wm->focus_order;
			else top->focused_at=++wm->focus_order;
		}
		present_stage(wm);
		return;
	}
	if (is_flow(wm)) {
		struct xdg_view *center=top;
		if(recenter && center && center->flowed_out) {
			center=NULL;
			for(int i=0;i<wm->visible_count;i++) if(!wm->slots[i]->minimized && !wm->slots[i]->flowed_out) {center=wm->slots[i];break;}
		}
		arrange_flow(wm,recenter?center:NULL);
		present_flow(wm);
		return;
	}
	for (int i = 0; i < wm->visible_count; ++i) {
		struct xdg_view *view = wm->slots[i];
		wlr_scene_node_set_enabled(&view->frame->node, !view->minimized && (wm->layout != STACK || view == top));
		place_view(view);
	}
	if (wm->layout == FLOAT && selected != NULL) {
		wlr_scene_node_raise_to_top(&selected->frame->node);
	}
}

static uint64_t *layout_order(struct xdg_view *view) {
	return view->wm->default_node_active?&view->home_order:&view->node_order;
}

static void project_ordered_view(struct xdg_view *slots[NODE_WINDOWS],int *count,struct xdg_view *view) {
	uint64_t *order=layout_order(view);
	if(!*order) *order=++view->wm->next_layout_order;
	int i=(*count)++;
	while(i>0 && *layout_order(slots[i-1])>*order) {
		slots[i]=slots[i-1];
		i--;
	}
	slots[i]=view;
}

static void project_home(struct nested *wm,bool recenter) {
	if(!wm->default_node_active) { clear_slots(wm); return; }
	struct xdg_view *next[NODE_WINDOWS]={0},*view,*previous=wm->focused;
	uint64_t group=previous?previous->group_id:0;
	int count=0;
	wl_list_for_each(view,&wm->views,link) {
		if(view->mapped && !view->dialog && !view->assigned && view->tree) {
			project_ordered_view(next,&count,view);
			if(count==NODE_WINDOWS)break;
		}
	}
	if(count==wm->visible_count && !memcmp(next,wm->slots,sizeof(next))) {
		if(recenter) present_node(wm,true,0);
		return;
	}
	clear_slots(wm);
	wm->visible_count=count;
	for(int i=0;i<count;++i) {
		wm->slots[i]=next[i];next[i]->slot=i;
	}
	// Preserve Inbox's initial focus when its first window appears.
	if(!best_view(wm,false))
		for(int i=0;i<count;i++) if(!next[i]->minimized) {
			next[i]->home_focused_at=++wm->focus_order;
			break;
		}
	present_node(wm,recenter,previous && previous->slot<0?group:0);
}

static void refresh_home(struct nested *wm) {
	struct xdg_view *old[NODE_WINDOWS],*previous=wm->focused;
	memcpy(old,wm->slots,sizeof(old));
	project_home(wm,false);
	struct wlr_surface *target=view_target(wm);
	for(int i=0;i<NODE_WINDOWS;++i)
		if(old[i] && old[i]->slot<0)
			nested_seat_forget(wm->seat,old[i]->toplevel->base->surface,target);
	if(!previous && target)nested_seat_switch(wm->seat,target,false);
	if(wm->drag && wm->drag->slot<0)wm->drag=NULL;
	refresh_blank_shell(wm);
	nested_seat_refresh(wm->seat);
	focus_shown_view(wm);
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
	if (strcmp(value,"flow")==0) {
		*layout=FLOW;
	} else if (strcmp(value,"flow-horizontal")==0) {
		*layout=FLOW_HORIZONTAL;
	} else if (strcmp(value,"flow-vertical")==0) {
		*layout=FLOW_VERTICAL;
	} else if (strcmp(value,"stage")==0) {
		*layout=STAGE;
	} else if (strcmp(value, "tiling") == 0) {
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

// Group membership belongs to toplevels, even while minimized, temporarily
// unmapped, or outside the selected Node. Detach on close or reassignment.
static void group_detach(struct xdg_view *view) {
	uint64_t id=view->group_id;
	if(!id)return;
	view->group_id=view->group_order=0;
	struct xdg_view *only=NULL,*part;
	wl_list_for_each(part,&view->wm->views,link) {
		if(part->group_id!=id)continue;
		if(only)return;
		only=part;
	}
	if(only)only->group_id=only->group_order=0;
}

static void reset_node_geometry(struct xdg_view *view) {
	group_detach(view);
	view->floating=(struct rect){0};
	view->flowed_out=view->maximized=false;
	view->flow_width=view->flow_height=0;
	view->stage_width=view->stage_height=0;
}

static void project_named(struct nested *wm, const struct domain_member members[NODE_WINDOWS],bool recenter) {
	struct xdg_view *previous=wm->focused;
	uint64_t group=previous?previous->group_id:0;
	clear_slots(wm);
	if (members != wm->members) {
		memcpy(wm->members, members, sizeof(wm->members));
	}
	for (int i = 0; i < NODE_WINDOWS && members[i].id != 0; ++i) {
		struct xdg_view *view = find_view(wm, members[i].id);
		if (view == NULL || view->dialog) {
			continue;
		}
		if (view->epoch != members[i].epoch) {
			view->epoch = members[i].epoch;
			view->focused_at = 0;
			view->node_order = 0;
			reset_node_geometry(view);
		}
		if(!view->node_order) view->node_order=++wm->next_layout_order;
		if (view->mapped && view->tree != NULL) {
			project_ordered_view(wm->slots,&wm->visible_count,view);
		}
	}
	for(int i=0;i<wm->visible_count;i++) wm->slots[i]->slot=i;
	present_node(wm,recenter,previous && previous->slot<0?group:0);
}

// Rank one requested window first before a view is projected, so the
// projection centers and focuses it directly. A named target must be in the
// member list (desktopd resolved it); an Inbox target must be unassigned.
// Anything else keeps the normal focus restore.
static struct xdg_view *prefer_window(struct nested *wm,uint64_t target,
		const struct domain_member members[NODE_WINDOWS]) {
	struct xdg_view *view=find_view(wm,target);
	if(!view || !view->mapped || !view->tree) return NULL;
	if(!members) {
		if(view->assigned) return NULL;
		view->home_focused_at=++wm->focus_order;
		return view;
	}
	for(int i=0;i<NODE_WINDOWS && members[i].id;i++) {
		if(members[i].id!=target) continue;
		if(view->epoch!=members[i].epoch) {
			// Match project_named's reset for a new membership epoch.
			view->epoch=members[i].epoch;
			view->node_order=0;
			reset_node_geometry(view);
		}
		view->focused_at=++wm->focus_order;
		return view;
	}
	return NULL;
}

// A minimized target is not eligible for the projection's best view;
// restore it through the normal preview-focus path once it has a slot.
static void focus_preferred(struct nested *wm,struct xdg_view *view) {
	if(view && view->slot>=0 && view!=wm->focused) strip_focus_view(wm,view);
}

static void refresh_named(struct nested *wm, const struct domain_member members[NODE_WINDOWS]) {
	struct xdg_view *old[NODE_WINDOWS],*previous=wm->focused;
	memcpy(old, wm->slots, sizeof(old));
	project_named(wm, members,false);
	struct wlr_surface *fallback = view_target(wm);
	if (fallback==NULL)
		for (int i=0;i<wm->visible_count;i++) if(wm->slots[i]->frame->node.enabled) {
			fallback=wm->slots[i]->toplevel->base->surface;
			break;
		}
	if (is_flow(wm) && previous && previous->slot<0 && fallback && !wm->focused)
		remember_focus(wm,fallback);
	for (int i = 0; i < NODE_WINDOWS; ++i) {
		if (old[i] != NULL && (old[i]->slot < 0 || !old[i]->frame->node.enabled)) {
			nested_seat_forget(wm->seat, old[i]->toplevel->base->surface, fallback);
		}
	}
	if (wm->drag != NULL && wm->drag->slot < 0) {
		wm->drag = NULL;
	}
	nested_seat_refresh(wm->seat);
	focus_shown_view(wm);
}

static void strip_focus_view(struct nested *wm,struct xdg_view *v) {
	if(!v || v->slot<0 || !v->mapped || wm->revealing) return;
	struct wlr_surface *surface=v->toplevel->base->surface;
	struct xdg_view *restoring[FLOW_GROUP_MAX]={0};
	struct wlr_box anchors[FLOW_GROUP_MAX]={0};
	bool expand_stage=false;
	int n=0;
	if(v->minimized) {
		expand_stage=is_stage(wm) && stage_same_unit(wm,wm->focused,v);
		n=group_members(wm,v,restoring,false);
		if(n<2) {restoring[0]=v;n=1;}
		tray_flights_clear(wm);
		for(int j=0;j<n;j++) {
			if(!restoring[j]->minimized)continue;
			anchors[j]=tray_flight_anchor(wm,restoring[j]);
			restoring[j]->minimized=false;
			if(restoring[j]!=v)stage_prime(restoring[j]);
		}
		v->minimized=false;
		if(!is_stage(wm)) {
			arrange_flow(wm,v);
			wlr_scene_node_set_enabled(&v->frame->node,true);
			place_view(v);
			stage_prime(v);
		}
	}
	remember_focus(wm,surface);
	if(named_view(wm) && wm->layout==STACK) project_named(wm,wm->members,false);
	else wm->focused=v;
	if(expand_stage) present_stage(wm);
	nested_seat_switch(wm->seat,surface,false);
	if((is_flow(wm) || is_stage(wm)) && !wm->stage_flight_count)
		for(int j=0;j<n;j++) if(anchors[j].width>0)
			tray_flight_start(wm,restoring[j],j,true,anchors[j]);
	wlr_output_schedule_frame(wm->output);
}

// A newly mapped window takes keyboard focus the first time it appears in
// the live view. A view switch drops the claim: a window that landed in a
// hidden Inbox doesn't steal focus later.
static void focus_shown_view(struct nested *wm) {
	for(int i=0;i<wm->visible_count;i++) {
		struct xdg_view *v=wm->slots[i];
		if(!v->focus_on_show || !v->mapped || v->minimized) continue;
		v->focus_on_show=false;
		strip_focus_view(wm,v);
		return;
	}
}

static bool group_can_join(struct nested *wm,struct xdg_view *source,struct xdg_view *target) {
	if(!group_large(wm) || wm->revealing || !source || !target || source==target ||
		source->slot<0 || target->slot<0 || !group_visible(target) || target->maximized || target->flowed_out ||
		!source->mapped || source->minimized || source->maximized ||
		(source->group_id && source->group_id==target->group_id))return false;
	struct xdg_view *old[FLOW_GROUP_MAX];
	return group_members(wm,target,old,false)<FLOW_GROUP_MAX;
}

static void group_join_views(struct nested *wm,struct xdg_view *source,struct xdg_view *target,struct rect from) {
	if(is_stage(wm)) tray_flights_clear(wm);
	group_detach(source);
	struct xdg_view *old[FLOW_GROUP_MAX];
	int n=group_members(wm,target,old,false);
	if(n<1) {
		target->group_id=++wm->next_group_id;
		target->group_order=++wm->next_group_order;
		target->group_sx=60;target->group_sy=50;target->group_gap=FLOW_GROUP_GAP;
	}
	source->group_id=target->group_id;
	source->group_order=++wm->next_group_order;
	source->group_sx=target->group_sx;source->group_sy=target->group_sy;source->group_gap=target->group_gap;
	source->flowed_out=false;
	if(is_stage(wm)) {
		stage_flights_clear(wm);
		wm->stage_painted=NULL;
		bool already_focused=wm->focused==source;
		strip_focus_view(wm,source);
		if(already_focused) present_stage(wm);
		nested_seat_refresh(wm->seat);
	} else {
		arrange_flow(wm,source);
		strip_focus_view(wm,source);
		if(wm->strip_motion) wm->group_join=(__typeof__(wm->group_join)){source->id,-1,from};
	}
	wlr_output_schedule_frame(wm->output);
}

enum { BEAM_BEGIN, BEAM_MOVE, BEAM_DROP, BEAM_CANCEL, BEAM_LIFT=8 };
static void beam_send(struct nested *wm,uint32_t phase,uint64_t id,double x,double y) {
	if(!wm->map_pointer ||
		wl_resource_get_version(wm->map_pointer)<MOTHERSHIP_MAP_POINTER_V1_BEAM_SINCE_VERSION) return;
	mothership_map_pointer_v1_send_beam(wm->map_pointer,phase,id>>32,(uint32_t)id,(int)round(x),(int)round(y));
}

static void group_drag_update(struct nested *wm,enum nested_move_phase phase,double x,double y) {
	struct xdg_view *source=find_view(wm,wm->group_grab.source);
	if(phase==MOVE_CANCEL || !source || source->slot<0 || (!group_large(wm) && !wm->group_grab.tray)) {
		if(wm->group_grab.beam) beam_send(wm,BEAM_CANCEL,wm->group_grab.source,x,y);
		wm->group_grab.source=wm->group_grab.target=0;wm->group_grab.beam=false;wm->drag=NULL;
		if(wm->output)wlr_output_schedule_frame(wm->output);
		return;
	}
	wm->group_grab.x=x;wm->group_grab.y=y;
	wm->group_grab.moved|=hypot(x-wm->group_grab.from_x,y-wm->group_grab.from_y)>=6.;
	// Tractor beam: a tray card lifted above the rail targets another Node.
	bool beam=wm->group_grab.tray && wm->group_grab.moved && y<wm->node_strip.y-BEAM_LIFT;
	if(beam!=wm->group_grab.beam) {
		wm->group_grab.beam=beam;
		if(beam) {
			int i=source->slot;
			beam_send(wm,BEAM_BEGIN,source->id,(wm->preview[i].painted_left+wm->preview[i].painted_right)/2.,
				wm->preview[i].painted_top);
		} else beam_send(wm,BEAM_CANCEL,source->id,x,y);
	}
	if(beam) {
		if(phase==MOVE_DROP) {
			// The shell decides the destination and submits the assignment.
			beam_send(wm,BEAM_DROP,source->id,x,y);
			wm->group_grab.source=wm->group_grab.target=0;wm->group_grab.beam=false;wm->drag=NULL;
		} else beam_send(wm,BEAM_MOVE,source->id,x,y);
		wm->group_grab.target=0;
		wlr_output_schedule_frame(wm->output);
		return;
	}
	struct xdg_view *target=NULL;
	if(wm->group_grab.moved && group_large(wm)) {
		if(wm->group_grab.tray) target=strip_hit(wm,x,y);
		else {
			struct nested_hit hit=hit_view(wm,x,y);
			for(int i=0;i<wm->visible_count;i++)
				if(wm->slots[i]->toplevel->base->surface==hit.toplevel) {target=wm->slots[i];break;}
		}
		if(!group_can_join(wm,source,target))target=NULL;
	}
	uint64_t id=target?target->id:0;
	int64_t now=group_ms();
	if(id!=wm->group_grab.target) {
		wm->group_grab.target=id;
		wm->group_grab.since=now;
	}
	if(phase==MOVE_DROP) {
		bool moved=wm->group_grab.moved;
		bool tray=wm->group_grab.tray;
		bool join=target && now-wm->group_grab.since>=300;
		struct rect from=source->placement;
		if(tray || from.width<=0 || from.height<=0) {
			int ow=wm->parent_window.width,oh=wm->parent_window.height;
			from.width=clamp_int(ow/5,120,380);from.height=clamp_int(oh/5,80,230);
			from.x=clamp_int(x-from.width/2,0,ow-from.width);
			from.y=clamp_int(y-from.height/2,0,oh-from.height);
		} else {
			from.x+=x-wm->group_grab.from_x;
			from.y+=y-wm->group_grab.from_y;
		}
		wm->group_grab.source=wm->group_grab.target=0;
		wm->drag=NULL;
		if(join)group_join_views(wm,source,target,from);
		else if(!moved && wm->focused!=source) strip_focus_view(wm,source);
	}
	wlr_output_schedule_frame(wm->output);
}

static void group_frame(struct nested *wm,struct timespec now) {
	if(!wm->group_grab.source && !wm->group_join.id &&
		!wm->group_ghost->node.enabled && !wm->group_marks[0]->node.enabled) return;
	struct wlr_box lane={0,0,wm->parent_window.width,wm->parent_window.height};
	int64_t ms=now.tv_sec*1000LL+now.tv_nsec/1000000;
	bool ghost=false,outline=false,advance=false;
	struct xdg_view *drag=find_view(wm,wm->group_grab.source);
	struct xdg_view *joining=find_view(wm,wm->group_join.id);
	if((group_large(wm) || wm->group_grab.beam) && !wm->revealing && drag && drag->slot>=0 && wm->group_grab.moved) {
		if(wm->group_grab.tray) {
			const int w=66,h=38;
			int x=clamp_int(wm->group_grab.x+12,0,lane.width-w);
			int y=clamp_int(wm->group_grab.y-h-12,0,lane.height-h);
			ghost=strip_image(wm->group_ghost,drag,&lane,x,y,w,h);
			if(ghost)wlr_scene_buffer_set_opacity(wm->group_ghost,.82);
		}
		struct xdg_view *target=find_view(wm,wm->group_grab.target);
		if(group_can_join(wm,drag,target)) {
			bool ready=ms-wm->group_grab.since>=300;
			struct rect r=target->placement;
			if(wm->group_grab.tray) {
				int i=target->slot;
				r=(struct rect){wm->preview[i].painted_left,wm->preview[i].painted_top,
					wm->preview[i].painted_right-wm->preview[i].painted_left,wm->preview[i].painted_height};
			}
			outline=r.width>0 && r.height>0;
			if(outline) strip_focus_marks(wm->group_marks,&lane,r.x,r.y,r.width,r.height,ready?1.:.3);
			advance=!ready;
		}
	} else if(group_large(wm) && !wm->revealing && joining && joining->slot>=0 && group_visible(joining)) {
		if(wm->group_join.started<0)wm->group_join.started=ms;
		double t=fmin(1.,(ms-wm->group_join.started)/235.);
		const double e=stage_ease(t);
		struct rect a=wm->group_join.from,b=joining->placement;
		struct rect r={preview_mix(a.x,b.x,e),preview_mix(a.y,b.y,e),
			preview_mix(a.width,b.width,e),preview_mix(a.height,b.height,e)};
		if(t<1 && r.y>=0 && r.y+r.height<=lane.height) {
			ghost=strip_image(wm->group_ghost,joining,&lane,r.x,r.y,r.width,r.height);
			if(ghost)wlr_scene_buffer_set_opacity(wm->group_ghost,.90);
		}
		wlr_scene_node_set_enabled(&joining->frame->node,!ghost);
		if(ghost)advance=true;
		else {
			wm->group_join.id=0;
			nested_seat_refresh(wm->seat);
		}
	} else {
		if(joining && joining->slot>=0 && is_flow(wm))present_flow(wm);
		wm->group_join.id=0;
	}
	if(!ghost) {
		wlr_scene_node_set_enabled(&wm->group_ghost->node,false);
		if(wm->group_ghost->buffer)wlr_scene_buffer_set_buffer(wm->group_ghost,NULL);
	}
	if(!outline)for(int i=0;i<9;i++)wlr_scene_node_set_enabled(&wm->group_marks[i]->node,false);
	if(advance)wlr_output_schedule_frame(wm->output);
}

static struct xdg_view *picker_view(struct nested *wm,uint64_t id) {
	if(picker_scope(wm) && id)
		for(int i=0;i<wm->visible_count;i++)
			if(wm->slots[i] && wm->slots[i]->id==id && wm->slots[i]->mapped) return wm->slots[i];
	return NULL;
}
static uint64_t recent_rank(struct nested *wm,struct xdg_view *v) {
	return v==wm->focused?UINT64_MAX:wm->home_active?v->home_focused_at:v->focused_at;
}
static int picker_snapshot(struct nested *wm,uint64_t ids[NODE_WINDOWS]) {
	struct xdg_view *order[NODE_WINDOWS];
	int n=0;
	for(int i=0;i<wm->visible_count;i++) {
		struct xdg_view *v=wm->slots[i];
		if(!v || !v->mapped) continue;
		int j=n++;
		while(j>0 && recent_rank(wm,order[j-1])<recent_rank(wm,v)) {
			order[j]=order[j-1];j--;
		}
		order[j]=v;
	}
	for(int i=0;i<n;i++) ids[i]=order[i]->id;
	return n;
}
static void picker_hide(struct nested *wm) {
	if(wm->picker_count && picker_protocol(wm))
		mothership_map_pointer_v1_send_picker_end(wm->map_pointer,wm->picker_serial);
	bool had_picker=wm->picker_count || wm->picker_keyboard;
	wm->picker_count=0;
	memset(wm->picker_tiles,0,sizeof(wm->picker_tiles));
	picker_focus_clear(wm);
	picker_draw(wm);
	if(had_picker && wm->output) wlr_output_schedule_frame(wm->output);
	wm->picker_keyboard=wm->picker_rail_sliding=false;
	wm->picker_mark_id=0;
}
static void picker_slide_to(struct nested *wm,uint64_t id) {
	struct xdg_view *v=picker_view(wm,id);
	if(!v) return;
	if(!wm->picker_keyboard) {
		picker_hide(wm);
		wm->picker_keyboard=true;
		wm->picker_rail_before=wm->strip_offset;
	}
	wm->picker_mark_id=id;
	wm->strip_hover=0;
	wm->picker_rail_from=wm->strip_offset;
	wm->picker_rail_to=strip_goal(wm,v,wm->strip_offset);
	wm->picker_rail_started=-1;
	wm->picker_rail_sliding=wm->picker_rail_to!=wm->strip_offset;
	wlr_output_schedule_frame(wm->output);
}

// This popup belongs exclusively to the +N button. Alt+Tab never maps it.
static void picker_show(struct nested *wm,const uint64_t *ids,int count) {
	if(!picker_protocol(wm) || !count) return;
	picker_hide(wm);
	if(++wm->picker_serial==0) ++wm->picker_serial;
	mothership_map_pointer_v1_send_picker_begin(wm->map_pointer,wm->picker_serial,false);
	bool overflow_only=wl_resource_get_version(wm->map_pointer)>=12;
	for(int i=0;i<count;i++) {
		struct xdg_view *v=picker_view(wm,ids[i]);
		if(!v || (overflow_only && preview_on_panel(wm,v))) continue;
		wm->picker_ids[wm->picker_count++]=ids[i];
		char title[513],app[257];
		snprintf(title,sizeof(title),"%s",v->toplevel->title?v->toplevel->title:"");
		snprintf(app,sizeof(app),"%s",v->toplevel->app_id?v->toplevel->app_id:"");
		mothership_map_pointer_v1_send_picker_row(wm->map_pointer,ids[i]>>32,ids[i],title,app);
	}
	uint64_t selected=wm->picker_count?wm->picker_ids[0]:0;
	mothership_map_pointer_v1_send_picker_highlight(wm->map_pointer,wm->picker_serial,selected>>32,selected);
}
static bool cycle_window(void *data,enum cycle_phase phase,int steps) {
	struct nested *wm=data;
	if(phase==CYCLE_CANCEL) {
		wm->cycle_count=0;
		if(wm->picker_keyboard) {
			wm->strip_offset=fmin(wm->picker_rail_before,strip_limit(wm));
			picker_hide(wm);
		}
		return true;
	}
	if(phase==CYCLE_BEGIN) {
		if(!picker_scope(wm)) return false;
		wm->cycle_count=picker_snapshot(wm,wm->cycle_ids);
		wm->cycle_has_origin=wm->focused!=NULL;
		return wm->cycle_count>1 || (wm->cycle_count==1 && !wm->cycle_has_origin);
	}
	int n=wm->cycle_count;
	int index=steps;
	// With no focused app, the first forward step selects the first
	// recent entry, not the second. Reverse starts at the other end.
	if(!wm->cycle_has_origin && index>0) index--;
	uint64_t id=n?wm->cycle_ids[(index%n+n)%n]:0;
	if(phase==CYCLE_PREVIEW && n>1 && picker_protocol(wm)) {
		picker_slide_to(wm,id);
	} else if(phase==CYCLE_COMMIT) {
		wm->cycle_count=0;
		struct xdg_view *target=picker_view(wm,id);
		if(wm->picker_keyboard) {
			wm->strip_offset=target?strip_goal(wm,target,wm->strip_offset):
				fmin(wm->picker_rail_before,strip_limit(wm));
			picker_hide(wm);
		}
		if(target && target!=wm->focused) strip_focus_view(wm,target);
	}
	return n>1;
}
static void map_pointer_window_picker(struct wl_client *client,struct wl_resource *resource) {
	(void)client;
	struct nested *wm=wl_resource_get_user_data(resource);
	if(wm->map_pointer!=resource || wm->picker_keyboard || !picker_scope(wm)) return;
	if(wm->reorder.active) {
		// Snapshot overflow after the tray settles, so its membership cannot
		// change under the newly opened picker.
		flow_reorder_finish(wm);
		struct timespec now;
		if(clock_gettime(CLOCK_MONOTONIC,&now)==0) strip_draw(wm,now);
		if(wl_resource_get_version(resource)>=12 && wm->last_window_overflow==0) return;
	}
	uint64_t ids[NODE_WINDOWS];
	int n=picker_snapshot(wm,ids);
	if(n>1) {
		wm->cycle_count=0;
		picker_show(wm,ids,n);
	}
}
static void map_pointer_window_pick(struct wl_client *client,struct wl_resource *resource,
		uint32_t serial,uint32_t hi,uint32_t lo) {
	(void)client;
	struct nested *wm=wl_resource_get_user_data(resource);
	if(wm->map_pointer!=resource || serial!=wm->picker_serial || (!wm->picker_count && !wm->picker_keyboard)) return;
	uint64_t id=((uint64_t)hi<<32)|lo;
	bool listed=false;
	for(int i=0;i<wm->picker_count;i++) listed|=wm->picker_ids[i]==id;
	struct xdg_view *target=listed?picker_view(wm,id):NULL;
	picker_hide(wm);
	wm->cycle_count=0;
	if(target && target!=wm->focused) strip_focus_view(wm,target);
}

static bool scroll_flow(void *data,double x,double y,unsigned axis,double delta) {
	struct nested *wm=data;
	if(pin_scroll(wm,x,y,axis,delta)) return true;
	if (!is_flow(wm) || wm->revealing || !delta || (wm->focused && wm->focused->maximized)) return false;
	if(wm->maximize.view) return true;
	if (axis==WL_POINTER_AXIS_HORIZONTAL_SCROLL &&
		flow_vertical(wm->layout,wm->parent_window.width,wm->parent_window.height)) return false;
	flow_reorder_finish(wm);
	wm->flow_sliding=false;
	int move=(int)fmax(-200,fmin(200,delta*3));
	int next=clamp_int(wm->flow_offset-move,wm->flow_low,wm->flow_high);
	if (next!=wm->flow_offset) {
		wm->flow_offset=next;
		present_flow(wm);
		nested_seat_refresh(wm->seat);
	}
	return true;
}

static void mode_slide_position(struct nested *wm,int x,int y) {
	struct xdg_view *v=wm->mode_slide_view;
	if(!v)return;
	v->placement.x+=x-wm->mode_slide_x;
	v->placement.y+=y-wm->mode_slide_y;
	wm->mode_slide_x=x;wm->mode_slide_y=y;
	wlr_scene_node_set_position(&v->frame->node,v->placement.x,v->placement.y);
}
static void mode_slide_finish(struct nested *wm) {
	if(!wm->mode_slide_view)return;
	mode_slide_position(wm,0,0);
	wm->mode_slide_view=NULL;
}
static void mode_slide_begin(struct nested *wm,struct xdg_view *v,struct rect from) {
	if(!wm->strip_motion || !v->frame->node.enabled)return;
	int x=from.x-v->placement.x,y=from.y-v->placement.y;
	if(!x && !y)return;
	wm->mode_slide_view=v;
	wm->mode_slide_x=wm->mode_slide_y=0;
	wm->mode_slide_from_x=x;wm->mode_slide_from_y=y;
	wm->mode_slide_start=-1;
	mode_slide_position(wm,x,y);
	wlr_output_schedule_frame(wm->output);
}
static void mode_slide_frame(struct nested *wm,struct timespec now) {
	if(!wm->mode_slide_view)return;
	if(!is_flow(wm) || wm->revealing || !wm->mode_slide_view->frame->node.enabled) {
		mode_slide_finish(wm);return;
	}
	int64_t ms=now.tv_sec*1000LL+now.tv_nsec/1000000;
	if(wm->mode_slide_start<0)wm->mode_slide_start=ms;
	double t=fmin(1,(ms-wm->mode_slide_start)/190.);
	double delay=1-t*t*(3-2*t);
	mode_slide_position(wm,(int)round(wm->mode_slide_from_x*delay),
		(int)round(wm->mode_slide_from_y*delay));
	if(t<1)wlr_output_schedule_frame(wm->output);
	else {wm->mode_slide_view=NULL;nested_seat_refresh(wm->seat);}
}

static void maximize_finish(struct nested *wm) {
	struct xdg_view *view=wm->maximize.view;
	if(!view)return;
	wm->maximize.view=NULL;
	stage_scale_clear(&wm->maximize.sprite);
	if(is_flow(wm) && view->slot>=0 && view->mapped && !view->minimized) {
		wlr_scene_node_set_enabled(&view->frame->node,true);
		apply_view_clip(view);
		nested_seat_refresh(wm->seat);
	}
}

static void maximize_frame(struct nested *wm,struct timespec now) {
	struct xdg_view *view=wm->maximize.view;
	if(!view)return;
	int64_t ms=now.tv_sec*1000LL+now.tv_nsec/1000000;
	if(wm->maximize.start<0)wm->maximize.start=ms;
	double t=(ms-wm->maximize.start)/200.;
	if(t>=1 || !wm->strip_motion || !is_flow(wm) || wm->revealing) {
		maximize_finish(wm);
		return;
	}
	struct wlr_box a=wm->maximize.from;
	struct rect b=view->placement;
	double e=stage_ease(t);
	wm->maximize.painted=(struct wlr_box){
		preview_mix(a.x,b.x,e),preview_mix(a.y,b.y,e),
		preview_mix(a.width,b.width,e),preview_mix(a.height,b.height,e),
	};
	stage_scale_draw_box(&wm->maximize.sprite,wm->maximize.painted);
	// The hidden live surface can still need a callback before it redraws.
	wlr_surface_for_each_surface(view->toplevel->base->surface,stage_send_frame,&now);
	wlr_output_schedule_frame(wm->output);
}

static void request_window_close(struct xdg_view *view) {
	wlr_xdg_toplevel_send_close(view->toplevel);
}

static bool close_focused_window(void *data,struct wlr_surface *surface) {
	struct nested *wm=data;
	if(wm->revealing || (wm->panel && wm->panel->wants_keyboard) || wm->picker_keyboard ||
		(!wm->default_node_active && (!wm->domain_active || wm->home_active))) return false;
	struct wlr_surface *target=dialog_keyboard_target(wm,surface);
	if (target!=surface) {
		wlr_xdg_toplevel_send_close(wlr_xdg_toplevel_try_from_wlr_surface(target));
		return true;
	}
	for(int i=0;i<wm->visible_count;i++) {
		struct xdg_view *view=wm->slots[i];
		if(view && view->toplevel->base->surface==surface && surface->mapped) {
			request_window_close(view);
			return true;
		}
	}
	return false;
}

static struct xdg_view *reorder_neighbor(struct nested *wm,struct xdg_view *view,
		bool vertical,int direction) {
	struct xdg_view *group[FLOW_GROUP_MAX];
	int count=group_members(wm,view,group,true);
	if(count>=2) {
		struct rect tiles[FLOW_GROUP_MAX];
		int focused=-1;
		for(int i=0;i<count;i++) {
			tiles[i]=group[i]->flow_base;
			if(group[i]==view) focused=i;
		}
		int neighbor=flow_group_neighbor(tiles,count,focused,vertical,direction);
		return neighbor>=0?group[neighbor]:NULL;
	}
	if(vertical!=flow_vertical(wm->layout,wm->parent_window.width,wm->parent_window.height)) return NULL;
	struct flow_order_entry entries[NODE_WINDOWS];
	for(int i=0;i<wm->visible_count;i++) {
		struct xdg_view *v=wm->slots[i];
		bool eligible=group_visible(v);
		// Only the first member in slot order represents a tiled group stop.
		if(eligible && v->group_id && group_large(wm))
			for(int j=0;j<i;j++)
				if(entries[j].eligible && entries[j].group==v->group_id) eligible=false;
		entries[i]=(struct flow_order_entry){.eligible=eligible,.group=v->group_id};
	}
	int neighbor=flow_order_neighbor(entries,wm->visible_count,view->slot,direction);
	return neighbor>=0?wm->slots[neighbor]:NULL;
}

static void reveal_reordered_view(struct nested *wm,struct xdg_view *view,bool vertical) {
	struct rect unit=view->flow_unit;
	int length=vertical?unit.height:unit.width;
	int extent=vertical?shell_node_dock_top(wm->parent_window.height):wm->parent_window.width;
	int start=(vertical?unit.y:unit.x)+wm->flow_offset;
	if(length<=extent) {
		if(start<0) wm->flow_offset-=start;
		else if(start+length>extent) wm->flow_offset-=start+length-extent;
	} else if(start>=extent || start+length<=0) {
		wm->flow_offset=flow_center(wm,view);
	}
	wm->flow_offset=clamp_int(wm->flow_offset,wm->flow_low,wm->flow_high);
}

static void swap_flow_views(struct nested *wm,struct xdg_view *view,struct xdg_view *neighbor) {
	struct rect from[NODE_WINDOWS];
	for(int i=0;i<wm->visible_count;i++) from[i]=wm->slots[i]->placement;
	double flow_from=wm->flow_offset, strip_from=wm->strip_offset;
	if(wm->reorder.active) {
		flow_from+=(wm->reorder.flow_from-flow_from)*wm->reorder.remaining;
		strip_from+=(wm->reorder.strip_from-strip_from)*wm->reorder.remaining;
	}
	int a=view->slot,b=neighbor->slot;
	uint64_t order=*layout_order(view);
	*layout_order(view)=*layout_order(neighbor);
	*layout_order(neighbor)=order;
	if(view->group_id) {
		order=view->group_order;
		view->group_order=neighbor->group_order;
		neighbor->group_order=order;
	}
	wm->slots[a]=neighbor;neighbor->slot=a;
	wm->slots[b]=view;view->slot=b;
	struct rect old=from[a];from[a]=from[b];from[b]=old;
	// Keep each scene tile and its hover/focus state with its window.
	__typeof__(wm->preview[0]) tile=wm->preview[a];
	wm->preview[a]=wm->preview[b];wm->preview[b]=tile;
	for(int i=0;i<NODE_WINDOWS;i++)
		wlr_scene_node_raise_to_top(&wm->preview[i].ring->node.parent->node);

	bool vertical=flow_vertical(wm->layout,wm->parent_window.width,wm->parent_window.height);
	arrange_flow(wm,NULL);
	reveal_reordered_view(wm,view,vertical);
	wm->strip_offset=strip_goal(wm,view,wm->strip_offset);
	if(wm->strip_motion && node_strip_active(wm)) {
		wm->reorder=(__typeof__(wm->reorder)){
			.active=true,.start=-1,.remaining=1,.flow_from=flow_from,.strip_from=strip_from,
		};
		for(int i=0;i<wm->visible_count;i++) {
			struct xdg_view *v=wm->slots[i];
			v->reorder_x=v->reorder_y=0;
			if(group_visible(v)) {
				struct rect to=v->flow_base;
				if(vertical) to.y+=wm->flow_offset;
				else to.x+=wm->flow_offset;
				// A different-sized group tile uses its final size once and
				// slides from the center of the previous tile.
				v->reorder_x=from[i].x-to.x+(from[i].width-to.width)/2;
				v->reorder_y=from[i].y-to.y+(from[i].height-to.height)/2;
			}
			wm->preview[i].reorder_from=wm->preview[i].id==v->id?wm->preview[i].painted:(struct wlr_box){0};
		}
	}
	present_flow(wm);
	nested_seat_refresh(wm->seat);
	wlr_output_schedule_frame(wm->output);
}

static bool reorder_focused_window(void *data,struct wlr_surface *surface,uint32_t key) {
	struct nested *wm=data;
	struct xdg_view *view=wm->focused;
	if(!picker_scope(wm) || !is_flow(wm) || !view || view->toplevel->base->surface!=surface ||
		(wm->panel && wm->panel->wants_keyboard) || wm->picker_count || wm->picker_keyboard) return false;
	if(!wm->output || !group_visible(view) || wm->drag || wm->group_grab.source ||
		wm->group_join.id || wm->maximize.view || wm->mode_slide_view) return true;
	for(int i=0;i<FLOW_GROUP_MAX;i++) if(wm->tray_flight[i].id) return true;
	bool vertical=key==KEY_UP || key==KEY_DOWN;
	int direction=key==KEY_UP || key==KEY_LEFT?-1:1;
	struct xdg_view *neighbor=reorder_neighbor(wm,view,vertical,direction);
	if(neighbor) swap_flow_views(wm,view,neighbor);
	return true;
}

// The shell owns Node numbers and the assignment request; the WM only
// reports which focused app window the chord addressed.
static bool move_focused_window(void *data,struct wlr_surface *surface,uint32_t slot) {
	struct nested *wm=data;
	struct xdg_view *view=wm->focused;
	if(!node_view(wm) || wm->revealing || !view || view->slot<0 ||
		view->toplevel->base->surface!=surface || !wm->map_pointer ||
		wl_resource_get_version(wm->map_pointer)<MOTHERSHIP_MAP_POINTER_V1_NODE_MOVE_SINCE_VERSION ||
		(wm->panel && wm->panel->wants_keyboard)) return false;
	mothership_map_pointer_v1_send_node_move(wm->map_pointer,slot,view->id>>32,(uint32_t)view->id);
	return true;
}

static void map_pointer_window_list(struct wl_client *client,struct wl_resource *resource,uint32_t serial) {
	(void)client;
	struct nested *wm=wl_resource_get_user_data(resource);
	if(resource!=wm->map_pointer) return;
	struct xdg_view *v;
	wl_list_for_each(v,&wm->views,link) {
		if(!v->mapped || !v->tree) continue;
		const char *title=v->toplevel->title?v->toplevel->title:"";
		const char *app=v->toplevel->app_id?v->toplevel->app_id:"";
		mothership_map_pointer_v1_send_window_list_row(resource,serial,v->id>>32,(uint32_t)v->id,title,app);
	}
	mothership_map_pointer_v1_send_window_list_end(resource,serial);
}

static void request_view_minimize(struct xdg_view *,bool);
static void group_show_settings(struct nested *,struct xdg_view *,bool);
static void set_view_maximized(struct xdg_view *v,bool maximized) {
	struct nested *wm=v->wm;
	if(wm->cleaning || wm->revealing || !is_flow(wm) || v->slot<0 ||
			!v->mapped || v->minimized || v->maximized==maximized ||
			(!v->frame->node.enabled && wm->maximize.view!=v)) return;
	struct xdg_view *group[FLOW_GROUP_MAX];
	if(group_members(wm,v,group,false)>=2)return;

	if(wm->maximize.view!=v) {
		maximize_finish(wm);
		if(wm->strip_motion && flight_capture(wm,&wm->maximize.sprite,v,wm->maximize_overlay)) {
			wm->maximize.view=v;
			wm->maximize.button=window_chrome_maximize_box(v->chrome);
			wm->maximize.painted=wm->maximize.sprite.full;
		}
	}
	mode_slide_finish(wm);
	// Keep the existing strip offset when restoring a strip member.
	wm->flow_sliding=false;
	v->maximized=maximized;
	struct wlr_surface *surface=v->toplevel->base->surface;
	remember_focus(wm,surface);
	if(wm->maximize.view==v) {
		wm->maximize.from=wm->maximize.painted;
		wm->maximize.start=-1;
		stage_scale_draw_box(&wm->maximize.sprite,wm->maximize.painted);
	}
	wlr_output_schedule_frame(wm->output);
	nested_seat_switch(wm->seat,surface,false);
}

static void activate_chrome(void *data,struct nested_hit hit) {
	struct nested *wm=data;
	struct xdg_view *v=hit.decoration;
	if (!v || v->slot<0 || !view_has_frame(wm) || wm->revealing) return;
	if(hit.chrome_action==WINDOW_CHROME_GROUP_LAYOUT) {group_show_settings(wm,v,true);return;}
	if(hit.chrome_action==WINDOW_CHROME_GROUP_MINIMIZE) {request_view_minimize(v,true);return;}
	if(hit.chrome_action==WINDOW_CHROME_CLOSE) {request_window_close(v);return;}
	if(hit.chrome_action==WINDOW_CHROME_MINIMIZE) {request_view_minimize(v,false);return;}
	if(hit.chrome_action==WINDOW_CHROME_MAXIMIZE) {set_view_maximized(v,!v->maximized);return;}

	bool slide=hit.chrome_action==WINDOW_CHROME_FLOAT && is_flow(wm) && !v->maximized;
	struct rect previous=v->placement;
	if(slide) {
		mode_slide_finish(wm);
		v->flowed_out=!v->flowed_out;
		if(v->flowed_out && !v->floating.width)
			v->floating=float_rect(v->placement,wm->parent_window.width,wm->parent_window.height);
		arrange_flow(wm,v->flowed_out?NULL:v);
	}
	remember_focus(wm,hit.toplevel);
	if(slide) {
		wm->flow_sliding=false;
		mode_slide_begin(wm,v,previous);
	}
	nested_seat_switch(wm->seat,hit.toplevel,false);
}

static struct wlr_surface *node_panel_key(void *data,uint32_t key) {
	struct nested *wm=data;
	if(!wm->map_pointer || wm->revealing || (!wm->default_node_active && (!wm->domain_active || wm->home_active)) ||
		!wm->panel || !wm->panel->layer->surface->mapped ||
		wl_resource_get_version(wm->map_pointer)<MOTHERSHIP_MAP_POINTER_V1_NODE_APPS_SINCE_VERSION) return NULL;
	if(key) {
		if(wm->panel->wants_keyboard)return NULL;
		if(key==KEY_S && !wm->default_node_active && !wm->overview_active &&
			wl_resource_get_version(wm->map_pointer)>=MOTHERSHIP_MAP_POINTER_V1_NODE_SAVE_LOADOUT_SINCE_VERSION)
			mothership_map_pointer_v1_send_node_save_loadout(wm->map_pointer);
		else if(key==KEY_A) mothership_map_pointer_v1_send_node_apps(wm->map_pointer);
		else if(key==KEY_N &&
			wl_resource_get_version(wm->map_pointer)>=MOTHERSHIP_MAP_POINTER_V1_NODE_NOTIFICATIONS_SINCE_VERSION)
			mothership_map_pointer_v1_send_node_notifications(wm->map_pointer);
		else if(key==KEY_SLASH &&
			wl_resource_get_version(wm->map_pointer)>=MOTHERSHIP_MAP_POINTER_V1_NODE_SLASH_SINCE_VERSION)
			mothership_map_pointer_v1_send_node_slash(wm->map_pointer);
		else if(key==KEY_COMMA &&
			wl_resource_get_version(wm->map_pointer)>=MOTHERSHIP_MAP_POINTER_V1_NODE_SETTINGS_SINCE_VERSION)
			mothership_map_pointer_v1_send_node_settings(wm->map_pointer);
		else return NULL;
	}
	return wm->panel->layer->surface;
}

static bool node_number(void *data,uint32_t number) {
	struct nested *wm=data;
	if(!wm->domain_active || !wm->map_pointer ||
		wl_resource_get_version(wm->map_pointer)<MOTHERSHIP_MAP_POINTER_V1_NODE_NUMBER_SINCE_VERSION) return false;
	mothership_map_pointer_v1_send_node_number(wm->map_pointer,number);
	return true;
}

static bool node_switch(void *data, enum node_switch_action action) {
	struct nested *wm = data;
	if (!wm->map_pointer ||
			wl_resource_get_version(wm->map_pointer) < MOTHERSHIP_MAP_POINTER_V1_NODE_SWITCH_STEP_SINCE_VERSION)
		return false;
	if (action == NODE_SWITCH_CANCEL) {
		mothership_map_pointer_v1_send_node_switch_finish(wm->map_pointer, 0);
		return true;
	}
	bool ready = !wm->cleaning && wm->domain_fd >= 0 && !wm->revealing &&
		!wm->screenshot.opening && !wm->screenshot.pending && !wm->screenshot.active &&
		!wm->picker_count && !wm->picker_keyboard && !wm->map_aim_enabled &&
		wm->panel && wm->panel->layer->surface->mapped &&
		(!wm->panel->wants_keyboard || wm->overview_active);
	if (action == NODE_SWITCH_COMMIT)
		mothership_map_pointer_v1_send_node_switch_finish(wm->map_pointer, ready);
	else if (ready)
		mothership_map_pointer_v1_send_node_switch_step(wm->map_pointer, action == NODE_SWITCH_NEXT ? 1 : -1);
	return ready;
}

static void home_key_tap(void *data) {
	struct nested *wm=data;
	bool map=wm->overview_active && wm->map_zoom_enabled && wm->map_hud &&
		wm->map_hud->layer->surface->mapped;
	if(!wm->map_pointer || wm->revealing ||
		(!map && !wm->default_node_active && (!wm->domain_active || wm->home_active))) return;
	if (wl_resource_get_version(wm->map_pointer)>=MOTHERSHIP_MAP_POINTER_V1_HOME_TAP_SINCE_VERSION)
		mothership_map_pointer_v1_send_home_tap(wm->map_pointer);
}

static void select_projection(struct nested *wm, bool keep_panel) {
	wm->drag = NULL;
	struct xdg_view *v;
	wl_list_for_each(v,&wm->views,link) v->focus_on_show=false;
	position_panel(wm->panel);
	nested_seat_switch(wm->seat, view_target(wm), keep_panel);
	refresh_blank_shell(wm);
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
	if(strcmp(op,"shortcuts")==0) {
		uint64_t close=domain_number(strtok_r(NULL," ",&tail));
		uint64_t apps=domain_number(strtok_r(NULL," ",&tail));
		uint64_t settings=domain_number(strtok_r(NULL," ",&tail));
		uint64_t node=domain_number(strtok_r(NULL," ",&tail));
		if(strtok_r(NULL," ",&tail) || !nested_seat_set_shortcuts(wm->seat,close,apps,settings,node)) {
			domain_disconnect(wm);
			return 0;
		}
		// The focused frame names the close key in its footer.
		if(wm->focused && wm->focused->slot>=0) place_view(wm->focused);
		domain_send(wm,"applied",serial);
		return 0;
	}
	if (strcmp(op,"prepare") == 0 && wm->overview_active && strtok_r(NULL," ",&tail)==NULL) {
		// Prepare confirms a live Overview without changing projection, app
		// layout or input. Desktopd resolves live members again at reveal.
		domain_send(wm,"applied",serial);
		return 0;
	}
	if (strcmp(op,"reveal")==0) {
		struct domain_member members[NODE_WINDOWS]={0};
		enum node_layout mode;
		if(!wm->overview_active || !read_layout(strtok_r(NULL," ",&tail),&mode) ||
				!read_members(&tail,members)) {
			domain_disconnect(wm);
			return 0;
		}
		wm->strip_offset=0;
		wm->cycle_count=0;picker_hide(wm);
		wm->default_node_active=wm->home_active=wm->overview_active=false;
		wm->domain_active=true;
		wm->layout=mode;
		wm->revealing=true;
		wm->reveal_start=0;
		wm->reveal_ack=serial;
		wm->aperture=(struct wlr_box){wm->parent_window.width/2,wm->parent_window.height/2,1,1};
		project_named(wm,members,true);
		wm->drag=NULL;
		nested_seat_switch(wm->seat,NULL,false);
		refresh_blank_shell(wm);
		position_panel(wm->panel);
		wlr_output_schedule_frame(wm->output);
		return 0;
	}
	struct xdg_view *preferred=NULL;
	uint64_t preferred_id=0;
	if(wm->revealing && strcmp(op,"assigned")!=0 && strcmp(op,"inbox")!=0) {
		wm->revealing=false;
		for(int i=0;i<wm->visible_count;i++) apply_view_clip(wm->slots[i]);
	}
	if (strcmp(op, "home") == 0 || strcmp(op, "home_node") == 0 || strcmp(op, "home_node_window") == 0) {
		wm->strip_offset=0;
		wm->cycle_count=0;picker_hide(wm);
		bool with_target=strcmp(op,"home_node_window")==0;
		bool default_node=with_target || strcmp(op,"home_node")==0;
		enum node_layout mode=FLOW;
		char *layout=strtok_r(NULL," ",&tail);
		uint64_t target=with_target?domain_number(strtok_r(NULL," ",&tail)):0;
		// An earlier desktopd sends only the Home projection ID.
		if ((layout && (!default_node || !read_layout(layout,&mode) ||
				(!flow_layout(mode) && mode!=STAGE))) || (with_target && (!layout || !target)) ||
				strtok_r(NULL," ",&tail)) {
			domain_disconnect(wm);
			return 0;
		}
		preferred=target?prefer_window(wm,target,NULL):NULL;
		clear_slots(wm);
		wm->default_node_active=default_node;
		wm->home_active = true;
		wm->overview_active = false;
		wm->layout=mode;
		project_home(wm,true);
		select_projection(wm, false);
		focus_preferred(wm,preferred);
		domain_send(wm, "applied", serial);
		return 0;
	}
	if (strcmp(op, "inbox") == 0) {
		struct xdg_view *views[NODE_WINDOWS];
		struct domain_member members[NODE_WINDOWS]={0};
		bool project=false;
		int count=0;
		for(char *token; (token=strtok_r(NULL," ",&tail))!=NULL;) {
			// A single move out of the shown Node carries its remaining members.
			if(strcmp(token,"project")==0) {
				if(count!=1 || !named_view(wm) || !read_members(&tail,members)) { domain_disconnect(wm); return 0; }
				project=true;
				break;
			}
			uint64_t id=domain_number(token);
			if(!id || count==NODE_WINDOWS) { domain_disconnect(wm); return 0; }
			views[count++]=find_view(wm,id);
		}
		if(!count) { domain_disconnect(wm); return 0; }
		// A view that closed while the request was in flight needs no move.
		for(int i=0;i<count;i++) if(views[i] && views[i]->assigned) {
			views[i]->assigned=false;
			reset_node_geometry(views[i]);
			pin_metadata_changed(views[i]);
		}
		if(project) refresh_named(wm,members);
		if(wm->home_active) refresh_home(wm);
		domain_send(wm,"applied",serial);
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
			view->home_order = 0;
			pin_metadata_changed(view);
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
		wm->strip_offset=0;
		wm->cycle_count=0;picker_hide(wm);
		wm->default_node_active=wm->home_active = false;
		wm->overview_active = true;
	} else if (strcmp(op, "layout") == 0 && node_view(wm)) {
		if (!read_layout(strtok_r(NULL, " ", &tail), &wm->layout) ||
				strtok_r(NULL, " ", &tail) != NULL) {
			domain_disconnect(wm);
			return 0;
		}
		if(wm->default_node_active)project_home(wm,true);
		else project_named(wm,wm->members,true);
	} else if (strcmp(op, "next") == 0 && node_view(wm) && (wm->layout == STACK || is_flow(wm) || is_stage(wm)) &&
			strtok_r(NULL, " ", &tail) == NULL) {
		if (is_flow(wm) || is_stage(wm)) {
			for(int step=1;step<=wm->visible_count;step++) {
				int slot=(wm->focused?wm->focused->slot:-1)+step;
				struct xdg_view *target=wm->slots[slot%wm->visible_count];
				if(target) {
					if(target!=wm->focused) strip_focus_view(wm,target);
					break;
				}
			}
			domain_send(wm,"applied",serial);
			return 0;
		}
		if (wm->visible_count > 0) {
			struct xdg_view *top = best_view(wm, true);
			int next = top != NULL ? (top->slot + 1) % wm->visible_count : 1 % wm->visible_count;
			strip_focus_view(wm,wm->slots[next]);
		}
	} else if (strcmp(op, "node") == 0 || strcmp(op, "focus") == 0 || strcmp(op, "node_window") == 0) {
		struct domain_member members[NODE_WINDOWS] = {0};
		enum node_layout mode = TILING;
		bool with_target=strcmp(op,"node_window")==0;
		if ((strcmp(op, "focus") != 0 && !read_layout(strtok_r(NULL, " ", &tail), &mode)) ||
				(with_target && !(preferred_id=domain_number(strtok_r(NULL," ",&tail)))) ||
				!read_members(&tail, members)) {
			domain_disconnect(wm);
			return 0;
		}
		preferred=preferred_id?prefer_window(wm,preferred_id,members):NULL;
		wm->strip_offset=0;
		wm->cycle_count=0;picker_hide(wm);
		wm->default_node_active=wm->home_active = false;
		wm->overview_active = false;
		wm->domain_active = true;
		wm->layout = mode;
		project_named(wm, members,true);
	} else {
		domain_disconnect(wm);
		return 0;
	}
	select_projection(wm, wm->map_zoom_enabled && strcmp(op, "overview") == 0);
	focus_preferred(wm,preferred);
	domain_send(wm, "applied", serial);
	return 0;
}

static void request_view_minimize(struct xdg_view *view,bool whole_group) {
	struct nested *wm=view->wm;
	if(!view->mapped || (!whole_group && view->minimized) || !wm->domain_active ||
			wm->cleaning) return;
	// Minimize is local visibility, not an unmap, Domain change, or
	// process suspension. Keep the slot and the saved client geometry.
	struct xdg_view *group[FLOW_GROUP_MAX];
	int n=view->slot>=0?group_members(wm,view,group,is_stage(wm) && !whole_group):0;
	if((n>=2)!=whole_group)return;
	if(wm->maximize.view==view)maximize_finish(wm);
	if(n<2) {group[0]=view;n=1;}
	struct xdg_view *previous=wm->focused;
	tray_flights_clear(wm);
	if(view->slot>=0 && (is_flow(wm) || is_stage(wm))) {
		wm->strip_offset=strip_goal(wm,view,wm->strip_offset);
		wm->strip_hover=0;
		for(int i=0;i<n;i++)tray_flight_start(wm,group[i],i,false,(struct wlr_box){0});
	}
	for(int i=0;i<n;i++) {
		struct xdg_view *part=group[i];
		part->minimized=true;
		if (wm->resize.view==part) resize_cancel(wm);
		if(wm->drag==part) {wm->drag=NULL;wm->group_grab.source=0;}
		if(wm->group_join.id==part->id)wm->group_join.id=0;
		if(wm->mode_slide_view==part) mode_slide_finish(wm);
	}
	if(view->slot<0) return;
	bool had_focus=previous && previous->minimized;
	if(had_focus || (is_stage(wm) && stage_same_unit(wm,wm->stage_painted,view)) || !is_stage(wm))
		present_node(wm,had_focus,0);
	if(had_focus && wm->focused && is_stage(wm)) stage_prime(wm->focused);
	struct wlr_surface *fallback=view_target(wm);
	// A newly mapped neighbour may not have a focus rank yet.
	for(int i=0;had_focus && !fallback && i<wm->visible_count;i++) {
		struct xdg_view *next=wm->slots[i];
		if(!next->frame->node.enabled) continue;
		fallback=next->toplevel->base->surface;
		remember_focus(wm,fallback);
	}
	for(int i=0;i<n;i++)nested_seat_forget(wm->seat,group[i]->toplevel->base->surface,fallback);
	nested_seat_refresh(wm->seat);
	wlr_output_schedule_frame(wm->output);
}

static void group_show_settings(struct nested *wm,struct xdg_view *view,bool from_frame) {
	if(!wm->map_pointer || wl_resource_get_version(wm->map_pointer)<MOTHERSHIP_MAP_POINTER_V1_GROUP_STATE_SINCE_VERSION ||
		!group_large(wm) || !picker_scope(wm))return;
	struct xdg_view *group[FLOW_GROUP_MAX];
	int n=group_members(wm,view,group,false);
	if(n<2)return;
	uint64_t id=view->group_id;
	if(from_frame && wl_resource_get_version(wm->map_pointer)>=MOTHERSHIP_MAP_POINTER_V1_GROUP_SETTINGS_ANCHOR_SINCE_VERSION)
		for(int i=0;i<NODE_WINDOWS/2;i++) {
			__typeof__(wm->group_frame[0]) *g=&wm->group_frame[i];
			if(g->leader==view->id) {
				struct wlr_box button=group_chrome_control_box(g->chrome,GROUP_CONTROL_LAYOUT);
				mothership_map_pointer_v1_send_group_settings_anchor(wm->map_pointer,
					g->box.x+button.x+button.width/2,g->box.y+button.y+button.height);
				break;
			}
		}
	bool hidden=true;
	for(int i=0;i<n;i++) hidden&=group[i]->minimized;
	mothership_map_pointer_v1_send_group_state(wm->map_pointer,id>>32,(uint32_t)id,n,hidden,
		view->group_sx,view->group_sy,view->group_gap);
}

static void map_pointer_group_query(struct wl_client *client,struct wl_resource *resource,wl_fixed_t x,wl_fixed_t y) {
	(void)client;
	struct nested *wm=wl_resource_get_user_data(resource);
	if(wm->map_pointer!=resource || !group_large(wm) || !picker_scope(wm))return;
	group_show_settings(wm,strip_hit(wm,wl_fixed_to_double(x),wl_fixed_to_double(y)),false);
}

static void map_pointer_group_action(struct wl_client *client,struct wl_resource *resource,
		uint32_t hi,uint32_t lo,uint32_t action,int32_t sx,int32_t sy,int32_t gap) {
	(void)client;
	struct nested *wm=wl_resource_get_user_data(resource);
	if(wm->map_pointer!=resource || !group_large(wm) || !picker_scope(wm))return;
	uint64_t id=((uint64_t)hi<<32)|lo;
	struct xdg_view *view=NULL;
	for(int i=0;id && i<wm->visible_count;i++)
		if(wm->slots[i]->group_id==id) {view=wm->slots[i];break;}
	struct xdg_view *group[FLOW_GROUP_MAX];
	int n=group_members(wm,view,group,false);
	if(n<2)return;
	if(action==MOTHERSHIP_MAP_POINTER_V1_GROUP_COMMAND_MINIMIZE) {
		request_view_minimize(view,true);
		return;
	}
	if(action==MOTHERSHIP_MAP_POINTER_V1_GROUP_COMMAND_RESTORE) {
		for(int i=0;i<n;i++) if(group[i]->minimized) {strip_focus_view(wm,group[i]);break;}
		return;
	}
	if(is_stage(wm)) tray_flights_clear(wm);
	if(action==MOTHERSHIP_MAP_POINTER_V1_GROUP_COMMAND_UNGROUP) {
		for(int i=0;i<n;i++)group[i]->group_id=group[i]->group_order=0;
	} else if(action==MOTHERSHIP_MAP_POINTER_V1_GROUP_COMMAND_APPLY) {
		for(int i=0;i<n;i++) {
			group[i]->group_sx=clamp_int(sx,25,75);
			group[i]->group_sy=clamp_int(sy,25,75);
			group[i]->group_gap=clamp_int(gap,0,FLOW_GROUP_GAP_MAX);
		}
	} else return;
	if(is_stage(wm)) present_stage(wm);
	else {
		arrange_flow(wm,wm->focused);
		present_flow(wm);
	}
	nested_seat_refresh(wm->seat);
	wlr_output_schedule_frame(wm->output);
}

static void on_view_request_maximize(struct wl_listener *listener,void *data) {
	(void)data;
	struct xdg_view *view=wl_container_of(listener,view,request_maximize);
	set_view_maximized(view,view->toplevel->requested.maximized);
	// XDG Shell requires a configure even when policy keeps the current state.
	if(view->toplevel->base->initialized)
		wlr_xdg_surface_schedule_configure(view->toplevel->base);
}

static void on_view_request_minimize(struct wl_listener *listener,void *data) {
	(void)data;
	struct xdg_view *view=wl_container_of(listener,view,request_minimize);
	request_view_minimize(view,false);
}

static void remove_view(struct xdg_view *);
static void project_new_view(struct xdg_view *);

static struct xdg_view *view_dialog_parent(struct xdg_view *view) {
	struct wlr_xdg_toplevel *toplevel=view->toplevel;
	struct wl_client *client=wl_resource_get_client(toplevel->resource);
	bool cosmic_chooser=!toplevel->parent && toplevel->app_id &&
		strcmp(toplevel->app_id,"com.system76.CosmicFiles")==0;
	struct xdg_view *candidate, *owner=NULL;
	int matches=0;
	wl_list_for_each(candidate,&view->wm->views,link) {
		if (candidate->toplevel==toplevel->parent) return candidate;
		// COSMIC Terminal embeds the COSMIC Files chooser. It creates a
		// separate toplevel on the same connection without a parent handle.
		if (cosmic_chooser && candidate->mapped && !candidate->dialog &&
				wl_resource_get_client(candidate->toplevel->resource)==client &&
				candidate->toplevel->app_id &&
				strcmp(candidate->toplevel->app_id,"com.system76.CosmicTerm")==0) {
			if (candidate==view->wm->focused) return candidate;
			owner=candidate;
			++matches;
		}
	}
	return matches==1 ? owner : NULL;
}

static void catalogue_view(struct xdg_view *view) {
	if (view->catalogued || view->dialog || view->wm->domain_fd<0) return;
	pid_t pid=0;
	wl_client_get_credentials(wl_resource_get_client(view->toplevel->resource),&pid,NULL,NULL);
	char packet[64];
	int n=pid>0 ? snprintf(packet,sizeof(packet),"new %" PRIu64 " %ld",view->id,(long)pid) :
		snprintf(packet,sizeof(packet),"new %" PRIu64,view->id);
	view->catalogued=domain_write(view->wm,packet,n);
	if (view->catalogued && valid_app_id(view->toplevel->app_id)) publish_app_id(view);
}

static void attach_dialog(struct xdg_view *view,struct xdg_view *parent) {
	struct nested *wm=view->wm;
	for (struct xdg_view *ancestor=parent; ancestor; ancestor=ancestor->dialog_parent)
		if (ancestor==view) return;
	view->dialog_parent=parent;
	if (!view->dialog) {
		view->dialog=true;
		if (view->slot>=0) remove_view(view);
		if (view->catalogued) {
			domain_send(wm,"close",view->id);
			view->catalogued=view->assigned=false;
		}
		wlr_scene_node_reparent(&view->frame->node,wm->dialog_tree);
		wlr_scene_node_set_position(&view->content->node,0,0);
		wlr_scene_node_set_enabled(&view->content->node,true);
		wlr_scene_subsurface_tree_set_clip(&view->tree->node,NULL);
		if (view->chrome) window_chrome_show(view->chrome,false);
		scene_glass_destroy(view->frame_glass);
		view->frame_glass=NULL;
		if (view->bar) wlr_scene_node_set_enabled(&view->bar->node,false);
		if (view->grip) wlr_scene_node_set_enabled(&view->grip->node,false);
		set_view_decoration(view,false);
		if (view->configured_maximized) wlr_xdg_toplevel_set_maximized(view->toplevel,false);
		if (view->configured_stage) wlr_xdg_toplevel_set_tiled(view->toplevel,0);
		view->configured_maximized=view->configured_stage=false;
		view->configure_w=view->configure_h=0;
		wlr_xdg_toplevel_set_size(view->toplevel,0,0);
	}
	wl_list_remove(&view->dialog_link);
	wl_list_insert(&wm->dialogs,&view->dialog_link);
	wlr_scene_node_raise_to_top(&view->frame->node);
	place_dialogs(wm);
	nested_seat_refresh_dialog(wm->seat);
	nested_seat_refresh(wm->seat);
}

static void on_view_parent(struct wl_listener *listener,void *data) {
	(void)data;
	struct xdg_view *view=wl_container_of(listener,view,set_parent);
	if (!view->mapped || view->wm->cleaning) return;
	struct xdg_view *parent=view_dialog_parent(view);
	if (parent) {
		attach_dialog(view,parent);
	} else if (view->dialog && view->dialog_parent && view->dialog_parent->mapped &&
			view->dialog_parent->toplevel->base->surface->mapped) {
		// An explicit unparent request from a live owner makes the
		// surface independent. When the owner disappears, keep the
		// chooser hidden until its client reparents or remaps it.
		project_new_view(view);
		nested_seat_refresh_dialog(view->wm->seat);
	}
}

static void project_new_view(struct xdg_view *view) {
	struct nested *wm = view->wm;
	if (view->dialog) {
		view->dialog=false;
		view->dialog_parent=NULL;
		wl_list_remove(&view->dialog_link);
		wl_list_init(&view->dialog_link);
		wlr_scene_node_set_enabled(&view->frame->node,false);
		wlr_scene_node_reparent(&view->frame->node,wm->app_tree);
		if (view->configured_stage) wlr_xdg_toplevel_set_tiled(view->toplevel,0);
		view->configured_stage=false;
		view->configure_w=view->configure_h=0;
		wlr_xdg_toplevel_set_size(view->toplevel,0,0);
	}
	catalogue_view(view);
	if (!wl_list_empty(&view->pin_link) && wm->output) wlr_output_schedule_frame(wm->output);
	view->focus_on_show = wm->domain_active;
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
	if (view->catalogued) domain_send(wm, "map", view->id);
}

static void on_view_map(struct wl_listener *listener, void *data) {
	struct xdg_view *view = wl_container_of(listener, view, map);
	view->mapped=true;
	struct xdg_view *parent=view_dialog_parent(view);
	if (parent) attach_dialog(view,parent);
	else project_new_view(view);
}

static void remove_view(struct xdg_view *view) {
	struct nested *wm = view->wm;
	if (wm->resize.view==view) resize_cancel(wm);
	if(wm->maximize.view==view)maximize_finish(wm);
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
	screenshot_remove_target(view->wm, view->id);
	owned_preview_clear(&view->preview_copy);
	view->minimized=false;
	view->mapped = false;
	view->focus_on_show = false;
	if (view->dialog) {
		wlr_scene_node_set_enabled(&view->frame->node,false);
		if (!view->wm->cleaning) {
			place_dialogs(view->wm);
			nested_seat_refresh_dialog(view->wm->seat);
			nested_seat_refresh(view->wm->seat);
		}
		return;
	}
	remove_view(view);
	if (!view->wm->cleaning && place_dialogs(view->wm)) nested_seat_refresh_dialog(view->wm->seat);
	if (!wl_list_empty(&view->pin_link) && view->wm->output && !view->wm->cleaning)
		wlr_output_schedule_frame(view->wm->output);
	if (view->catalogued && !view->wm->cleaning) {
		domain_send(view->wm, "unmap", view->id);
	}
}

static void on_view_tree_destroy(struct wl_listener *listener, void *data) {
	struct xdg_view *view = wl_container_of(listener, view, tree_destroy);
	wl_list_remove(&listener->link);
	view->tree = NULL;
}

static void on_view_title(struct wl_listener *listener,void *data) {
	(void)data;
	struct xdg_view *view=wl_container_of(listener,view,title);
	if (!view->wm->cleaning) {
		if(view->slot>=0) place_view(view);
		pin_metadata_changed(view);
	}
}

static void on_view_commit(struct wl_listener *listener, void *data) {
	(void)data;
	struct xdg_view *view = wl_container_of(listener, view, commit);
	struct wlr_surface *surface=view->toplevel->base->surface;
	// A callback, input-region or opaque-region-only commit doesn't
	// change the pixels/viewport of an already-owned preview.
	if(surface->current.committed & (WLR_SURFACE_STATE_BUFFER | WLR_SURFACE_STATE_SURFACE_DAMAGE |
			WLR_SURFACE_STATE_BUFFER_DAMAGE | WLR_SURFACE_STATE_TRANSFORM |
			WLR_SURFACE_STATE_SCALE | WLR_SURFACE_STATE_VIEWPORT))
		++view->preview_generation;
	if(view->mapped && view->slot>=0)
		render_debug_surface(view->wm->render_debug,RENDER_APP,view->id,view->toplevel->base->surface);
	if (view->toplevel->base->initial_commit) {
		// wm_capabilities was added in xdg_toplevel version 5.
		if(wl_resource_get_version(view->toplevel->resource)>=5)
			wlr_xdg_toplevel_set_wm_capabilities(view->toplevel,
				view->toplevel->scheduled.wm_capabilities | WLR_XDG_TOPLEVEL_WM_CAPABILITIES_MINIMIZE |
				WLR_XDG_TOPLEVEL_WM_CAPABILITIES_MAXIMIZE);
		set_view_decoration(view,false);
		wlr_xdg_surface_schedule_configure(view->toplevel->base);
	}
	if (view->dialog) {
		if (place_dialogs(view->wm)) nested_seat_refresh_dialog(view->wm->seat);
	} else place_view(view);
	if (view->slot>=0 && node_strip_active(view->wm)) wlr_output_schedule_frame(view->wm->output);
	if(!wl_list_empty(&view->pin_link) && view->wm->output)
		for(int i=0;i<view->wm->pin.live;i++) if(view->wm->pin.items[i].id==view->id) {
			wlr_output_schedule_frame(view->wm->output);
			break;
		}
}

static void on_view_configure(struct wl_listener *listener,void *data) {
	(void)data;
	struct xdg_view *view=wl_container_of(listener,view,configure);
	render_debug_activity(view->wm->render_debug,RENDER_CONFIGURE);
}

static void on_view_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct xdg_view *view = wl_container_of(listener, view, destroy);
	view->toplevel->base->data = NULL;
	pin_forget(view->wm,view);
	owned_preview_clear(&view->preview_copy);
	detach_view_decoration(view);
	view->mapped = false;
	group_detach(view);
	if (!view->dialog) remove_view(view);
	struct xdg_view *dialog;
	wl_list_for_each(dialog,&view->wm->dialogs,dialog_link) {
		if (dialog->dialog_parent==view) dialog->dialog_parent=NULL;
	}
	wl_list_remove(&view->dialog_link);
	if (!view->wm->cleaning && view->catalogued) {
		domain_send(view->wm, "close", view->id);
	}
	wl_list_remove(&view->link);
	wl_list_remove(&view->map.link);
	wl_list_remove(&view->unmap.link);
	wl_list_remove(&view->commit.link);
	wl_list_remove(&view->configure.link);
	wl_list_remove(&view->request_maximize.link);
	wl_list_remove(&view->request_minimize.link);
	wl_list_remove(&view->set_parent.link);
	wl_list_remove(&view->app_id.link);
	wl_list_remove(&view->title.link);
	wl_list_remove(&view->destroy.link);
	if (view->tree != NULL) {
		wl_list_remove(&view->tree_destroy.link);
	}
	scene_glass_destroy(view->frame_glass);
	if (view->chrome) window_chrome_destroy(view->chrome);
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
		view->content = wlr_scene_tree_create(view->frame);
	}
	if (view->content != NULL) {
		// Clip the toplevel surface to its pane while letting menus extend past it.
		view->tree = wlr_scene_xdg_surface_create(view->content, toplevel->base);
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
	toplevel->base->data = view->content;
	view->id = ++wm->next_window_id;
	view->slot = -1;
	wl_list_init(&view->pin_link);
	wl_list_init(&view->dialog_link);
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
	view->configure.notify=on_view_configure;
	wl_signal_add(&toplevel->base->events.configure,&view->configure);
	view->request_maximize.notify=on_view_request_maximize;
	wl_signal_add(&toplevel->events.request_maximize,&view->request_maximize);
	view->request_minimize.notify=on_view_request_minimize;
	wl_signal_add(&toplevel->events.request_minimize,&view->request_minimize);
	view->set_parent.notify=on_view_parent;
	wl_signal_add(&toplevel->events.set_parent,&view->set_parent);
	view->destroy.notify = on_view_destroy;
	wl_signal_add(&toplevel->events.destroy, &view->destroy);
	view->app_id.notify = on_view_app_id;
	wl_signal_add(&toplevel->events.set_app_id, &view->app_id);
	view->title.notify=on_view_title;
	wl_signal_add(&toplevel->events.set_title,&view->title);
	wl_list_insert(wm->views.prev, &view->link);
}

static void native_output_added(struct wl_listener *listener,void *data) {
	struct nested *wm=wl_container_of(listener,wm,native_added);
	struct wlr_output *output=data;
	// The existing layout is one logical canvas. Publish only the first
	// active connector; do not expose a partially managed second output.
	const char *connector=getenv("MOTHERSHIP_OUTPUT");
	if(!wm->headless_test && connector && *connector && strcmp(connector,output->name)!=0) return;
	if(!wm->output && (wm->headless_test?wlr_output_is_headless(output):wlr_output_is_drm(output))) {
		wm->output=output;
	}
}

static void native_scope_active(struct wl_listener *listener,void *data) {
	(void)data;
	struct nested *wm=wl_container_of(listener,wm,session_active);
	nested_seat_local_active(wm->seat,wm->hw_session->active);
	if(wm->ready && wm->output && wm->hw_session->active) wlr_output_schedule_frame(wm->output);
}

static void native_scope_destroy(struct wl_listener *listener,void *data) {
	(void)data;
	struct nested *wm=wl_container_of(listener,wm,session_destroy);
	wl_list_remove(&wm->session_active.link);
	wl_list_remove(&wm->session_destroy.link);
	wm->hw_session=NULL;
	if(!wm->cleaning) stop(wm,"native seat ended");
}

static void native_logout(void *data) { stop(data,NULL); }

static bool render_capture_key(void *data) {
	struct nested *wm=data;
	return render_debug_toggle(wm->render_debug);
}

enum { IDLE_SECONDS=300, IDLE_CHECK_MS=30000 };
static int64_t monotonic_ms(void) {
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC,&now);
	return now.tv_sec*1000LL+now.tv_nsec/1000000;
}
// Input only resets a timestamp; nothing about it leaves the WM except the
// end of an idle period.
static void seat_activity(void *data) {
	struct nested *wm=data;
	wm->last_input_ms=monotonic_ms();
	if(wm->idle_reported) {
		wm->idle_reported=false;
		domain_send(wm,"active",1);
	}
}
// Report idle once, with the seconds since the last input, so desktopd can
// end a Mission Log segment where the person actually stopped.
static int idle_check(void *data) {
	struct nested *wm=data;
	int64_t quiet=(monotonic_ms()-wm->last_input_ms)/1000;
	if(!wm->idle_reported && wm->domain_source && quiet>=IDLE_SECONDS) {
		wm->idle_reported=true;
		domain_send(wm,"idle",(uint64_t)quiet);
	}
	wl_event_source_timer_update(wm->idle_timer,IDLE_CHECK_MS);
	return 0;
}

static bool create_input_seat(struct nested *wm,struct wl_display *parent) {
	wm->seat=nested_seat_create(wm->server,parent,parent?wlr_wl_output_get_surface(wm->output):NULL,
		hit_view,remember_focus,move_cursor,start_view_move,update_view_move,map_scroll,map_contact,
		map_weapon,activate_chrome,scroll_flow,scroll_node_strip,cycle_window,home_key_tap,node_panel_key,node_number,
		node_switch,close_focused_window,reorder_focused_window,render_capture_key,wm);
	if (!wm->seat) return false;
	nested_seat_dialog_bind(wm->seat,dialog_keyboard_target);
	nested_seat_screenshot_bind(wm->seat, screenshot_key, screenshot_abort, wm);
	nested_seat_move_bind(wm->seat, move_focused_window);
	nested_seat_activity_bind(wm->seat, seat_activity);
	nested_seat_pin_bind(wm->seat,pin_toggle,pin_dismiss);
	wm->last_input_ms=monotonic_ms();
	wm->idle_timer=wl_event_loop_add_timer(wl_display_get_event_loop(wm->server),idle_check,wm);
	if(wm->idle_timer) wl_event_source_timer_update(wm->idle_timer,IDLE_CHECK_MS);
	return true;
}

extern void wmLoadDisplay(char *,int *,int *,int *,int *);
extern char *wmSaveDisplay(char *,int,int,int,int);

static struct wlr_output_mode *find_output_mode(struct wlr_output *out,int w,int h,int refresh) {
	struct wlr_output_mode *mode;
	wl_list_for_each(mode,&out->modes,link)
		if(mode->width==w && mode->height==h && mode->refresh==refresh)return mode;
	return NULL;
}

static bool display_fits(int w,int h,int scale) {
	return scale>=100 && scale<=300 && w>=640 && h>=360 &&
		(int64_t)w*100>=640*scale && (int64_t)h*100>=360*scale;
}

static bool headless_display_mode(int w,int h,int refresh) {
	return refresh==60000 && ((w==1280 && h==720) || (w==960 && h==540));
}

static void native_logical_size(struct nested *wm,int w,int h,uint32_t scale) {
	wm->parent_window.scale_120=scale;
	wm->parent_window.width=(int)lround((double)w*120/scale);
	wm->parent_window.height=(int)lround((double)h*120/scale);
	wlr_scene_rect_set_size(wm->background,wm->parent_window.width,wm->parent_window.height);
}

static bool native_output_state(struct nested *wm,struct wlr_output_state *state,bool saved) {
	struct wlr_output *out=wm->output;
	struct wlr_output_mode *mode=wlr_output_preferred_mode(out);
	int w=mode?mode->width:out->width,h=mode?mode->height:out->height;
	if(w<=0 || h<=0) return false;
	int target_w=0,target_h=0,target_hz=0,percent=0;
	if(saved)wmLoadDisplay(out->name,&target_w,&target_h,&target_hz,&percent);
	struct wlr_output_mode *stored=find_output_mode(out,target_w,target_h,target_hz);
	bool custom=wm->headless_test && headless_display_mode(target_w,target_h,target_hz);
	uint32_t scale=120;
	if((stored || custom) && display_fits(target_w,target_h,percent)) {
		mode=stored;w=target_w;h=target_h;scale=(uint32_t)lround(percent*1.2);
	} else custom=false;
	const char *setting=getenv("MOTHERSHIP_OUTPUT_SCALE");
	if(setting && *setting) {
		char *end;
		double value=strtod(setting,&end);
		if(*end || !isfinite(value) || value<1 || value>3) return false;
		if(display_fits(w,h,lround(value*100)))scale=(uint32_t)lround(value*120.);
	}
	if(mode)wlr_output_state_set_mode(state,mode);
	else if(custom)wlr_output_state_set_custom_mode(state,w,h,target_hz);
	wlr_output_state_set_scale(state,(float)scale/120.f);
	native_logical_size(wm,w,h,scale);
	return true;
}

static void display_query(struct wl_client *client,struct wl_resource *resource) {
	(void)client;
	struct nested *wm=wl_resource_get_user_data(resource);
	if(resource!=wm->map_pointer || !wm->native_output || !wm->output)return;
	struct wlr_output *out=wm->output;
	char name[160];
	snprintf(name,sizeof(name),"%s%s%s",out->make?out->make:"",
		out->make && out->model?" ":"",out->model?out->model:"");
	mothership_map_pointer_v1_send_display_current(resource,*name?name:out->name,out->name,
		out->width,out->height,wm->headless_test?60000:out->refresh,lround(out->scale*100));
	struct wlr_output_mode *mode;
	wl_list_for_each(mode,&out->modes,link)
		if(display_fits(mode->width,mode->height,100))
			mothership_map_pointer_v1_send_display_mode(resource,mode->width,mode->height,mode->refresh);
	if(wm->headless_test) {
		mothership_map_pointer_v1_send_display_mode(resource,1280,720,60000);
		mothership_map_pointer_v1_send_display_mode(resource,960,540,60000);
	}
	mothership_map_pointer_v1_send_display_done(resource);
}

static void display_apply(struct wl_client *client,struct wl_resource *resource,int32_t w,int32_t h,
		int32_t hz,int32_t scale) {
	(void)client;
	struct nested *wm=wl_resource_get_user_data(resource);
	if(resource!=wm->map_pointer || !wm->native_output || !wm->output)return;
	if(wm->hw_session && !wm->hw_session->active) {
		mothership_map_pointer_v1_send_display_result(resource,0,"The local display is inactive.");
		return;
	}
	if(wm->display_request.pending)return;
	if(wm->output->frame_pending) {
		// DRM cannot modeset over an in-flight page flip. The next frame
		// event processes this Apply before normal rendering resumes.
		wm->display_request.pending=true;
		wm->display_request.w=w;wm->display_request.h=h;
		wm->display_request.hz=hz;wm->display_request.scale=scale;
		wlr_output_schedule_frame(wm->output);
		return;
	}
	display_commit(wm,resource,w,h,hz,scale);
}

static void display_commit(struct nested *wm,struct wl_resource *resource,int w,int h,int hz,int scale) {
	struct wlr_output *out=wm->output;
	struct wlr_output_mode *mode=find_output_mode(out,w,h,hz);
	bool custom=wm->headless_test && headless_display_mode(w,h,hz);
	if((!mode && !custom) || !display_fits(w,h,scale)) {
		mothership_map_pointer_v1_send_display_result(resource,0,"This mode or scale is not available.");
		return;
	}
	uint32_t scale120=(uint32_t)lround(scale*1.2);
	float factor=scale120/120.f;
	uint32_t applied_scale=(uint32_t)lround(factor*100);
	if(w!=out->width || h!=out->height || (!custom && mode!=out->current_mode) || factor!=out->scale) {
		struct wlr_output_state state;
		wlr_output_state_init(&state);
		state.allow_reconfiguration=true;
		if(mode)wlr_output_state_set_mode(&state,mode);
		else wlr_output_state_set_custom_mode(&state,w,h,hz);
		wlr_output_state_set_scale(&state,factor);
		bool applied=wlr_scene_output_build_state(wm->scene_output,&state,NULL) &&
			state.buffer && wlr_output_test_state(out,&state) && wlr_output_commit_state(out,&state);
		wlr_output_state_finish(&state);
		if(!applied) {
			wlr_output_schedule_frame(out);
			mothership_map_pointer_v1_send_display_result(resource,0,"The output rejected the settings. No changes applied.");
			return;
		}
		screenshot_problem(wm, "The display size or scale changed.", true);

		resize_cancel(wm);
		maximize_finish(wm);
		native_logical_size(wm,w,h,scale120);
		if(wm->cursor_size)reload_cursor(wm,wm->cursor_size);
		mode_slide_finish(wm);wm->flow_sliding=false;
		wm->drag=NULL;
		if(is_flow(wm)) {arrange_flow(wm,wm->focused);present_flow(wm);}
		else if(is_stage(wm)) present_stage(wm);
		else for(int i=0;i<NODE_WINDOWS;i++) place_view(wm->slots[i]);
		position_panel(wm->panel);
		position_panel(wm->map_hud);
		position_panel(wm->wallpaper);
		position_panel(wm->node_telemetry);
		position_panel(wm->node_switcher);
		nested_seat_local_extent(wm->seat,wm->parent_window.width,wm->parent_window.height);
		nested_seat_refresh(wm->seat);
		wlr_output_schedule_frame(out);
	}
	char *error=wmSaveDisplay(out->name,w,h,hz,applied_scale);
	mothership_map_pointer_v1_send_display_result(resource,error?2:1,error?error:"");
	free(error);
}

int mothership_wm_run(struct mothership_control *control, int stop_read_fd, int domain_fd, const char **reason) {
	if (atomic_load(&control->phase) == STARTUP_INTERRUPTED) {
		*reason = NULL;
		return 0;
	}
	const char *headless=getenv("MOTHERSHIP_NATIVE_TEST_HEADLESS");
	struct nested wm = { .domain_fd = domain_fd,.native_output=control->parent_fd<0,
		.screenshot.pid_fd = -1 };
	wm.headless_test=wm.native_output && headless && !strcmp(headless,"1");
	wl_list_init(&wm.views);
	wl_list_init(&wm.pin.windows);
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
	screenshot_watch_pid(&wm, loop);
	*reason = "could not watch the shutdown signal";
	stop_source = wl_event_loop_add_fd(loop, stop_read_fd, WL_EVENT_READABLE, on_stop_fd, &wm);
	if (stop_source == NULL) {
		goto done;
	}
	if(wm.native_output) {
		*reason="could not open a native DRM/input seat; start from an active local login";
		wm.backend=wm.headless_test?wlr_headless_backend_create(loop):wlr_backend_autocreate(loop,&wm.hw_session);
	} else {
		*reason="could not retain the selected parent Wayland display";
		const int fd=fcntl(control->parent_fd,F_DUPFD_CLOEXEC,0);
		if(fd<0 || (parent=wl_display_connect_to_fd(fd))==NULL) goto done;
		*reason="parent display cannot provide the wlroots Wayland backend";
		wm.backend=wlr_wl_backend_create(loop,parent);
	}
	if(!wm.backend) goto done;
	wm.backend_destroy.notify=on_backend_destroy;
	wl_signal_add(&wm.backend->events.destroy,&wm.backend_destroy);
	if(parent && wl_display_get_error(parent)!=0) goto done;
	if(wm.native_output && !wm.headless_test && !wm.hw_session) goto done;
	if(wm.hw_session) {
		wm.session_active.notify=native_scope_active;
		wm.session_destroy.notify=native_scope_destroy;
		wl_signal_add(&wm.hw_session->events.active,&wm.session_active);
		wl_signal_add(&wm.hw_session->events.destroy,&wm.session_destroy);
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
			wlr_subcompositor_create(wm.server) == NULL ||
			wlr_data_device_manager_create(wm.server) == NULL ||
			wlr_viewporter_create(wm.server) == NULL ||
			wlr_fractional_scale_manager_v1_create(wm.server, 1) == NULL) {
		goto done;
	}
	const char *presentation=getenv("MOTHERSHIP_EXPERIMENT_PRESENTATION");
	if ((!presentation || strcmp(presentation,"0")!=0) &&
			wlr_presentation_create(wm.server,wm.backend,2) == NULL) {
		fprintf(stderr,"Mothership: could not advertise Wayland presentation feedback\n");
	}
	if (renderer->features.timeline && wm.backend->features.timeline) {
		int drm_fd = wlr_renderer_get_drm_fd(renderer);
		if (drm_fd >= 0) {
			if (wlr_linux_drm_syncobj_manager_v1_create(wm.server, 1, drm_fd) == NULL) {
				fprintf(stderr, "Mothership: could not advertise Wayland explicit sync\n");
			} else {
				fprintf(stderr, "Mothership: Wayland explicit sync enabled\n");
			}
		}
	}
	struct wlr_xdg_shell *xdg_shell = wlr_xdg_shell_create(wm.server, 3);
	struct wlr_xdg_foreign_registry *foreign=wlr_xdg_foreign_registry_create(wm.server);
	if (foreign) {
		wlr_xdg_foreign_v1_create(wm.server,foreign);
		wlr_xdg_foreign_v2_create(wm.server,foreign);
	}
	struct wlr_xdg_decoration_manager_v1 *decorations=wlr_xdg_decoration_manager_v1_create(wm.server);
	if (xdg_shell == NULL || decorations == NULL) {
		goto done;
	}
	wm.new_decoration.notify=on_new_decoration;
	wl_signal_add(&decorations->events.new_toplevel_decoration,&wm.new_decoration);
	wm.toplevel.notify = on_toplevel;
	wl_signal_add(&xdg_shell->events.new_toplevel, &wm.toplevel);
	wm.new_popup.notify = on_app_popup;
	wl_signal_add(&xdg_shell->events.new_popup, &wm.new_popup);

	if(wm.native_output) {
		*reason="could not set up the local input seat";
		if(!create_input_seat(&wm,NULL) ||
				!nested_seat_attach_local(wm.seat,wm.backend,wm.hw_session,native_logout)) goto done;
		wm.native_added.notify=native_output_added;
		wl_signal_add(&wm.backend->events.new_output,&wm.native_added);
		if(wm.headless_test && !wlr_headless_add_output(wm.backend,1280,720)) goto done;
	}
	*reason="could not start the Wayland backend";
	if(!wlr_backend_start(wm.backend)) goto done;
	if(wm.native_output) {
		*reason="no connected DRM display was available for the native session";
		for(int i=0;!wm.output && !wm.stopping && wm.backend && i<30;i++) {
			if(wl_event_loop_dispatch(loop,100)<0) goto done;
		}
	} else {
		*reason="could not create the nested output";
		if(!nested_parent_open(&wm.parent_window,parent,parent_geometry_changed,parent_closed,&wm)) goto done;
		wm.output=wlr_wl_output_create_from_surface(wm.backend,wm.parent_window.surface);
		if(!wm.output) wm.parent_window.surface=NULL; // The backend owns it even on allocation failure.
	}
	if(!wm.output || wm.stopping) goto done;
	*reason="could not initialize output rendering";

	if (!wlr_output_init_render(wm.output, allocator, renderer)) {
		goto done;
	}
	wm.scene_output = wlr_scene_output_create(wm.scene, wm.output);
	const float color[4] = {0.07f, 0.10f, 0.14f, 1.0f};
	wm.background = wlr_scene_rect_create(&wm.scene->tree, 800, 600, color);
	if (wm.scene_output == NULL || wm.background == NULL) {
		goto done;
	}
	wm.wallpaper_tree = wlr_scene_tree_create(&wm.scene->tree);
	wm.app_tree = wlr_scene_tree_create(&wm.scene->tree);
	wl_list_init(&wm.dialogs);
	if (wm.app_tree) wm.dialog_tree=wlr_scene_tree_create(wm.app_tree);
	wm.maximize_overlay = wlr_scene_tree_create(&wm.scene->tree);
	wm.resize.preview = window_resize_preview_create(&wm.scene->tree);
	wm.pin.tree = wlr_scene_tree_create(&wm.scene->tree);
	wm.hud_tree = wlr_scene_tree_create(&wm.scene->tree);
	wm.shell_tree = wlr_scene_tree_create(&wm.scene->tree);
	if (wm.wallpaper_tree == NULL || wm.app_tree == NULL || wm.dialog_tree == NULL || wm.maximize_overlay == NULL ||
			wm.resize.preview == NULL || wm.hud_tree == NULL || wm.shell_tree == NULL ||
			wm.pin.tree == NULL) {
		goto done;
	}
	if(!pin_scene_init(&wm)) goto done;
	wm.node_strip_tree=wlr_scene_tree_create(&wm.scene->tree);
	if(!wm.node_strip_tree)goto done;
	wm.node_switcher_tree = wlr_scene_tree_create(&wm.scene->tree);
	if (!wm.node_switcher_tree) goto done;
	const float preview_fallback[4]={.045f,.08f,.116f,1},preview_mark[4]={.29f,.42f,.48f,1};
	const float group_ink[4]={.23f,.42f,.49f,1};
	for(int i=0;i<NODE_WINDOWS;i++) {
		wm.group_tray_ring[i]=wlr_scene_rect_create(wm.node_strip_tree,1,1,group_ink);
		if(!wm.group_tray_ring[i])goto done;
		wlr_scene_node_set_enabled(&wm.group_tray_ring[i]->node,false);
	}
	for(int i=0;i<NODE_WINDOWS;i++) {
		struct wlr_scene_tree *tile=wlr_scene_tree_create(wm.node_strip_tree);
		if(!tile)goto done;
		wm.preview[i].ring=wlr_scene_rect_create(tile,1,1,preview_mark);
		wm.preview[i].background=wlr_scene_rect_create(tile,1,1,preview_fallback);
		wm.preview[i].placeholder=wlr_scene_rect_create(tile,1,1,preview_mark);
		wm.preview[i].image=wlr_scene_buffer_create(tile,NULL);
		if(!wm.preview[i].ring || !wm.preview[i].background || !wm.preview[i].placeholder || !wm.preview[i].image)goto done;
		for(int j=0;j<9;j++) {
			wm.preview[i].focus_marks[j]=wlr_scene_rect_create(tile,1,1,preview_mark);
			if(!wm.preview[i].focus_marks[j])goto done;
			wlr_scene_node_set_enabled(&wm.preview[i].focus_marks[j]->node,false);
		}
	}
	wlr_scene_node_set_enabled(&wm.node_strip_tree->node,false);
	wm.stage_overlay=wlr_scene_tree_create(&wm.scene->tree);
	if(!wm.stage_overlay) goto done;
	wlr_scene_node_place_below(&wm.stage_overlay->node,&wm.node_strip_tree->node);
	wm.picker_tree=wlr_scene_tree_create(&wm.scene->tree);
	if(!wm.picker_tree) goto done;
	wlr_scene_node_set_enabled(&wm.picker_tree->node,false);
	for(int i=0;i<NODE_WINDOWS;i++) {
		wm.picker_preview[i]=wlr_scene_buffer_create(wm.picker_tree,NULL);
		if(!wm.picker_preview[i]) goto done;
	}
	wm.picker_scale=150;
	wm.strip_hover_scale=100;
	wm.picker_focus_tree=wlr_scene_tree_create(&wm.scene->tree);
	if(!wm.picker_focus_tree) goto done;
	wlr_scene_node_set_enabled(&wm.picker_focus_tree->node,false);
	wm.picker_opened_at=-1;
	wm.picker_beam.scene=wlr_scene_buffer_create(wm.picker_focus_tree,NULL);
	if(!wm.picker_beam.scene) goto done;
	const float focus_background[4]={.027f,.064f,.091f,1};
	wm.picker_focus_face=wlr_scene_rect_create(wm.picker_focus_tree,1,1,focus_background);
	if(!wm.picker_focus_face)goto done;
	for(int i=0;i<FLOW_GROUP_MAX;i++) {
		wm.picker_focus_image[i]=wlr_scene_buffer_create(wm.picker_focus_tree,NULL);
		if(!wm.picker_focus_image[i])goto done;
	}
	for(int i=0;i<9;i++) {
		wm.picker_focus_marks[i]=wlr_scene_rect_create(wm.picker_focus_tree,1,1,preview_mark);
		if(!wm.picker_focus_marks[i]) goto done;
	}
	wm.group_ghost=wlr_scene_buffer_create(&wm.scene->tree,NULL);
	if(!wm.group_ghost)goto done;
	wlr_scene_node_set_enabled(&wm.group_ghost->node,false);
	for(int i=0;i<9;i++) {
		wm.group_marks[i]=wlr_scene_rect_create(&wm.scene->tree,1,1,group_ink);
		if(!wm.group_marks[i])goto done;
		wlr_scene_node_set_enabled(&wm.group_marks[i]->node,false);
	}
	if (wm.domain_fd >= 0) {
		struct wlr_layer_shell_v1 *layer = wlr_layer_shell_v1_create(wm.server, 4);
		if (layer == NULL ||
				wl_global_create(wm.server,&mothership_map_pointer_v1_interface,mothership_map_pointer_v1_interface.version,&wm,map_pointer_bind)==NULL) {
			goto done;
		}
		wm.new_panel.notify = on_new_panel;
		wl_signal_add(&layer->events.new_surface, &wm.new_panel);
	}


	wm.video_frost=video_frost_create(wm.wallpaper_tree,wm.output);
	const char *scene_glass_opt_in=getenv("MOTHERSHIP_EXPERIMENT_GRID_FROST");
	wm.frame_glass_enabled=!scene_glass_opt_in || strcmp(scene_glass_opt_in,"0")!=0;
	if(wm.frame_glass_enabled)
		for(unsigned i=0;i<SCENE_GLASS_COUNT;i++)
			wm.scene_glass[i].glass=scene_glass_create(wm.output,wm.shell_tree);
	wm.frame.notify = on_frame;
	wl_signal_add(&wm.output->events.frame, &wm.frame);
	wm.output_destroy.notify = on_output_destroy;
	wl_signal_add(&wm.output->events.destroy, &wm.output_destroy);
	struct wlr_output_state initial;
	wlr_output_state_init(&initial);
	wlr_output_state_set_enabled(&initial, true);
	*reason = "could not enable the output and submit its first frame";
	bool presented =
		(wm.native_output?native_output_state(&wm,&initial,true):output_geometry(&wm,&initial)) &&
		wlr_scene_output_build_state(wm.scene_output, &initial, NULL) &&
		initial.buffer != NULL &&
		wlr_output_commit_state(wm.output, &initial);
	wlr_output_state_finish(&initial);
	if(!presented && wm.native_output && wm.output && !wm.stopping) {
		wlr_output_state_init(&initial);
		wlr_output_state_set_enabled(&initial,true);
		presented=native_output_state(&wm,&initial,false) &&
			wlr_scene_output_build_state(wm.scene_output,&initial,NULL) &&
			initial.buffer && wlr_output_commit_state(wm.output,&initial);
		wlr_output_state_finish(&initial);
	}
	if (!presented || wm.stopping || (parent && wl_display_get_error(parent)!=0)) {
		goto done;
	}
	wlr_output_create_global(wm.output, wm.server);
	if(!reload_cursor(&wm,wmLoadCursorSize())) reload_cursor(&wm,24);

	*reason="could not create the Wayland input seat";
	if(parent && !create_input_seat(&wm,parent)) goto done;
	if(wm.native_output)
		nested_seat_local_extent(wm.seat,wm.parent_window.width,wm.parent_window.height);

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
		nested_seat_switch(wm.seat, NULL, false);
		if (wm.domain_source == NULL || !domain_send(&wm, "ready_window_focus", 0)) {
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
	wm.render_debug=render_debug_init(loop,wm.output,wm.headless_test?"headless":
		wm.native_output?"drm":"nested",wlr_renderer_is_gles2(renderer));
	nested_seat_set_render_debug(wm.seat,wm.render_debug);
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
	screenshot_reset(&wm);
	screenshot_pid_cleanup(&wm);
	nested_seat_set_render_debug(wm.seat,NULL);
	render_debug_destroy(wm.render_debug);
	wm.render_debug=NULL;
	if (wm.idle_timer != NULL) {
		wl_event_source_remove(wm.idle_timer);
		wm.idle_timer=NULL;
	}
	domain_disconnect(&wm);
	if (wm.server != NULL) {
		wl_display_destroy_clients(wm.server);
	}
	nested_seat_destroy(wm.seat);
	wm.seat=NULL;
	if (wm.new_panel.notify != NULL) {
		wl_list_remove(&wm.new_panel.link);
	}
	if (wm.toplevel.notify != NULL) {
		wl_list_remove(&wm.toplevel.link);
	}
	if (wm.new_popup.notify != NULL) {
		wl_list_remove(&wm.new_popup.link);
	}
	if (wm.new_decoration.notify != NULL) {
		wl_list_remove(&wm.new_decoration.link);
	}
	for(unsigned i=0;i<SCENE_GLASS_COUNT;i++) scene_glass_destroy(wm.scene_glass[i].glass);
	for(int i=0;i<NODE_WINDOWS/2;i++)
		group_chrome_destroy(wm.group_frame[i].chrome);
	video_frost_destroy(wm.video_frost);
	if (wm.backend != NULL) {
		wlr_backend_destroy(wm.backend);
	}
	if(wm.hw_session) wlr_session_destroy(wm.hw_session);
	if (wm.cursor_theme != NULL) {
		wlr_xcursor_theme_destroy(wm.cursor_theme);
	}
	window_resize_preview_destroy(wm.resize.preview);
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
		nested_parent_finish(&wm.parent_window);
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
