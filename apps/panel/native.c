//go:build linux && cgo && panel

#include <gtk/gtk.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "native.h"
#include "map_hud.h"
#include "map-pointer.h"
#include "map-camera.h"
#include "shell-paint.h"

/* Stable GTK Layer Shell 1.3 runtime ABI; only the calls used by this
 * fixed overlay. https://github.com/wmww/gtk4-layer-shell/blob/v1.3.0/include/gtk4-layer-shell.h */
enum layer_edge { RIGHT_EDGE = 1, BOTTOM_EDGE = 3 };
enum layer_keys { KEYS_NONE = 0, KEYS_ON_DEMAND = 2 };
extern guint gtk_layer_get_protocol_version(void);
extern void gtk_layer_init_for_window(GtkWindow *);
extern void gtk_layer_set_namespace(GtkWindow *, const char *);
extern void gtk_layer_set_anchor(GtkWindow *, enum layer_edge, gboolean);
extern void gtk_layer_set_margin(GtkWindow *, enum layer_edge, int);
extern void gtk_layer_set_keyboard_mode(GtkWindow *, enum layer_keys);
extern void panelAction(int, char *, char *);
extern char *panelSaveMapSettings(char *, int, int, int, int, int, int);

static GtkWindow *window;
static GtkWidget *title, *status, *entry, *nodes, *windows;
static GtkWidget *create, *move, *fly, *home, *terminal, *files;
static GtkWidget *form, *normal, *overview_form, *entry_button;
static GtkWidget *map_navigation_form, *map_view_open, *map_view_normal, *navigation_done;
static GtkWidget *map_settings_form, *map_border_input, *map_outer_input, *map_inner_input;
static GtkWidget *map_clear_input, *map_frosted_input, *map_settings_note, *map_settings_save;
static GtkWidget *map_animation_input[MAP_ANIMATION_COUNT], *map_fps_input;
static GtkWidget *map_pan_label,*map_pan_minus,*map_pan_plus,*map_pan_reset;
static int theme_pan_speed=MAP_PAN_DEFAULT,saved_pan_override,pan_override;
static char *saved_map_border;
static int saved_map_outer, saved_map_inner;
static gboolean saved_map_frosted,saved_map_fps;
static enum map_animation saved_map_animation=MAP_ANIMATION_LINEAR, map_animation=MAP_ANIMATION_LINEAR;
static gboolean map_settings_active,settings_populating;
static gboolean navigating,map_action_wait,map_action_sent;
static guint camera_tick, navigation_timeout;
static gint64 camera_last;
static unsigned camera_held, shortcut_held;
static struct map_velocity camera_velocity;
static GtkWidget *node_grid, *node_layouts, *layout_buttons[3], *next_stack, *overview_home;
static GHashTable *positions;
static unsigned node_count;
static char *current_node, *current_mode;
static gboolean overview, map_visible, blank_home;
static gboolean showing, expanded, region_pending;
static GMainLoop *loop;
static int enabled;
static char *last_nodes, *last_windows;

