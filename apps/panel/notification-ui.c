//go:build linux && cgo && panel

#include "notification-ui.h"
#include "home-panel.h"
#include "analog-glass.h"
#include "analog-button.h"
#include "map-pointer.h"
#include "map-guide.h"

extern void panelMarkNotificationsRead(char *);
extern void panel_redraw_node_bell(void);
extern void panelRefreshNotificationLog(void);
extern int panelClearNotificationLog(char *);
extern void panelMarkNotificationRead(guint);
extern int panelDeleteNotification(guint);

static GtkWidget *log_pane,*toast,*dismiss_toast;
static GtkWidget *group_list,*combo,*log_actions,*last_group,*empty,*mark_all;
static GtkWidget *clear_log,*clear_status;
static GtkWidget *log_keys;
static GtkWidget *toast_source,*toast_title,*toast_body;
static char *toast_node;
static void (*on_region)(void);
static guint toast_timer;
static gboolean repopulating;
static GtkStringList *filter_names;
static GPtrArray *filter_ids;
static GHashTable *source_counts;
static int unread_total;
static char *locked_source,*locked_label;
static gboolean opening_log;
static GPtrArray *rows;
static GHashTable *expanded_rows;
static guint restore_id,next_focus_id;
static gboolean restore_row_focus;
static unsigned held_actions;
enum { TOAST_INSET=24, TOAST_WIDTH=336, TOAST_HEIGHT=104, TOAST_CLOSE_RIGHT=17, TOAST_CLOSE_TOP=14 };

static GtkWidget *label(const char *v,const char *cls) {
	GtkWidget *w=gtk_label_new(v);
	gtk_label_set_xalign(GTK_LABEL(w),0);
	gtk_label_set_ellipsize(GTK_LABEL(w),PANGO_ELLIPSIZE_END);
	if(cls) gtk_widget_add_css_class(w,cls);
	return w;
}
static GtkWidget *toast_line(const char *cls,int lines) {
	GtkWidget *w=label("",cls);
	gtk_label_set_single_line_mode(GTK_LABEL(w),lines==1);
	gtk_label_set_lines(GTK_LABEL(w),lines);
	if(lines>1) {
		gtk_label_set_wrap(GTK_LABEL(w),TRUE);
		gtk_label_set_wrap_mode(GTK_LABEL(w),PANGO_WRAP_WORD_CHAR);
	}
	return w;
}

static void toast_text(GtkWidget *w,const char *value) {
	// Notification senders may include line breaks. The toast is a preview,
	// not a document; the log keeps the original formatting.
	g_autofree char *plain=g_strdup(value);
	g_strdelimit(plain,"\r\n\t\v\f",' ');
	gtk_label_set_text(GTK_LABEL(w),plain);
}

static GtkWidget *overlay_content(GtkWidget **content) {
	GtkWidget *w=gtk_overlay_new();
	gtk_overlay_set_child(GTK_OVERLAY(w),analog_glass_new());
	*content=gtk_box_new(GTK_ORIENTATION_VERTICAL,7);
	gtk_widget_add_css_class(*content,"notification-content");
	gtk_overlay_add_overlay(GTK_OVERLAY(w),*content);
	gtk_overlay_set_measure_overlay(GTK_OVERLAY(w),*content,TRUE);
	gtk_widget_set_visible(w,FALSE);
	return w;
}
static GtkWidget *cross(void) {
	GtkWidget *b=gtk_button_new_from_icon_name("window-close-symbolic");
	analog_button_style(b,8,5);
	gtk_accessible_update_property(GTK_ACCESSIBLE(b),GTK_ACCESSIBLE_PROPERTY_LABEL,"Dismiss",-1);
	gtk_widget_set_focus_on_click(b,FALSE);
	return b;
}
static void close_log_clicked(GtkButton *b,gpointer unused) {(void)b;(void)unused;notice_log_close();}
static void close_toast_clicked(GtkButton *b,gpointer unused) {(void)b;(void)unused;notice_toast_close();}
static const char *selected_scope(void) {
	if(locked_source) return locked_source;
	guint index=gtk_drop_down_get_selected(GTK_DROP_DOWN(combo));
	if(index>=filter_ids->len) return NULL;
	return index?g_ptr_array_index(filter_ids,index):"";
}
static void mark_read(GtkButton *b,gpointer unused) {
	(void)b;(void)unused;
	g_autofree char *source=g_strdup(selected_scope());
	if(source) panelMarkNotificationsRead(source);
}
static void log_guide(GtkDrawingArea *area,cairo_t *cr,int width,int height,gpointer data) {
	(void)area;(void)height;(void)data;notification_log_guide_draw(cr,width);
}

