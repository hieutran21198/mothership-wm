//go:build linux && cgo && panel

#include <gtk/gtk.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "native.h"
#include "map_hud.h"
#include "map-arcade.h"
#include "shell-type.h"
#include "map-pointer.h"
#include "display-settings.h"
#include "shortcut-settings.h"
#include "node-volume.h"
#include "node-switcher.h"
#include "generated-map-pointer.h"
#include "map-camera.h"
#include "shell-paint.h"
#include "home-panel.h"
#include "map-guide.h"
#include "app-grid.h"
#include "slash-ui.h"
#include "analog-glass.h"
#include "analog-button.h"
#include "notification-ui.h"
#include "screenshot.h"
#include "system-tray.h"

/* Stable GTK Layer Shell 1.3 runtime ABI; only the calls used by this
 * shell overlay. https://github.com/wmww/gtk4-layer-shell/blob/v1.3.0/include/gtk4-layer-shell.h */
enum layer_edge { LEFT_EDGE=0, RIGHT_EDGE=1, TOP_EDGE=2, BOTTOM_EDGE=3 };
enum layer_keys { KEYS_NONE = 0, KEYS_ON_DEMAND = 2 };
extern guint gtk_layer_get_protocol_version(void);
extern void gtk_layer_init_for_window(GtkWindow *);
extern void gtk_layer_set_namespace(GtkWindow *, const char *);
extern void gtk_layer_set_anchor(GtkWindow *, enum layer_edge, gboolean);
extern void gtk_layer_set_margin(GtkWindow *, enum layer_edge, int);
extern void gtk_layer_set_keyboard_mode(GtkWindow *, enum layer_keys);
extern void panelAction(int, char *, char *);
extern void panelDeleteNode(char *, int);
extern void panelSlashEnterNode(char *,int,uint64_t);
extern char *panelSaveNodePreferences(int,int,int,int);
extern int panelReloadConfig(void);
extern char *panelSaveHomeSettings(int,int,int,int,int);
extern void panelShellNotification(char *);
extern void panelNodeNotFound(unsigned int);
extern void panelNodeSwitchStep(int direction,int allowed,int include_default);
extern void panelNodeSwitchFinish(int commit,int include_default);
extern void panelSystemAction(int);
extern char *panelSaveMapSettings(char *, int, int, int, int, int, int);

static GtkWindow *window;
static GtkWidget *title, *status, *entry, *nodes, *windows;
static GtkWidget *create, *move, *fly, *home, *terminal, *files;
static GtkWidget *form, *normal, *overview_form, *entry_button;
static GtkWidget *home_settings_body,*display_settings_body,*shortcuts_body;
static GtkWidget *dock, *home_settings, *width_input, *cursor_size_input, *width_note, *video_off, *video_on, *home_guide_off, *home_guide_on;
static GtkWidget *home_guide;
static GtkWidget *node_dock,*node_rail,*node_hover,*node_keys,*node_settings,*node_guide_off,*node_guide_on,*node_guide_error;
static GtkWindow *node_telemetry_layer;
static GtkWidget *node_actions[3],*node_bell,*node_bell_ink,*node_speaker,*node_tray_button,*node_readout,*node_telemetry_off,*node_telemetry_on;
static GtkWidget *window_picker,*picker_grid,*picker_button,*picker_button_label;
enum { PICKER_TILE_CAP=8, PICKER_GAP=8, PICKER_INSET=12 };
static GtkWidget *picker_tiles[PICKER_TILE_CAP];
static int picker_tile_count,picker_cols;
static void picker_fit(void);
static void picker_send_bounds(void);
static uint32_t picker_serial,node_window_count;
static int node_window_overflow=-1;
static gboolean picker_mouse;
static uint64_t picker_selected;
static void dismiss_window_picker(uint64_t id);
static gboolean picker_key(guint key);
static gboolean saved_node_telemetry,node_log_keys,node_volume_keys;
static GtkWidget *node_preview_zoom,*node_hover_zoom;
enum { NODE_PREVIEW_ZOOM_MIN=100, NODE_PREVIEW_ZOOM_MAX=300, NODE_HOVER_ZOOM_MIN=100, NODE_HOVER_ZOOM_MAX=300 };
static int saved_node_preview_zoom=150,saved_node_hover_zoom=100;
static GtkWidget *node_layout_row,*node_layout_flow,*node_layout_stage;
static void node_stage_settings_sync(void);
static int node_bell_center,node_tray_center;
static gboolean tray_keys;
static char *tray_node;
static gboolean saved_node_guide=TRUE;
enum { NODE_PREVIEW_HEIGHT=55, NODE_GUIDE_WIDTH=413, PICKER_BUTTON_SIZE=36 };
static int node_tray_x,node_tray_width,node_strip_x,node_strip_width,node_left;
static double node_pressed=-1;
static GtkWidget *group_popover,*group_sx,*group_sy,*group_gap;
static uint64_t group_layout_id;
static GdkRectangle group_anchor;
static gboolean group_anchor_frame;

static void node_settings_close(void);
static void node_panel_update(void);
static const char *node_notice_source(void);
void panel_redraw_node_bell(void);
static void set_expanded(gboolean);
static gboolean saved_home_guide=TRUE;
static gboolean saved_video_enabled=TRUE;
static GtkWidget *slash_box, *app_grid;
static gboolean app_grid_open, app_grid_key_held;
static gboolean default_node, default_node_enabled=TRUE, default_node_key_held;
static gboolean arcade_entry_held,arcade_escape_held;
static unsigned arcade_keys_held;
static void close_app_grid(void);
static void clear_app_grid(void);
static gboolean can_open_app_grid(void);
static void open_app_grid(void);
static gboolean dock_settings_open, slash_open, slash_in_node, map_menu, desktop_ready;
enum { CURSOR_SIZE_MIN=16,CURSOR_SIZE_MAX=64 };
static int saved_cursor_size=24;
static int output_width, output_height, home_width=HOME_WIDTH_DEFAULT, saved_home_width=HOME_WIDTH_DEFAULT;
static GtkWidget *toast_mode_input[3];
static guint settings_close_id;
static gboolean settings_motion_alternate;
enum { SETTINGS_MOTION_MS=250 };
static int saved_toast_mode;
static void update_dock(void);
static void open_slash(gboolean in_node);
static void open_node_notice(GtkButton *,gpointer);
static void open_system_tray(GtkButton *,gpointer);
static void close_slash(void);
static void close_dock_settings(void);
static void open_dock_settings(void);
static void dock_cancel(GtkButton *,gpointer);
static void map_menu_set(gboolean);
static void home_action(enum home_action);
static gboolean editing_input(void);
static gboolean map_plain_view(void);
static void exit_map_view(void);
static void open_map_settings(GtkButton *,gpointer);
static void cancel_map_settings(GtkButton *,gpointer);
static GtkWidget *map_view_open, *map_view_normal, *map_clear_buffer;
static GtkWidget *map_settings_form, *map_border_input, *map_outer_input, *map_inner_input;
static GtkWidget *map_clear_input, *map_frosted_input, *map_settings_note, *map_settings_save;
static GtkWidget *map_animation_input[MAP_ANIMATION_COUNT], *map_fps_input, *map_border_swatch;
static GtkWidget *map_pan_label,*map_pan_minus,*map_pan_plus;
static int theme_pan_speed=MAP_PAN_DEFAULT,saved_pan_override,pan_override;
static char *saved_map_border,*default_map_border;
static int default_map_outer,default_map_inner;
static gboolean settings_comma_held,settings_escape_held;
static int saved_map_outer, saved_map_inner;
static gboolean saved_map_frosted,saved_map_fps;
static enum map_animation saved_map_animation=MAP_ANIMATION_LINEAR, map_animation=MAP_ANIMATION_LINEAR;
static gboolean map_settings_active,settings_populating;
static gboolean navigating,map_action_wait,map_action_sent;
static guint camera_tick, navigation_timeout;
static gint64 camera_last;
static unsigned camera_held, shortcut_held, number_held;
static gboolean map_space_held;
static struct {
	gboolean pending, can_drag, dragging;
	double x,y;
} map_drag;
static struct map_velocity camera_velocity;
static GtkWidget *node_grid, *node_layouts, *layout_buttons[3], *next_stack, *overview_home;
static GHashTable *positions;
static unsigned node_count;
static char *current_node, *current_mode, *last_connected;
static gboolean returning_map;
static char *relay_target;
static uint64_t relay_serial, relay_token;
static guint relay_timeout;
static gboolean relay_acknowledged, relay_revealing, relay_projected;
static gboolean overview, map_visible, blank_home;
static gboolean showing, expanded, region_pending;
static GMainLoop *loop;
static int enabled;
static char *last_nodes, *last_windows;

struct update {
	char *view, *message, *nodes, *windows, *node, *mode, *profiles;
	int enabled, overview, map_visible, blank_home, default_node, ready;
	uint64_t connect_failed,prepared_relay;
};
static struct update *queued;
static GMutex update_lock;
static gboolean update_scheduled;

static const char *selected(GtkWidget *list) {
	GtkListBoxRow *row = gtk_list_box_get_selected_row(GTK_LIST_BOX(list));
	return row == NULL ? NULL : g_object_get_data(G_OBJECT(row), "key");
}

static void refresh_actions(void) {
	gtk_widget_set_sensitive(map_view_open, enabled);
	if (map_view_normal) {
		gtk_widget_set_sensitive(map_view_normal, enabled);
		gtk_button_set_label(GTK_BUTTON(map_view_normal),current_node && *current_node?"Disconnect":"Map View");
	}
	gboolean node = selected(nodes) != NULL;
	gtk_widget_set_sensitive(create, enabled && *gtk_editable_get_text(GTK_EDITABLE(entry)));
	gtk_widget_set_sensitive(move, enabled && node && selected(windows) != NULL);
	gtk_widget_set_sensitive(fly, enabled && node);
	gtk_widget_set_sensitive(home, enabled);
	gtk_widget_set_sensitive(terminal, enabled);
	gtk_widget_set_sensitive(files, enabled);
	gtk_widget_set_sensitive(entry, enabled);
	gtk_widget_set_sensitive(overview_home, enabled);
	gboolean named = current_node != NULL && *current_node && current_mode != NULL && *current_mode;
	gtk_widget_set_visible(node_layouts, named);
	const char *modes[] = {"stack", "float", "flow"};
	gboolean flowing = named && (strcmp(current_mode,"flow")==0 || g_str_has_prefix(current_mode,"flow-"));
	for (int i = 0; i < 3; ++i) {
		gboolean active = named && (strcmp(current_mode, modes[i]) == 0 || (i==2 && flowing));
		gtk_widget_set_sensitive(layout_buttons[i], enabled && !active);
		if (active) gtk_widget_add_css_class(layout_buttons[i], "suggested-action");
		else gtk_widget_remove_css_class(layout_buttons[i], "suggested-action");
	}
	gtk_widget_set_visible(next_stack, named && (flowing || g_strcmp0(current_mode, "stack") == 0));
	gtk_widget_set_sensitive(next_stack, enabled);
	for (GtkWidget *child = gtk_widget_get_first_child(node_grid); child != NULL;
			child = gtk_widget_get_next_sibling(child)) {
		gtk_widget_set_sensitive(child, enabled);
	}
}

static void list_changed(GtkListBox *list, GtkListBoxRow *row, gpointer data) {
	refresh_actions();
}

static void text_changed(GtkEditable *editable, gpointer data) {
	refresh_actions();
}

void panel_set_default_node_enabled(int enabled) {
	default_node_enabled=enabled!=0;
	update_dock();
}

void panel_home_tap(void) {
	// The WM recognizes bare Meta without capturing ordinary app chords.
	if(navigating) {
		if(map_plain_view() && !map_hud_arcade_active()) exit_map_view();
		return;
	}
	close_slash();
	clear_app_grid();
	node_settings_close();
	panelAction(4,"","");
}

void panel_node_apps(void) {
	if (!desktop_ready || overview || map_visible || app_grid_open ||
		(!default_node && (!current_node || !*current_node))) return;
	node_settings_close();
	if (expanded) set_expanded(FALSE);
	open_app_grid();
}

void panel_node_slash(void) {
	if(!node_dock || !gtk_widget_get_visible(node_dock)) return;
	open_slash(TRUE);
}

void panel_node_notifications(void) {
	open_node_notice(NULL,NULL);
}

static void return_keys(void) {
	gtk_layer_set_keyboard_mode(window, KEYS_NONE);
	gtk_window_set_focus(window, NULL);
}

static void schedule_region(void);

static gboolean set_input_region(gpointer data) {
	region_pending = FALSE;
	GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(window));
	if (surface == NULL) {
		return G_SOURCE_REMOVE;
	}
	cairo_region_t *region=cairo_region_create();
	if(screenshot_visible() || slash_open || notice_log_visible() || node_volume_keys || tray_keys || app_grid_open || (window_picker && gtk_widget_get_visible(window_picker)) ||
		(node_settings && gtk_widget_get_visible(node_settings))) {
		// Receive the surrounding press/release pair while a popup is
		// open so its dismiss click never reaches the underlying app.
		cairo_rectangle_int_t outside={0,0,gdk_surface_get_width(surface),gdk_surface_get_height(surface)};
		cairo_region_union_rectangle(region,&outside);
	}
	GtkWidget *hangar=home_dock_hangar();
	GtkWidget *part[]={dock,node_dock,node_settings,slash_box,entry_button,form,notice_log_visible()?notice_log_widget():NULL,notice_close_target(),node_volume_widget(),
		hangar && gtk_widget_get_can_target(hangar)?hangar:NULL};
	for(size_t i=0;i<G_N_ELEMENTS(part);i++) {
		GtkWidget *w=part[i];
		if(!w || !gtk_widget_get_visible(w) || (w==form && (!expanded || navigation_timeout || (navigating && !map_menu)))) continue;
		graphene_rect_t bounds;
		if (gtk_widget_compute_bounds(w,GTK_WIDGET(window),&bounds)) {
			cairo_rectangle_int_t r={(int)bounds.origin.x,(int)bounds.origin.y,
				(int)bounds.size.width,(int)bounds.size.height};
			cairo_region_union_rectangle(region,&r);
		}
	}
	gdk_surface_set_input_region(surface,region);
	picker_send_bounds();
	cairo_region_destroy(region);
	gtk_widget_queue_draw(GTK_WIDGET(window));
	return G_SOURCE_REMOVE;
}

static gboolean after_layout(GtkWidget *widget, GdkFrameClock *clock, gpointer data) {
	// GTK sets a default input region during layout. Apply the shell's
	// narrow collapsed region once after that work, not every frame.
	g_idle_add(set_input_region, NULL);
	return G_SOURCE_REMOVE;
}

static void schedule_region(void) {
	if (!region_pending) {
		region_pending = TRUE;
		gtk_widget_add_tick_callback(GTK_WIDGET(window), after_layout, NULL, NULL);
	}
}

static gboolean popup_click_inside(GtkWidget *part,double x,double y) {
	graphene_rect_t bounds;
	return part && gtk_widget_get_mapped(part) &&
		gtk_widget_compute_bounds(part,GTK_WIDGET(window),&bounds) &&
		graphene_rect_contains_point(&bounds,&GRAPHENE_POINT_INIT(x,y));
}
static void dismiss_popup_press(GtkGestureClick *click,int count,double x,double y,gpointer data) {
	(void)count;(void)data;
	if(screenshot_visible()) return;
	if(slash_open) {
		// The entry and dropdown are separate panes; their gap is outside.
		for(GtkWidget *pane=gtk_widget_get_first_child(slash_box);pane;pane=gtk_widget_get_next_sibling(pane))
			if(popup_click_inside(pane,x,y)) return;
		gtk_gesture_set_state(GTK_GESTURE(click),GTK_EVENT_SEQUENCE_CLAIMED);
		return;
	}
	GtkWidget *popup=app_grid_open?app_grid:notice_log_visible()?notice_log_widget():node_volume_keys?node_volume_widget():
		tray_keys?system_tray_widget():
		window_picker && gtk_widget_get_visible(window_picker)?window_picker:
		node_settings && gtk_widget_get_visible(node_settings)?node_settings:NULL;
	if(!popup || popup_click_inside(popup,x,y) ||
		(popup==node_settings && popup_click_inside(node_actions[2],x,y)) ||
		(popup==notice_log_widget() && popup_click_inside(notice_close_target(),x,y)) ||
		(popup==node_volume_widget() && popup_click_inside(node_speaker,x,y)) ||
		(popup==system_tray_widget() && (popup_click_inside(system_tray_trigger(),x,y) ||
			popup_click_inside(node_bell,x,y) || popup_click_inside(home_dock_notice_button(),x,y))) ||
		(popup==window_picker && (popup_click_inside(picker_button,x,y) ||
			popup_click_inside(node_hover,x,y)))) return;
	gtk_gesture_set_state(GTK_GESTURE(click),GTK_EVENT_SEQUENCE_CLAIMED);
}
static void dismiss_popup_release(GtkGestureClick *click,int count,double x,double y,gpointer data) {
	(void)count;(void)x;(void)y;(void)data;
	GtkGesture *gesture=GTK_GESTURE(click);
	GdkEventSequence *sequence=gtk_gesture_single_get_current_sequence(GTK_GESTURE_SINGLE(click));
	if(gtk_gesture_get_sequence_state(gesture,sequence)!=GTK_EVENT_SEQUENCE_CLAIMED) return;
	if(slash_open) close_slash();
	else if(app_grid_open) close_app_grid();
	else if(window_picker && gtk_widget_get_visible(window_picker)) dismiss_window_picker(0);
	else if(tray_keys)system_tray_close();
	else if(node_settings && gtk_widget_get_visible(node_settings))node_settings_close();
	else if(!node_volume_close())notice_log_close();
}

static void settings_motion_reset(GtkWidget *pane) {
	g_clear_handle_id(&settings_close_id,g_source_remove);
	gtk_widget_remove_css_class(pane,"settings-enter");
	gtk_widget_remove_css_class(pane,"settings-enter-again");
	gtk_widget_remove_css_class(pane,"settings-exit");
}
static gboolean settings_motion_allowed(GtkWidget *pane) {
	gboolean allowed=FALSE;
	g_object_get(gtk_widget_get_settings(pane),"gtk-enable-animations",&allowed,NULL);
	return allowed && (pane!=map_settings_form || saved_map_animation!=MAP_ANIMATION_OFF);
}
static void settings_motion_open(GtkWidget *pane) {
	settings_motion_reset(pane);
	if(settings_motion_allowed(pane)) {
		settings_motion_alternate=!settings_motion_alternate;
		gtk_widget_add_css_class(pane,settings_motion_alternate?"settings-enter":"settings-enter-again");
	}
	gtk_widget_set_visible(pane,TRUE);
}
static void restore_toast_setting(void) {
	if(toast_mode_input[0])
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toast_mode_input[saved_toast_mode]),TRUE);
}
static int selected_toast_setting(void) {
	for(int i=0;i<3;i++)
		if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(toast_mode_input[i]))) return i;
	return saved_toast_mode;
}

static void close_map_settings(void) {
	if (!map_settings_active) return;
	map_settings_active = FALSE;
	map_animation=saved_map_animation;
	pan_override=saved_pan_override;
	map_hud_show_fps(saved_map_fps);
	map_hud_set_spacing(saved_map_border, saved_map_outer, saved_map_inner, saved_map_frosted);
	gtk_widget_remove_css_class(form, "mothership-settings-surface");
	gtk_widget_remove_css_class(form, "shell-settings-positioned");
	settings_motion_reset(map_settings_form);
	gtk_widget_set_visible(map_settings_form, FALSE);
	gtk_widget_set_overflow(form, GTK_OVERFLOW_VISIBLE);
	gtk_widget_set_visible(status, TRUE);
	gtk_widget_set_visible(normal, !overview);
	gtk_widget_set_visible(overview_form, overview);
}

static void cancel_connect_relay(void) {
	g_clear_handle_id(&relay_timeout,g_source_remove);
	map_hud_relay_cancel();
	g_clear_pointer(&relay_target,g_free);
	relay_token=0;
	relay_acknowledged=relay_revealing=relay_projected=FALSE;
}

static void cancel_map_drag(void) {
	map_drag.pending=map_drag.can_drag=map_drag.dragging=FALSE;
	map_hud_press_connect(FALSE,0,0);
	map_hud_guide_grab(FALSE);
}

static void stop_key_pan(void) {
	camera_held = 0;
	map_hud_guide_keys(0);
	camera_velocity = (struct map_velocity){0};
	if (camera_tick) {
		gtk_widget_remove_tick_callback(map_hud_clock(), camera_tick);
		camera_tick = 0;
	}
}

static void stop_camera_input(void) {
	map_space_held=FALSE;
	map_hud_guide_reset();
	cancel_map_drag();
	map_hud_allow_route(FALSE);
	map_hud_cancel_zoom();
	stop_key_pan();
}

static void end_navigation(gboolean keep_paint) {
	if (!navigating && !navigation_timeout) return;
	if (navigation_timeout) {
		g_source_remove(navigation_timeout);
		navigation_timeout = 0;
	}
	navigating = FALSE;
	map_hud_arcade_stop();
	number_held=0;
	map_action_wait = map_action_sent = FALSE;
	map_pointer_zoom_enabled(FALSE,map_hud_outer_margin());
	stop_camera_input();
	if(!keep_paint) {
		cancel_connect_relay();
		map_hud_finish_effect();
		map_hud_deselect();
	}
	gtk_widget_remove_css_class(form, "mothership-map-keyboard");
	gtk_widget_set_visible(normal, !overview);
	gtk_widget_set_visible(overview_form, overview);
	gtk_widget_set_visible(status, TRUE);
	return_keys();
}

static void stop_navigation(void) {end_navigation(FALSE);}

static void set_expanded(gboolean value) {
	expanded = value;
	if (!value) {
		map_menu=FALSE;
		clear_app_grid();
		close_dock_settings();
		close_slash();
		stop_navigation();
		close_map_settings();
		return_keys();
	}
	if(value && !navigation_timeout && !navigating && !map_settings_active && !dock_settings_open) {
		gtk_widget_set_visible(normal,!overview);
		gtk_widget_set_visible(overview_form,overview);
		gtk_widget_set_visible(status,TRUE);
	}
	gtk_widget_set_visible(form, value);
	update_dock();
}

static void toggle_center(GtkButton *button, gpointer data) {
	(void)button;
	gboolean open=GPOINTER_TO_INT(data);
	if(navigating) {map_menu_set(open);return;}
	if (open) gtk_layer_set_keyboard_mode(window, KEYS_ON_DEMAND);
	set_expanded(open);
	if (open) gtk_widget_grab_focus(form);
}