struct update {
	char *view, *message, *nodes, *windows, *node, *mode, *profiles;
	int enabled, overview, map_visible, blank_home;
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
	if (map_view_normal) gtk_widget_set_sensitive(map_view_normal, enabled);
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
	if (expanded && !navigating) {
		gdk_surface_set_input_region(surface, NULL);
	} else {
		graphene_rect_t bounds;
		if (!gtk_widget_compute_bounds(navigating?navigation_done:entry_button, GTK_WIDGET(window), &bounds) ||
				bounds.size.width <= 0 || bounds.size.height <= 0) {
			schedule_region();
			return G_SOURCE_REMOVE;
		}
		cairo_rectangle_int_t rect = {
			(int)bounds.origin.x, (int)bounds.origin.y,
			(int)bounds.size.width, (int)bounds.size.height,
		};
		cairo_region_t *region = cairo_region_create_rectangle(&rect);
		gdk_surface_set_input_region(surface, region);
		cairo_region_destroy(region);
	}
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

static void close_map_settings(void) {
	if (!map_settings_active) return;
	map_settings_active = FALSE;
	map_animation=saved_map_animation;
	pan_override=saved_pan_override;
	map_hud_show_fps(saved_map_fps);
	map_hud_set_spacing(saved_map_border, saved_map_outer, saved_map_inner, saved_map_frosted);
	gtk_widget_remove_css_class(form, "mothership-settings-surface");
	gtk_widget_set_visible(map_settings_form, FALSE);
	gtk_widget_set_visible(status, TRUE);
	gtk_widget_set_visible(normal, !overview);
	gtk_widget_set_visible(overview_form, overview);
}

static void stop_navigation(void) {
	if (!navigating && !navigation_timeout) return;
	if (navigation_timeout) {
		g_source_remove(navigation_timeout);
		navigation_timeout = 0;
	}
	navigating = FALSE;
	map_action_wait = map_action_sent = FALSE;
	map_pointer_zoom_enabled(FALSE,map_hud_outer_margin());
	map_hud_cancel_zoom();
	map_hud_finish_effect();
	map_hud_close_profile();
	camera_held = 0;
	camera_velocity = (struct map_velocity){0};
	if (camera_tick) {
		gtk_widget_remove_tick_callback(map_hud_clock(), camera_tick);
		camera_tick = 0;
	}
	gtk_widget_set_visible(map_navigation_form, FALSE);
	gtk_widget_set_visible(normal, !overview);
	gtk_widget_set_visible(overview_form, overview);
	gtk_widget_set_visible(status, TRUE);
	return_keys();
}

static void set_expanded(gboolean value) {
	expanded = value;
	if (!value) {
		stop_navigation();
		close_map_settings();
		return_keys();
	}
	gtk_widget_set_visible(form, value);
	gtk_widget_set_visible(entry_button, !value);
	schedule_region();
}

static void toggle_center(GtkButton *button, gpointer data) {
	(void)button;
	gboolean open=GPOINTER_TO_INT(data);
	if (open) gtk_layer_set_keyboard_mode(window, KEYS_ON_DEMAND);
	set_expanded(open);
	if (open) gtk_widget_grab_focus(form);
}

static void open_input(GtkGestureClick *gesture, int presses, double x, double y, gpointer data) {
	gtk_layer_set_keyboard_mode(window, KEYS_ON_DEMAND);
}

enum camera_direction { MOVE_LEFT=1, MOVE_RIGHT=2, MOVE_UP=4, MOVE_DOWN=8 };

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

static void fly_to_node(const char *name) {
	if (!enabled || !overview) return;
	char *target=g_strdup(name);
	set_expanded(FALSE);
	panelAction(12,target,"");
	g_free(target);
}

void panel_map_click(double x,double y) {
	if (enabled && navigating && map_visible) map_hud_click(x,y,fly_to_node);
}

void panel_map_scroll(double x,double y,double delta) {
	if (navigating && map_visible) map_hud_zoom(x,y,delta,map_animation);
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
	map_hud_open_effect(map_animation);
	map_pointer_zoom_enabled(TRUE,map_hud_outer_margin());
	gtk_widget_set_visible(normal, FALSE);
	gtk_widget_set_visible(overview_form, FALSE);
	gtk_widget_set_visible(status, FALSE);
	gtk_widget_set_visible(map_navigation_form, TRUE);
	gtk_widget_grab_focus(map_navigation_form);
	schedule_region();
}

static gboolean navigation_focus_timeout(gpointer unused) {
	(void)unused;
	navigation_timeout = 0;
	map_action_wait = map_action_sent = FALSE;
	map_pointer_zoom_enabled(FALSE,map_hud_outer_margin());
	return_keys();
	gtk_label_set_text(GTK_LABEL(status), "Keyboard focus unavailable. Click Map View again.");
	return G_SOURCE_REMOVE;
}

static void open_map_view(GtkButton *button, gpointer unused) {
	(void)button; (void)unused;
	if (!enabled || navigation_timeout || navigating) return;
	if (!expanded) set_expanded(TRUE);
	map_action_wait = TRUE;
	navigation_timeout = g_timeout_add(750, navigation_focus_timeout, NULL);
	// Arm before asking for keyboard ownership so WM can retain this
	// already-focused shell across the acknowledged Overview projection.
	map_pointer_zoom_enabled(TRUE,map_hud_outer_margin());
	gtk_layer_set_keyboard_mode(window, KEYS_ON_DEMAND);
	if (gtk_window_is_active(window)) navigation_focused();
}

static void exit_map_view(void) {
	map_hud_dismiss_effect();
	set_expanded(FALSE);
	// Overview already hides app windows; only dismiss its Map overlay.
	if (overview && map_visible) panelAction(14, "", "");
}

static void leave_navigation(GtkButton *button, gpointer unused) {
	(void)button; (void)unused;
	exit_map_view();
}

static void lose_window_focus(GObject *window, GParamSpec *prop, gpointer data) {
	(void)prop; (void)data;
	if (gtk_window_is_active(GTK_WINDOW(window))) navigation_focused();
	else {
		shortcut_held=0;
		if (navigating || (map_action_wait && map_action_sent)) set_expanded(FALSE);
	}
}

static unsigned modal_shortcut(guint key) {
	switch(gdk_keyval_to_lower(key)) {
	case GDK_KEY_m: return 1;
	case GDK_KEY_h: return 2;
	default: return 0;
	}
}

static gboolean editing_input(void) {
	for(GtkWidget *w=gtk_window_get_focus(window);w;w=gtk_widget_get_parent(w))
		if (GTK_IS_EDITABLE(w) || GTK_IS_TEXT_VIEW(w)) return TRUE;
	return FALSE;
}

static gboolean key_pressed(GtkEventControllerKey *keys, guint key, guint code,
		GdkModifierType mods, gpointer data) {
	(void)keys; (void)code; (void)data;
	if (key == GDK_KEY_Escape)
		return navigating && map_hud_close_profile();
	unsigned shortcut=modal_shortcut(key);
	if (shortcut && !(mods & (GDK_SHIFT_MASK|GDK_CONTROL_MASK|GDK_ALT_MASK|GDK_SUPER_MASK|GDK_META_MASK|GDK_HYPER_MASK))
			&& !editing_input() && !map_settings_active) {
		if (shortcut_held & shortcut) return TRUE;
		if (enabled && (shortcut==2 || overview || blank_home)) {
			shortcut_held |= shortcut;
			if (overview && map_visible) exit_map_view();
			else if (shortcut==1) open_map_view(NULL,NULL);
			else if (!overview && !blank_home) {
				set_expanded(FALSE);
				panelAction(14,"overview","");
			}
			return TRUE;
		}
	}
	if (!navigating) return FALSE;
	unsigned dir=direction(key);
	if (!dir || (mods & (GDK_CONTROL_MASK|GDK_ALT_MASK|GDK_SUPER_MASK))) return FALSE;
	if (!(camera_held & dir)) {
		camera_held|=dir;
		directions_changed();
	}
	return TRUE;
}

static void key_released(GtkEventControllerKey *keys,guint key,guint code,
		GdkModifierType mods,gpointer data) {
	(void)keys; (void)code; (void)mods; (void)data;
	shortcut_held&=~modal_shortcut(key);
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
	fly_to_node(g_object_get_data(G_OBJECT(button),"node"));
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
	gtk_widget_set_tooltip_text(status, u->message);
	if (g_strcmp0(last_nodes, u->nodes) != 0) {
		g_free(last_nodes);
		last_nodes = g_strdup(u->nodes);
		build_list(nodes, u->nodes, FALSE);
		build_overview(u->nodes);
	}
	if (g_strcmp0(last_windows, u->windows) != 0) {
		g_free(last_windows);
		last_windows = g_strdup(u->windows);
		build_list(windows, u->windows, TRUE);
	}
	enabled = u->enabled;
	blank_home = u->blank_home;
	g_free(current_node);
	g_free(current_mode);
	current_node = g_strdup(u->node);
	current_mode = g_strdup(u->mode);
	gboolean show_nodes=u->map_visible && u->overview;
	if (overview != (u->overview != 0)) {
		overview = u->overview != 0;
		gtk_widget_set_visible(normal, !map_settings_active && !navigating && !overview);
		gtk_widget_set_visible(overview_form, !map_settings_active && !navigating && overview);
		if (!map_action_wait || !overview) set_expanded(FALSE);
	} else if (map_visible != show_nodes && overview) {
		// Keep the clicked Map View surface until the focus handshake.
		if ((!show_nodes || !navigation_timeout) && !map_action_wait)
			set_expanded(FALSE);
	}
	map_visible = show_nodes;
	if(gtk_window_is_active(window)) navigation_focused();
	if(!map_visible && navigating) stop_navigation();
	map_hud_update(u->nodes, positions, u->node, u->profiles, overview, map_visible);
	refresh_actions();
	if (!showing && enabled) {
		showing = TRUE;
		gtk_window_present(window);
		schedule_region();
	}
	free_update(u);
	return G_SOURCE_REMOVE;
}

void panel_post(const char *view, const char *message, const char *d, const char *w, const char *node, const char *mode, const char *profiles, int can_submit, int overview_on, int map_visible_on, int blank_home_on) {
	struct update *u = g_new(struct update, 1);
	*u = (struct update){ g_strdup(view), g_strdup(message), g_strdup(d), g_strdup(w),
		g_strdup(node), g_strdup(mode), g_strdup(profiles), can_submit, overview_on, map_visible_on, blank_home_on };
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
		if (gtk_check_button_get_active(GTK_CHECK_BUTTON(map_animation_input[i]))) return i;
	return MAP_ANIMATION_LINEAR;
}

static void update_pan_setting(void) {
	int speed=effective_pan_speed();
	char label[12];
	g_snprintf(label,sizeof label,"%d%%",speed);
	gtk_label_set_text(GTK_LABEL(map_pan_label),label);
	gtk_widget_set_sensitive(map_pan_minus,speed>MAP_PAN_MIN);
	gtk_widget_set_sensitive(map_pan_plus,speed<MAP_PAN_MAX);
	gtk_widget_set_sensitive(map_pan_reset,pan_override!=0);
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
	map_hud_show_fps(gtk_check_button_get_active(GTK_CHECK_BUTTON(map_fps_input)));
	const char *color = gtk_editable_get_text(GTK_EDITABLE(map_border_input));
	bool valid = valid_border_color(color);
	gtk_widget_set_sensitive(map_settings_save, valid);
	gtk_widget_set_tooltip_text(map_settings_note, NULL);
	gtk_label_set_text(GTK_LABEL(map_settings_note),
		valid ? "Changes preview on the map. Save to keep them."
		      : "Enter #RRGGBB or #RRGGBBAA.");
	map_hud_set_spacing(valid ? color : saved_map_border,
		gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(map_outer_input)),
		gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(map_inner_input)),
		gtk_check_button_get_active(GTK_CHECK_BUTTON(map_frosted_input)));
}