static void hide_log_error(void) {
	gtk_widget_set_visible(clear_status,FALSE);
}

static void clear_selected(GtkButton *b,gpointer unused) {
	(void)b;(void)unused;
	g_autofree char *source=g_strdup(selected_scope());
	if(!source || !gtk_widget_get_sensitive(clear_log))return;
	hide_log_error();
	if(!panelClearNotificationLog(source)) {
		gtk_label_set_text(GTK_LABEL(clear_status),"Could not clear notifications. History was kept.");
		gtk_widget_set_visible(clear_status,TRUE);
	}
}

static guint row_id(GtkWidget *row) {
	return row?GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(row),"notice-id")):0;
}

static GtkWidget *focused_row(void) {
	GtkRoot *root=gtk_widget_get_root(log_pane);
	GtkWidget *focus=root?gtk_root_get_focus(root):NULL;
	return row_id(focus)?focus:NULL;
}

static GtkWidget *visible_row(guint id) {
	for(guint i=0;i<rows->len;i++) {
		GtkWidget *row=g_ptr_array_index(rows,i);
		if((!id || row_id(row)==id) && gtk_widget_get_visible(gtk_widget_get_parent(row))) return row;
	}
	return NULL;
}

static GtkWidget *neighbor_row(GtkWidget *row,int step) {
	guint i;
	if(!g_ptr_array_find(rows,row,&i)) return NULL;
	for(int n=(int)i+step;n>=0 && n<(int)rows->len;n+=step) {
		GtkWidget *candidate=g_ptr_array_index(rows,n);
		if(gtk_widget_get_visible(gtk_widget_get_parent(candidate))) return candidate;
	}
	return NULL;
}

static void focus_row(GtkWidget *row) {gtk_widget_grab_focus(row?row:log_pane);}

static void row_focus_changed(GtkWidget *row,GParamSpec *prop,gpointer unused) {
	(void)prop;(void)unused;
	analog_item_select(row,gtk_widget_has_focus(row));
}

static void row_clicked(GtkGestureClick *click,int count,double x,double y,gpointer unused) {
	(void)count;(void)x;(void)y;(void)unused;
	focus_row(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(click)));
}

static void show_description(GtkWidget *row,GtkLabel *body,gboolean expanded) {
	gtk_label_set_lines(body,expanded?-1:3);
	gtk_label_set_ellipsize(body,expanded?PANGO_ELLIPSIZE_NONE:PANGO_ELLIPSIZE_END);
	gtk_accessible_update_state(GTK_ACCESSIBLE(row),GTK_ACCESSIBLE_STATE_EXPANDED,expanded,-1);
}

static unsigned action_key(guint key) {
	switch(key) {
	case GDK_KEY_BackSpace: return 1;
	case GDK_KEY_space: return 2;
	case GDK_KEY_Return: case GDK_KEY_KP_Enter: return 4;
	default: return 0;
	}
}