static void open_input(GtkGestureClick *gesture, int presses, double x, double y, gpointer data) {
	gtk_layer_set_keyboard_mode(window, KEYS_ON_DEMAND);
}

enum camera_direction { MOVE_LEFT=MAP_GUIDE_A, MOVE_RIGHT=MAP_GUIDE_D, MOVE_UP=MAP_GUIDE_W, MOVE_DOWN=MAP_GUIDE_S };

static unsigned direction(guint key) {
	switch(gdk_keyval_to_lower(key)) {
	case GDK_KEY_a: return MOVE_LEFT;
	case GDK_KEY_d: return MOVE_RIGHT;
	case GDK_KEY_w: return MOVE_UP;
	case GDK_KEY_s: return MOVE_DOWN;
	default: return 0;
	}
}

static gboolean advance_camera(GtkWidget *widget, GdkFrameClock *clock, gpointer unused) {
	(void)widget; (void)unused;
	if (!navigating) {
		camera_tick=0;
		return G_SOURCE_REMOVE;
	}
	gint64 now=gdk_frame_clock_get_frame_time(clock);
	double x,y;
	bool moving=map_velocity_move(&camera_velocity,camera_last?(now-camera_last)/1000000.:0,&x,&y);
	camera_last=now;
	map_hud_translate(x,y);
	if(!moving) {
		camera_tick=0;
		return G_SOURCE_REMOVE;
	}
	return G_SOURCE_CONTINUE;
}

static int effective_pan_speed(void) {
	return pan_override?pan_override:theme_pan_speed;
}

static void directions_changed(void) {
	map_hud_guide_keys(camera_held);
	int dx=!!(camera_held&MOVE_RIGHT)-!!(camera_held&MOVE_LEFT);
	int dy=!!(camera_held&MOVE_DOWN)-!!(camera_held&MOVE_UP);
	map_velocity_target(&camera_velocity,dx,dy,map_animation,effective_pan_speed());
	if (!dx && !dy && map_animation!=MAP_ANIMATION_EASE) {
		if (camera_tick) gtk_widget_remove_tick_callback(map_hud_clock(),camera_tick);
		camera_tick=0;
		return;
	}
	if(!camera_tick) {
		camera_last=0;
		camera_tick=gtk_widget_add_tick_callback(map_hud_clock(),advance_camera,NULL,NULL);
	}
}

static void connect_retract_finished(void) {
	map_hud_dismiss_effect();
	cancel_connect_relay();
}

static gboolean connect_relay_timeout(gpointer unused) {
	(void)unused;
	relay_timeout=0;
	cancel_connect_relay();
	return G_SOURCE_REMOVE;
}

static void connect_relay_finished(void) {
	if(!relay_acknowledged || !relay_target) return;
	relay_revealing=TRUE;
	g_clear_handle_id(&relay_timeout,g_source_remove);
	relay_timeout=g_timeout_add(4000,connect_relay_timeout,NULL);
	char token[32];
	g_snprintf(token,sizeof(token),"%" G_GUINT64_FORMAT,(guint64)relay_token);
	panelAction(15,"",token);
}

static void connect_node(const char *name,gboolean animate) {
	if (!enabled || !name || !*name || relay_target) return;
	// Keep the existing map visible if the command is rejected. Only the
	// acknowledged projection event ends an active Map View.
	char *target=g_strdup(name);
	cancel_map_drag();
	map_hud_pause_route();
	char token[32]="";
	if(animate && navigating && map_visible && map_animation!=MAP_ANIMATION_OFF) {
		map_hud_cancel_zoom();
		stop_key_pan();
		map_hud_guide_reset();
		if(map_hud_relay_begin(target,connect_relay_finished)) {
			relay_target=g_strdup(target);
			relay_token=++relay_serial;
			relay_timeout=g_timeout_add(4000,connect_relay_timeout,NULL);
			g_snprintf(token,sizeof(token),"%" G_GUINT64_FORMAT,(guint64)relay_token);
		}
	}
	panelAction(12,target,token);
	g_free(target);
}

static gboolean enter_numbered_node(uint32_t slot) {
	if(slot==1) {
		if(!default_node_enabled) return FALSE;
		if(!default_node || map_visible || overview) {
			cancel_connect_relay();
			panelAction(16,"","");
		}
		return TRUE;
	}
	const char *name=map_hud_shortcut((int)slot);
	if(!name) return FALSE;
	if(map_visible || overview || g_strcmp0(current_node,name)!=0) {
		cancel_connect_relay();
		connect_node(name,FALSE);
	}
	return TRUE;
}

void panel_node_number(uint32_t slot) {
	if(!enabled || slot<1 || slot>9) return;
	if(!enter_numbered_node(slot)) panelNodeNotFound(slot);
}

static gboolean node_switch_allowed(void) {
	return !screenshot_visible() && !slash_open && !app_grid_open &&
		!dock_settings_open && !map_settings_active && !map_menu &&
		!notice_log_visible() && !node_volume_keys && !tray_keys &&
		!picker_serial && !relay_target && !map_hud_arcade_active() &&
		!(node_settings && gtk_widget_get_visible(node_settings)) &&
		!(group_popover && gtk_widget_get_visible(group_popover));
}

void panel_node_switch_step(int32_t direction) {
	panelNodeSwitchStep(direction,node_switch_allowed(),default_node_enabled);
}

void panel_node_switch_finish(uint32_t commit) {
	panelNodeSwitchFinish(commit && node_switch_allowed(),default_node_enabled);
}

struct node_switch_update {
	char **names, **descriptions;
	int *counts;
	size_t length, current, selected;
};

static gboolean paint_node_switch_update(gpointer data) {
	struct node_switch_update *u=data;
	if (u && node_switch_allowed()) {
		node_switcher_show((const char *const *)u->names,(const char *const *)u->descriptions,
			u->counts,u->length,u->current,u->selected);
	} else {
		node_switcher_hide();
	}
	return G_SOURCE_REMOVE;
}

static void free_node_switch_update(gpointer data) {
	struct node_switch_update *u=data;
	if (!u) return;
	g_strfreev(u->names);
	g_strfreev(u->descriptions);
	g_free(u->counts);
	g_free(u);
}

void panel_post_node_switcher(char *const *names,char *const *descriptions,const int *counts,
		size_t length,size_t current,size_t selected) {
	struct node_switch_update *u=NULL;
	if (length) {
		u=g_new(struct node_switch_update,1);
		*u=(struct node_switch_update){
			.names=g_new0(char *,length+1),.descriptions=g_new0(char *,length+1),
			.counts=g_memdup2(counts,length*sizeof(*counts)),
			.length=length,.current=current,.selected=selected,
		};
		for (size_t i=0;i<length;++i) {
			u->names[i]=g_strdup(names[i]);
			u->descriptions[i]=g_strdup(descriptions[i]);
		}
	}
	g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,paint_node_switch_update,u,free_node_switch_update);
}

static void connect_to_node(const char *name) {connect_node(name,TRUE);}

void panel_map_click(double x,double y) {
	if (!map_hud_arcade_active() && !relay_target && enabled && navigating && map_visible) map_hud_click(x,y,connect_to_node);
}

void panel_map_contact(uint32_t phase,double x,double y) {
	if (phase==MOTHERSHIP_MAP_POINTER_V1_CONTACT_PHASE_CANCEL || relay_target || map_hud_arcade_active() ||
			!enabled || !navigating || !map_visible) {
		cancel_map_drag();
		return;
	}
	if (phase==MOTHERSHIP_MAP_POINTER_V1_CONTACT_PHASE_BEGIN) {
		cancel_map_drag();
		map_drag.pending=TRUE;
		map_drag.can_drag=map_hud_drag_can_start(x,y);
		map_hud_press_connect(TRUE,x,y);
		map_hud_guide_grab(map_drag.can_drag);
		map_drag.x=x;map_drag.y=y;
		return;
	}
	if (!map_drag.pending) return;
	// If a candidate leaves the frame, do not resume on re-entry even
	// if the primary button is still down.
	if (map_drag.can_drag && !map_hud_drag_inside(x,y)) {
		cancel_map_drag();
		return;
	}
	double dx=x-map_drag.x,dy=y-map_drag.y;
	if (!map_drag.dragging && hypot(dx,dy)>=6) {
		if (!map_drag.can_drag) {
			cancel_map_drag();
			return;
		}
		map_drag.dragging=TRUE;
		stop_key_pan();
		map_hud_cancel_zoom();
	}
	if (map_drag.dragging) {
		// The pointer pulls content, opposite the world-camera translation.
		// This helper converts logical screen deltas by the current zoom.
		map_hud_translate(-dx,-dy);
		map_drag.x=x;map_drag.y=y;
	}
	if (phase==MOTHERSHIP_MAP_POINTER_V1_CONTACT_PHASE_END) {
		gboolean click=!map_drag.dragging;
		cancel_map_drag();
		if (click) panel_map_click(x,y);
	}
}

void panel_map_scroll(double x,double y,double delta) {
	if (!map_hud_arcade_active() && !relay_target && navigating && map_visible && !map_drag.dragging) {
		cancel_map_drag();
		map_hud_zoom(x,y,delta,map_animation);
	}
}

static void navigation_focused(void) {
	if (!navigation_timeout) return;
	if (map_action_wait && !map_action_sent) {
		map_action_sent = TRUE;
		panelAction(13, overview ? "" : "overview", "");
		return;
	}
	if (!map_visible || (map_action_wait && (!overview || !enabled))) return;
	g_source_remove(navigation_timeout);
	navigation_timeout = 0;
	map_action_wait = map_action_sent = FALSE;
	navigating = TRUE;
	map_hud_allow_route(map_animation!=MAP_ANIMATION_OFF);
	map_hud_open_effect(returning_map?MAP_ANIMATION_OFF:map_animation);
	if(returning_map) {
		map_hud_restore_selection(last_connected);
		returning_map=FALSE;
	}
	map_pointer_zoom_enabled(TRUE,map_hud_outer_margin());
	gtk_widget_set_visible(normal, FALSE);
	gtk_widget_set_visible(overview_form, FALSE);
	gtk_widget_set_visible(status, FALSE);
	gtk_widget_add_css_class(form, "mothership-map-keyboard");
	gtk_widget_grab_focus(form);
	schedule_region();
}

static gboolean navigation_focus_timeout(gpointer unused) {
	(void)unused;
	navigation_timeout = 0;
	map_action_wait = map_action_sent = FALSE;
	map_pointer_zoom_enabled(FALSE,map_hud_outer_margin());
	gtk_widget_remove_css_class(form,"mothership-map-keyboard");
	set_expanded(FALSE);
	panel_home_notice("Map View could not get keyboard focus. Try again.");
	return G_SOURCE_REMOVE;
}

static void open_map_view(GtkButton *button, gpointer unused) {
	(void)button; (void)unused;
	if (!enabled || navigation_timeout || navigating || dock_settings_open || map_settings_active) return;
	clear_app_grid();
	returning_map=!overview && current_node && *current_node;
	if(!returning_map) map_hud_deselect();
	map_action_wait = TRUE;
	navigation_timeout = g_timeout_add(750, navigation_focus_timeout, NULL);
	// The focus carrier is not a menu. Hide child paint before mapping it,
	// then keep it transparent on the intermediate Overview snapshot.
	gtk_widget_set_visible(normal,FALSE);
	gtk_widget_set_visible(overview_form,FALSE);
	gtk_widget_set_visible(status,FALSE);
	gtk_widget_add_css_class(form,"mothership-map-keyboard");
	if (!expanded) set_expanded(TRUE);
	else schedule_region();
	// Arm before asking for keyboard ownership so WM can retain this
	// already-focused shell across the acknowledged Overview projection.
	map_pointer_zoom_enabled(TRUE,map_hud_outer_margin());
	gtk_layer_set_keyboard_mode(window, KEYS_ON_DEMAND);
	if (gtk_window_is_active(window)) navigation_focused();
}

static void exit_map_view(void) {
	cancel_connect_relay();
	map_hud_dismiss_effect();
	set_expanded(FALSE);
	// Overview already hides app windows; only dismiss its Map overlay.
	if (overview && map_visible) panelAction(14, "", "");
}

static void lose_window_focus(GObject *window, GParamSpec *prop, gpointer data) {
	(void)prop; (void)data;
	if(screenshot_visible()) return;
	if (gtk_window_is_active(GTK_WINDOW(window))) {
		if(navigating && overview && map_visible) {
			// Parent focus returned to the same Map. Restore its existing
			// input mode, without replaying the reveal or changing view.
			if (!map_menu) gtk_widget_grab_focus(form);
			map_hud_arcade_focus(TRUE);
			map_hud_allow_route(map_animation!=MAP_ANIMATION_OFF);
			map_pointer_zoom_enabled(TRUE,map_hud_outer_margin());
		} else navigation_focused();
		if (map_menu) map_pointer_zoom_enabled(FALSE,map_hud_outer_margin());
	} else {
		if (slash_open) close_slash();
		if(picker_mouse) dismiss_window_picker(0);
		close_app_grid();
		app_grid_key_held=FALSE;
		default_node_key_held=FALSE;
		arcade_entry_held=arcade_escape_held=FALSE;
		arcade_keys_held=0;
		map_hud_arcade_focus(FALSE);
		shortcut_held=0;
		settings_comma_held=settings_escape_held=FALSE;
		number_held=0;
		if(navigating) {
			// Alt+Tab is focus loss, not a request to exit Map View.
			// Clear held/repeating motion, then await the next fresh input.
			stop_camera_input();
			map_pointer_zoom_enabled(FALSE,map_hud_outer_margin());
		} else if(!relay_target && map_action_wait && map_action_sent) set_expanded(FALSE);
	}
}

static unsigned modal_shortcut(guint key) {
	switch(gdk_keyval_to_lower(key)) {
	case GDK_KEY_m: return 1;
	case GDK_KEY_r: return 4;
	case GDK_KEY_n: return 8;
	default: return 0;
	}
}

static gboolean editing_input(void) {
	for(GtkWidget *w=gtk_window_get_focus(window);w;w=gtk_widget_get_parent(w))
		if (GTK_IS_EDITABLE(w) || GTK_IS_TEXT_VIEW(w)) return TRUE;
	return FALSE;
}

static gboolean map_plain_view(void) {
	return enabled && navigating && map_visible && !map_menu && !map_settings_active &&
		!dock_settings_open && !relay_target && !editing_input();
}

static gboolean shell_key_repeat(GtkEventControllerKey *keys,guint key,guint code,GdkModifierType mods,gpointer data) {
	(void)keys;(void)code;(void)data;
	if(screenshot_visible()) return screenshot_key(key,mods);
	return (app_grid_key_held && gdk_keyval_to_lower(key)==GDK_KEY_a) ||
		(settings_comma_held && key==GDK_KEY_comma);
}

static gboolean key_pressed(GtkEventControllerKey *keys, guint key, guint code,
		GdkModifierType mods, gpointer data) {
	(void)keys; (void)code; (void)data;
	if(screenshot_visible()) return FALSE;
	const GdkModifierType commands=GDK_SHIFT_MASK|GDK_CONTROL_MASK|GDK_ALT_MASK|GDK_SUPER_MASK|GDK_META_MASK|GDK_HYPER_MASK;
	guint lower=gdk_keyval_to_lower(key);
	if(notice_log_visible() && (lower==GDK_KEY_w || lower==GDK_KEY_a)) return FALSE;
	unsigned game_bit=map_arcade_key_bit(key);
	if (map_hud_arcade_active()) {
		if (mods & commands) return FALSE;
		if (key==GDK_KEY_Escape) {
			arcade_escape_held=TRUE;
			map_hud_arcade_stop();
			map_hud_allow_route(map_animation!=MAP_ANIMATION_OFF);
		} else if(game_bit) {
			arcade_keys_held|=game_bit;
			map_hud_arcade_key(key,TRUE);
		}
		return TRUE;
	}
	// Auto-repeat from an arcade press cannot pan/deselect the restored Map.
	if ((key==GDK_KEY_Escape && arcade_escape_held) ||
	    (game_bit && (arcade_keys_held&game_bit)) ||
	    (lower==GDK_KEY_g && arcade_entry_held)) return TRUE;
	if(key==GDK_KEY_g && !(mods & commands) && enabled && navigating &&
	   map_visible && !relay_target && !map_menu && !map_settings_active &&
	   !dock_settings_open && !app_grid_open && !slash_open && !editing_input() &&
	   map_hud_arcade_start()) {
		arcade_entry_held=TRUE;
		stop_camera_input();
		return TRUE;
	}
	if (relay_target && navigating) {
		if((key==GDK_KEY_Escape || lower==GDK_KEY_q) && !(mods & commands)) {
			exit_map_view(); return TRUE;
		}
		if(key==GDK_KEY_m &&
				!(mods & (GDK_SHIFT_MASK|GDK_CONTROL_MASK|GDK_ALT_MASK|GDK_SUPER_MASK|GDK_META_MASK|GDK_HYPER_MASK))) {
			exit_map_view(); return TRUE;
		}
		return TRUE;
	}
	if (key==GDK_KEY_comma && !(mods & commands)) {
		if (editing_input()) return FALSE;
		if (map_settings_active && navigating) {
			settings_comma_held=TRUE;
			cancel_map_settings(NULL,NULL);
			return TRUE;
		}
		if (map_plain_view()) {
			settings_comma_held=TRUE;
			map_menu_set(TRUE);
			open_map_settings(NULL,NULL);
			return TRUE;
		}
		gboolean blank_shell=blank_home || (overview && !map_visible);
		if (enabled && blank_shell && gtk_window_is_active(window) && !map_visible &&
				!navigating && !navigation_timeout && !slash_open && !app_grid_open &&
				(!expanded || dock_settings_open)) {
			settings_comma_held=TRUE;
			if(dock_settings_open) dock_cancel(NULL,NULL);
			else open_dock_settings();
			return TRUE;
		}
	}
	if (key==GDK_KEY_Escape && !(mods & commands)) {
		if (settings_escape_held) return TRUE;
		if (map_settings_active || dock_settings_open) {
			settings_escape_held=TRUE;
			if(map_settings_active) cancel_map_settings(NULL,NULL); else dock_cancel(NULL,NULL);
			return TRUE;
		}
	}
	if(picker_mouse && !(mods & commands) && picker_key(key)) return TRUE;
	if (key == GDK_KEY_Escape) {
		if(node_settings && gtk_widget_get_visible(node_settings)) {node_settings_close();return TRUE;}
		if(system_tray_close())return TRUE;
		if(node_volume_close())return TRUE;
		if(notice_log_close()) return TRUE;
		if(home_dock_hangar_close()) return TRUE;
		if(app_grid_open) {close_app_grid();return TRUE;}
		if(slash_open) {if(slash_ui_composing()) return FALSE;close_slash();return TRUE;}
		if(map_menu) {map_menu_set(FALSE);return TRUE;}
		if(expanded && !navigating) {set_expanded(FALSE);return TRUE;}
		if(map_plain_view() && !(mods & commands)) {exit_map_view();return TRUE;}
		return FALSE;
	}
	if (lower==GDK_KEY_q && !(mods & commands) && notice_log_close()) return TRUE;
	if (map_plain_view() && !(mods & commands)) {
		if (lower==GDK_KEY_q) {exit_map_view();return TRUE;}
		if (key==GDK_KEY_space) {
			if(!map_space_held) {
				map_space_held=TRUE;
				cancel_map_drag();
				stop_key_pan();
				map_hud_cancel_zoom();
				map_hud_center_mothership();
				map_hud_guide_command(MAP_GUIDE_SPACE,TRUE);
			}
			return TRUE;
		}
	}
	if(gdk_keyval_to_lower(key)==GDK_KEY_a && app_grid_key_held) return TRUE;
	if(app_grid_open) return FALSE;
	if(lower==GDK_KEY_w && default_node_key_held) return TRUE;
	if(key==GDK_KEY_w && !(mods & commands) && enabled && default_node_enabled &&
			gtk_window_is_active(window) && !expanded && !slash_open && !navigating &&
			!map_visible && !editing_input() && (blank_home || overview)) {
		default_node_key_held=TRUE;
		panelAction(16,"","");
		return TRUE;
	}
	if(key==GDK_KEY_a && !(mods & commands) && can_open_app_grid() && !editing_input()) {
		app_grid_key_held=TRUE;
		open_app_grid();
		return TRUE;
	}
	if(key==GDK_KEY_slash && !(mods & commands) && enabled && !expanded && !editing_input() &&
			!navigating && !map_visible && (blank_home || (overview && !map_visible))) {
		open_slash(FALSE);
		return TRUE;
	}
	unsigned shortcut=modal_shortcut(key);
	if (shortcut==8) {
		if(notice_log_visible() && gtk_window_is_active(window) && !(mods & commands)) {
			if(!(shortcut_held&shortcut)){shortcut_held|=shortcut;notice_log_close();}
			return TRUE;
		}
		if (!enabled || !(blank_home || overview) || !gtk_window_is_active(window) ||
				expanded || navigating || map_visible || navigation_timeout ||
				slash_open || editing_input() || (mods & commands)) return FALSE;
		if (!(shortcut_held&shortcut)) {
			shortcut_held|=shortcut;
			home_dock_hangar_close();
			system_tray_close();
			notice_log_toggle();
		}
		return TRUE;
	}
	if (shortcut==4) {
		// Closing Map leaves an intentionally blank, shell-owned Overview
		// instead of exposing the apps in Home / Inbox.
		gboolean blank_shell=blank_home || (overview && !map_visible);
		if (!enabled || !blank_shell || !gtk_window_is_active(window) || expanded ||
				navigating || navigation_timeout || editing_input() ||
				(mods & (GDK_SHIFT_MASK|GDK_CONTROL_MASK|GDK_ALT_MASK|GDK_SUPER_MASK|GDK_META_MASK|GDK_HYPER_MASK))) return FALSE;
		if (!(shortcut_held&shortcut)) {
			shortcut_held|=shortcut;
			if(!panelReloadConfig()) panel_home_notice("Could not reload. Kept your current options.");
			else panel_home_notice("Configuration reloaded.");
		}
		return TRUE;
	}
	if(shortcut==1 && (dock_settings_open || map_settings_active) &&
			!(mods & commands) && !editing_input()) return TRUE;
	if (shortcut && !(mods & (GDK_SHIFT_MASK|GDK_CONTROL_MASK|GDK_ALT_MASK|GDK_SUPER_MASK|GDK_META_MASK|GDK_HYPER_MASK))
			&& !editing_input() && !map_settings_active) {
		if (shortcut_held & shortcut) return TRUE;
		if (enabled && (overview || blank_home)) {
			shortcut_held |= shortcut;
			if (overview && map_visible) exit_map_view();
			else open_map_view(NULL,NULL);
			return TRUE;
		}
	}
	if (!navigating || map_menu || editing_input()) return FALSE;
	if ((key==GDK_KEY_Return || key==GDK_KEY_KP_Enter) && !(mods & commands)) {
		connect_node(map_hud_selected(),TRUE);
		return TRUE;
	}
	if (key>=GDK_KEY_1 && key<=GDK_KEY_9 &&
			!(mods & (GDK_SHIFT_MASK|GDK_CONTROL_MASK|GDK_ALT_MASK|GDK_SUPER_MASK|GDK_META_MASK|GDK_HYPER_MASK)) &&
			!editing_input()) {
		unsigned bit=1u<<(key-GDK_KEY_1);
		if (!(number_held&bit)) {
			number_held|=bit;
			enter_numbered_node((uint32_t)(key-GDK_KEY_1)+1);
		}
		return TRUE;
	}
	unsigned dir=direction(key);
	if (!dir || (mods & (GDK_CONTROL_MASK|GDK_ALT_MASK|GDK_SUPER_MASK))) return FALSE;
	if (map_drag.dragging) return TRUE;
	if (!(camera_held & dir)) {
		camera_held|=dir;
		directions_changed();
	}
	return TRUE;
}