void panel_set_map_settings(const char *border, int outer, int inner, int frosted, int animation, int show_fps, int theme_pan, int requested_pan_override, int max_outer, int max_inner) {
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
	gtk_check_button_set_active(GTK_CHECK_BUTTON(saved_map_frosted ? map_frosted_input : map_clear_input), TRUE);
	gtk_check_button_set_active(GTK_CHECK_BUTTON(map_animation_input[saved_map_animation]), TRUE);
	gtk_check_button_set_active(GTK_CHECK_BUTTON(map_fps_input),saved_map_fps);
	pan_override=saved_pan_override;
	update_pan_setting();
	settings_populating = FALSE;
	gtk_widget_set_visible(normal, FALSE);
	gtk_widget_set_visible(overview_form, FALSE);
	gtk_widget_set_visible(status, FALSE);
	gtk_widget_add_css_class(form, "mothership-settings-surface");
	gtk_widget_set_visible(map_settings_form, TRUE);
	preview_map_settings(NULL, NULL);
	schedule_region();
}

static void cancel_map_settings(GtkButton *button, gpointer data) {
	(void)button; (void)data;
	close_map_settings();
	return_keys();
	schedule_region();
}

static void save_map_settings(GtkButton *button, gpointer data) {
	(void)button; (void)data;
	const char *color=saved_map_border;
	int outer=saved_map_outer, inner=saved_map_inner;
	gboolean frosted=saved_map_frosted;
	color=gtk_editable_get_text(GTK_EDITABLE(map_border_input));
	if (!valid_border_color(color)) return;
	gtk_spin_button_update(GTK_SPIN_BUTTON(map_outer_input));
	gtk_spin_button_update(GTK_SPIN_BUTTON(map_inner_input));
	outer=gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(map_outer_input));
	inner=gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(map_inner_input));
	frosted=gtk_check_button_get_active(GTK_CHECK_BUTTON(map_frosted_input));
	enum map_animation animation=selected_map_animation();
	gboolean show_fps=gtk_check_button_get_active(GTK_CHECK_BUTTON(map_fps_input));
	char *error=panelSaveMapSettings((char *)color,outer,inner,frosted,animation,show_fps,pan_override);
	if (error != NULL) {
		gtk_label_set_text(GTK_LABEL(map_settings_note), error);
		gtk_widget_set_tooltip_text(map_settings_note, error);
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
	close_map_settings();
	return_keys();
	schedule_region();
}