static gboolean row_key(GtkEventControllerKey *keys,guint key,guint code,GdkModifierType mods,gpointer unused) {
	(void)keys;(void)code;(void)unused;
	const GdkModifierType commands=GDK_CONTROL_MASK|GDK_ALT_MASK|GDK_SUPER_MASK|GDK_META_MASK|GDK_HYPER_MASK;
	unsigned action=action_key(key);
	if(action & held_actions) return TRUE;
	if(mods & commands) return FALSE;
	if(mods & GDK_SHIFT_MASK) {
		if(key!=GDK_KEY_BackSpace && key!=GDK_KEY_Return && key!=GDK_KEY_KP_Enter) return FALSE;
		GtkWidget *button=gtk_widget_get_first_child(combo);
		if(GTK_IS_TOGGLE_BUTTON(button) && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button))) return FALSE;
		held_actions|=action;
		hide_log_error();
		if(key==GDK_KEY_BackSpace) {
			if(!focused_row()) focus_row(NULL);
			clear_selected(NULL,NULL);
		} else mark_read(NULL,NULL);
		return TRUE;
	}
	GtkWidget *row=focused_row();
	if(!row) return FALSE;
	int step=0;
	switch(gdk_keyval_to_lower(key)) {
	case GDK_KEY_w: case GDK_KEY_a: step=-1;break;
	case GDK_KEY_s: case GDK_KEY_d: step=1;break;
	}
	if(step) {
		GtkWidget *next=neighbor_row(row,step);
		if(next) focus_row(next);
		return TRUE;
	}
	if(!action) return FALSE;
	held_actions|=action;
	guint id=row_id(row);
	if(key==GDK_KEY_BackSpace) {
		GtkWidget *next=neighbor_row(row,1);
		if(!next) next=neighbor_row(row,-1);
		next_focus_id=row_id(next);
		hide_log_error();
		if(panelDeleteNotification(id)) g_hash_table_remove(expanded_rows,GUINT_TO_POINTER(id));
		else {
			gtk_label_set_text(GTK_LABEL(clear_status),"Could not delete the notification. History was kept.");
			gtk_widget_set_visible(clear_status,TRUE);
		}
		next_focus_id=0;
	} else if(key==GDK_KEY_space) {
		GtkLabel *body=g_object_get_data(G_OBJECT(row),"notice-description");
		if(body) {
			gboolean expanded=!g_hash_table_remove(expanded_rows,GUINT_TO_POINTER(id));
			if(expanded) g_hash_table_add(expanded_rows,GUINT_TO_POINTER(id));
			show_description(row,body,expanded);
		}
	} else panelMarkNotificationRead(id);
	return TRUE;
}

static void row_key_released(GtkEventControllerKey *keys,guint key,guint code,GdkModifierType mods,gpointer unused) {
	(void)keys;(void)code;(void)mods;(void)unused;
	held_actions&=~action_key(key);
}

static void log_focus_left(GtkEventControllerFocus *focus,gpointer unused) {
	(void)focus;(void)unused;
	held_actions=0;
}