static void key_released(GtkEventControllerKey *keys,guint key,guint code,
		GdkModifierType mods,gpointer data) {
	(void)keys; (void)code; (void)mods; (void)data;
	if(key==GDK_KEY_space && map_space_held) {
		map_space_held=FALSE;
		map_hud_guide_command(MAP_GUIDE_SPACE,FALSE);
	}
	if(key==GDK_KEY_Escape) arcade_escape_held=settings_escape_held=FALSE;
	if(key==GDK_KEY_comma) settings_comma_held=FALSE;
	if(gdk_keyval_to_lower(key)==GDK_KEY_g) arcade_entry_held=FALSE;
	arcade_keys_held&=~map_arcade_key_bit(key);
	map_hud_arcade_key(key,FALSE);
	if(gdk_keyval_to_lower(key)==GDK_KEY_a) app_grid_key_held=FALSE;
	if(gdk_keyval_to_lower(key)==GDK_KEY_w) default_node_key_held=FALSE;
	shortcut_held&=~modal_shortcut(key);
	if(key>=GDK_KEY_1 && key<=GDK_KEY_9) number_held&=~(1u<<(key-GDK_KEY_1));
	unsigned dir=direction(key);
	if(navigating && (camera_held&dir)) {
		camera_held&=~dir;
		directions_changed();
	}
}

static void submit(GtkWidget *widget, gpointer data) {
	int op = GPOINTER_TO_INT(data);
	if (op == 0) {
		return_keys();
		return;
	}
	if (!enabled) {
		return;
	}
	const char *node = op == 1 ? gtk_editable_get_text(GTK_EDITABLE(entry)) : selected(nodes);
	const char *ref = selected(windows);
	if (op >= 8 && op <= 11) {
		node = current_node;
	}
	if (((op == 1 || op == 2 || op == 3) && (node == NULL || !*node)) ||
			(op == 2 && ref == NULL)) {
		return;
	}
	if(op==3) {connect_to_node(node);return;}
	// Release the toolkit's keyboard target; the compositor preserves or
	// reconciles the application's own focus through the existing WM path.
	char *a = g_strdup(node == NULL ? "" : node);
	char *b = g_strdup(ref == NULL ? "" : ref);
	return_keys();
	panelAction(op, a, b);
	g_free(a);
	g_free(b);
}

static void build_list(GtkWidget *list, const char *rows, gboolean composite) {
	char *previous = g_strdup(selected(list));
	GtkWidget *child;
	while ((child = gtk_widget_get_first_child(list)) != NULL) {
		gtk_list_box_remove(GTK_LIST_BOX(list), child);
	}
	char **lines = g_strsplit(rows, "\n", -1);
	for (int i = 0; lines[i] != NULL; ++i) {
		char *key = lines[i], *label = lines[i];
		if (!*key) {
			continue;
		}
		if (composite) {
			char *sep = strchr(key, '\t');
			if (sep == NULL) {
				continue;
			}
			*sep = 0;
			label = sep + 1;
		}
		GtkWidget *row = gtk_list_box_row_new();
		GtkWidget *body = gtk_label_new(label);
		gtk_label_set_xalign(GTK_LABEL(body), 0);
		gtk_label_set_ellipsize(GTK_LABEL(body), PANGO_ELLIPSIZE_END);
		gtk_widget_set_margin_start(body, 6);
		gtk_widget_set_margin_end(body, 6);
		gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), body);
		g_object_set_data_full(G_OBJECT(row), "key", g_strdup(key), g_free);
		gtk_list_box_append(GTK_LIST_BOX(list), row);
		if (previous != NULL && strcmp(previous, key) == 0) {
			gtk_list_box_select_row(GTK_LIST_BOX(list), GTK_LIST_BOX_ROW(row));
		}
	}
	g_strfreev(lines);
	g_free(previous);
}

static void enter_node(GtkButton *button,gpointer data) {
	(void)data;
	connect_to_node(g_object_get_data(G_OBJECT(button),"node"));
}

static void build_overview(const char *names) {
	while (gtk_widget_get_first_child(node_grid) != NULL) {
		gtk_grid_remove(GTK_GRID(node_grid), gtk_widget_get_first_child(node_grid));
	}
	char **lines = g_strsplit(names, "\n", -1);
	for (int i = 0; lines[i] != NULL; ++i) {
		if (!*lines[i]) continue;
		unsigned at = GPOINTER_TO_UINT(g_hash_table_lookup(positions, lines[i]));
		if (at == 0) {
			at = ++node_count;
			g_hash_table_insert(positions, g_strdup(lines[i]), GUINT_TO_POINTER(at));
		}
		GtkWidget *btn = gtk_button_new_with_label(lines[i]);
		gtk_widget_set_size_request(btn, 130, 48);
		g_object_set_data_full(G_OBJECT(btn), "node", g_strdup(lines[i]), g_free);
		g_signal_connect(btn,"clicked",G_CALLBACK(enter_node),NULL);
		gtk_grid_attach(GTK_GRID(node_grid), btn, (at - 1) % 3, (at - 1) / 3, 1, 1);
	}
	g_strfreev(lines);
}

static void free_update(struct update *u) {
	if (u != NULL) {
		g_free(u->view); g_free(u->message); g_free(u->nodes); g_free(u->windows);
		g_free(u->node); g_free(u->mode); g_free(u->profiles);
		g_free(u);
	}
}

static gboolean apply_update(gpointer data) {
	g_mutex_lock(&update_lock);
	struct update *u = queued;
	queued = NULL;
	update_scheduled = FALSE;
	g_mutex_unlock(&update_lock);
	gtk_label_set_text(GTK_LABEL(title), u->view);
	gtk_label_set_text(GTK_LABEL(status),u->message);
	if (g_strcmp0(last_nodes, u->nodes) != 0) {
		g_free(last_nodes);
		last_nodes = g_strdup(u->nodes);
		slash_ui_nodes(last_nodes);
		home_dock_set_nodes(last_nodes);
		build_list(nodes, u->nodes, FALSE);
		build_overview(u->nodes);
	}
	if (g_strcmp0(last_windows, u->windows) != 0) {
		g_free(last_windows);
		last_windows = g_strdup(u->windows);
		build_list(windows, u->windows, TRUE);
	}
	enabled = u->enabled;
	desktop_ready = u->ready;
	blank_home = u->blank_home;
	gboolean node_changed=default_node!=(u->default_node!=0) ||
		g_strcmp0(current_node?current_node:"",u->node)!=0;
	gboolean entered_node=node_changed && (*u->node || u->default_node);
	if(slash_open && slash_in_node && node_changed) close_slash();
	default_node=u->default_node;
	g_free(current_node);
	g_free(current_mode);
	current_node = g_strdup(u->node);
	if(node_log_keys && g_strcmp0(u->node,"")==0 && !default_node)notice_log_dismiss(TRUE);
	if(entered_node)notice_log_dismiss(TRUE);
	notice_context(*current_node?current_node:default_node?"@default":"@home",saved_toast_mode);
	panel_redraw_node_bell();
	current_mode = g_strdup(u->mode);
	if(*u->node) {
		g_free(last_connected);
		last_connected=g_strdup(u->node);
	}
	gboolean show_nodes=u->map_visible && u->overview;
	gboolean keep_relay_paint=FALSE;
	if (relay_target) {
		gboolean selected=u->ready && *u->node && g_str_equal(u->node,relay_target);
		if(relay_revealing && selected) {
			if(!relay_projected) {
				relay_projected=TRUE;
				g_clear_handle_id(&relay_timeout,g_source_remove);
				end_navigation(TRUE);
				map_hud_retract(connect_retract_finished);
			}
			keep_relay_paint=TRUE;
		} else if (relay_projected || !show_nodes || !u->ready || u->connect_failed==relay_token) {
			if(u->connect_failed==relay_token && navigating)
				g_warning("Map connection: %s",u->message);
			cancel_connect_relay();
		} else if(!relay_acknowledged && u->prepared_relay==relay_token) {
			relay_acknowledged=TRUE;
			map_hud_relay_ack();
		}
	}
	if (overview != (u->overview != 0)) {
		overview = u->overview != 0;
		gtk_widget_set_visible(normal, !map_settings_active && !navigation_timeout && !navigating && !overview);
		gtk_widget_set_visible(overview_form, !map_settings_active && !navigation_timeout && !navigating && overview);
		if (!map_action_wait || !overview) set_expanded(FALSE);
	} else if (map_visible != show_nodes && overview) {
		// Keep the clicked Map View surface until the focus handshake.
		if ((!show_nodes || !navigation_timeout) && !map_action_wait)
			set_expanded(FALSE);
	}
	if (entered_node && !overview) {
		clear_app_grid();
		close_slash();
		if(expanded) set_expanded(FALSE);
	}
	map_visible = show_nodes;
	if(map_visible && (app_grid_open || gtk_widget_get_visible(app_grid))) clear_app_grid();
	if(gtk_window_is_active(window)) navigation_focused();
	if(!map_visible && navigating) stop_navigation();
	map_hud_update(u->nodes, positions, u->node, u->profiles, overview, map_visible || keep_relay_paint);
	refresh_actions();
	update_dock();
	if (!showing && enabled) {
		showing = TRUE;
		gtk_window_present(window);
		schedule_region();
	}
	free_update(u);
	return G_SOURCE_REMOVE;
}

void panel_post(const char *view, const char *message, const char *d, const char *w, const char *node, const char *mode, const char *profiles, int can_submit, int overview_on, int map_visible_on, int blank_home_on,int default_node_on,int ready,uint64_t connect_failed,uint64_t prepared_relay) {
	struct update *u = g_new(struct update, 1);
	*u = (struct update){ g_strdup(view), g_strdup(message), g_strdup(d), g_strdup(w),
		g_strdup(node), g_strdup(mode), g_strdup(profiles), can_submit, overview_on, map_visible_on, blank_home_on,default_node_on,ready,connect_failed,prepared_relay };
	g_mutex_lock(&update_lock);
	free_update(queued);
	queued = u;
	if (!update_scheduled) {
		update_scheduled = TRUE;
		g_idle_add(apply_update, NULL);
	}
	g_mutex_unlock(&update_lock);
}

static GtkWidget *button(const char *label, int op, GtkWidget *box) {
	GtkWidget *w = gtk_button_new_with_label(label);
	g_signal_connect(w, "clicked", G_CALLBACK(submit), GINT_TO_POINTER(op));
	gtk_box_append(GTK_BOX(box), w);
	return w;
}

static GtkWidget *list_column(GtkWidget *box, const char *heading) {
	GtkWidget *column = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
	gtk_widget_set_hexpand(column, TRUE);
	gtk_box_append(GTK_BOX(box), column);
	GtkWidget *label = gtk_label_new(heading);
	gtk_widget_add_css_class(label,"shell-readout");
	gtk_label_set_xalign(GTK_LABEL(label), 0);
	gtk_box_append(GTK_BOX(column), label);
	GtkWidget *scroll = gtk_scrolled_window_new();
	gtk_widget_set_size_request(scroll, 140, 72);
	gtk_widget_set_vexpand(scroll, TRUE);
	gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(scroll), TRUE);
	gtk_box_append(GTK_BOX(column), scroll);
	GtkWidget *list = gtk_list_box_new();
	g_signal_connect(list, "row-selected", G_CALLBACK(list_changed), NULL);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), list);
	return list;
}

static gboolean close_window(GtkWindow *w, gpointer data) {
	cancel_connect_relay();
	return_keys();
	g_main_loop_quit(loop);
	return FALSE;
}

static gboolean valid_border_color(const char *color) {
	size_t length = strlen(color);
	if ((length != 7 && length != 9) || color[0] != '#') return FALSE;
	for (size_t i = 1; i < length; ++i)
		if (!g_ascii_isxdigit(color[i])) return FALSE;
	return TRUE;
}

static enum map_animation selected_map_animation(void) {
	for (int i=0;i<MAP_ANIMATION_COUNT;i++)
		if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(map_animation_input[i]))) return i;
	return MAP_ANIMATION_LINEAR;
}

static void update_pan_setting(void) {
	int speed=effective_pan_speed();
	char label[12];
	g_snprintf(label,sizeof label,"%d%%",speed);
	gtk_label_set_text(GTK_LABEL(map_pan_label),label);
	gtk_widget_set_sensitive(map_pan_minus,speed>MAP_PAN_MIN);
	gtk_widget_set_sensitive(map_pan_plus,speed<MAP_PAN_MAX);
}

static void change_pan_speed(GtkButton *button,gpointer data) {
	(void)button;
	int step=GPOINTER_TO_INT(data);
	pan_override=step?CLAMP(effective_pan_speed()+step,MAP_PAN_MIN,MAP_PAN_MAX):0;
	update_pan_setting();
	if (navigating) directions_changed();
}

static void preview_map_settings(GtkWidget *widget, gpointer data) {
	(void)widget; (void)data;
	if (!map_settings_active || settings_populating) return;
	map_animation=selected_map_animation();
	gboolean show_fps=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(map_fps_input));
	map_hud_show_fps(show_fps);
	analog_button_label(map_fps_input,show_fps?"On":"Off");
	gtk_widget_queue_draw(map_border_swatch);
	const char *color = gtk_editable_get_text(GTK_EDITABLE(map_border_input));
	bool valid = valid_border_color(color);
	gtk_widget_set_sensitive(map_settings_save, valid);
	gtk_label_set_text(GTK_LABEL(map_settings_note),valid ? "" : "Enter #RRGGBB or #RRGGBBAA.");
	gtk_widget_set_visible(map_settings_note,!valid);
	map_hud_set_spacing(valid ? color : saved_map_border,
		gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(map_outer_input)),
		gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(map_inner_input)),
		gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(map_frosted_input)));
	update_dock();
}

void panel_set_map_settings(const char *border, int outer, int inner, int frosted, int animation, int show_fps, int theme_pan, int requested_pan_override, int max_outer, int max_inner, const char *default_border, int default_outer, int default_inner) {
	g_free(default_map_border);
	default_map_border=g_strdup(default_border);
	default_map_outer=default_outer;default_map_inner=default_inner;
	g_free(saved_map_border);
	saved_map_border = g_strdup(border);
	saved_map_outer = outer;
	saved_map_inner = inner;
	saved_map_frosted = frosted != 0;
	map_animation=saved_map_animation=animation;
	saved_map_fps=show_fps!=0;
	theme_pan_speed=CLAMP(theme_pan,MAP_PAN_MIN,MAP_PAN_MAX);
	pan_override=saved_pan_override=requested_pan_override;
	update_pan_setting();
	map_hud_show_fps(saved_map_fps);
	gtk_spin_button_set_range(GTK_SPIN_BUTTON(map_outer_input), 0, max_outer);
	gtk_spin_button_set_range(GTK_SPIN_BUTTON(map_inner_input), 0, max_inner);
	map_hud_set_spacing(border, outer, inner, saved_map_frosted);
}

static void open_map_settings(GtkButton *button, gpointer data) {
	(void)button; (void)data;
	if (!saved_map_border) return;
	map_settings_active = TRUE;
	settings_populating = TRUE;
	gtk_editable_set_text(GTK_EDITABLE(map_border_input), saved_map_border);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(map_outer_input), saved_map_outer);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(map_inner_input), saved_map_inner);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(saved_map_frosted ? map_frosted_input : map_clear_input), TRUE);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(map_animation_input[saved_map_animation]), TRUE);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(map_fps_input),saved_map_fps);
	pan_override=saved_pan_override;
	update_pan_setting();
	settings_populating = FALSE;
	gtk_widget_set_visible(normal, FALSE);
	gtk_widget_set_visible(overview_form, FALSE);
	gtk_widget_set_visible(status, FALSE);
	gtk_widget_add_css_class(form, "mothership-settings-surface");
	gtk_widget_add_css_class(form, "shell-settings-positioned");
	gtk_widget_set_overflow(form, GTK_OVERFLOW_HIDDEN);
	settings_motion_open(map_settings_form);
	preview_map_settings(NULL, NULL);
	gtk_widget_grab_focus(form);
	schedule_region();
}

static gboolean finish_settings_close(gpointer data) {
	(void)data;settings_close_id=0;
	if(map_menu && navigating) map_menu_set(FALSE);
	else set_expanded(FALSE);
	return G_SOURCE_REMOVE;
}
static void request_settings_close(GtkWidget *pane) {
	if(settings_close_id) return;
	if(!gtk_widget_get_mapped(pane) || !settings_motion_allowed(pane)) {
		finish_settings_close(NULL);return;
	}
	gtk_widget_remove_css_class(pane,"settings-enter");
	gtk_widget_remove_css_class(pane,"settings-enter-again");
	gtk_widget_add_css_class(pane,"settings-exit");
	settings_close_id=g_timeout_add(SETTINGS_MOTION_MS+17,finish_settings_close,NULL);
}
static void cancel_map_settings(GtkButton *button, gpointer data) {
	(void)button; (void)data;
	request_settings_close(map_settings_form);
}

static void save_map_settings(GtkButton *button, gpointer data) {
	(void)button; (void)data;
	if(settings_close_id) return;
	const char *color=saved_map_border;
	int outer=saved_map_outer, inner=saved_map_inner;
	gboolean frosted=saved_map_frosted;
	color=gtk_editable_get_text(GTK_EDITABLE(map_border_input));
	if (!valid_border_color(color)) return;
	gtk_spin_button_update(GTK_SPIN_BUTTON(map_outer_input));
	gtk_spin_button_update(GTK_SPIN_BUTTON(map_inner_input));
	outer=gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(map_outer_input));
	inner=gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(map_inner_input));
	frosted=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(map_frosted_input));
	enum map_animation animation=selected_map_animation();
	gboolean show_fps=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(map_fps_input));
	char *error=panelSaveMapSettings((char *)color,outer,inner,frosted,animation,show_fps,pan_override);
	if (error != NULL) {
		gtk_label_set_text(GTK_LABEL(map_settings_note), error);
		gtk_widget_set_visible(map_settings_note,TRUE);
		free(error);
		return;
	}
	g_free(saved_map_border);
	saved_map_border=g_strdup(color);
	saved_map_outer = outer;
	saved_map_inner = inner;
	saved_map_frosted = frosted;
	saved_map_animation=animation;
	saved_map_fps=show_fps;
	saved_pan_override=pan_override;
	request_settings_close(map_settings_form);
}

static void settings_insert(GtkGrid *grid, int row, const char *label, GtkWidget *entry) {
	GtkWidget *name=gtk_label_new_with_mnemonic(label);
	gtk_label_set_mnemonic_widget(GTK_LABEL(name),entry);
	gtk_widget_set_halign(name,GTK_ALIGN_START);
	gtk_widget_set_hexpand(name,TRUE);
	gtk_grid_attach(grid,name,0,row,1,1);
	gtk_widget_set_halign(entry,GTK_ALIGN_END);
	gtk_grid_attach(grid,entry,1,row,1,1);
}

static void settings_section(GtkGrid *grid,int row,const char *label) {
	GtkWidget *heading=gtk_label_new(label);
	gtk_widget_set_halign(heading,GTK_ALIGN_START);
	gtk_widget_add_css_class(heading,"shell-settings-section");
	gtk_grid_attach(grid,heading,0,row,2,1);
}

static void reset_map_settings(GtkButton *button,gpointer data) {
	(void)button;(void)data;
	settings_populating=TRUE;
	gtk_editable_set_text(GTK_EDITABLE(map_border_input),default_map_border);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(map_outer_input),default_map_outer);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(map_inner_input),default_map_inner);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(map_clear_input),TRUE);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(map_animation_input[MAP_ANIMATION_LINEAR]),TRUE);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(map_fps_input),FALSE);
	change_pan_speed(NULL,NULL);
	settings_populating=FALSE;
	preview_map_settings(NULL,NULL);
}

static void draw_settings_face(GtkDrawingArea *a,cairo_t *cr,int w,int h,gpointer unused) {
	(void)a;(void)unused;
	map_frame_outline(cr,1,1,w-2,h-2,0);
	cairo_set_source_rgb(cr,.055,.106,.145);
	cairo_fill(cr);
	GdkRGBA steel={.45,.60,.67,1};
	map_frame_strokes(cr,1,1,w-2,h-2,&steel);
}

static void add_settings_button(GtkBox *bar) {
	GtkWidget *control = gtk_button_new_with_label("Map settings");
	analog_button_style(control,9,5);
	g_signal_connect(control, "clicked", G_CALLBACK(open_map_settings), NULL);
	gtk_box_append(bar, control);
}

static void draw_border_swatch(GtkDrawingArea *area,cairo_t *cr,int w,int h,gpointer unused) {
	(void)area;(void)unused;
	GdkRGBA color;
	const char *text=gtk_editable_get_text(GTK_EDITABLE(map_border_input));
	if (!valid_border_color(text)) text=saved_map_border;
	if (!text || !gdk_rgba_parse(&color,text)) return;
	double y=(h-20)*.5;
	cairo_move_to(cr,4,y+.5);
	cairo_line_to(cr,w-4,y+.5);cairo_line_to(cr,w-.5,y+4);
	cairo_line_to(cr,w-.5,y+16);cairo_line_to(cr,w-4,y+19.5);
	cairo_line_to(cr,4,y+19.5);cairo_line_to(cr,.5,y+16);
	cairo_line_to(cr,.5,y+4);cairo_close_path(cr);
	cairo_set_source_rgba(cr,color.red,color.green,color.blue,color.alpha);cairo_fill_preserve(cr);
	cairo_set_source_rgb(cr,.42,.60,.69);cairo_set_line_width(cr,1);cairo_stroke(cr);
}

