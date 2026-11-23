//go:build linux && cgo && panel

#include <gtk/gtk.h>
#include <string.h>
#include "native.h"

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

static GtkWindow *window;
static GtkWidget *title, *status, *entry, *nodes, *windows;
static GtkWidget *create, *move, *fly, *home, *terminal, *files;
static GtkWidget *form, *normal, *overview_form, *entry_button;
static GtkWidget *node_grid, *node_layouts, *layout_buttons[3], *next_stack, *overview_home;
static GHashTable *positions;
static unsigned node_count;
static char *current_node, *current_mode;
static gboolean overview;
static gboolean showing, expanded, region_pending;
static GMainLoop *loop;
static int enabled;
static char *last_nodes, *last_windows;

struct update {
	char *view, *message, *nodes, *windows, *node, *mode;
	int enabled, overview;
};
static struct update *queued;
static GMutex update_lock;
static gboolean update_scheduled;

static const char *selected(GtkWidget *list) {
	GtkListBoxRow *row = gtk_list_box_get_selected_row(GTK_LIST_BOX(list));
	return row == NULL ? NULL : g_object_get_data(G_OBJECT(row), "key");
}

static void refresh_actions(void) {
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
	const char *modes[] = {"stack", "float", "tiling"};
	for (int i = 0; i < 3; ++i) {
		gboolean active = named && strcmp(current_mode, modes[i]) == 0;
		gtk_widget_set_sensitive(layout_buttons[i], enabled && !active);
		if (active) gtk_widget_add_css_class(layout_buttons[i], "suggested-action");
		else gtk_widget_remove_css_class(layout_buttons[i], "suggested-action");
	}
	gtk_widget_set_visible(next_stack, named && g_strcmp0(current_mode, "stack") == 0);
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
	if (expanded) {
		gdk_surface_set_input_region(surface, NULL);
	} else {
		graphene_rect_t bounds;
		if (!gtk_widget_compute_bounds(entry_button, GTK_WIDGET(window), &bounds) ||
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

static void set_expanded(gboolean value) {
	expanded = value;
	if (!value) {
		return_keys();
	}
	gtk_widget_set_visible(form, value);
	gtk_widget_set_visible(entry_button, !value);
	schedule_region();
}

static void toggle_center(GtkButton *button, gpointer data) {
	set_expanded(GPOINTER_TO_INT(data));
}

static void open_input(GtkGestureClick *gesture, int presses, double x, double y, gpointer data) {
	gtk_layer_set_keyboard_mode(window, KEYS_ON_DEMAND);
}

static gboolean key_pressed(GtkEventControllerKey *keys, guint key, guint code,
		GdkModifierType mods, gpointer data) {
	if (key == GDK_KEY_Escape) {
		set_expanded(FALSE);
		return TRUE;
	}
	return FALSE;
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

static void enter_node(GtkButton *button, gpointer data) {
	if (!enabled || !overview) {
		return;
	}
	return_keys();
	char *name = g_object_get_data(G_OBJECT(button), "node");
	panelAction(12, name, "");
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
		g_signal_connect(btn, "clicked", G_CALLBACK(enter_node), NULL);
		gtk_grid_attach(GTK_GRID(node_grid), btn, (at - 1) % 3, (at - 1) / 3, 1, 1);
	}
	g_strfreev(lines);
}

static void free_update(struct update *u) {
	if (u != NULL) {
		g_free(u->view); g_free(u->message); g_free(u->nodes); g_free(u->windows);
		g_free(u->node); g_free(u->mode);
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
	gtk_label_set_text(GTK_LABEL(status), u->message);
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
	g_free(current_node);
	g_free(current_mode);
	current_node = g_strdup(u->node);
	current_mode = g_strdup(u->mode);
	if (overview != (u->overview != 0)) {
		overview = u->overview != 0;
		gtk_widget_set_visible(normal, !overview);
		gtk_widget_set_visible(overview_form, overview);
		set_expanded(overview);
	}
	refresh_actions();
	if (!showing && enabled) {
		showing = TRUE;
		gtk_window_present(window);
		schedule_region();
	}
	free_update(u);
	return G_SOURCE_REMOVE;
}

void panel_post(const char *view, const char *message, const char *d, const char *w, const char *node, const char *mode, int can_submit, int overview_on) {
	struct update *u = g_new(struct update, 1);
	*u = (struct update){ g_strdup(view), g_strdup(message), g_strdup(d), g_strdup(w),
		g_strdup(node), g_strdup(mode), can_submit, overview_on };
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

int panel_init(void) {
	gdk_set_allowed_backends("wayland");
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
		".mothership-controls { background: @window_bg_color; border-radius: 10px; padding: 8px; }");
	gtk_style_context_add_provider_for_display(gdk_display_get_default(),
		GTK_STYLE_PROVIDER(skin), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref(skin);
	gtk_widget_add_css_class(GTK_WIDGET(window), "mothership-shell");
	GtkWidget *overlay = gtk_overlay_new();
	gtk_window_set_child(window, overlay);
	form = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
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
	button("Overview", 7, bar);
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
	layout_buttons[2] = button("Tiling", 10, node_layouts);
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
	overview_home = button("Home / Inbox", 4, overview_bar);
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
	GtkEventController *keys = gtk_event_controller_key_new();
	g_signal_connect(keys, "key-pressed", G_CALLBACK(key_pressed), NULL);
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