static void filter(GtkDropDown *b,GParamSpec *prop,gpointer unused) {
	(void)prop;(void)unused;
	if(repopulating) return;
	if(prop) hide_log_error();
	(void)b;
	const char *scope=selected_scope();
	const char *key=scope && *scope?scope:NULL;
	gboolean records=FALSE;
	for (GtkWidget *g=gtk_widget_get_first_child(group_list);g;g=gtk_widget_get_next_sibling(g)) {
		const char *id=g_object_get_data(G_OBJECT(g),"source");
		gboolean show=!key || g_strcmp0(key,id)==0;
		gtk_widget_set_visible(g,show);
		records|=show;
	}
	gtk_widget_set_sensitive(clear_log,records);
	gtk_widget_set_sensitive(mark_all,key?notice_source_unread(key)>0:unread_total>0);
	gtk_label_set_text(GTK_LABEL(empty),key?"No notifications for this Node.":"No notifications yet.");
	gtk_widget_set_visible(empty,!records);
	if(prop && notice_log_visible()) focus_row(visible_row(0));
}
void notice_init(GtkOverlay *root,void (*region)(void)) {
	on_region=region;
	GtkCssProvider *skin=gtk_css_provider_new();
	gtk_css_provider_load_from_string(skin,
		".notification-content { padding: 14px 17px; color: #d7e9f3; }"
		".notification-log-content { padding: 18px 21px; }"
		".notification-log-entry { padding: 10px 12px; }"
		".notification-content separator { background: #355660; border: 0; min-height: 1px; }"
		".notification-title { font-weight: bold; font-size: 16px; }"
		".notification-meta { color: #a2bfce; font-size: 12px; }"
		".notification-empty { color: #a2bfce; padding: 14px 12px; }"
		".notification-group { color: #8adee8; font-weight: bold; }"
		".notification-unread { color: #70ddeb; }"
		".notification-log-scroll scrollbar.vertical { min-width: 9px; margin-left: 5px; background: #111f27; border: 0; }"
		".notification-log-scroll scrollbar.vertical trough { min-width: 9px; background: #111f27; border: 0; border-radius: 0; }"
		".notification-log-scroll scrollbar.vertical slider { min-width: 5px; min-height: 24px; margin: 0 2px;"
		" background: #426b78; border: 0; border-radius: 0; box-shadow: none; }"
		".notification-log-scroll scrollbar.vertical:hover slider { background: #6297a5; }"
		".notification-log-scroll scrollbar.vertical slider:active,"
		".notification-log-scroll scrollbar.vertical.dragging slider { background: #85c9d5; }");
	gtk_style_context_add_provider_for_display(gdk_display_get_default(),GTK_STYLE_PROVIDER(skin),GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref(skin);
	GtkWidget *body;
	log_pane=overlay_content(&body);
	gtk_widget_set_focusable(log_pane,TRUE);
	rows=g_ptr_array_new();
	expanded_rows=g_hash_table_new(g_direct_hash,g_direct_equal);
	GtkEventController *keys=gtk_event_controller_key_new();
	gtk_event_controller_set_propagation_phase(keys,GTK_PHASE_CAPTURE);
	g_signal_connect(keys,"key-pressed",G_CALLBACK(row_key),NULL);
	g_signal_connect(keys,"key-released",G_CALLBACK(row_key_released),NULL);
	gtk_widget_add_controller(log_pane,keys);
	GtkEventController *focus=gtk_event_controller_focus_new();
	g_signal_connect(focus,"leave",G_CALLBACK(log_focus_left),NULL);
	gtk_widget_add_controller(log_pane,focus);
	analog_glass_set_scene_backdrop(gtk_overlay_get_child(GTK_OVERLAY(log_pane)),SCENE_GLASS_LOG);
	gtk_widget_add_css_class(body,"notification-log-content");
	gtk_box_set_spacing(GTK_BOX(body),12);
	gtk_widget_set_halign(log_pane,GTK_ALIGN_START);gtk_widget_set_valign(log_pane,GTK_ALIGN_END);
	gtk_overlay_add_overlay(root,log_pane);
	GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);
	gtk_box_append(GTK_BOX(body),row);
	GtkWidget *heading=label("Notification Log","notification-title");
	gtk_widget_add_css_class(heading,"shell-system-title");
	gtk_widget_set_hexpand(heading,TRUE);
	gtk_box_append(GTK_BOX(row),heading);
	GtkWidget *b=cross();
	g_signal_connect(b,"clicked",G_CALLBACK(close_log_clicked),NULL);
	gtk_box_append(GTK_BOX(row),b);
	row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);
	log_actions=row;
	gtk_box_append(GTK_BOX(body),row);
	filter_names=gtk_string_list_new(NULL);
	filter_ids=g_ptr_array_new_with_free_func(g_free);
	combo=gtk_drop_down_new(G_LIST_MODEL(g_object_ref(filter_names)),NULL);
	GtkWidget *filter_button=gtk_widget_get_first_child(combo);
	if(GTK_IS_TOGGLE_BUTTON(filter_button)) analog_button_style(filter_button,8,5);
	gtk_widget_set_hexpand(combo,TRUE);gtk_widget_set_halign(combo,GTK_ALIGN_START);
	gtk_accessible_update_property(GTK_ACCESSIBLE(combo),GTK_ACCESSIBLE_PROPERTY_LABEL,"Filter by Node",-1);
	g_signal_connect(combo,"notify::selected",G_CALLBACK(filter),NULL);
	gtk_box_append(GTK_BOX(row),combo);
	mark_all=gtk_button_new_with_label("Mark all read");
	analog_button_style(mark_all,10,6);
	g_signal_connect(mark_all,"clicked",G_CALLBACK(mark_read),NULL);
	gtk_box_append(GTK_BOX(row),mark_all);
	clear_log=gtk_button_new_with_label("Clear");
	analog_button_style(clear_log,10,6);
	g_signal_connect(clear_log,"clicked",G_CALLBACK(clear_selected),NULL);
	gtk_box_append(GTK_BOX(row),clear_log);
	clear_status=label("","notification-meta");
	gtk_box_append(GTK_BOX(body),clear_status);
	hide_log_error();
	GtkWidget *scroll=gtk_scrolled_window_new();
	gtk_widget_add_css_class(scroll,"notification-log-scroll");
	gtk_scrolled_window_set_overlay_scrolling(GTK_SCROLLED_WINDOW(scroll),FALSE);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),GTK_POLICY_NEVER,GTK_POLICY_AUTOMATIC);
	gtk_widget_set_vexpand(scroll,TRUE);
	gtk_box_append(GTK_BOX(body),scroll);
	GtkWidget *list=gtk_box_new(GTK_ORIENTATION_VERTICAL,10);
	GtkWidget *viewport=gtk_viewport_new(NULL,NULL);
	gtk_viewport_set_scroll_to_focus(GTK_VIEWPORT(viewport),TRUE);
	gtk_viewport_set_child(GTK_VIEWPORT(viewport),list);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll),viewport);
	group_list=gtk_box_new(GTK_ORIENTATION_VERTICAL,16);
	gtk_box_append(GTK_BOX(list),group_list);
	empty=label("No notifications yet.","notification-empty");gtk_box_append(GTK_BOX(list),empty);
	log_keys=gtk_drawing_area_new();
	gtk_widget_set_can_target(log_keys,FALSE);
	gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(log_keys),40);
	gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(log_keys),log_guide,NULL,NULL);
	gtk_box_append(GTK_BOX(body),log_keys);
	gtk_accessible_update_property(GTK_ACCESSIBLE(log_pane),GTK_ACCESSIBLE_PROPERTY_DESCRIPTION,
		"W or A moves to the previous record. S or D moves to the next record. "
		"Backspace deletes the focused record. Space expands or collapses its description. "
		"Enter marks it as read. Shift plus Enter marks all in this view as read. "
		"Shift plus Backspace deletes all in this view. Q, N or Escape closes the notification log.",-1);
	notice_log_begin();notice_log_end();

	toast=overlay_content(&body);
	gtk_widget_add_css_class(toast,"notification-toast");
	gtk_overlay_set_measure_overlay(GTK_OVERLAY(toast),body,FALSE);
	gtk_widget_set_overflow(toast,GTK_OVERFLOW_HIDDEN);
	gtk_widget_set_size_request(toast,TOAST_WIDTH,TOAST_HEIGHT);
	gtk_widget_set_halign(toast,GTK_ALIGN_END);gtk_widget_set_valign(toast,GTK_ALIGN_START);
	gtk_widget_set_margin_top(toast,TOAST_INSET);gtk_widget_set_margin_end(toast,TOAST_INSET);
	gtk_widget_set_can_target(toast,FALSE);
	gtk_overlay_add_overlay(root,toast);
	row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,9);
	gtk_box_append(GTK_BOX(body),row);
	GtkWidget *icon=gtk_image_new_from_icon_name("preferences-system-notifications-symbolic");
	gtk_image_set_pixel_size(GTK_IMAGE(icon),21);
	gtk_box_append(GTK_BOX(row),icon);
	GtkWidget *copy=gtk_box_new(GTK_ORIENTATION_VERTICAL,3);
	gtk_widget_set_hexpand(copy,TRUE);gtk_widget_set_can_target(copy,FALSE);
	gtk_widget_set_margin_end(copy,43);
	gtk_box_append(GTK_BOX(row),copy);
	toast_source=toast_line("notification-meta",1);gtk_box_append(GTK_BOX(copy),toast_source);
	toast_title=toast_line("notification-title",1);gtk_box_append(GTK_BOX(copy),toast_title);
	toast_body=toast_line("notification-meta",2);gtk_box_append(GTK_BOX(copy),toast_body);
	dismiss_toast=cross();
	gtk_widget_add_css_class(dismiss_toast,"notification-toast-dismiss");
	gtk_widget_set_focusable(dismiss_toast,FALSE);
	gtk_widget_set_halign(dismiss_toast,GTK_ALIGN_END);
	gtk_widget_set_valign(dismiss_toast,GTK_ALIGN_START);
	gtk_widget_set_margin_end(dismiss_toast,TOAST_INSET+TOAST_CLOSE_RIGHT);
	gtk_widget_set_margin_top(dismiss_toast,TOAST_INSET+TOAST_CLOSE_TOP);
	g_signal_connect(dismiss_toast,"clicked",G_CALLBACK(close_toast_clicked),NULL);
	// The visual toast is pass-through. GTK must be able to pick the
	// dismiss button independently, not through the non-targetable toast.
	gtk_overlay_add_overlay(root,dismiss_toast);
	gtk_widget_set_visible(dismiss_toast,FALSE);
}
GtkWidget *notice_log_widget(void) { return log_pane; }
int notice_log_visible(void) { return log_pane && gtk_widget_get_visible(log_pane); }
void notice_unread_count(int count) { home_dock_notice_count(count); }
GtkWidget *notice_close_target(void) { return dismiss_toast && gtk_widget_get_visible(dismiss_toast)?dismiss_toast:NULL; }
void notice_layout(int w,int h,int bell,int gap) {
	if(!log_pane) return;
	int width=MIN(510,MAX(1,w-48));
	gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(log_keys),width-42<272?60:40);
	gtk_widget_set_size_request(log_pane,width,CLAMP(h-gap-24,100,448));
	gtk_widget_set_margin_start(log_pane,CLAMP(bell-width/2,24,MAX(24,w-24-width)));
	gtk_widget_set_margin_bottom(log_pane,gap);
	gtk_widget_set_size_request(toast,MIN(TOAST_WIDTH,MAX(1,w-2*TOAST_INSET)),TOAST_HEIGHT);
}
void notice_log_toggle_for(const char *source,const char *name) {
	if(notice_log_close()) return;
	// A Node log is already scoped to its origin. Only Home offers
	// scope selection; the shared model still drives read and clear.
	const gboolean scoped=source && *source;
	if(scoped) {
		locked_source=g_strdup(source);
		locked_label=g_strdup(name);
	}
	gtk_widget_set_visible(combo,!scoped);
	gtk_widget_set_halign(log_actions,scoped?GTK_ALIGN_END:GTK_ALIGN_FILL);
	gtk_widget_set_visible(log_pane,TRUE);
	opening_log=TRUE;
	panelRefreshNotificationLog();
	opening_log=FALSE;
	home_dock_notice_open(TRUE);on_region();
}
void notice_log_toggle(void) {notice_log_toggle_for(NULL,NULL);}
int notice_source_unread(const char *source) {
	return source_counts && source?GPOINTER_TO_INT(g_hash_table_lookup(source_counts,source)):0;
}
void notice_set_source_count(const char *source,int count) {
	if(!source_counts)source_counts=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);
	if(count) g_hash_table_replace(source_counts,g_strdup(source),GINT_TO_POINTER(count));
	else g_hash_table_remove(source_counts,source);
	if(notice_log_visible())filter(GTK_DROP_DOWN(combo),NULL,NULL);
	panel_redraw_node_bell();
}
gboolean notice_log_close(void) {
	if (!log_pane || !gtk_widget_get_visible(log_pane)) return FALSE;
	hide_log_error();
	g_hash_table_remove_all(expanded_rows);
	g_clear_pointer(&locked_source,g_free);
	g_clear_pointer(&locked_label,g_free);
	held_actions=0;
	gtk_widget_set_visible(log_pane,FALSE);home_dock_notice_open(FALSE);on_region();return TRUE;
}