static void style_map_stepper(GtkWidget *spin) {
	for(GtkWidget *w=gtk_widget_get_first_child(spin);w;w=gtk_widget_get_next_sibling(w))
		if(GTK_IS_BUTTON(w)) analog_button_style(w,7,4);
}

static void init_map_settings(void) {
	map_settings_form=gtk_overlay_new();
	gtk_widget_add_css_class(map_settings_form,"map-settings-motion");
	gtk_widget_set_visible(map_settings_form,FALSE);
	gtk_box_append(GTK_BOX(form),map_settings_form);
	gtk_overlay_set_child(GTK_OVERLAY(map_settings_form),analog_glass_new());
	GtkWidget *content=gtk_box_new(GTK_ORIENTATION_VERTICAL,9);
	gtk_widget_add_css_class(content,"shell-settings-content");
	gtk_overlay_add_overlay(GTK_OVERLAY(map_settings_form),content);
	gtk_overlay_set_measure_overlay(GTK_OVERLAY(map_settings_form),content,TRUE);
	gtk_widget_set_vexpand(map_settings_form,TRUE);

	GtkWidget *head=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6);
	GtkWidget *heading=gtk_label_new("MAP SETTINGS");
	gtk_widget_set_halign(heading,GTK_ALIGN_START);
	gtk_widget_set_hexpand(heading,TRUE);
	gtk_widget_add_css_class(heading,"shell-settings-title");
	gtk_box_append(GTK_BOX(head),heading);
	GtkWidget *comma=gtk_label_new(",");
	gtk_widget_add_css_class(comma,"shell-settings-key");
	gtk_box_append(GTK_BOX(head),comma);
	GtkWidget *esc=gtk_button_new_with_label("Esc");
	analog_button_style(esc,9,5);
	gtk_accessible_update_property(GTK_ACCESSIBLE(esc),GTK_ACCESSIBLE_PROPERTY_DESCRIPTION,"Close without saving",-1);
	g_signal_connect(esc,"clicked",G_CALLBACK(cancel_map_settings),NULL);
	gtk_box_append(GTK_BOX(head),esc);
	gtk_box_append(GTK_BOX(content),head);
	gtk_box_append(GTK_BOX(content),gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

	GtkWidget *body=gtk_scrolled_window_new();
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(body),GTK_POLICY_NEVER,GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_overlay_scrolling(GTK_SCROLLED_WINDOW(body),FALSE);
	gtk_widget_set_vexpand(body,TRUE);
	gtk_box_append(GTK_BOX(content),body);
	GtkGrid *grid=GTK_GRID(gtk_grid_new());
	gtk_grid_set_column_spacing(grid,9);
	gtk_grid_set_row_spacing(grid,13);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(body),GTK_WIDGET(grid));
	settings_section(grid,0,"CAMERA");
	GtkWidget *speed=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,7);
	map_pan_minus=gtk_button_new_with_label("−");
	analog_button_style(map_pan_minus,9,5);
	map_pan_plus=gtk_button_new_with_label("+");
	analog_button_style(map_pan_plus,9,5);
	map_pan_label=gtk_label_new("100%");
	gtk_label_set_width_chars(GTK_LABEL(map_pan_label),5);
	gtk_accessible_update_property(GTK_ACCESSIBLE(map_pan_minus),GTK_ACCESSIBLE_PROPERTY_LABEL,
		"Decrease pan speed",-1);
	gtk_accessible_update_property(GTK_ACCESSIBLE(map_pan_plus),GTK_ACCESSIBLE_PROPERTY_LABEL,
		"Increase pan speed",-1);
	gtk_box_append(GTK_BOX(speed),map_pan_minus);
	gtk_box_append(GTK_BOX(speed),map_pan_label);
	gtk_box_append(GTK_BOX(speed),map_pan_plus);
	settings_insert(grid,1,"Pan speed",speed);
	g_signal_connect(map_pan_minus,"clicked",G_CALLBACK(change_pan_speed),GINT_TO_POINTER(-MAP_PAN_STEP));
	g_signal_connect(map_pan_plus,"clicked",G_CALLBACK(change_pan_speed),GINT_TO_POINTER(MAP_PAN_STEP));
	GtkWidget *animations=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,5);
	const char *labels[]={"Off","Linear","Ease"};
	for(int i=0;i<MAP_ANIMATION_COUNT;i++) {
		map_animation_input[i]=gtk_toggle_button_new_with_label(labels[i]);
		analog_button_style(map_animation_input[i],9,5);
		if(i) gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(map_animation_input[i]),
			GTK_TOGGLE_BUTTON(map_animation_input[0]));
		gtk_box_append(GTK_BOX(animations),map_animation_input[i]);
		g_signal_connect(map_animation_input[i],"toggled",G_CALLBACK(preview_map_settings),NULL);
	}
	settings_insert(grid,2,"Motion",animations);
	gtk_grid_attach(grid,gtk_separator_new(GTK_ORIENTATION_HORIZONTAL),0,3,2,1);
	settings_section(grid,4,"DISPLAY");
	GtkWidget *choices=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,5);
	map_clear_input=gtk_toggle_button_new_with_label("Clear");
	analog_button_style(map_clear_input,9,5);
	map_frosted_input=gtk_toggle_button_new_with_label("Frosted");
	analog_button_style(map_frosted_input,9,5);
	gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(map_frosted_input),GTK_TOGGLE_BUTTON(map_clear_input));
	gtk_box_append(GTK_BOX(choices),map_clear_input);
	gtk_box_append(GTK_BOX(choices),map_frosted_input);
	settings_insert(grid,5,"Glass",choices);
	g_signal_connect(map_frosted_input,"toggled",G_CALLBACK(preview_map_settings),NULL);
	GtkWidget *hint=gtk_label_new("Frosted glass increases GPU load with video wallpaper.");
	gtk_widget_add_css_class(hint,"shell-settings-hint");
	gtk_label_set_xalign(GTK_LABEL(hint),0);
	gtk_label_set_wrap(GTK_LABEL(hint),TRUE);
	gtk_grid_attach(grid,hint,0,6,2,1);
	map_outer_input=gtk_spin_button_new_with_range(0,1,1);
	map_inner_input=gtk_spin_button_new_with_range(0,1,1);
	style_map_stepper(map_outer_input);style_map_stepper(map_inner_input);
	gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(map_outer_input),TRUE);
	gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(map_inner_input),TRUE);
	gtk_editable_set_width_chars(GTK_EDITABLE(map_outer_input),4);
	gtk_editable_set_width_chars(GTK_EDITABLE(map_inner_input),4);
	settings_insert(grid,7,"Outer margin",map_outer_input);
	settings_insert(grid,8,"Grid padding",map_inner_input);
	g_signal_connect(map_outer_input,"value-changed",G_CALLBACK(preview_map_settings),NULL);
	g_signal_connect(map_inner_input,"value-changed",G_CALLBACK(preview_map_settings),NULL);

	GtkWidget *display=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,7);
	map_border_input=gtk_entry_new();
	gtk_entry_set_max_length(GTK_ENTRY(map_border_input),9);
	gtk_editable_set_width_chars(GTK_EDITABLE(map_border_input),9);
	gtk_accessible_update_property(GTK_ACCESSIBLE(map_border_input),GTK_ACCESSIBLE_PROPERTY_DESCRIPTION,"Use #RRGGBB or #RRGGBBAA",-1);
	gtk_accessible_update_property(GTK_ACCESSIBLE(map_border_input),GTK_ACCESSIBLE_PROPERTY_LABEL,"Border color",-1);
	GtkWidget *border_name=gtk_label_new("Border");
	gtk_label_set_mnemonic_widget(GTK_LABEL(border_name),map_border_input);
	gtk_box_append(GTK_BOX(display),border_name);
	map_border_swatch=gtk_drawing_area_new();
	gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(map_border_swatch),23);
	gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(map_border_swatch),draw_border_swatch,NULL,NULL);
	gtk_box_append(GTK_BOX(display),map_border_swatch);
	gtk_box_append(GTK_BOX(display),map_border_input);
	GtkWidget *spacer=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0);
	gtk_widget_set_hexpand(spacer,TRUE);gtk_box_append(GTK_BOX(display),spacer);
	gtk_box_append(GTK_BOX(display),gtk_label_new("FPS"));
	map_fps_input=gtk_toggle_button_new_with_label("Off");
	analog_button_style(map_fps_input,9,5);
	gtk_accessible_update_property(GTK_ACCESSIBLE(map_fps_input),GTK_ACCESSIBLE_PROPERTY_LABEL,"Show FPS",-1);
	gtk_accessible_update_property(GTK_ACCESSIBLE(map_fps_input),GTK_ACCESSIBLE_PROPERTY_DESCRIPTION,"Show motion-layer render FPS, not display presentation",-1);
	gtk_box_append(GTK_BOX(display),map_fps_input);
	gtk_grid_attach(grid,display,0,9,2,1);
	g_signal_connect(map_border_input,"changed",G_CALLBACK(preview_map_settings),NULL);
	g_signal_connect(map_fps_input,"toggled",G_CALLBACK(preview_map_settings),NULL);

	map_settings_note=gtk_label_new("");
	gtk_widget_add_css_class(map_settings_note,"shell-settings-hint");
	gtk_label_set_xalign(GTK_LABEL(map_settings_note),0);
	gtk_label_set_wrap(GTK_LABEL(map_settings_note),TRUE);
	gtk_label_set_ellipsize(GTK_LABEL(map_settings_note),PANGO_ELLIPSIZE_END);
	gtk_label_set_lines(GTK_LABEL(map_settings_note),2);
	gtk_widget_set_visible(map_settings_note,FALSE);
	gtk_box_append(GTK_BOX(content),map_settings_note);
	gtk_box_append(GTK_BOX(content),gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
	GtkWidget *actions=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,7);
	gtk_box_append(GTK_BOX(content),actions);
	GtkWidget *reset=gtk_button_new_with_label("Reset");
	analog_button_style(reset,9,5);
	gtk_accessible_update_property(GTK_ACCESSIBLE(reset),GTK_ACCESSIBLE_PROPERTY_DESCRIPTION,"Preview defaults. Save to keep them.",-1);
	g_signal_connect(reset,"clicked",G_CALLBACK(reset_map_settings),NULL);
	gtk_box_append(GTK_BOX(actions),reset);
	spacer=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0);
	gtk_widget_set_hexpand(spacer,TRUE);gtk_box_append(GTK_BOX(actions),spacer);
	GtkWidget *cancel=gtk_button_new_with_label("Cancel");
	analog_button_style(cancel,9,5);
	g_signal_connect(cancel,"clicked",G_CALLBACK(cancel_map_settings),NULL);
	gtk_box_append(GTK_BOX(actions),cancel);
	map_settings_save=gtk_button_new_with_label("Save");
	analog_button_style(map_settings_save,9,5);
	gtk_widget_add_css_class(map_settings_save,"shell-settings-save");
	g_signal_connect(map_settings_save,"clicked",G_CALLBACK(save_map_settings),NULL);
	gtk_box_append(GTK_BOX(actions),map_settings_save);
}

static int fit_home(void) {
	return MAX(1,MIN(home_width,output_width-24));
}

static void update_dock(void) {
	if(!dock) return;
	// Delete completes inline; pending submission must not dismiss Slash.
	gboolean visible=(enabled || slash_open) && !default_node && (!current_node || !*current_node);

	gtk_widget_set_visible(dock,visible);
	node_panel_update();
	gtk_widget_set_visible(entry_button,!visible && !expanded && enabled && (!node_dock || !gtk_widget_get_visible(node_dock)));
	home_dock_layout(output_width,output_height,home_width,map_visible,can_open_app_grid(),app_grid_open,
		enabled && !map_visible && !expanded && !slash_open && !app_grid_open && (blank_home || overview),system_tray_has_items());
	gboolean in_node=node_dock && gtk_widget_get_visible(node_dock);
	if(slash_open && (slash_in_node?!in_node:(!visible || map_visible))) {
		close_slash();
		return;
	}
	notice_layout(output_width,output_height,in_node?node_bell_center:
		(output_width-fit_home())/2+home_dock_notice_center(),HOME_HEIGHT+HOME_BOTTOM+16);
	if((!visible && !in_node) || map_visible || expanded || slash_open || app_grid_open) notice_log_dismiss(TRUE);
	if(tray_keys && (map_visible || expanded || app_grid_open || slash_open ||
		(in_node ? system_tray_trigger()!=node_tray_button || gtk_widget_get_visible(node_settings) ||
			g_strcmp0(tray_node,node_notice_source())!=0 :
			!visible || system_tray_trigger()!=home_dock_tray_button()))) {
		system_tray_close();
	}
	system_tray_place(output_width,output_height,in_node?node_tray_center:
		(output_width-fit_home())/2+home_dock_tray_center(),HOME_HEIGHT+HOME_BOTTOM+16,in_node);
	if(map_settings_active && navigating) {
		int inset,width,height;
		map_hud_settings_room(output_width,output_height,&inset,&width,&height);
		gtk_widget_set_margin_start(form,12);
		gtk_widget_set_halign(form,GTK_ALIGN_END);
		gtk_widget_set_valign(form,GTK_ALIGN_START);
		gtk_widget_set_margin_top(form,inset);
		gtk_widget_set_margin_end(form,inset);
		gtk_widget_set_margin_bottom(form,0);
		gtk_widget_set_size_request(form,MIN(482,width),MIN(565,height));
	} else if(dock_settings_open) {
		// Follow the actual tray, including its 600 px cap. Below the
		// readable minimum use the available viewport instead of clipping.
		int width=MIN(MAX(1,output_width-24),MAX(312,home_dock_telemetry_width()));
		int dx=home_dock_telemetry_center();
		gtk_widget_set_halign(form,GTK_ALIGN_CENTER);
		gtk_widget_set_valign(form,GTK_ALIGN_END);
		gtk_widget_set_margin_start(form,12+MAX(0,dx*2));
		gtk_widget_set_margin_end(form,12+MAX(0,-dx*2));
		gtk_widget_set_margin_top(form,8);
		gtk_widget_set_margin_bottom(form,HOME_HEIGHT+HOME_BOTTOM+16);
		int height=gtk_widget_get_visible(display_settings_body)?300:gtk_widget_get_visible(shortcuts_body)?325:422;
		gtk_widget_set_size_request(form,width,MIN(height,MAX(1,output_height-HOME_HEIGHT-HOME_BOTTOM-24)));
	} else {
		gtk_widget_set_margin_start(form,12);
		gtk_widget_set_halign(form,visible?GTK_ALIGN_CENTER:GTK_ALIGN_END);
		gtk_widget_set_valign(form,GTK_ALIGN_END);
		gtk_widget_set_margin_top(form,8);
		gtk_widget_set_margin_end(form,12);
		gtk_widget_set_margin_bottom(form,visible?HOME_HEIGHT+HOME_BOTTOM+10:12);
		gtk_widget_set_size_request(form,map_settings_active?MIN(482,MAX(1,output_width-24)):450,
			map_settings_active?CLAMP(output_height-125,120,565):-1);
	}
	if(home_guide) {
		int items=default_node_enabled?7:6;
		int dock_width=fit_home();
		int available=MAX(0,dock_width-2*HOME_GUIDE_INSET);
		int columns=MIN(items,available/HOME_GUIDE_CELL_WIDTH);
		int width=columns*HOME_GUIDE_CELL_WIDTH+2*HOME_GUIDE_INSET;
		int height=home_guide_height(width,default_node_enabled);
		gboolean show=saved_home_guide && visible && columns>=2 &&
			output_height>=HOME_HEIGHT+HOME_BOTTOM+16+height+24 &&
			!map_visible && !expanded && !navigating && !slash_open && !app_grid_open &&
			(blank_home || overview);
		gtk_widget_set_visible(home_guide,show);
		if(show) {
			gtk_widget_set_margin_start(home_guide,(output_width-dock_width)/2);
			gtk_widget_set_size_request(home_guide,width,height);
			gtk_widget_queue_draw(home_guide);
		}
	}
	slash_ui_fit(output_width,output_height);
	schedule_region();
}

static void viewport_changed(GtkDrawingArea *a,int w,int h,gpointer user) {
	(void)a;(void)user;
	app_grid_resize(w,h);
	if(output_width==w && output_height==h) return;
	output_width=w;output_height=h;
	update_dock();
}

void panel_set_saved_cursor_size(int size) {
	saved_cursor_size=CLAMP(size,CURSOR_SIZE_MIN,CURSOR_SIZE_MAX);
	map_pointer_set_cursor_size(saved_cursor_size);
}
void panel_set_home_width(int w) {
	saved_home_width=home_width=CLAMP(w,HOME_WIDTH_MIN,HOME_WIDTH_MAX);
	update_dock();
}

void panel_set_node_preferences(int enabled,int telemetry,int zoom,int hover) {
	saved_node_guide=enabled!=0;
	saved_node_telemetry=telemetry!=0;
	saved_node_preview_zoom=CLAMP(zoom,NODE_PREVIEW_ZOOM_MIN,NODE_PREVIEW_ZOOM_MAX);
	panel_set_picker_scale(saved_node_preview_zoom);
	saved_node_hover_zoom=CLAMP(hover,NODE_HOVER_ZOOM_MIN,NODE_HOVER_ZOOM_MAX);
	panel_set_strip_hover_scale(saved_node_hover_zoom);
	node_panel_update();
}

void panel_set_home_guide(int enabled) {
	saved_home_guide=enabled!=0;
	if(home_guide_on && !dock_settings_open)
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(saved_home_guide?home_guide_on:home_guide_off),TRUE);
	update_dock();
}

void panel_set_video_enabled(int enabled) {
	saved_video_enabled=enabled!=0;
	if(video_on && !dock_settings_open)
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(saved_video_enabled?video_on:video_off),TRUE);
}

void panel_set_toast_mode(int mode) {
	saved_toast_mode=CLAMP(mode,0,2);
	if(!dock_settings_open) restore_toast_setting();
	notice_context(current_node && *current_node?current_node:default_node?"@default":"@home",saved_toast_mode);
}

static gboolean display_notice(gpointer data) {
	panelShellNotification(data);
	return G_SOURCE_REMOVE;
}
void panel_home_notice(const char *message) {
	g_idle_add_full(G_PRIORITY_DEFAULT,display_notice,g_strdup(message),g_free);
}

struct dock_readout {
	int busy,n,cores[256]; double used,total,rx,tx;
	gboolean network; int health,health_cpu; char time[8],date[8];
};
static gboolean render_home_sample(gpointer data) {
	struct dock_readout *s=data;
	home_dock_sample(s->busy,s->cores,s->n,s->used,s->total,s->rx,s->tx,s->network,s->health,s->health_cpu,s->time,s->date);
	return G_SOURCE_REMOVE;
}
void panel_home_sample(int busy,const int *cores,int n,double used,double total,
		double rx,double tx,int network,int health,int health_cpu,const char *time,const char *date) {
	struct dock_readout *s=g_new0(struct dock_readout,1);
	s->busy=busy;s->n=CLAMP(n,0,(int)G_N_ELEMENTS(s->cores));
	if(s->n) memcpy(s->cores,cores,s->n*sizeof(int));
	s->used=used;s->total=total;s->rx=rx;s->tx=tx;s->network=network;s->health=health;s->health_cpu=health_cpu;
	g_strlcpy(s->time,time,sizeof s->time);g_strlcpy(s->date,date,sizeof s->date);
	g_idle_add_full(G_PRIORITY_DEFAULT,render_home_sample,s,g_free);
}

static gboolean can_open_app_grid(void) {
	return enabled && !relay_target && !expanded && !slash_open && !navigating &&
		!map_visible && (blank_home || overview);
}

static void open_app_grid(void) {
	node_volume_close();
	dismiss_window_picker(0);
	app_grid_open=TRUE;
	gtk_layer_set_keyboard_mode(window,KEYS_ON_DEMAND);
	app_grid_show(!overview && (default_node || (current_node && *current_node)));
	update_dock();
}

static void close_app_grid(void) {
	if(!app_grid_open) return;
	app_grid_open=FALSE;
	app_grid_hide(FALSE);
	return_keys();
	update_dock();
}
static void clear_app_grid(void) {
	if(app_grid_open) {
		app_grid_open=FALSE;
		app_grid_hide(TRUE);
		return_keys();
		update_dock();
	} else app_grid_hide(TRUE);
}
static void open_slash(gboolean in_node) {
	if(expanded || map_visible || slash_open || app_grid_open) return;
	if(in_node && gtk_widget_get_visible(node_settings)) return;
	dismiss_window_picker(0);
	notice_log_close();
	node_volume_close();
	home_dock_hangar_close();
	app_grid_hide(TRUE);
	slash_open=TRUE;
	slash_in_node=in_node;
	gtk_layer_set_keyboard_mode(window,KEYS_ON_DEMAND);
	slash_ui_show(in_node);
	update_dock();
}
static void close_slash(void) {
	if(!slash_open) return;
	slash_open=FALSE;
	slash_ui_hide();
	return_keys();
	update_dock();
}
static void slash_enter_node(const char *name,gboolean create,guint64 request) {
	panelSlashEnterNode((char *)name,create,request);
}
static void slash_delete(const char *name,int force) {panelDeleteNode((char *)name,force);}
static void open_display_settings(GtkButton *,gpointer);
static void open_shortcuts(GtkButton *,gpointer);
static void slash_open_view(enum slash_view view) {
	if(view==SLASH_SCREENSHOT) {screenshot_open_from_menu();return;}
	if(node_dock && gtk_widget_get_visible(node_dock)) {
		if(view==SLASH_NOTIFICATIONS) open_node_notice(NULL,NULL);
		else if(view==SLASH_SETTINGS) panel_open_node_settings();
		return;
	}
	if(view==SLASH_MAP) open_map_view(NULL,NULL);
	else if(view==SLASH_NOTIFICATIONS) {
		if(!notice_log_visible()) home_action(HOME_NOTIFICATIONS);
	} else {
		open_dock_settings();
		if(view==SLASH_DISPLAY) open_display_settings(NULL,NULL);
		else if(view==SLASH_SHORTCUTS) open_shortcuts(NULL,NULL);
	}
}