static void settings_insert(GtkGrid *grid, int row, const char *label, GtkWidget *entry) {
	GtkWidget *name = gtk_label_new_with_mnemonic(label);
	gtk_label_set_mnemonic_widget(GTK_LABEL(name), entry);
	gtk_widget_add_css_class(name, "mothership-settings-label");
	gtk_widget_set_halign(name, GTK_ALIGN_START);
	gtk_grid_attach(grid, name, 0, row, 1, 1);
	gtk_widget_set_hexpand(entry, TRUE);
	gtk_grid_attach(grid, entry, 1, row, 1, 1);
	GtkGesture *gesture = gtk_gesture_click_new();
	gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(gesture), GTK_PHASE_CAPTURE);
	g_signal_connect(gesture, "pressed", G_CALLBACK(open_input), NULL);
	gtk_widget_add_controller(entry, GTK_EVENT_CONTROLLER(gesture));
}

static void add_settings_button(GtkBox *bar) {
	GtkWidget *control = gtk_button_new_with_label("Map settings");
	g_signal_connect(control, "clicked", G_CALLBACK(open_map_settings), NULL);
	gtk_box_append(bar, control);
}

static void init_map_settings(void) {
	map_settings_form = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
	gtk_widget_set_visible(map_settings_form, FALSE);
	gtk_box_append(GTK_BOX(form), map_settings_form);
	GtkWidget *heading = gtk_label_new("Center Map Settings");
	gtk_widget_add_css_class(heading, "mothership-settings-label");
	gtk_widget_add_css_class(heading, "heading");
	gtk_widget_set_halign(heading, GTK_ALIGN_START);
	gtk_box_append(GTK_BOX(map_settings_form), heading);
	GtkGrid *grid = GTK_GRID(gtk_grid_new());
	gtk_grid_set_column_spacing(grid, 16);
	gtk_grid_set_row_spacing(grid, 6);
	gtk_box_append(GTK_BOX(map_settings_form), GTK_WIDGET(grid));
	map_border_input = gtk_entry_new();
	gtk_entry_set_max_length(GTK_ENTRY(map_border_input), 9);
	gtk_entry_set_placeholder_text(GTK_ENTRY(map_border_input), "#6F91AC9F");
	map_outer_input = gtk_spin_button_new_with_range(0, 1, 1);
	map_inner_input = gtk_spin_button_new_with_range(0, 1, 1);
	gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(map_outer_input), TRUE);
	gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(map_inner_input), TRUE);
	settings_insert(grid, 0, "Border _color (RGBA hex)", map_border_input);
	settings_insert(grid, 1, "_Outer margin (px)", map_outer_input);
	settings_insert(grid, 2, "_Inner padding (px)", map_inner_input);
	GtkWidget *choices = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
	map_clear_input = gtk_check_button_new_with_label("Clear");
	map_frosted_input = gtk_check_button_new_with_label("Frosted");
	gtk_widget_add_css_class(map_clear_input, "mothership-settings-label");
	gtk_widget_add_css_class(map_frosted_input, "mothership-settings-label");
	gtk_check_button_set_group(GTK_CHECK_BUTTON(map_frosted_input), GTK_CHECK_BUTTON(map_clear_input));
	gtk_check_button_set_active(GTK_CHECK_BUTTON(map_clear_input), TRUE);
	gtk_box_append(GTK_BOX(choices), map_clear_input);
	gtk_box_append(GTK_BOX(choices), map_frosted_input);
	settings_insert(grid, 3, "Surface", choices);
	g_signal_connect(map_frosted_input, "toggled", G_CALLBACK(preview_map_settings), NULL);
	g_signal_connect(map_border_input, "changed", G_CALLBACK(preview_map_settings), NULL);
	g_signal_connect(map_outer_input, "value-changed", G_CALLBACK(preview_map_settings), NULL);
	g_signal_connect(map_inner_input, "value-changed", G_CALLBACK(preview_map_settings), NULL);
	GtkWidget *animations=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,12);
	const char *labels[]={"Off","Linear","Ease in/out"};
	for (int i=0;i<MAP_ANIMATION_COUNT;i++) {
		map_animation_input[i]=gtk_check_button_new_with_label(labels[i]);
		gtk_widget_add_css_class(map_animation_input[i],"mothership-settings-label");
		if(i) gtk_check_button_set_group(GTK_CHECK_BUTTON(map_animation_input[i]),
			GTK_CHECK_BUTTON(map_animation_input[0]));
		gtk_box_append(GTK_BOX(animations),map_animation_input[i]);
		g_signal_connect(map_animation_input[i],"toggled",G_CALLBACK(preview_map_settings),NULL);
	}
	settings_insert(grid,4,"Camera animation",animations);
	GtkWidget *speed=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);
	GtkWidget *stepper=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0);
	gtk_widget_add_css_class(stepper,"linked");
	map_pan_minus=gtk_button_new_with_label("−");
	gtk_widget_set_tooltip_text(map_pan_minus,"Decrease WASD speed by 10 percentage points");
	gtk_accessible_update_property(GTK_ACCESSIBLE(map_pan_minus),GTK_ACCESSIBLE_PROPERTY_LABEL,
		"Decrease WASD speed",-1);
	map_pan_label=gtk_label_new("100%");
	gtk_label_set_width_chars(GTK_LABEL(map_pan_label),5);
	gtk_widget_add_css_class(map_pan_label,"mothership-settings-label");
	map_pan_plus=gtk_button_new_with_label("+");
	gtk_widget_set_tooltip_text(map_pan_plus,"Increase WASD speed by 10 percentage points");
	gtk_accessible_update_property(GTK_ACCESSIBLE(map_pan_plus),GTK_ACCESSIBLE_PROPERTY_LABEL,
		"Increase WASD speed",-1);
	map_pan_reset=gtk_button_new_with_label("Reset");
	gtk_widget_add_css_class(map_pan_reset,"flat");
	gtk_widget_set_tooltip_text(map_pan_reset,"Use the default speed from the Map theme");
	gtk_box_append(GTK_BOX(stepper),map_pan_minus);
	gtk_box_append(GTK_BOX(stepper),map_pan_label);
	gtk_box_append(GTK_BOX(stepper),map_pan_plus);
	gtk_box_append(GTK_BOX(speed),stepper);
	gtk_box_append(GTK_BOX(speed),map_pan_reset);
	settings_insert(grid,5,"WASD speed",speed);
	g_signal_connect(map_pan_minus,"clicked",G_CALLBACK(change_pan_speed),GINT_TO_POINTER(-MAP_PAN_STEP));
	g_signal_connect(map_pan_plus,"clicked",G_CALLBACK(change_pan_speed),GINT_TO_POINTER(MAP_PAN_STEP));
	g_signal_connect(map_pan_reset,"clicked",G_CALLBACK(change_pan_speed),NULL);
	map_fps_input=gtk_check_button_new_with_label("Show FPS");
	gtk_widget_set_tooltip_text(map_fps_input,
		"Counts motion-layer renders, not display presentation.");
	gtk_widget_add_css_class(map_fps_input,"mothership-settings-label");
	settings_insert(grid,6,"Debug",map_fps_input);
	g_signal_connect(map_fps_input,"toggled",G_CALLBACK(preview_map_settings),NULL);
	map_settings_note = gtk_label_new("");
	gtk_widget_add_css_class(map_settings_note, "mothership-settings-label");
	gtk_label_set_xalign(GTK_LABEL(map_settings_note), 0);
	gtk_label_set_wrap(GTK_LABEL(map_settings_note), TRUE);
	gtk_label_set_ellipsize(GTK_LABEL(map_settings_note), PANGO_ELLIPSIZE_END);
	gtk_label_set_lines(GTK_LABEL(map_settings_note), 2);
	gtk_box_append(GTK_BOX(map_settings_form), map_settings_note);
	GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_widget_set_halign(actions, GTK_ALIGN_END);
	gtk_box_append(GTK_BOX(map_settings_form), actions);
	GtkWidget *cancel = gtk_button_new_with_label("Cancel");
	g_signal_connect(cancel, "clicked", G_CALLBACK(cancel_map_settings), NULL);
	gtk_box_append(GTK_BOX(actions), cancel);
	map_settings_save = gtk_button_new_with_label("Save");
	gtk_widget_add_css_class(map_settings_save, "suggested-action");
	g_signal_connect(map_settings_save, "clicked", G_CALLBACK(save_map_settings), NULL);
	gtk_box_append(GTK_BOX(actions), map_settings_save);
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