static char *selected,*selected_name;
void notice_log_begin(void) {
	GtkWidget *row=focused_row();
	restore_row_focus=opening_log || row || gtk_widget_has_focus(log_pane);
	restore_id=next_focus_id?next_focus_id:row_id(row);
	if(row) focus_row(NULL);
	g_ptr_array_set_size(rows,0);
	g_clear_pointer(&selected,g_free);
	g_clear_pointer(&selected_name,g_free);
	guint index=gtk_drop_down_get_selected(GTK_DROP_DOWN(combo));
	if(locked_source) {
		selected=g_strdup(locked_source);
		selected_name=g_strdup(locked_label);
	} else if(!opening_log && index>0 && index<filter_ids->len) {
		selected=g_strdup(g_ptr_array_index(filter_ids,index));
		selected_name=g_strdup(gtk_string_list_get_string(filter_names,index));
	}
	repopulating=TRUE;
	gtk_string_list_splice(filter_names,0,filter_ids->len,NULL);
	g_ptr_array_set_size(filter_ids,0);
	gtk_string_list_append(filter_names,"All");
	g_ptr_array_add(filter_ids,g_strdup("@all"));
	GtkWidget *child;
	while((child=gtk_widget_get_first_child(group_list))) gtk_box_remove(GTK_BOX(group_list),child);
	last_group=NULL;unread_total=0;
}
void notice_log_group(const char *source,const char *name,int unread) {
	unread_total+=unread;
	gtk_string_list_append(filter_names,name);
	g_ptr_array_add(filter_ids,g_strdup(source));
	GtkWidget *g=gtk_box_new(GTK_ORIENTATION_VERTICAL,6);
	g_object_set_data_full(G_OBJECT(g),"source",g_strdup(source),g_free);
	gtk_box_append(GTK_BOX(group_list),g);
	last_group=g;
	GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,10);
	gtk_box_append(GTK_BOX(g),row);
	gtk_box_append(GTK_BOX(row),label(name,"notification-group"));
	if(unread) {
		char count[12];g_snprintf(count,sizeof count,"%d",unread);
		gtk_box_append(GTK_BOX(row),label(count,"notification-unread"));
	}
	GtkWidget *line=gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_widget_set_hexpand(line,TRUE);gtk_widget_set_valign(line,GTK_ALIGN_CENTER);
	gtk_box_append(GTK_BOX(row),line);
}
static void unread_mark(GtkDrawingArea *area,cairo_t *cr,int w,int h,gpointer data) {
	(void)area;(void)h;
	if(!GPOINTER_TO_INT(data)) return;
	cairo_set_source_rgb(cr,.44,.87,.92);
	cairo_arc(cr,w*.5,9.,2.,0,2*G_PI);
	cairo_fill(cr);
}