static void map_menu_set(gboolean open) {
	if(!navigating || map_menu==open) return;
	map_menu=open;
	if(open) {
		map_hud_arcade_stop();
		stop_camera_input();
		map_pointer_zoom_enabled(FALSE,map_hud_outer_margin());
		gtk_widget_remove_css_class(form,"mothership-map-keyboard");
		gtk_widget_set_visible(overview_form,TRUE);
		gtk_widget_set_visible(status,TRUE);
	} else {
		close_dock_settings();
		close_map_settings();
		gtk_widget_set_visible(normal,FALSE);
		gtk_widget_set_visible(overview_form,FALSE);
		gtk_widget_set_visible(status,FALSE);
		gtk_widget_add_css_class(form,"mothership-map-keyboard");
		gtk_layer_set_keyboard_mode(window,KEYS_ON_DEMAND);
		gtk_widget_grab_focus(form);
		map_hud_allow_route(map_animation!=MAP_ANIMATION_OFF);
		map_pointer_zoom_enabled(TRUE,map_hud_outer_margin());
	}
	update_dock();
}
static void display_close(void) {
	if(dock_settings_open && gtk_widget_get_visible(display_settings_body))dock_cancel(NULL,NULL);
}
static void open_display_settings(GtkButton *button,gpointer data) {
	(void)button;(void)data;
	if(!dock_settings_open || settings_close_id)return;
	// Opening Display does not save any draft in the parent pane.
	home_width=saved_home_width;
	gtk_widget_set_visible(home_settings_body,FALSE);
	gtk_widget_set_visible(display_settings_body,TRUE);
	display_settings_open();
	gtk_widget_grab_focus(form);
	update_dock();
}
static void shortcuts_close(void) {
	if(dock_settings_open && gtk_widget_get_visible(shortcuts_body))dock_cancel(NULL,NULL);
}
static void open_shortcuts(GtkButton *button,gpointer data) {
	(void)button;(void)data;
	if(!dock_settings_open || settings_close_id)return;
	home_width=saved_home_width;
	gtk_widget_set_visible(home_settings_body,FALSE);
	gtk_widget_set_visible(shortcuts_body,TRUE);
	shortcut_settings_open();
	gtk_widget_grab_focus(form);
	update_dock();
}
static void close_dock_settings(void) {
	if(!dock_settings_open) return;
	dock_settings_open=FALSE;
	gtk_widget_set_visible(display_settings_body,FALSE);
	gtk_widget_set_visible(shortcuts_body,FALSE);
	gtk_widget_set_visible(home_settings_body,TRUE);
	home_width=saved_home_width;
	settings_motion_reset(home_settings);
	gtk_widget_set_visible(home_settings, FALSE);
	gtk_widget_set_overflow(form, GTK_OVERFLOW_VISIBLE);
	gtk_widget_remove_css_class(form,"shell-settings-positioned");
	gtk_widget_set_visible(normal,!overview && !map_settings_active);
	gtk_widget_set_visible(overview_form,overview && !map_settings_active);
	gtk_widget_set_visible(status,!map_settings_active);
	update_dock();
}
static void dock_preview(GtkSpinButton *spin,gpointer data) {
	(void)data;
	if(!dock_settings_open) return;
	home_width=gtk_spin_button_get_value_as_int(spin);
	update_dock();
}
static void dock_preview_entry(GtkEditable *e,gpointer data) {
	(void)data;
	if(!dock_settings_open) return;
	const char *value=gtk_editable_get_text(e);
	gint64 width;
	if(g_ascii_string_to_signed(value,10,HOME_WIDTH_MIN,HOME_WIDTH_MAX,&width,NULL)) {
		home_width=(int)width;
		update_dock();
	}
}
static void dock_cancel(GtkButton *b,gpointer data) {
	(void)b;(void)data;
	request_settings_close(home_settings);
}
static void dock_save(GtkButton *b,gpointer data) {
	(void)b;(void)data;
	if(settings_close_id) return;
	gtk_spin_button_update(GTK_SPIN_BUTTON(width_input));
	int next=gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(width_input));
	gtk_spin_button_update(GTK_SPIN_BUTTON(cursor_size_input));
	char *error=panelSaveHomeSettings(next,gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(video_on)),
		gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(home_guide_on)),
		selected_toast_setting(),gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(cursor_size_input)));
	if(error) {
		gtk_label_set_text(GTK_LABEL(width_note),error);
		gtk_widget_set_visible(width_note,TRUE);
		free(error);
		return;
	}
	saved_home_width=next;
	dock_cancel(NULL,NULL);
}
static void from_dock_to_map_settings(GtkButton *b,gpointer data) {
	(void)b;(void)data;
	if(settings_close_id) return;
	close_dock_settings();
	open_map_settings(NULL,NULL);
}
static void open_dock_settings(void) {
	notice_toast_close();
	close_map_settings();
	if(navigating) map_menu_set(TRUE);
	else if(!expanded) toggle_center(NULL,GINT_TO_POINTER(TRUE));
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(width_input),saved_home_width);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(cursor_size_input),saved_cursor_size);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(saved_video_enabled?video_on:video_off),TRUE);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(saved_home_guide?home_guide_on:home_guide_off),TRUE);
	gtk_widget_set_visible(width_note,FALSE);
	restore_toast_setting();
	dock_settings_open=TRUE;
	gtk_widget_set_visible(normal,FALSE);
	gtk_widget_set_visible(overview_form,FALSE);
	gtk_widget_set_visible(status,FALSE);
	gtk_widget_add_css_class(form,"shell-settings-positioned");
	gtk_widget_set_overflow(form, GTK_OVERFLOW_HIDDEN);
	settings_motion_open(home_settings);
	update_dock();
}

static void prepare_screenshot(gboolean invoking_menu) {
	// The group popup has its own Wayland surface. On a hotkey capture it
	// closes after the snapshot so it cannot cover the selection overlay.
	if(group_popover) gtk_popover_popdown(GTK_POPOVER(group_popover));
	if(!invoking_menu) return;
	if(navigating) map_menu_set(FALSE);
	else if(expanded) set_expanded(FALSE);
}

static void home_action(enum home_action action) {
	if(!enabled || relay_target) return;
	if(action==HOME_MAP || action==HOME_CENTER) close_app_grid();
	switch(action) {
	case HOME_MAP:
		if(dock_settings_open || map_settings_active) break;
		close_slash();
		if(map_visible) exit_map_view(); else open_map_view(NULL,NULL);
		break;
	case HOME_APPS:
		if(app_grid_open) close_app_grid();
		else if(can_open_app_grid()) open_app_grid();
		break;
	case HOME_CENTER:
		close_slash();
		if(navigating) map_menu_set(!map_menu);
		else toggle_center(NULL,GINT_TO_POINTER(!expanded));
		break;
	case HOME_RELOAD:
		if(!expanded && !slash_open && !app_grid_open && !map_visible && (blank_home || overview)) {
			if(panelReloadConfig()) panel_home_notice("Configuration reloaded.");
			else panel_home_notice("Could not reload. Kept your current options.");
		}
		break;
	case HOME_HANGAR: system_tray_close();home_dock_hangar_toggle();break;
	case HOME_AUDIO: panelSystemAction(0);break;
	case HOME_LOCK: panelSystemAction(1);break;
	case HOME_TRAY: open_system_tray(GTK_BUTTON(home_dock_tray_button()),NULL);break;
	case HOME_NOTIFICATIONS:
		if(!expanded && !map_visible && !app_grid_open && !slash_open) {
			home_dock_hangar_close();
			system_tray_close();
			notice_log_toggle();
		}
		break;
	default: break;
	}
}
static void draw_home_guide(GtkDrawingArea *area,cairo_t *cr,int width,int height,gpointer unused) {
	(void)area;(void)height;(void)unused;
	home_guide_draw(cr,width,default_node_enabled);
}

static void home_hangar_connect(const char *name) {
	if(!can_open_app_grid() || app_grid_open || default_node || (current_node && *current_node)) return;
	notice_log_close();
	connect_node(name,FALSE);
}

static GtkWidget *mechanical_on_off(GtkWidget **off,GtkWidget **on,const char *subject) {
	GtkWidget *pair=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6);
	*off=gtk_toggle_button_new_with_label("Off");
	*on=gtk_toggle_button_new_with_label("On");
	gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(*on),GTK_TOGGLE_BUTTON(*off));
	GtkWidget *choices[]={*off,*on};
	for(int i=0;i<2;i++) {
		g_autofree char *name=g_strdup_printf("%s: %s",subject,i?"On":"Off");
		home_mechanical_button(choices[i],FALSE);
		gtk_accessible_update_property(GTK_ACCESSIBLE(choices[i]),GTK_ACCESSIBLE_PROPERTY_LABEL,name,-1);
		gtk_box_append(GTK_BOX(pair),choices[i]);
	}
	return pair;
}
void panel_open_node_settings(void) {
	if(!node_dock || !gtk_widget_get_visible(node_dock) || gtk_widget_get_visible(node_settings)) return;
	dismiss_window_picker(0);
	clear_app_grid();
	notice_log_close();
	node_volume_close();
	GtkWidget *guide=saved_node_guide?node_guide_on:node_guide_off;
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(guide),TRUE);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(saved_node_telemetry?node_telemetry_on:node_telemetry_off),TRUE);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(node_preview_zoom),saved_node_preview_zoom);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(node_hover_zoom),saved_node_hover_zoom);
	gtk_widget_set_visible(node_guide_error,FALSE);
	node_stage_settings_sync();
	gtk_widget_set_visible(node_settings,TRUE);
	gtk_layer_set_keyboard_mode(window,KEYS_ON_DEMAND);
	gtk_widget_grab_focus(guide);
	update_dock();
}

static void node_button(gpointer unused,gpointer data) {
	(void)unused;
	int action=GPOINTER_TO_INT(data);
	if(action==1) {
		node_settings_close();
		close_app_grid();
		panel_home_tap();
	} else if(action==0) {
		if(app_grid_open) close_app_grid();
		else {node_settings_close();open_app_grid();}
	} else {
		if(gtk_widget_get_visible(node_settings)) {node_settings_close();return;}
		panel_open_node_settings();
	}
}
static void node_settings_close(void) {
	if(!node_settings || !gtk_widget_get_visible(node_settings)) return;
	gtk_widget_set_visible(node_settings,FALSE);
	return_keys();
	update_dock();
}
static void node_save(GtkButton *unused,gpointer data) {
	(void)unused;(void)data;
	gboolean show=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(node_guide_on));
	gboolean telemetry=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(node_telemetry_on));
	GtkSpinButton *spin=GTK_SPIN_BUTTON(node_preview_zoom);
	gtk_spin_button_update(spin);
	int zoom=gtk_spin_button_get_value_as_int(spin);
	gtk_spin_button_update(GTK_SPIN_BUTTON(node_hover_zoom));
	int hover=gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(node_hover_zoom));
	char *err=panelSaveNodePreferences(show,telemetry,zoom,hover);
	if(err){
		gtk_label_set_text(GTK_LABEL(node_guide_error),err);
		gtk_widget_set_visible(node_guide_error,TRUE);
		free(err);
		return;
	}
	saved_node_guide=show;
	saved_node_telemetry=telemetry;
	saved_node_preview_zoom=zoom;
	saved_node_hover_zoom=hover;
	panel_set_picker_scale(zoom);
	panel_set_strip_hover_scale(hover);
	node_settings_close();
}
static void node_cancel(GtkButton *unused,gpointer data) {(void)unused;(void)data;node_settings_close();}
static void node_glyph(GtkDrawingArea *a,cairo_t *cr,int w,int h,gpointer data) {
	(void)a;
	cairo_translate(cr,(w-24)*.5,(h-24)*.5);
	cairo_set_source_rgb(cr,.67,.86,.92);
	cairo_set_line_width(cr,1.5);cairo_set_line_cap(cr,CAIRO_LINE_CAP_ROUND);
	int id=GPOINTER_TO_INT(data);
	if(id==0){home_apps_glyph(cr);cairo_stroke(cr);return;}
	if(id==1){
		cairo_move_to(cr,5,17);cairo_line_to(cr,12,5);cairo_line_to(cr,19,17);
		cairo_line_to(cr,12,14);cairo_close_path(cr);
		cairo_move_to(cr,12,9);cairo_line_to(cr,12,13);
	} else {
		for(int i=0;i<3;i++){
			int y=7+5*i,x=i==1?15:9;
			cairo_move_to(cr,5,y);cairo_line_to(cr,19,y);
			cairo_move_to(cr,x,y-1.8);cairo_line_to(cr,x,y+1.8);
		}
	}
	cairo_stroke(cr);
}
static void node_face(GtkDrawingArea *a,cairo_t *cr,int w,int h,gpointer data) {
	(void)a;(void)h;(void)data;
	home_node_dock_face(cr,w,node_tray_x,node_tray_width);
	if(node_telemetry_layer && gtk_widget_get_visible(GTK_WIDGET(node_telemetry_layer))) {
		cairo_set_source_rgb(cr,.22,.34,.39);
		cairo_set_line_width(cr,1);
		cairo_move_to(cr,node_strip_x-8+.5,9);
		cairo_line_to(cr,node_strip_x-8+.5,HOME_HEIGHT-9);
		cairo_stroke(cr);
	}
}
static void node_keycaps(GtkDrawingArea *a,cairo_t *cr,int w,int h,gpointer data) {
	(void)a;(void)w;(void)h;(void)data;
	char app,settings,app_label[16],settings_label[16];
	shortcut_guide(&app,&settings);
	g_snprintf(app_label,sizeof app_label,"Meta + %c",app);
	g_snprintf(settings_label,sizeof settings_label,"Meta + %c",settings);
	const char *keys[]={"Meta","Alt + Tab","Meta + Tab",app_label,"Meta + N",settings_label};
	int widths[]={43,68,75,65,65,65},x=1;
	for(size_t i=0;i<G_N_ELEMENTS(keys);i++){
		cairo_set_source_rgba(cr,.05,.105,.145,.95);
		int r=x+widths[i];
		cairo_move_to(cr,x+4,2);cairo_line_to(cr,r-4,2);
		cairo_line_to(cr,r,6);cairo_line_to(cr,r,17);
		cairo_line_to(cr,r-4,21);cairo_line_to(cr,x+4,21);
		cairo_line_to(cr,x,17);cairo_line_to(cr,x,6);
		cairo_close_path(cr);cairo_fill_preserve(cr);
		cairo_set_source_rgb(cr,.34,.48,.56);cairo_set_line_width(cr,1);cairo_stroke(cr);
		PangoLayout *l=pango_cairo_create_layout(cr);
		PangoFontDescription *font=shell_font_description(NULL,10,PANGO_WEIGHT_MEDIUM);
		pango_layout_set_font_description(l,font);pango_font_description_free(font);
		pango_layout_set_text(l,keys[i],-1);
		int tw,th;pango_layout_get_pixel_size(l,&tw,&th);
		cairo_set_source_rgb(cr,.67,.81,.86);
		cairo_move_to(cr,x+(widths[i]-tw)*.5,2+(19-th)*.5);
		pango_cairo_show_layout(cr,l);g_object_unref(l);
		x+=widths[i]+6;
	}
}
static void node_motion_at(GtkEventControllerMotion *motion,double x,double y,gpointer data) {
	(void)motion;(void)y;(void)data;
	panel_node_strip_pointer(node_left+node_strip_x+x,FALSE);
}
static void node_motion_leave(GtkEventControllerMotion *motion,gpointer data) {
	(void)motion;(void)data;
	node_pressed=-1;
	panel_node_strip_pointer(-1,FALSE);
}
static void node_click(GtkGestureClick *c,int count,double x,double y,gpointer data) {
	(void)count;(void)y;(void)data;
	node_pressed=x;
	gtk_gesture_set_state(GTK_GESTURE(c),GTK_EVENT_SEQUENCE_CLAIMED);
}
static void node_release(GtkGestureClick *c,int count,double x,double y,gpointer data) {
	(void)c;(void)count;(void)data;
	if(node_pressed>=0 && fabs(node_pressed-x)<5 && y>=0 &&
		y<gtk_widget_get_height(node_hover) && x>=0 && x<gtk_widget_get_width(node_hover)) {
		if(picker_serial) dismiss_window_picker(0);
		panel_node_strip_pointer(node_left+node_strip_x+x,TRUE);
	}
	node_pressed=-1;
}
static void group_menu_clicked(GtkButton *button,gpointer data) {
	(void)button;
	panel_node_group_action(group_layout_id,GPOINTER_TO_UINT(data),
		gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(group_sx)),
		gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(group_sy)),
		gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(group_gap)));
	gtk_popover_popdown(GTK_POPOVER(group_popover));
}
static GtkWidget *group_spin(GtkBox *box,const char *text,int min,int max,int value) {
	GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,12);
	GtkWidget *label=gtk_label_new(text);
	gtk_widget_set_hexpand(label,TRUE);
	gtk_label_set_xalign(GTK_LABEL(label),0);
	gtk_box_append(GTK_BOX(row),label);
	GtkWidget *spin=gtk_spin_button_new_with_range(min,max,1);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin),value);
	gtk_accessible_update_property(GTK_ACCESSIBLE(spin),GTK_ACCESSIBLE_PROPERTY_LABEL,text,-1);
	gtk_box_append(GTK_BOX(row),spin);
	gtk_box_append(box,row);
	return spin;
}
static void group_menu_button(GtkBox *box,const char *name,unsigned action) {
	GtkWidget *button=gtk_button_new_with_label(name);
	analog_button_style(button,7,3);
	g_signal_connect(button,"clicked",G_CALLBACK(group_menu_clicked),GUINT_TO_POINTER(action));
	gtk_box_append(box,button);
}
void panel_node_group_settings_anchor(int x,int y) {
	group_anchor=(GdkRectangle){x-node_left-node_strip_x,y-shell_node_dock_top(output_height),1,1};
	group_anchor_frame=TRUE;
}
void panel_node_group_state(uint64_t id,unsigned count,gboolean hidden,int sx,int sy,int gap) {
	if(!node_hover || !node_dock || !gtk_widget_get_visible(node_dock) || count<2)return;
	if(!group_popover) {
		group_popover=gtk_popover_new();
		gtk_widget_set_parent(group_popover,node_hover);
		gtk_popover_set_has_arrow(GTK_POPOVER(group_popover),FALSE);
		gtk_popover_set_position(GTK_POPOVER(group_popover),GTK_POS_TOP);
		gtk_widget_add_css_class(group_popover,"flow-group-settings");
	}
	gtk_popover_set_position(GTK_POPOVER(group_popover),group_anchor_frame?GTK_POS_BOTTOM:GTK_POS_TOP);
	group_layout_id=id;
	GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,8);
	GtkWidget *title=gtk_label_new("Group Layout");
	gtk_widget_add_css_class(title,"shell-system-title");
	gtk_label_set_xalign(GTK_LABEL(title),0);
	gtk_box_append(GTK_BOX(box),title);
	group_sx=group_spin(GTK_BOX(box),"Split (%)",25,75,sx);
	group_sy=group_spin(GTK_BOX(box),"Row split (%)",25,75,sy);
	gtk_widget_set_visible(gtk_widget_get_parent(group_sy),count>2);
	group_gap=group_spin(GTK_BOX(box),"Tiling Gap (px)",0,72,gap);
	GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6);
	group_menu_button(GTK_BOX(row),hidden?"Restore Group":"Minimize Group",
		hidden?MOTHERSHIP_MAP_POINTER_V1_GROUP_COMMAND_RESTORE:MOTHERSHIP_MAP_POINTER_V1_GROUP_COMMAND_MINIMIZE);
	group_menu_button(GTK_BOX(row),"Ungroup",MOTHERSHIP_MAP_POINTER_V1_GROUP_COMMAND_UNGROUP);
	group_menu_button(GTK_BOX(row),"Apply",MOTHERSHIP_MAP_POINTER_V1_GROUP_COMMAND_APPLY);
	gtk_box_append(GTK_BOX(box),row);
	gtk_popover_set_child(GTK_POPOVER(group_popover),box);
	GdkRectangle anchor=group_anchor;
	int natural=0;
	gtk_widget_measure(box,GTK_ORIENTATION_HORIZONTAL,-1,NULL,&natural,NULL,NULL);
	// Allow room for the popover contents border and padding on both sides.
	int inset=MIN(output_width/2,(natural+1)/2+16);
	int origin=node_left+node_strip_x;
	anchor.x=CLAMP(anchor.x+origin,inset,output_width-inset)-origin;
	gtk_popover_set_pointing_to(GTK_POPOVER(group_popover),&anchor);
	gtk_popover_popup(GTK_POPOVER(group_popover));
}
static void node_group_menu(GtkGestureClick *gesture,int count,double x,double y,gpointer unused) {
	(void)count;(void)unused;
	group_anchor_frame=FALSE;
	group_anchor=(GdkRectangle){(int)x,-4,1,1};
	panel_node_group_query(node_left+node_strip_x+x,
		shell_node_dock_top(output_height)+y);
	gtk_gesture_set_state(GTK_GESTURE(gesture),GTK_EVENT_SEQUENCE_CLAIMED);
}