int panel_init(void) {
	if (!gtk_init_check() || gtk_layer_get_protocol_version() < 4) {
		return 0;
	}
	window = GTK_WINDOW(gtk_window_new());
	gtk_window_set_title(window, "Mothership Command Center");
	gtk_window_set_default_size(window, 520, 306);
	gtk_window_set_resizable(window, FALSE);
	gtk_window_set_decorated(window, FALSE);
	gtk_layer_init_for_window(window);
	gtk_layer_set_namespace(window, "mothership-command-center");
	gtk_layer_set_anchor(window, RIGHT_EDGE, TRUE);
	gtk_layer_set_anchor(window, BOTTOM_EDGE, TRUE);
	gtk_layer_set_margin(window, RIGHT_EDGE, 12);
	gtk_layer_set_margin(window, BOTTOM_EDGE, 12);
	gtk_layer_set_keyboard_mode(window, KEYS_NONE);

	GtkCssProvider *skin = gtk_css_provider_new();
	gtk_css_provider_load_from_string(skin,
		"window.mothership-shell { background: transparent; }"
		".mothership-controls { background: @window_bg_color; border-radius: 10px; padding: 8px; }"
		".mothership-controls.mothership-settings-surface { background-color: #172435; }"
		".mothership-settings-label { color: #EDF5FB; }"
		".mothership-render-fps { color: #EDF5FB; background: #172435; "
		"padding: 4px 8px; font-family: monospace; font-size: 11px; }");
	gtk_style_context_add_provider_for_display(gdk_display_get_default(),
		GTK_STYLE_PROVIDER(skin), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref(skin);
	gtk_widget_add_css_class(GTK_WIDGET(window), "mothership-shell");
	map_hud_init();
	map_pointer_init(window);
	GtkWidget *overlay = gtk_overlay_new();
	gtk_window_set_child(window, overlay);
	form = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	gtk_widget_set_focusable(form, TRUE);
	gtk_widget_add_css_class(form, "mothership-controls");
	gtk_widget_set_margin_start(form, 12);
	gtk_widget_set_margin_end(form, 12);
	gtk_widget_set_margin_top(form, 8);
	gtk_widget_set_margin_bottom(form, 8);
	gtk_overlay_set_child(GTK_OVERLAY(overlay), form);
	entry_button = gtk_button_new_with_label("Command Center");
	gtk_widget_set_halign(entry_button, GTK_ALIGN_END);
	gtk_widget_set_valign(entry_button, GTK_ALIGN_END);
	g_signal_connect(entry_button, "clicked", G_CALLBACK(toggle_center), GINT_TO_POINTER(TRUE));
	gtk_overlay_add_overlay(GTK_OVERLAY(overlay), entry_button);
	normal = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	gtk_box_append(GTK_BOX(form), normal);
	GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_box_append(GTK_BOX(normal), bar);
	GtkWidget *heading = gtk_label_new("Command Center");
	gtk_widget_add_css_class(heading, "heading");
	gtk_box_append(GTK_BOX(bar), heading);
	title = gtk_label_new("Connecting…");
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
	fly = button("Fly to Node", 3, actions);
	home = button("Home", 4, actions);
	terminal = button("Terminal", 5, actions);
	files = button("File Manager", 6, actions);
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
	gtk_widget_set_hexpand(overview_title, TRUE);
	gtk_label_set_xalign(GTK_LABEL(overview_title), 0);
	gtk_box_append(GTK_BOX(overview_bar), overview_title);
	add_settings_button(GTK_BOX(overview_bar));
	overview_home = button("Home / Inbox", 4, overview_bar);
	map_view_open=gtk_button_new_with_label("Map View");
	g_signal_connect(map_view_open,"clicked",G_CALLBACK(open_map_view),NULL);
	gtk_box_append(GTK_BOX(overview_bar),map_view_open);
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
	gtk_label_set_xalign(GTK_LABEL(status), 0);
	gtk_label_set_ellipsize(GTK_LABEL(status), PANGO_ELLIPSIZE_END);
	gtk_box_append(GTK_BOX(form), status);
	init_map_settings();
	map_navigation_form=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,12);
	gtk_widget_set_focusable(map_navigation_form, TRUE);
	gtk_widget_set_visible(map_navigation_form,FALSE);
	gtk_box_append(GTK_BOX(form),map_navigation_form);
	GtkWidget *hint=gtk_label_new("WASD · Scroll to zoom · M close · H Home");
	gtk_box_append(GTK_BOX(map_navigation_form),hint);
	navigation_done=gtk_button_new_with_label("Done");
	g_signal_connect(navigation_done,"clicked",G_CALLBACK(leave_navigation),NULL);
	gtk_box_append(GTK_BOX(map_navigation_form),navigation_done);
	g_signal_connect(window,"notify::is-active",G_CALLBACK(lose_window_focus),NULL);
	GtkEventController *keys = gtk_event_controller_key_new();
	g_signal_connect(keys, "key-pressed", G_CALLBACK(key_pressed), NULL);
	g_signal_connect(keys, "key-released", G_CALLBACK(key_released), NULL);
	gtk_widget_add_controller(GTK_WIDGET(window), keys);
	g_signal_connect(window, "close-request", G_CALLBACK(close_window), NULL);
	refresh_actions();
	set_expanded(FALSE);
	return 1;
}

void panel_run(void) {
	loop = g_main_loop_new(NULL, FALSE);
	g_main_loop_run(loop);
	g_main_loop_unref(loop);
}

static gboolean quit_loop(gpointer data) {
	g_main_loop_quit(loop);
	return G_SOURCE_REMOVE;
}

void panel_quit(void) {
	g_idle_add(quit_loop, NULL);
}