static void source_glyph(GtkDrawingArea *area,cairo_t *cr,int w,int h,gpointer unused) {
	(void)area;(void)unused;
	cairo_translate(cr,(w-13)*.5,(h-13)*.5);
	cairo_set_source_rgb(cr,.56,.68,.74);
	cairo_set_line_width(cr,1.15);
	cairo_move_to(cr,.5,.5);cairo_line_to(cr,9.5,.5);cairo_line_to(cr,12.5,3.5);
	cairo_line_to(cr,12.5,12.5);cairo_line_to(cr,.5,12.5);cairo_close_path(cr);
	cairo_move_to(cr,3.5,5);cairo_line_to(cr,9.5,5);
	cairo_move_to(cr,3.5,8.5);cairo_line_to(cr,8,8.5);
	cairo_stroke(cr);
}

void notice_log_row(guint id,const char *app,const char *title,const char *body,const char *stamp,int unread) {
	if(!last_group) return;
	GtkWidget *row=gtk_grid_new();
	gtk_grid_set_column_spacing(GTK_GRID(row),8);
	gtk_grid_set_row_spacing(GTK_GRID(row),3);
	gtk_widget_add_css_class(row,"notification-log-entry");
	GtkWidget *entry=analog_item_new(row);
	analog_item_set_inherit_backdrop(entry);
	gtk_widget_set_focusable(entry,TRUE);
	g_object_set_data(G_OBJECT(entry),"notice-id",GUINT_TO_POINTER(id));
	g_signal_connect(entry,"notify::has-focus",G_CALLBACK(row_focus_changed),NULL);
	GtkGesture *click=gtk_gesture_click_new();
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click),GDK_BUTTON_PRIMARY);
	g_signal_connect(click,"released",G_CALLBACK(row_clicked),NULL);
	gtk_widget_add_controller(entry,GTK_EVENT_CONTROLLER(click));
	gtk_box_append(GTK_BOX(last_group),entry);
	g_ptr_array_add(rows,entry);

	// Keep one read marker at the headline. Old records have an app name,
	// not a stored icon, so keep the shared information glyph.
	GtkWidget *mark=gtk_drawing_area_new();
	gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(mark),unread_mark,GINT_TO_POINTER(unread),NULL);
	gtk_widget_set_can_target(mark,FALSE);
	gtk_widget_set_size_request(mark,8,18);
	gtk_widget_set_valign(mark,GTK_ALIGN_START);
	gtk_grid_attach(GTK_GRID(row),mark,0,0,1,1);
	GtkWidget *heading=label(title,NULL);
	gtk_widget_set_hexpand(heading,TRUE);
	gtk_widget_set_valign(heading,GTK_ALIGN_START);
	gtk_label_set_wrap(GTK_LABEL(heading),TRUE);
	gtk_label_set_wrap_mode(GTK_LABEL(heading),PANGO_WRAP_WORD_CHAR);
	gtk_label_set_max_width_chars(GTK_LABEL(heading),40);
	gtk_label_set_ellipsize(GTK_LABEL(heading),PANGO_ELLIPSIZE_NONE);
	gtk_grid_attach(GTK_GRID(row),heading,1,0,1,1);
	GtkWidget *time=label(stamp,"notification-meta");
	gtk_label_set_ellipsize(GTK_LABEL(time),PANGO_ELLIPSIZE_NONE);
	gtk_label_set_single_line_mode(GTK_LABEL(time),TRUE);
	gtk_widget_set_valign(time,GTK_ALIGN_START);
	gtk_widget_set_margin_top(time,1);
	gtk_grid_attach(GTK_GRID(row),time,2,0,1,1);
	GtkWidget *icon=gtk_drawing_area_new();
	gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(icon),source_glyph,NULL,NULL);
	gtk_widget_set_can_target(icon,FALSE);
	gtk_widget_set_size_request(icon,16,16);
	gtk_widget_set_valign(icon,GTK_ALIGN_START);
	gtk_widget_set_margin_top(icon,1);
	if(*app) gtk_accessible_update_property(GTK_ACCESSIBLE(icon),GTK_ACCESSIBLE_PROPERTY_LABEL,app,-1);
	gtk_grid_attach(GTK_GRID(row),icon,3,0,1,1);
	if(*body) {
		GtkWidget *message=label(body,"notification-meta");
		gtk_label_set_wrap(GTK_LABEL(message),TRUE);
		gtk_label_set_wrap_mode(GTK_LABEL(message),PANGO_WRAP_WORD_CHAR);
		gtk_label_set_max_width_chars(GTK_LABEL(message),62);
		g_object_set_data(G_OBJECT(entry),"notice-description",message);
		show_description(entry,GTK_LABEL(message),g_hash_table_contains(expanded_rows,GUINT_TO_POINTER(id)));
		gtk_grid_attach(GTK_GRID(row),message,1,1,3,1);
	}
}
void notice_log_end(void) {
	guint index=0;
	if(selected) for(guint i=1;i<filter_ids->len;i++)
		if(g_str_equal(selected,g_ptr_array_index(filter_ids,i))) {index=i;break;}
	if(selected && !index) {
		index=filter_ids->len;
		gtk_string_list_append(filter_names,selected_name);
		g_ptr_array_add(filter_ids,g_strdup(selected));
	}
	gtk_drop_down_set_selected(GTK_DROP_DOWN(combo),index);
	g_clear_pointer(&selected,g_free);
	g_clear_pointer(&selected_name,g_free);
	repopulating=FALSE;filter(GTK_DROP_DOWN(combo),NULL,NULL);
	if(restore_row_focus) {
		GtkWidget *row=visible_row(restore_id);
		focus_row(row?row:visible_row(0));
	}
	home_dock_notice_count(unread_total);
}
void notice_toast_close(void) {
	if(!toast || !gtk_widget_get_visible(toast)) return;
	g_clear_handle_id(&toast_timer,g_source_remove);
	gtk_widget_set_visible(dismiss_toast,FALSE);
	gtk_widget_set_visible(toast,FALSE);g_clear_pointer(&toast_node,g_free);on_region();
}
static gboolean toast_expire(gpointer unused) {(void)unused;toast_timer=0;notice_toast_close();return G_SOURCE_REMOVE;}
void notice_context(const char *node,int mode) {
	if(toast_node && (mode==2 || (mode==0 && !g_str_equal(toast_node,node)))) notice_toast_close();
}
void notice_show(const char *node,const char *source,const char *app,const char *title,const char *body) {
	g_free(toast_node);toast_node=g_strdup(node);
	g_autofree char *by=g_strdup_printf("%s · %s",source,app);
	toast_text(toast_source,by);
	toast_text(toast_title,title);
	toast_text(toast_body,body);
	gtk_widget_set_visible(toast_body,*body!=0);
	gtk_widget_set_visible(toast,TRUE);
	gtk_widget_set_visible(dismiss_toast,TRUE);
	g_clear_handle_id(&toast_timer,g_source_remove);
	toast_timer=g_timeout_add_seconds(5,toast_expire,NULL);
	on_region();
}