static const char *node_notice_source(void) {
	return current_node && *current_node?current_node:"@default";
}
static void node_bell_draw(GtkDrawingArea *a,cairo_t *cr,int w,int h,gpointer unused) {
	(void)a;(void)unused;
	cairo_translate(cr,w*.5,h*.5);
	cairo_set_source_rgb(cr,.69,.87,.92);
	cairo_set_line_width(cr,1.4);
	cairo_move_to(cr,-7,4);cairo_curve_to(cr,-4,2,-5,-1,-4,-4);
	cairo_curve_to(cr,-3,-8,3,-8,4,-4);cairo_curve_to(cr,5,-1,4,2,7,4);
	cairo_close_path(cr);cairo_new_sub_path(cr);cairo_arc(cr,0,4,2,0,G_PI);
	cairo_stroke(cr);
	if(notice_source_unread(node_notice_source())>0) {
		cairo_set_source_rgb(cr,.46,.84,.91);
		cairo_arc(cr,9,-9,2.2,0,2*G_PI);cairo_fill(cr);
	}
}
void panel_redraw_node_bell(void) {
	if(node_bell_ink)gtk_widget_queue_draw(node_bell_ink);
}
static void notice_region_changed(void) {
	if(node_log_keys && !notice_log_visible()) {
		node_log_keys=FALSE;
		return_keys();
	}
	node_panel_update();
	schedule_region();
}
static void volume_region_changed(void) {
	if(node_volume_keys && !gtk_widget_get_visible(node_volume_widget())) {
		node_volume_keys=FALSE;
		return_keys();
	}
	schedule_region();
}
static void tray_region_changed(void) {
	if(tray_keys && !gtk_widget_get_visible(system_tray_widget())) {
		tray_keys=FALSE;
		g_clear_pointer(&tray_node,g_free);
		return_keys();
	}
	home_dock_tray_open(system_tray_trigger()==home_dock_tray_button());
	if(node_tray_button)gtk_widget_queue_draw(gtk_button_get_child(GTK_BUTTON(node_tray_button)));
	update_dock();
	schedule_region();
}
static void open_system_tray(GtkButton *button,gpointer unused) {
	(void)unused;
	if(system_tray_close())return;
	if(!system_tray_has_items() || expanded || app_grid_open || slash_open || map_visible ||
		navigating || relay_target || gtk_widget_get_visible(node_settings))return;
	if(GTK_WIDGET(button)==home_dock_tray_button() && !can_open_app_grid())return;
	notice_log_close();
	node_volume_close();
	home_dock_hangar_close();
	dismiss_window_picker(0);
	tray_keys=TRUE;
	tray_node=GTK_WIDGET(button)==node_tray_button?g_strdup(node_notice_source()):NULL;
	gtk_layer_set_keyboard_mode(window,KEYS_ON_DEMAND);
	if(!system_tray_open(GTK_WIDGET(button))) {tray_keys=FALSE;g_clear_pointer(&tray_node,g_free);return_keys();}
}
static void open_node_volume(GtkButton *button,gpointer unused) {
	(void)button;(void)unused;
	if(!node_dock || !gtk_widget_get_visible(node_dock) || app_grid_open ||
			gtk_widget_get_visible(node_settings))return;
	if(node_volume_close())return;
	notice_log_close();
	dismiss_window_picker(0);
	node_volume_keys=TRUE;
	gtk_layer_set_keyboard_mode(window,KEYS_ON_DEMAND);
	const char *source=node_notice_source();
	node_volume_open(source,default_node?"Default Node":source);
	node_panel_update();
}
static void open_node_notice(GtkButton *button,gpointer unused) {
	(void)button;(void)unused;
	if(!node_dock || !gtk_widget_get_visible(node_dock) || app_grid_open ||
		gtk_widget_get_visible(node_settings))return;
	if(notice_log_close())return;
	system_tray_close();
	node_volume_close();
	dismiss_window_picker(0);
	node_log_keys=TRUE;
	gtk_layer_set_keyboard_mode(window,KEYS_ON_DEMAND);
	const char *source=node_notice_source();
	notice_log_toggle_for(source,default_node?"Default Node":source);
}

static void node_panel_update(void) {
	if(!node_dock)return;
	gboolean shown=desktop_ready && !overview && (default_node || (current_node && *current_node)) &&
		!map_visible && !navigating && !expanded;
	gtk_widget_set_visible(node_dock,shown);
	node_volume_context(shown?node_notice_source():NULL);
	node_stage_settings_sync();
	if(picker_serial && (!shown || app_grid_open || notice_log_visible() || gtk_widget_get_visible(node_settings))) dismiss_window_picker(0);
	if(!shown && gtk_widget_get_visible(node_settings)) {
		gtk_widget_set_visible(node_settings,FALSE);
		return_keys();
	}
	const int inset=7, gap=16, min_preview=208, min_telemetry=156, max_wing=164;
	int w=fit_home();
	if(saved_node_telemetry) {
		// Expand the Node dock, not Home, to fit the full readout and preview.
		const int full_dock=2*(max_wing+inset)+gap+min_preview+NODE_TELEMETRY_FULL_WIDTH;
		w=MAX(w,MIN(full_dock,output_width-24));
	}
	int wing=MAX(46,MIN(max_wing,w/4)),tray=w-2*wing;
	node_left=(output_width-w)/2;
	gboolean changed=node_tray_x!=wing || node_tray_width!=tray;
	node_tray_x=wing;node_tray_width=tray;
	int side=CLAMP(wing/10,5,13),button_gap=CLAMP(wing/20,4,8);
	gboolean has_tray=system_tray_has_items();
	int count=has_tray?3:2;
	int bell_w=CLAMP((wing-2*side-(count-1)*button_gap)/count,8,44);
	int bell_x=w-side-bell_w, tray_x=bell_x-button_gap-bell_w;
	int speaker_x=has_tray?tray_x-button_gap-bell_w:tray_x;
	node_volume_layout(output_width,output_height,node_left+speaker_x+bell_w/2,HOME_HEIGHT+HOME_BOTTOM+16);
	node_bell_center=node_left+bell_x+bell_w/2;
	node_tray_center=node_left+tray_x+bell_w/2;
	static gboolean last_has_tray;
	gtk_widget_set_visible(node_tray_button,has_tray);
	if(changed || has_tray!=last_has_tray) {
		gtk_fixed_move(GTK_FIXED(node_dock),node_bell,bell_x,18);
		gtk_widget_set_size_request(node_bell,bell_w,36);
		gtk_fixed_move(GTK_FIXED(node_dock),node_speaker,speaker_x,18);
		gtk_widget_set_size_request(node_speaker,bell_w,36);
		gtk_fixed_move(GTK_FIXED(node_dock),node_tray_button,tray_x,18);
		gtk_widget_set_size_request(node_tray_button,bell_w,36);
		last_has_tray=has_tray;
	}

	if(changed){
		gtk_widget_set_size_request(node_dock,w,HOME_HEIGHT);
		gtk_widget_set_size_request(node_rail,w,HOME_HEIGHT);
		gtk_widget_queue_draw(node_rail);
		int pad=CLAMP(wing/10,6,13),cell=CLAMP((wing-2*pad-10)/3,10,44);
		for(int i=0;i<3;i++){
			gtk_fixed_move(GTK_FIXED(node_dock),node_actions[i],pad+i*(cell+5),18);
			gtk_widget_set_size_request(node_actions[i],cell,36);
		}
		gtk_fixed_move(GTK_FIXED(node_dock),node_hover,wing+7,0);
		gtk_widget_set_size_request(node_hover,MAX(1,tray-14),NODE_PREVIEW_HEIGHT);
	}
	gboolean tray_available=shown && tray>=44;
	gboolean previews=tray_available && !app_grid_open && !gtk_widget_get_visible(node_settings);
	gboolean log_open=notice_log_visible();
	int inner=tray-2*inset;
	// The mounted readout follows Node scope, physical fit and user preference only.
	// Overlay policy applies separately to previews, controls and the keyboard guide.
	gboolean telemetry_visible=tray_available && saved_node_telemetry &&
		inner>=min_telemetry+gap+min_preview;
	int readout=telemetry_visible?MIN(HOME_TELEMETRY_WIDTH_MAX,
		MIN(MAX((inner-gap)/2,NODE_TELEMETRY_FULL_WIDTH),inner-gap-min_preview)):0;
	int start=wing+inset+(telemetry_visible?readout+gap:0);
	int tray_space=MAX(1,inner-(telemetry_visible?readout+gap:0));
	gboolean reserve_button=previews && !log_open && node_window_count>1 &&
		(int)node_window_count*SHELL_PREVIEW_PITCH-6>tray_space;
	int strip=MAX(1,tray_space-(reserve_button?PICKER_BUTTON_SIZE+4:0));
	int hidden=node_window_overflow>=0?node_window_overflow:MAX(0,(int)node_window_count-strip/SHELL_PREVIEW_PITCH);
	gboolean list_available=reserve_button && hidden>0;
	if(list_available) {
		g_autofree char *label=g_strdup_printf("+%d",hidden);
		gtk_label_set_text(GTK_LABEL(picker_button_label),label);
		int preview_center=NODE_PREVIEW_HEIGHT-SHELL_PREVIEW_BOTTOM-
			(NODE_PREVIEW_HEIGHT-SHELL_PREVIEW_REST_INSET)/2;
		gtk_fixed_move(GTK_FIXED(node_dock),picker_button,start+strip+4,preview_center-PICKER_BUTTON_SIZE/2);
	}
	gtk_widget_set_visible(picker_button,list_available);
	picker_fit();
	if(node_strip_x!=start || node_strip_width!=strip) {
		node_pressed=-1;
		panel_node_strip_pointer(-1,FALSE);
		node_strip_x=start;
		node_strip_width=strip;
		gtk_widget_queue_draw(node_rail);
	}
	if(telemetry_visible) {
		gtk_layer_set_margin(node_telemetry_layer,LEFT_EDGE,node_left+wing+inset);
		gtk_layer_set_margin(node_telemetry_layer,TOP_EDGE,shell_node_dock_top(output_height)+4);
		gtk_widget_set_size_request(node_readout,readout,NODE_TELEMETRY_SCREEN_HEIGHT);
	}
	gtk_widget_set_visible(GTK_WIDGET(node_telemetry_layer),telemetry_visible);
	gtk_fixed_move(GTK_FIXED(node_dock),node_hover,start,0);
	gtk_widget_set_size_request(node_hover,strip,NODE_PREVIEW_HEIGHT);
	gtk_widget_set_visible(node_hover,previews);
	gboolean animate=TRUE;
	g_object_get(gtk_widget_get_settings(node_dock),"gtk-enable-animations",&animate,NULL);
	panel_node_strip(node_left+start,shell_node_dock_top(output_height),
		previews?strip:0,NODE_PREVIEW_HEIGHT,animate);
	gboolean guide=previews && saved_node_guide && w>=NODE_GUIDE_WIDTH+24 && output_height>=200;
	gtk_widget_set_visible(node_keys,guide && !log_open && !slash_open);
	gtk_widget_set_margin_start(node_keys,node_left);
	gtk_widget_set_size_request(node_settings,MIN(MAX(1,output_width-24),330),-1);
}
static void node_stage_settings_sync(void) {
	if(!node_layout_row)return;
	gboolean node=(default_node || (current_node && *current_node)) && current_mode && *current_mode;
	gtk_widget_set_visible(node_layout_row,node);
	if(!node)return;
	gboolean stage=g_strcmp0(current_mode,"stage")==0;
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(stage?node_layout_stage:node_layout_flow),TRUE);
}

static void node_stage_choice(GtkButton *button,gpointer data) {
	(void)button;
	if(!current_mode || !*current_mode || (!default_node && (!current_node || !*current_node)))return;
	gboolean stage=GPOINTER_TO_INT(data)!=0;
	if(stage==(g_strcmp0(current_mode,"stage")==0)) {
		node_stage_settings_sync();
		return;
	}
	// Update the acknowledged toggle from the next desktopd snapshot.
	panelAction(17,default_node?"":current_node,stage?"stage":"flow");
	node_settings_close();
}

static uint64_t picker_row_id(GtkWidget *row) {
	const uint64_t *id=g_object_get_data(G_OBJECT(row),"window-id");
	return id?*id:0;
}
static void hide_window_picker(void) {
	if(!window_picker) return;
	gtk_widget_set_visible(window_picker,FALSE);
	analog_button_on(picker_button,FALSE);
	gboolean had_keys=picker_mouse;
	picker_mouse=FALSE;
	picker_serial=0;
	picker_selected=0;
	if(had_keys) return_keys();
	schedule_region();
}
static void dismiss_window_picker(uint64_t id) {
	if(!picker_serial) return;
	panel_pick_window(picker_serial,id);
	hide_window_picker();
}
void panel_window_count(uint32_t count) {
	if(node_window_count!=count && node_dock) {
		node_window_count=count;
		node_panel_update();
		schedule_region();
	}
}
void panel_window_overflow(uint32_t count) {
	if(node_dock && node_window_overflow!=(int)count) {
		node_window_overflow=(int)count;
		node_panel_update();
		schedule_region();
	}
}
void panel_picker_begin(uint32_t serial,gboolean keyboard) {
	if(!window_picker || !node_dock || !gtk_widget_get_visible(node_dock) ||
			app_grid_open || slash_open || notice_log_visible() || gtk_widget_get_visible(node_settings)) {
		panel_pick_window(serial,0);
		return;
	}
	while(gtk_widget_get_first_child(picker_grid))
		gtk_grid_remove(GTK_GRID(picker_grid),gtk_widget_get_first_child(picker_grid));
	picker_tile_count=0;
	picker_serial=serial;
	picker_selected=0;
	picker_mouse=!keyboard;
}
static void picker_row_clicked(GtkButton *b,gpointer unused) {
	(void)unused;
	dismiss_window_picker(picker_row_id(gtk_widget_get_parent(GTK_WIDGET(b))));
}
static void picker_fit(void) {
	if(!picker_grid) return;
	int room=MAX(1,output_width-24);
	int cols=CLAMP((room-2*PICKER_INSET+PICKER_GAP)/126,1,3);
	picker_cols=MIN(cols,MAX(1,picker_tile_count));
	int rows=MAX(1,(picker_tile_count+picker_cols-1)/picker_cols);
	int width=MIN(room,2*PICKER_INSET+picker_cols*170+(picker_cols-1)*PICKER_GAP);
	int height=CLAMP((MAX(1,output_height-HOME_BOTTOM-HOME_HEIGHT-16-2*PICKER_INSET)-
		(rows-1)*PICKER_GAP)/rows,12,102);
	gtk_widget_set_size_request(window_picker,width,-1);
	GtkLayoutManager *manager=gtk_widget_get_layout_manager(picker_grid);
	for(int i=0;i<picker_tile_count;i++) {
		GtkWidget *tile=picker_tiles[i];
		GtkGridLayoutChild *slot=GTK_GRID_LAYOUT_CHILD(gtk_layout_manager_get_layout_child(manager,tile));
		gtk_grid_layout_child_set_column(slot,i%picker_cols);
		gtk_grid_layout_child_set_row(slot,i/picker_cols);
		gtk_widget_set_size_request(tile,1,height);
	}
}
static void picker_send_bounds(void) {
	if(!picker_serial || !gtk_widget_get_mapped(window_picker)) return;
	for(int i=0;i<picker_tile_count;i++) {
		GtkWidget *tile=picker_tiles[i];
		graphene_rect_t r;
		if(gtk_widget_compute_bounds(tile,GTK_WIDGET(window),&r)) {
			int left=(int)ceil(r.origin.x)+4,top=(int)ceil(r.origin.y)+4;
			int right=(int)floor(r.origin.x+r.size.width)-4;
			int bottom=(int)floor(r.origin.y+r.size.height)-4;
			if(right>left && bottom>top)
				panel_place_window_preview(picker_serial,picker_row_id(tile),left,top,right-left,bottom-top);
		}
	}
}
void panel_picker_row(uint64_t id,const char *title_text,const char *app_id) {
	if(!picker_serial || picker_tile_count>=PICKER_TILE_CAP) return;
	g_autofree char *valid_title=g_utf8_make_valid(title_text,-1);
	g_autofree char *valid_app=g_utf8_make_valid(app_id,-1);
	const char *name=*valid_title?valid_title:*valid_app?valid_app:"Untitled window";
	GtkWidget *button=gtk_button_new();
	gtk_widget_add_css_class(button,"window-picker-tile");
	gtk_accessible_update_property(GTK_ACCESSIBLE(button),GTK_ACCESSIBLE_PROPERTY_LABEL,name,-1);
	g_signal_connect(button,"clicked",G_CALLBACK(picker_row_clicked),NULL);
	GtkWidget *plate=analog_item_new(button);
	gtk_widget_set_hexpand(plate,TRUE);
	uint64_t *identity=g_new(uint64_t,1);*identity=id;
	g_object_set_data_full(G_OBJECT(plate),"window-id",identity,g_free);
	gtk_grid_attach(GTK_GRID(picker_grid),plate,picker_tile_count,0,1,1);
	picker_tiles[picker_tile_count++]=plate;
}
void panel_picker_highlight(uint32_t serial,uint64_t id) {
	if(serial!=picker_serial) return;
	picker_selected=id;
	GtkWidget *selected=NULL;
	for(int i=0;i<picker_tile_count;i++) {
		GtkWidget *tile=picker_tiles[i];
		gboolean active=picker_row_id(tile)==id;
		analog_item_select(tile,active);
		if(active) selected=tile;
	}
	if(!picker_tile_count) {
		// When every candidate is on the rail, the WM draws the prospective
		// choice there. Do not cancel that Alt sequence or map an empty table.
		if(picker_mouse) dismiss_window_picker(0);
		return;
	}
	picker_fit();
	gtk_widget_set_visible(window_picker,TRUE);
	if(picker_mouse) {
		analog_button_on(picker_button,TRUE);
		gtk_layer_set_keyboard_mode(window,KEYS_ON_DEMAND);
		if(selected) gtk_widget_grab_focus(gtk_widget_get_last_child(selected));
	}
	schedule_region();
}
void panel_picker_end(uint32_t serial) {
	if(!serial || serial==picker_serial) hide_window_picker();
}
static gboolean picker_key(guint key) {
	if(key==GDK_KEY_Escape) {dismiss_window_picker(0);return TRUE;}
	if(key==GDK_KEY_Return || key==GDK_KEY_KP_Enter) {dismiss_window_picker(picker_selected);return TRUE;}
	if(key!=GDK_KEY_w && key!=GDK_KEY_a && key!=GDK_KEY_s && key!=GDK_KEY_d) return FALSE;
	int current=0;
	while(current<picker_tile_count && picker_row_id(picker_tiles[current])!=picker_selected) current++;
	if(current==picker_tile_count || !picker_cols) return TRUE;
	int next=current;
	if(key==GDK_KEY_w) next-=picker_cols;
	else if(key==GDK_KEY_s) next+=picker_cols;
	else if(key==GDK_KEY_a && current%picker_cols>0) next--;
	else if(key==GDK_KEY_d && current%picker_cols<picker_cols-1) next++;
	if(next>=0 && next<picker_tile_count) panel_picker_highlight(picker_serial,picker_row_id(picker_tiles[next]));
	return TRUE;
}
static void open_window_picker(GtkButton *button,gpointer unused) {
	(void)button;(void)unused;
	if(picker_serial) dismiss_window_picker(0);
	else panel_request_window_picker();
}
static void init_window_picker(GtkOverlay *overlay) {
	window_picker=gtk_overlay_new();
	gtk_widget_set_halign(window_picker,GTK_ALIGN_CENTER);
	gtk_widget_set_valign(window_picker,GTK_ALIGN_END);
	gtk_widget_set_margin_bottom(window_picker,HOME_BOTTOM+HOME_HEIGHT+16);
	gtk_overlay_set_child(GTK_OVERLAY(window_picker),analog_glass_new());
	picker_grid=gtk_grid_new();
	gtk_grid_set_column_homogeneous(GTK_GRID(picker_grid),TRUE);
	gtk_grid_set_row_homogeneous(GTK_GRID(picker_grid),TRUE);
	gtk_grid_set_column_spacing(GTK_GRID(picker_grid),PICKER_GAP);
	gtk_grid_set_row_spacing(GTK_GRID(picker_grid),PICKER_GAP);
	gtk_widget_set_margin_start(picker_grid,PICKER_INSET);
	gtk_widget_set_margin_end(picker_grid,PICKER_INSET);
	gtk_widget_set_margin_top(picker_grid,PICKER_INSET);
	gtk_widget_set_margin_bottom(picker_grid,PICKER_INSET);
	gtk_overlay_add_overlay(GTK_OVERLAY(window_picker),picker_grid);
	gtk_overlay_set_measure_overlay(GTK_OVERLAY(window_picker),picker_grid,TRUE);
	gtk_accessible_update_property(GTK_ACCESSIBLE(window_picker),GTK_ACCESSIBLE_PROPERTY_LABEL,"Windows in this Node",-1);
	gtk_widget_set_visible(window_picker,FALSE);
	gtk_overlay_add_overlay(overlay,window_picker);
}
static void draw_node_tray(GtkDrawingArea *area,cairo_t *cr,int w,int h,gpointer unused) {
	(void)unused;
	GtkWidget *button=gtk_widget_get_parent(GTK_WIDGET(area));
	GtkStateFlags state=gtk_widget_get_state_flags(button);
	gboolean active=system_tray_trigger()==button;
	cairo_translate(cr,w/2.,h/2.);
	double scale=CLAMP((w-3)/17.,.25,1.);
	cairo_scale(cr,scale,scale);
	cairo_set_source_rgb(cr,active || (state&GTK_STATE_FLAG_PRELIGHT)?.65:.70,active?.94:.80,active?.97:.84);
	cairo_set_line_width(cr,1.25);
	cairo_set_line_cap(cr,CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(cr,CAIRO_LINE_JOIN_ROUND);
	home_tray_glyph(cr);
	cairo_stroke(cr);
}
static void node_panel_init(GtkOverlay *overlay) {
	node_dock=gtk_fixed_new();
	gtk_widget_set_halign(node_dock,GTK_ALIGN_CENTER);gtk_widget_set_valign(node_dock,GTK_ALIGN_END);
	gtk_widget_set_margin_bottom(node_dock,HOME_BOTTOM);
	node_rail=gtk_drawing_area_new();
	gtk_widget_set_can_target(node_rail,FALSE);
	gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(node_rail),node_face,NULL,NULL);
	gtk_fixed_put(GTK_FIXED(node_dock),node_rail,0,0);
	node_hover=gtk_drawing_area_new();
	gtk_fixed_put(GTK_FIXED(node_dock),node_hover,0,0);
	GtkEventController *motion=gtk_event_controller_motion_new();
	g_signal_connect(motion,"enter",G_CALLBACK(node_motion_at),NULL);
	g_signal_connect(motion,"motion",G_CALLBACK(node_motion_at),NULL);
	g_signal_connect(motion,"leave",G_CALLBACK(node_motion_leave),NULL);
	gtk_widget_add_controller(node_hover,motion);
	GtkGesture *click=gtk_gesture_click_new();
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click),GDK_BUTTON_PRIMARY);
	g_signal_connect(click,"pressed",G_CALLBACK(node_click),NULL);
	g_signal_connect(click,"released",G_CALLBACK(node_release),NULL);
	gtk_widget_add_controller(node_hover,GTK_EVENT_CONTROLLER(click));
	GtkGesture *secondary=gtk_gesture_click_new();
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(secondary),GDK_BUTTON_SECONDARY);
	g_signal_connect(secondary,"released",G_CALLBACK(node_group_menu),NULL);
	gtk_widget_add_controller(node_hover,GTK_EVENT_CONTROLLER(secondary));
	const char *names[]={"App Grid","Return to Home","Node Panel Settings"};
	for(int i=0;i<3;i++){
		GtkWidget *icon=gtk_drawing_area_new(),*button=gtk_button_new();
		gtk_widget_set_size_request(icon,12,22);
		gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(icon),node_glyph,GINT_TO_POINTER(i),NULL);
		gtk_button_set_child(GTK_BUTTON(button),icon);
		gtk_widget_add_css_class(button,"home-dock-button");
		gtk_widget_add_css_class(button,"node-dock-action");
		home_mechanical_button(button,FALSE);
		gtk_widget_set_focus_on_click(button,FALSE);
		gtk_accessible_update_property(GTK_ACCESSIBLE(button),GTK_ACCESSIBLE_PROPERTY_LABEL,names[i],-1);
		g_signal_connect(button,"clicked",G_CALLBACK(node_button),GINT_TO_POINTER(i));
		node_actions[i]=button;
		gtk_fixed_put(GTK_FIXED(node_dock),button,0,18);
	}
	node_bell_ink=gtk_drawing_area_new();
	gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(node_bell_ink),node_bell_draw,NULL,NULL);
	node_bell=gtk_button_new();
	gtk_button_set_child(GTK_BUTTON(node_bell),node_bell_ink);
	gtk_widget_add_css_class(node_bell,"home-dock-button");
	gtk_widget_add_css_class(node_bell,"node-dock-action");
	home_mechanical_button(node_bell,FALSE);
	gtk_widget_set_focus_on_click(node_bell,FALSE);
	gtk_accessible_update_property(GTK_ACCESSIBLE(node_bell),GTK_ACCESSIBLE_PROPERTY_LABEL,"Notifications for this Node",-1);
	g_signal_connect(node_bell,"clicked",G_CALLBACK(open_node_notice),NULL);
	gtk_fixed_put(GTK_FIXED(node_dock),node_bell,0,18);
	GtkWidget *tray_ink=gtk_drawing_area_new();
	gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(tray_ink),draw_node_tray,NULL,NULL);
	node_tray_button=gtk_button_new();
	gtk_button_set_child(GTK_BUTTON(node_tray_button),tray_ink);
	gtk_widget_add_css_class(node_tray_button,"home-dock-button");
	gtk_widget_add_css_class(node_tray_button,"node-dock-action");
	home_mechanical_button(node_tray_button,FALSE);
	gtk_widget_set_focus_on_click(node_tray_button,FALSE);
	gtk_accessible_update_property(GTK_ACCESSIBLE(node_tray_button),GTK_ACCESSIBLE_PROPERTY_LABEL,"System Tray",-1);
	gtk_accessible_update_state(GTK_ACCESSIBLE(node_tray_button),GTK_ACCESSIBLE_STATE_EXPANDED,FALSE,-1);
	g_signal_connect(node_tray_button,"clicked",G_CALLBACK(open_system_tray),NULL);
	gtk_fixed_put(GTK_FIXED(node_dock),node_tray_button,0,18);
	gtk_widget_set_visible(node_tray_button,FALSE);
	node_speaker=gtk_button_new_from_icon_name("audio-volume-high-symbolic");
	gtk_widget_add_css_class(node_speaker,"home-dock-button");
	gtk_widget_add_css_class(node_speaker,"node-dock-action");
	gtk_widget_add_css_class(node_speaker,"node-volume-trigger");
	home_mechanical_button(node_speaker,FALSE);
	gtk_widget_set_focus_on_click(node_speaker,FALSE);
	gtk_accessible_update_property(GTK_ACCESSIBLE(node_speaker),GTK_ACCESSIBLE_PROPERTY_LABEL,"Volume for this Node",-1);
	g_signal_connect(node_speaker,"clicked",G_CALLBACK(open_node_volume),NULL);
	gtk_fixed_put(GTK_FIXED(node_dock),node_speaker,0,18);
	picker_button=gtk_button_new_with_label("+1");
	picker_button_label=gtk_button_get_child(GTK_BUTTON(picker_button));
	gtk_widget_set_halign(picker_button_label,GTK_ALIGN_CENTER);
	gtk_widget_set_valign(picker_button_label,GTK_ALIGN_CENTER);
	gtk_widget_add_css_class(picker_button,"window-picker-trigger");
	analog_button_style(picker_button,0,0);
	gtk_widget_set_focus_on_click(picker_button,FALSE);
	gtk_accessible_update_property(GTK_ACCESSIBLE(picker_button),GTK_ACCESSIBLE_PROPERTY_LABEL,
		"Show windows outside the preview lane",-1);
	g_signal_connect(picker_button,"clicked",G_CALLBACK(open_window_picker),NULL);
	gtk_widget_set_size_request(picker_button,PICKER_BUTTON_SIZE,PICKER_BUTTON_SIZE);
	gtk_widget_set_visible(picker_button,FALSE);
	gtk_fixed_put(GTK_FIXED(node_dock),picker_button,0,0);
	gtk_widget_set_visible(node_dock,FALSE);
	gtk_overlay_add_overlay(overlay,node_dock);
	// Leave all dock chrome/input on the original shell surface. A telemetry
	// tick updates this small paint-only buffer, not the full-screen shell.
	node_telemetry_layer=paint_only_layer("mothership-node-telemetry","Mothership Node Telemetry",NULL,NULL);
	gtk_layer_set_anchor(node_telemetry_layer,RIGHT_EDGE,FALSE);
	gtk_layer_set_anchor(node_telemetry_layer,BOTTOM_EDGE,FALSE);
	node_readout=home_node_telemetry_new();
	gtk_window_set_child(node_telemetry_layer,node_readout);
	// The keyboard guide stays on its noninteractive layer below app windows.
	GtkWidget *annotations=gtk_overlay_new();
	gtk_widget_set_halign(annotations,GTK_ALIGN_FILL);
	gtk_widget_set_valign(annotations,GTK_ALIGN_END);
	gtk_widget_set_size_request(annotations,-1,SHELL_NODE_GUIDE_HEIGHT);
	gtk_widget_set_margin_bottom(annotations,HOME_BOTTOM+HOME_HEIGHT+SHELL_NODE_GUIDE_GAP);
	gtk_widget_set_can_target(annotations,FALSE);
	node_keys=gtk_drawing_area_new();
	gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(node_keys),NODE_GUIDE_WIDTH);
	gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(node_keys),SHELL_NODE_GUIDE_HEIGHT);
	gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(node_keys),node_keycaps,NULL,NULL);
	gtk_widget_set_halign(node_keys,GTK_ALIGN_START);gtk_widget_set_valign(node_keys,GTK_ALIGN_CENTER);
	gtk_widget_set_can_target(node_keys,FALSE);gtk_widget_set_visible(node_keys,FALSE);
	gtk_overlay_add_overlay(GTK_OVERLAY(annotations),node_keys);
	wallpaper_add_guide(annotations);
	node_settings=gtk_overlay_new();
	gtk_widget_set_halign(node_settings,GTK_ALIGN_CENTER);gtk_widget_set_valign(node_settings,GTK_ALIGN_END);
	gtk_widget_set_margin_bottom(node_settings,HOME_BOTTOM+HOME_HEIGHT+16);
	GtkWidget *back=gtk_drawing_area_new();
	gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(back),draw_settings_face,NULL,NULL);
	gtk_overlay_set_child(GTK_OVERLAY(node_settings),back);
	GtkWidget *body=gtk_box_new(GTK_ORIENTATION_VERTICAL,12);
	gtk_widget_add_css_class(body,"shell-settings-content");
	gtk_overlay_add_overlay(GTK_OVERLAY(node_settings),body);
	gtk_overlay_set_measure_overlay(GTK_OVERLAY(node_settings),body,TRUE);
	GtkWidget *head=gtk_label_new("Node Panel Settings");
	gtk_widget_add_css_class(head,"shell-settings-title");
	gtk_label_set_xalign(GTK_LABEL(head),0);
	gtk_box_append(GTK_BOX(body),head);
	gtk_box_append(GTK_BOX(body),gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
	GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);
	GtkWidget *label=gtk_label_new("Keyboard guide");
	gtk_label_set_xalign(GTK_LABEL(label),0);gtk_widget_set_hexpand(label,TRUE);
	gtk_box_append(GTK_BOX(row),label);
	gtk_box_append(GTK_BOX(row),mechanical_on_off(&node_guide_off,&node_guide_on,"Keyboard guide"));
	gtk_box_append(GTK_BOX(body),row);
	row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);
	label=gtk_label_new("Telemetry");
	gtk_label_set_xalign(GTK_LABEL(label),0);gtk_widget_set_hexpand(label,TRUE);
	gtk_box_append(GTK_BOX(row),label);
	gtk_box_append(GTK_BOX(row),mechanical_on_off(&node_telemetry_off,&node_telemetry_on,"Telemetry"));
	gtk_box_append(GTK_BOX(body),row);
	row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);
	label=gtk_label_new("Preview zoom");
	gtk_label_set_xalign(GTK_LABEL(label),0);gtk_widget_set_hexpand(label,TRUE);
	gtk_box_append(GTK_BOX(row),label);
	node_preview_zoom=gtk_spin_button_new_with_range(NODE_PREVIEW_ZOOM_MIN,NODE_PREVIEW_ZOOM_MAX,1);
	gtk_widget_add_css_class(node_preview_zoom,"home-mechanical-spin");
	gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(node_preview_zoom),TRUE);
	gtk_editable_set_width_chars(GTK_EDITABLE(node_preview_zoom),3);
	gtk_accessible_update_property(GTK_ACCESSIBLE(node_preview_zoom),GTK_ACCESSIBLE_PROPERTY_LABEL,"Preview zoom",-1);
	gtk_accessible_update_property(GTK_ACCESSIBLE(node_preview_zoom),GTK_ACCESSIBLE_PROPERTY_DESCRIPTION,
		"Alt+Tab thumbnail zoom, in percent",-1);
	gtk_box_append(GTK_BOX(row),node_preview_zoom);
	gtk_box_append(GTK_BOX(row),gtk_label_new("%"));
	gtk_box_append(GTK_BOX(body),row);
	row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);
	label=gtk_label_new("Hover zoom");
	gtk_label_set_xalign(GTK_LABEL(label),0);gtk_widget_set_hexpand(label,TRUE);
	gtk_box_append(GTK_BOX(row),label);
	node_hover_zoom=gtk_spin_button_new_with_range(NODE_HOVER_ZOOM_MIN,NODE_HOVER_ZOOM_MAX,5);
	gtk_widget_add_css_class(node_hover_zoom,"home-mechanical-spin");
	gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(node_hover_zoom),TRUE);
	gtk_editable_set_width_chars(GTK_EDITABLE(node_hover_zoom),3);
	gtk_accessible_update_property(GTK_ACCESSIBLE(node_hover_zoom),GTK_ACCESSIBLE_PROPERTY_LABEL,"Hover zoom",-1);
	gtk_accessible_update_property(GTK_ACCESSIBLE(node_hover_zoom),GTK_ACCESSIBLE_PROPERTY_DESCRIPTION,
		"Preview strip hover zoom, in percent",-1);
	gtk_box_append(GTK_BOX(row),node_hover_zoom);
	gtk_box_append(GTK_BOX(row),gtk_label_new("%"));
	gtk_box_append(GTK_BOX(body),row);
	node_layout_row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);
	label=gtk_label_new("Layout");
	gtk_label_set_xalign(GTK_LABEL(label),0);gtk_widget_set_hexpand(label,TRUE);
	gtk_box_append(GTK_BOX(node_layout_row),label);
	node_layout_flow=gtk_toggle_button_new_with_label("Flow");
	node_layout_stage=gtk_toggle_button_new_with_label("Stage");
	gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(node_layout_stage),GTK_TOGGLE_BUTTON(node_layout_flow));
	home_mechanical_button(node_layout_flow,FALSE);
	home_mechanical_button(node_layout_stage,FALSE);
	gtk_box_append(GTK_BOX(node_layout_row),node_layout_flow);
	gtk_box_append(GTK_BOX(node_layout_row),node_layout_stage);
	g_signal_connect(node_layout_flow,"clicked",G_CALLBACK(node_stage_choice),GINT_TO_POINTER(0));
	g_signal_connect(node_layout_stage,"clicked",G_CALLBACK(node_stage_choice),GINT_TO_POINTER(1));
	gtk_box_append(GTK_BOX(body),node_layout_row);
	node_guide_error=gtk_label_new("");
	gtk_label_set_wrap(GTK_LABEL(node_guide_error),TRUE);
	gtk_widget_set_visible(node_guide_error,FALSE);
	gtk_box_append(GTK_BOX(body),node_guide_error);
	gtk_box_append(GTK_BOX(body),gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
	row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);
	gtk_widget_set_halign(row,GTK_ALIGN_END);
	const char *buttons[]={"Cancel","Save"};
	for(int i=0;i<2;i++){
		GtkWidget *b=gtk_button_new_with_label(buttons[i]);
		home_mechanical_button(b,i==1);
		g_signal_connect(b,"clicked",i?G_CALLBACK(node_save):G_CALLBACK(node_cancel),NULL);
		gtk_box_append(GTK_BOX(row),b);
	}
	gtk_box_append(GTK_BOX(body),row);
	gtk_widget_set_visible(node_settings,FALSE);
	gtk_overlay_add_overlay(overlay,node_settings);
}

static void init_home_controls(GtkOverlay *overlay) {
	dock=home_dock_new(home_action,home_hangar_connect,schedule_region);
	gtk_overlay_add_overlay(overlay,dock);
	gtk_overlay_add_overlay(overlay,home_dock_hangar());
	node_panel_init(overlay);
	init_window_picker(overlay);
	home_guide=gtk_drawing_area_new();
	gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(home_guide),draw_home_guide,NULL,NULL);
	gtk_widget_set_halign(home_guide,GTK_ALIGN_START);
	gtk_widget_set_valign(home_guide,GTK_ALIGN_END);
	gtk_widget_set_margin_bottom(home_guide,HOME_HEIGHT+HOME_BOTTOM+16);
	gtk_widget_set_can_target(home_guide,FALSE);
	gtk_widget_set_visible(home_guide,FALSE);
	gtk_overlay_add_overlay(overlay,home_guide);
	app_grid=app_grid_new(close_app_grid);
	gtk_overlay_add_overlay(overlay,app_grid);
	slash_box=slash_ui_new(close_slash,slash_enter_node,slash_open_view,schedule_region,slash_delete);
	gtk_overlay_add_overlay(overlay,slash_box);
	notice_init(overlay,notice_region_changed);
	node_volume_init(overlay,volume_region_changed);
	system_tray_init(overlay,tray_region_changed);
	home_settings=gtk_overlay_new();
	gtk_widget_add_css_class(home_settings,"home-settings-motion");
	gtk_box_append(GTK_BOX(form),home_settings);
	gtk_widget_set_visible(home_settings,FALSE);
	GtkWidget *settings_face=gtk_drawing_area_new();
	gtk_widget_set_can_target(settings_face,FALSE);
	gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(settings_face),draw_settings_face,NULL,NULL);
	gtk_overlay_set_child(GTK_OVERLAY(home_settings),settings_face);
	GtkWidget *content=home_settings_body=gtk_box_new(GTK_ORIENTATION_VERTICAL,9);
	gtk_widget_add_css_class(content,"shell-settings-content");
	gtk_overlay_add_overlay(GTK_OVERLAY(home_settings),content);
	gtk_overlay_set_measure_overlay(GTK_OVERLAY(home_settings),content,TRUE);
	gtk_widget_set_vexpand(home_settings,TRUE);

	GtkWidget *head=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6);
	GtkWidget *heading=gtk_label_new("HOME PANEL SETTINGS");
	gtk_widget_add_css_class(heading,"shell-settings-title");
	gtk_widget_set_halign(heading,GTK_ALIGN_START);
	gtk_widget_set_hexpand(heading,TRUE);
	gtk_label_set_ellipsize(GTK_LABEL(heading),PANGO_ELLIPSIZE_END);
	gtk_box_append(GTK_BOX(head),heading);
	GtkWidget *comma=gtk_label_new(",");
	gtk_widget_add_css_class(comma,"shell-settings-key");
	gtk_box_append(GTK_BOX(head),comma);
	GtkWidget *esc=gtk_button_new_with_label("Esc");
	home_mechanical_button(esc,FALSE);
	gtk_accessible_update_property(GTK_ACCESSIBLE(esc),GTK_ACCESSIBLE_PROPERTY_DESCRIPTION,"Close without saving",-1);
	g_signal_connect(esc,"clicked",G_CALLBACK(dock_cancel),NULL);
	gtk_box_append(GTK_BOX(head),esc);
	gtk_box_append(GTK_BOX(content),head);
	gtk_box_append(GTK_BOX(content),gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

	GtkWidget *body=gtk_scrolled_window_new();
	gtk_widget_add_css_class(body,"home-settings-scroll");
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(body),GTK_POLICY_NEVER,GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_overlay_scrolling(GTK_SCROLLED_WINDOW(body),FALSE);
	gtk_widget_set_vexpand(body,TRUE);
	gtk_box_append(GTK_BOX(content),body);
	GtkGrid *grid=GTK_GRID(gtk_grid_new());
	gtk_grid_set_column_spacing(grid,9);
	gtk_grid_set_row_spacing(grid,11);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(body),GTK_WIDGET(grid));
	settings_section(grid,0,"LAYOUT");
	width_input=gtk_spin_button_new_with_range(HOME_WIDTH_MIN,HOME_WIDTH_MAX,1);
	gtk_widget_add_css_class(width_input,"home-mechanical-spin");
	gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(width_input),TRUE);
	gtk_editable_set_width_chars(GTK_EDITABLE(width_input),4);
	settings_insert(grid,1,"Panel _width",width_input);
	gtk_accessible_update_property(GTK_ACCESSIBLE(width_input),GTK_ACCESSIBLE_PROPERTY_DESCRIPTION,"Width in logical pixels",-1);
	g_signal_connect(width_input,"value-changed",G_CALLBACK(dock_preview),NULL);
	g_signal_connect(width_input,"changed",G_CALLBACK(dock_preview_entry),NULL);
	GtkWidget *guide_switch=mechanical_on_off(&home_guide_off,&home_guide_on,"Home keyboard guide");
	settings_insert(grid,2,"Keyboard guide",guide_switch);
	cursor_size_input=gtk_spin_button_new_with_range(CURSOR_SIZE_MIN,CURSOR_SIZE_MAX,4);
	gtk_widget_add_css_class(cursor_size_input,"home-mechanical-spin");
	gtk_widget_add_css_class(cursor_size_input,"cursor-size-input");
	gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(cursor_size_input),TRUE);
	gtk_editable_set_width_chars(GTK_EDITABLE(cursor_size_input),4);
	settings_insert(grid,3,"Cursor _size",cursor_size_input);
	gtk_accessible_update_property(GTK_ACCESSIBLE(cursor_size_input),GTK_ACCESSIBLE_PROPERTY_DESCRIPTION,"Pointer size in logical pixels", -1);
	GtkWidget *display_link=gtk_button_new_with_label("Display ↗");
	home_mechanical_button(display_link,FALSE);
	gtk_widget_set_halign(display_link,GTK_ALIGN_START);
	GtkWidget *links=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6);
	gtk_grid_attach(grid,links,0,4,2,1);
	gtk_box_append(GTK_BOX(links),display_link);
	GtkWidget *shortcut_link=gtk_button_new_with_label("Keyboard Shortcuts ↗");
	home_mechanical_button(shortcut_link,FALSE);
	gtk_box_append(GTK_BOX(links),shortcut_link);
	g_signal_connect(shortcut_link,"clicked",G_CALLBACK(open_shortcuts),NULL);
	display_settings_bind_link(display_link);
	g_signal_connect(display_link,"clicked",G_CALLBACK(open_display_settings),NULL);
	gtk_grid_attach(grid,gtk_separator_new(GTK_ORIENTATION_HORIZONTAL),0,5,2,1);

	settings_section(grid,6,"WALLPAPER");
	GtkWidget *video=mechanical_on_off(&video_off,&video_on,"Video wallpaper");
	settings_insert(grid,7,"Video wallpaper",video);
	GtkWidget *hint=gtk_label_new("4K video can increase GPU load.");
	gtk_widget_add_css_class(hint,"shell-settings-hint");
	gtk_label_set_xalign(GTK_LABEL(hint),0);
	gtk_label_set_wrap(GTK_LABEL(hint),TRUE);
	gtk_grid_attach(grid,hint,0,8,2,1);
	settings_section(grid,9,"Show toasts");
	GtkWidget *toast_modes=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6);
	const char *toast_labels[]={"Current Node","All Nodes","Off"};
	for(int i=0;i<3;i++) {
		toast_mode_input[i]=gtk_toggle_button_new_with_label(toast_labels[i]);
		home_mechanical_button(toast_mode_input[i],FALSE);
		if(i) gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(toast_mode_input[i]),GTK_TOGGLE_BUTTON(toast_mode_input[0]));
		gtk_box_append(GTK_BOX(toast_modes),toast_mode_input[i]);
	}
	gtk_accessible_update_property(GTK_ACCESSIBLE(toast_modes),GTK_ACCESSIBLE_PROPERTY_LABEL,"Show toasts",-1);
	gtk_grid_attach(grid,toast_modes,0,10,2,1);
	width_note=gtk_label_new("");
	gtk_label_set_wrap(GTK_LABEL(width_note),TRUE);
	gtk_widget_add_css_class(width_note,"shell-settings-hint");
	gtk_label_set_xalign(GTK_LABEL(width_note),0);
	gtk_widget_set_visible(width_note,FALSE);
	gtk_box_append(GTK_BOX(content),width_note);
	gtk_box_append(GTK_BOX(content),gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

	GtkWidget *actions=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6);
	gtk_box_append(GTK_BOX(content),actions);
	GtkWidget *map=gtk_button_new_with_label("Map Settings ↗");
	gtk_widget_add_css_class(map,"shell-settings-link");
	g_signal_connect(map,"clicked",G_CALLBACK(from_dock_to_map_settings),NULL);
	gtk_box_append(GTK_BOX(actions),map);
	GtkWidget *spacer=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0);
	gtk_widget_set_hexpand(spacer,TRUE);
	gtk_box_append(GTK_BOX(actions),spacer);
	GtkWidget *cancel=gtk_button_new_with_label("Cancel");
	home_mechanical_button(cancel,FALSE);
	g_signal_connect(cancel,"clicked",G_CALLBACK(dock_cancel),NULL);
	gtk_box_append(GTK_BOX(actions),cancel);
	GtkWidget *save=gtk_button_new_with_label("Save");
	home_mechanical_button(save,TRUE);
	g_signal_connect(save,"clicked",G_CALLBACK(dock_save),NULL);
	gtk_box_append(GTK_BOX(actions),save);
	display_settings_body=display_settings_new(display_close);
	gtk_overlay_add_overlay(GTK_OVERLAY(home_settings),display_settings_body);
	gtk_overlay_set_measure_overlay(GTK_OVERLAY(home_settings),display_settings_body,TRUE);
	gtk_widget_set_visible(display_settings_body,FALSE);
	shortcuts_body=shortcut_settings_new(shortcuts_close);
	gtk_overlay_add_overlay(GTK_OVERLAY(home_settings),shortcuts_body);
	gtk_overlay_set_measure_overlay(GTK_OVERLAY(home_settings),shortcuts_body,TRUE);
	gtk_widget_set_visible(shortcuts_body,FALSE);
	GtkWidget *hide=gtk_button_new_with_label("Hide");
	gtk_box_append(GTK_BOX(overview_form),hide);
	g_signal_connect(hide,"clicked",G_CALLBACK(toggle_center),NULL);
}

static void empty_input(GtkWidget *widget, gpointer data) {
	(void)data;
	GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(widget));
	if (surface != NULL) {
		cairo_region_t *empty = cairo_region_create();
		gdk_surface_set_input_region(surface, empty);
		cairo_region_destroy(empty);
	}
}

GtkWindow *paint_only_layer(const char *name, const char *title,
		GtkDrawingAreaDrawFunc draw, GtkWidget **canvas) {
	GtkWindow *layer = GTK_WINDOW(gtk_window_new());
	gtk_window_set_title(layer, title);
	gtk_window_set_decorated(layer, FALSE);
	gtk_widget_set_focusable(GTK_WIDGET(layer), FALSE);
	gtk_widget_add_css_class(GTK_WIDGET(layer), "mothership-shell");
	gtk_layer_init_for_window(layer);
	gtk_layer_set_namespace(layer, name);
	gtk_layer_set_keyboard_mode(layer, KEYS_NONE);
	for (int edge = 0; edge < 4; ++edge) gtk_layer_set_anchor(layer, edge, TRUE);
	if (canvas) {
		*canvas=gtk_drawing_area_new();
		gtk_widget_set_focusable(*canvas,FALSE);
		gtk_widget_set_can_target(*canvas,FALSE);
		gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(*canvas),draw,NULL,NULL);
		gtk_window_set_child(layer,*canvas);
	}
	g_signal_connect(layer, "realize", G_CALLBACK(empty_input), NULL);
	g_signal_connect(layer, "map", G_CALLBACK(empty_input), NULL);
	return layer;
}

// Keep a transparent one-pixel node even before the initial allocation
// and across dock/menu transitions. It never joins the Wayland input region.
static void mark_clear_buffer(GtkDrawingArea *area,cairo_t *cr,int w,int h,gpointer unused) {
	(void)area;(void)w;(void)h;(void)unused;
	cairo_set_operator(cr,CAIRO_OPERATOR_SOURCE);
	cairo_set_source_rgba(cr,0,0,0,0);
	cairo_paint(cr);
}

int panel_init(void) {
	if (!gtk_init_check() || gtk_layer_get_protocol_version() < 4) {
		return 0;
	}
	window = GTK_WINDOW(gtk_window_new());
	gtk_window_set_title(window, "Mothership Command Center");
	gtk_window_set_default_size(window, 1, 1);
	gtk_window_set_resizable(window, TRUE);
	gtk_window_set_decorated(window, FALSE);
	gtk_layer_init_for_window(window);
	gtk_layer_set_namespace(window, "mothership-command-center");
	for(int edge=0;edge<4;edge++) gtk_layer_set_anchor(window,edge,TRUE);
	gtk_layer_set_keyboard_mode(window, KEYS_NONE);

	GtkCssProvider *skin = gtk_css_provider_new();
	g_autofree char *css=g_strdup_printf(
		"window.mothership-shell { background: transparent; font-family: \"%s\", sans-serif; font-size: 14px; font-weight: 500; }"
		"window.mothership-shell .heading { font-weight: 600; }"
		".mothership-controls { background-color: #172435; border-radius: 10px; padding: 8px; }"
		".mothership-controls .heading, .mothership-controls .shell-readout { color: #EDF5FB; }"
		".mothership-controls button { padding-left: 8px; padding-right: 8px; }"
		".mothership-controls.mothership-settings-surface { background-color: #172435; }"
		".mothership-settings-surface entry, .mothership-settings-surface button { min-height: 22px; padding-top: 2px; padding-bottom: 2px; }"
		".mothership-controls.mothership-map-keyboard { background: transparent; padding: 0; }"
		".mothership-settings-label { color: #EDF5FB; }"
		".flow-group-settings > contents { background: #101d29; border: 1px solid #567683; border-radius: 2px; padding: 12px; color: #d5e9f0; }"
		".flow-group-settings spinbutton, .flow-group-settings spinbutton text, .flow-group-settings spinbutton button { color: #d5e9f0; background: #172a34; background-image: none; box-shadow: none; border-color: #304b59; }"
		".display-choice > button { color: #d6e8f0; background: #172a34; border: 1px solid #3b5867; border-radius: 2px; }"
		".display-choice > button:hover { background: #203b47; border-color: #6da5b6; }"
		".mothership-controls.shell-settings-positioned { background: transparent; padding: 0; }"
		"@keyframes home-settings-slide { from { transform: translateY(34px); opacity: 0; }"
		" to { transform: translateY(0px); opacity: 1; } }"
		"@keyframes home-settings-replay { from { transform: translateY(34px); opacity: 0; }"
		" to { transform: translateY(0px); opacity: 1; } }"
		"@keyframes map-settings-slide { from { transform: translateX(44px); opacity: 0; }"
		" to { transform: translateX(0px); opacity: 1; } }"
		"@keyframes map-settings-replay { from { transform: translateX(44px); opacity: 0; }"
		" to { transform: translateX(0px); opacity: 1; } }"
		"@keyframes home-settings-leave { from { transform: translateY(0px); opacity: 1; }"
		" to { transform: translateY(34px); opacity: 0; } }"
		"@keyframes map-settings-leave { from { transform: translateX(0px); opacity: 1; }"
		" to { transform: translateX(44px); opacity: 0; } }"
		".settings-enter, .settings-enter-again, .settings-exit { animation-duration: %dms; animation-timing-function: ease-in-out; }"
		".home-settings-motion.settings-enter { animation-name: home-settings-slide; }"
		".home-settings-motion.settings-exit { animation-name: home-settings-leave; }"
		".map-settings-motion.settings-enter { animation-name: map-settings-slide; }"
		".map-settings-motion.settings-exit { animation-name: map-settings-leave; }"
		".home-settings-motion.settings-enter-again { animation-name: home-settings-replay; }"
		".map-settings-motion.settings-enter-again { animation-name: map-settings-replay; }"
		".settings-exit { animation-fill-mode: forwards; }"
		".shell-settings-content { padding: 17px 21px; color: #b8cedb; font-size: 14px; }"
		".shell-settings-content separator { background: #304b59; min-height: 1px; }"
		".home-settings-scroll scrollbar.vertical { margin-left: 6px; padding: 0; border: 0; "
		"background: transparent; background-image: none; box-shadow: none; }"
		".home-settings-scroll scrollbar.vertical trough { min-width: 6px; padding: 0; border: 0; "
		"background: #101d26; background-image: none; box-shadow: none; border-radius: 0; }"
		".home-settings-scroll scrollbar.vertical slider { min-width: 6px; min-height: 24px; margin: 0; "
		"padding: 0; border: 0; border-radius: 0; box-shadow: none; background: #45616f; background-image: none; }"
		".home-settings-scroll scrollbar.vertical slider:hover, "
		".home-settings-scroll scrollbar.vertical slider:active { background: #7195a4; }"
		".shell-settings-title { font-size: 19px; font-weight: 400; color: #e0edf4; }"
		".shell-settings-title, window.mothership-shell .shell-system-title { font-family: \"" SHELL_DISPLAY_FONT "\", \"Chakra Petch\", sans-serif; font-weight: 400; }"
		".shell-settings-section { font-size: 11px; color: #84abba; font-weight: bold; }"
		".shell-settings-hint { font-size: 12px; color: #a3becb; }"
		".shell-settings-content button, .shell-settings-key { color: #cee0e7; border: 1px solid #3b5665; "
		"background: #162632; border-radius: 3px; box-shadow: none; padding: 2px 8px; min-height: 23px; }"
		".shell-settings-content button:checked, .shell-settings-content .shell-settings-save { "
		"background: #203d4b; border-color: #77b6c6; color: #eef9fa; }"
		".shell-settings-content button:hover { background: #294653; }"
		".shell-settings-content .shell-settings-link { color: #a9c9d8; background: transparent; border-color: transparent; padding-left: 0; }"
		".shell-settings-content .shell-settings-link:hover { color: #eef9fa; }"
		".shell-settings-content button:disabled { color: #77929b; }"
		".shell-settings-content entry, .shell-settings-content spinbutton, .shell-settings-content spinbutton text { "
		"color: #e1edf8; background: #142633; border-color: #3b5665; box-shadow: none; }"
		".shell-settings-content spinbutton text { padding-left: 8px; padding-right: 8px; }"
		".shell-settings-content button.home-mechanical-button, "
		".shell-settings-content button.home-mechanical-button:hover, "
		".shell-settings-content button.home-mechanical-button:active, "
		".shell-settings-content button.home-mechanical-button:checked { "
		"border: 0; border-radius: 0; box-shadow: none; background: transparent; "
		"padding: 0; min-height: 29px; outline-color: transparent; }"
		".home-mechanical-button .home-mechanical-label { padding: 2px 9px; "
		"transition: transform 105ms ease-out; }"
		".home-mechanical-button:active .home-mechanical-label, "
		".home-mechanical-spin button:active image { transform: translateY(1px); transition: transform 60ms ease-out; }"
		".shell-settings-content button.home-mechanical-primary, "
		".shell-settings-content button.home-mechanical-button:checked { color: #eef9fa; }"
		".shell-settings-content .home-mechanical-spin button { box-shadow: inset 0 -2px #101e27; }"
		".shell-settings-content .home-mechanical-spin button:active { background: #10212b; box-shadow: none; }"
		".home-dock-button { min-height: 0; min-width: 0; padding: 0; border: 0; border-radius: 0; margin: 0; background: transparent; box-shadow: none; outline-offset: -3px; }"
		".home-dock-button:focus-visible { outline-color: transparent; }"
		".node-dock-action .home-mechanical-label { padding: 0; }"
		".home-hangar { color: #b9d4df; font-size: 12px; padding: 14px; }"
		".home-hangar button label { color: #b9d4df; }"
		"@keyframes hangar-takeoff { from { transform: translateY(0); opacity: 1; }"
		" to { transform: translateY(-25px); opacity: 0; } }"
		".hangar-takeoff { animation: hangar-takeoff 200ms ease-in both; }"
		".home-slash { color: #a4e0ea; padding: 5px 15px; min-height: 37px; }"
		".slash-suggestions { padding: 12px 15px; color: #dcebf4; }"
		".slash-row, .slash-row:hover, .slash-row:active { background: transparent; border: 0; box-shadow: none; padding: 0; margin: 0; transform: none; transition: none; }"
		".slash-row label { color: #dcebf4; }"
		".slash-row-line { padding: 0 11px; }"
		".slash-empty { padding: 10px; color: #a2bfce; }"
		".home-slash entry { color: #e1edf8; caret-color: #aaf1f4; background: transparent; border: 0; border-radius: 0; box-shadow: none; outline: none; margin: 0; padding: 0 4px; min-height: 25px; }"
		".home-slash entry:focus-within { outline: none; box-shadow: none; }"
		".app-grid-search { color: #d9eaf3; caret-color: #aaf1f4; background: transparent; border: 0; "
		"box-shadow: none; padding: 0 14px; min-height: 39px; }"
		".app-grid-search:focus-within { outline: none; }"
		".app-grid-search placeholder { color: #aac1ce; }"
		".app-grid-click { min-height: 0; min-width: 0; padding: 0; border: 0; border-radius: 0; "
		"margin: 0; background: transparent; box-shadow: none; outline-color: transparent; }"
		".app-grid-divider { background: #2a424f; min-height: 1px; }"
		".window-picker-trigger { font-size: 13px; font-weight: 700; }"
		".window-picker-tile { background: rgba(12, 27, 37, .40); border: 1px solid rgba(115, 173, 193, .38);"
		"min-width: 0; min-height: 0; padding: 0; border-radius: 0; box-shadow: none; }"
		".window-picker-tile:hover { background: rgba(54, 91, 106, .32); }"
		".window-picker-tile:focus-visible { outline-color: #88c7d2; outline-offset: -2px; }"
		".app-grid-label { color: #e0edf4; font-size: 12px; }"
		".app-grid-full-name { color: #e0edf4; background: #132932; padding: 2px 8px; font-size: 11px; }"
		".home-notice { color: #dbedfa; background: #101e28; border: 1px solid #607f91; padding: 6px 10px; }"
		".mothership-render-fps { color: #EDF5FB; background: #172435; "
		"padding: 4px 8px; font-size: 12px; }",shell_font_family(),SETTINGS_MOTION_MS);
	gtk_css_provider_load_from_string(skin,css);
	gtk_style_context_add_provider_for_display(gdk_display_get_default(),
		GTK_STYLE_PROVIDER(skin), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref(skin);
	gtk_widget_add_css_class(GTK_WIDGET(window), "mothership-shell");
	map_hud_init();
	map_pointer_init(window);
	GtkWidget *overlay = gtk_overlay_new();
	gtk_window_set_child(window, overlay);
	GtkWidget *viewport=gtk_drawing_area_new();
	gtk_widget_set_can_target(viewport,FALSE);
	gtk_widget_set_focusable(viewport,TRUE);
	g_signal_connect(viewport,"resize",G_CALLBACK(viewport_changed),NULL);
	gtk_overlay_set_child(GTK_OVERLAY(overlay),viewport);
	map_clear_buffer=gtk_drawing_area_new();
	gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(map_clear_buffer),mark_clear_buffer,NULL,NULL);
	gtk_widget_set_size_request(map_clear_buffer,1,1);
	gtk_widget_set_halign(map_clear_buffer,GTK_ALIGN_START);
	gtk_widget_set_valign(map_clear_buffer,GTK_ALIGN_START);
	gtk_widget_set_can_target(map_clear_buffer,FALSE);
	gtk_overlay_add_overlay(GTK_OVERLAY(overlay),map_clear_buffer);
	form = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	gtk_widget_set_focusable(form, TRUE);
	gtk_widget_add_css_class(form, "mothership-controls");
	gtk_widget_set_margin_start(form, 12);
	gtk_widget_set_margin_end(form, 12);
	gtk_widget_set_margin_top(form, 8);
	gtk_widget_set_margin_bottom(form, 8);
	gtk_widget_set_halign(form,GTK_ALIGN_END);
	gtk_widget_set_valign(form,GTK_ALIGN_END);
	gtk_widget_set_size_request(form,450,-1);
	gtk_overlay_add_overlay(GTK_OVERLAY(overlay), form);
	entry_button = gtk_button_new_with_label("Command Center");
	gtk_widget_set_halign(entry_button, GTK_ALIGN_END);
	gtk_widget_set_valign(entry_button, GTK_ALIGN_END);
	gtk_widget_set_margin_end(entry_button,12);
	gtk_widget_set_margin_bottom(entry_button,12);
	g_signal_connect(entry_button, "clicked", G_CALLBACK(toggle_center), GINT_TO_POINTER(TRUE));
	gtk_overlay_add_overlay(GTK_OVERLAY(overlay), entry_button);
	normal = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	gtk_box_append(GTK_BOX(form), normal);
	GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_box_append(GTK_BOX(normal), bar);
	GtkWidget *heading = gtk_label_new("Command Center");
	gtk_widget_add_css_class(heading, "heading");
	gtk_widget_add_css_class(heading, "shell-system-title");
	gtk_box_append(GTK_BOX(bar), heading);
	title = gtk_label_new("Connecting…");
	gtk_widget_add_css_class(title,"shell-readout");
	gtk_label_set_ellipsize(GTK_LABEL(title),PANGO_ELLIPSIZE_END);
	gtk_widget_set_hexpand(title, TRUE);
	gtk_label_set_xalign(GTK_LABEL(title), 0);
	gtk_box_append(GTK_BOX(bar), title);
	map_view_normal = gtk_button_new_with_label("Map View");
	g_signal_connect(map_view_normal,"clicked",G_CALLBACK(open_map_view),NULL);
	gtk_box_append(GTK_BOX(bar),map_view_normal);
	add_settings_button(GTK_BOX(bar));
	GtkWidget *hide = gtk_button_new_with_label("Hide");
	g_signal_connect(hide, "clicked", G_CALLBACK(toggle_center), GINT_TO_POINTER(FALSE));
	gtk_box_append(GTK_BOX(bar), hide);

	GtkWidget *create_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_box_append(GTK_BOX(normal), create_row);
	entry = gtk_entry_new();
	gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Node name");
	gtk_entry_set_max_length(GTK_ENTRY(entry), 32);
	gtk_widget_set_hexpand(entry, TRUE);
	gtk_box_append(GTK_BOX(create_row), entry);
	g_signal_connect(entry, "changed", G_CALLBACK(text_changed), NULL);
	g_signal_connect(entry, "activate", G_CALLBACK(submit), GINT_TO_POINTER(1));
	GtkGesture *gesture = gtk_gesture_click_new();
	gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(gesture), GTK_PHASE_CAPTURE);
	g_signal_connect(gesture, "pressed", G_CALLBACK(open_input), NULL);
	gtk_widget_add_controller(entry, GTK_EVENT_CONTROLLER(gesture));
	create = button("Create Node", 1, create_row);

	GtkWidget *lists = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_widget_set_vexpand(lists, TRUE);
	gtk_box_append(GTK_BOX(normal), lists);
	nodes = list_column(lists, "Nodes");
	windows = list_column(lists, "Live windows");

	GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_box_append(GTK_BOX(normal), actions);
	move = button("Attach / Move", 2, actions);
	fly = button("Connect", 3, actions);
	home = button("Home", 4, actions);
	terminal = button("Terminal", 5, actions);
	files = button("File Manager", 6, actions);
	GtkWidget *screenshot_entry = gtk_button_new_with_label("Screenshot");
	gtk_box_append(GTK_BOX(actions), screenshot_entry);
	node_layouts = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_box_append(GTK_BOX(normal), node_layouts);
	layout_buttons[0] = button("Stack", 8, node_layouts);
	layout_buttons[1] = button("Float", 9, node_layouts);
	layout_buttons[2] = button("Flow", 10, node_layouts);
	next_stack = button("Next window", 11, node_layouts);

	overview_form = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	gtk_box_append(GTK_BOX(form), overview_form);
	gtk_widget_set_visible(overview_form, FALSE);
	GtkWidget *overview_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_box_append(GTK_BOX(overview_form), overview_bar);
	GtkWidget *overview_title = gtk_label_new("Overview · Nodes");
	gtk_widget_add_css_class(overview_title, "heading");
	gtk_widget_add_css_class(overview_title, "shell-system-title");
	gtk_widget_set_hexpand(overview_title, TRUE);
	gtk_label_set_xalign(GTK_LABEL(overview_title), 0);
	gtk_box_append(GTK_BOX(overview_bar), overview_title);
	add_settings_button(GTK_BOX(overview_bar));
	overview_home = button("Home / Inbox", 4, overview_bar);
	map_view_open=gtk_button_new_with_label("Map View");
	g_signal_connect(map_view_open,"clicked",G_CALLBACK(open_map_view),NULL);
	gtk_box_append(GTK_BOX(overview_bar),map_view_open);
	GtkWidget *overview_screenshot = gtk_button_new_with_label("Screenshot");
	gtk_box_append(GTK_BOX(overview_form), overview_screenshot);
	GtkWidget *scroll = gtk_scrolled_window_new();
	gtk_widget_set_size_request(scroll, -1, 145);
	gtk_widget_set_vexpand(scroll, TRUE);
	gtk_box_append(GTK_BOX(overview_form), scroll);
	node_grid = gtk_grid_new();
	gtk_grid_set_column_spacing(GTK_GRID(node_grid), 10);
	gtk_grid_set_row_spacing(GTK_GRID(node_grid), 10);
	gtk_widget_set_halign(node_grid, GTK_ALIGN_CENTER);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), node_grid);
	positions = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	status = gtk_label_new("");
	gtk_widget_add_css_class(status,"shell-readout");
	gtk_label_set_xalign(GTK_LABEL(status), 0);
	gtk_label_set_ellipsize(GTK_LABEL(status), PANGO_ELLIPSIZE_END);
	gtk_box_append(GTK_BOX(form), status);
	init_map_settings();
	init_home_controls(GTK_OVERLAY(overlay));
	screenshot_init(window, GTK_OVERLAY(overlay), screenshot_entry, overview_screenshot,
		prepare_screenshot, schedule_region, panel_home_notice);
	GtkGesture *dismiss_popup=gtk_gesture_click_new();
	gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(dismiss_popup),GTK_PHASE_CAPTURE);
	g_signal_connect(dismiss_popup,"pressed",G_CALLBACK(dismiss_popup_press),NULL);
	g_signal_connect(dismiss_popup,"released",G_CALLBACK(dismiss_popup_release),NULL);
	gtk_widget_add_controller(GTK_WIDGET(window),GTK_EVENT_CONTROLLER(dismiss_popup));
	g_signal_connect(window,"notify::is-active",G_CALLBACK(lose_window_focus),NULL);
	GtkEventController *repeat_guard=gtk_event_controller_key_new();
	gtk_event_controller_set_propagation_phase(repeat_guard,GTK_PHASE_CAPTURE);
	g_signal_connect(repeat_guard,"key-pressed",G_CALLBACK(shell_key_repeat),NULL);
	gtk_widget_add_controller(GTK_WIDGET(window),repeat_guard);
	GtkEventController *keys = gtk_event_controller_key_new();
	g_signal_connect(keys, "key-pressed", G_CALLBACK(key_pressed), NULL);
	g_signal_connect(repeat_guard,"key-released",G_CALLBACK(key_released),NULL);
	gtk_widget_add_controller(GTK_WIDGET(window), keys);
	g_signal_connect(window, "close-request", G_CALLBACK(close_window), NULL);
	refresh_actions();
	set_expanded(FALSE);
	gtk_window_set_focus(window,viewport);
	return 1;
}

void panel_run(void) {
	loop = g_main_loop_new(NULL, FALSE);
	g_main_loop_run(loop);
	cancel_connect_relay();
	g_main_loop_unref(loop);
}

static gboolean quit_loop(gpointer data) {
	g_main_loop_quit(loop);
	return G_SOURCE_REMOVE;
}

void panel_quit(void) {
	g_idle_add(quit_loop, NULL);
}
