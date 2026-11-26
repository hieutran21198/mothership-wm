//go:build linux && cgo && panel

#include <gio/gdesktopappinfo.h>
#include <gtk/gtk.h>
#include <math.h>
#include "app-grid.h"
#include "analog-glass.h"
#include "analog-button.h"
#include "map-pointer.h"
#include "map-guide.h"

extern char *panelSaveAppPin(char *,int);
extern char *panelLaunchGridApp(GDesktopAppInfo *,char *,char *,int);
extern int panelGridAppSupported(char *,char *,int);
extern void panel_home_notice(const char *);

enum {
	GRID_COLS=5, GRID_ROWS=4, MAX_GRID_TILES=GRID_COLS*GRID_ROWS,
	GRID_WIDTH=784, TILE_HEIGHT=88, MIN_TILE_HEIGHT=70, TILE_GAP=9,
	GRID_PADDING=26, GRID_SPACING=12, PAGER_HEIGHT=32,
	GRID_FIXED_HEIGHT=2*GRID_PADDING+4*GRID_SPACING+39+1+PAGER_HEIGHT+19,
};
enum grid_section { GRID_PINNED, GRID_ALL, GRID_SEARCH };
struct app_tile {
	GtkWidget *wrap,*button,*face,*pin,*label;
	GtkEventController *motion;
	GDesktopAppInfo *info;
	gboolean hover,focussed;
};
static GtkWidget *launcher,*tiles,*search,*full_name,*guide,*previous,*next,*page_label;
static void (*close_grid)(void);
static GPtrArray *catalog,*installed_pins,*results;
static GHashTable *pinned;
static struct app_tile shown[MAX_GRID_TILES];
static enum grid_section section;
static int page,count,cols=GRID_COLS,rows=GRID_ROWS,tile_height=TILE_HEIGHT;
static gboolean resetting, space_held;
static guint search_return_key;
static guint grid_tick;
static gint64 grid_started;
static double grid_from,grid_to;
static gboolean grid_open;

static const char *display_name(GDesktopAppInfo *info) {
	return g_app_info_get_display_name(G_APP_INFO(info));
}
static const char *app_id(GDesktopAppInfo *info) {
	return g_app_info_get_id(G_APP_INFO(info));
}
static gboolean is_pinned(GDesktopAppInfo *info) {
	return pinned && g_hash_table_contains(pinned,app_id(info));
}
static gboolean verified_candidate(GDesktopAppInfo *info) {
	return GPOINTER_TO_INT(g_object_get_data(G_OBJECT(info),"mothership-grid-supported"))!=0;
}

static int compare_apps(gconstpointer a,gconstpointer b) {
	GDesktopAppInfo *l=*(GDesktopAppInfo *const *)a,*r=*(GDesktopAppInfo *const *)b;
	// Put explicitly supported launch paths first, then sort by name and ID.
	gboolean left=verified_candidate(l), right=verified_candidate(r);
	if(left!=right) return left?-1:1;
	int order=g_utf8_collate(display_name(l),display_name(r));
	return order?order:g_strcmp0(app_id(l),app_id(r));
}
GPtrArray *app_catalog_snapshot(void) {
	GPtrArray *items=g_ptr_array_new_with_free_func(g_object_unref);
	GList *installed=g_app_info_get_all();
	for(GList *n=installed;n;n=n->next) {
		GAppInfo *info=n->data;
		if(!G_IS_DESKTOP_APP_INFO(info) || !g_app_info_should_show(info)) continue;
		const char *id=g_app_info_get_id(info),*name=g_app_info_get_display_name(info);
		if(!id || !*id || !name || !*name) continue;
		const char *bin=g_app_info_get_executable(info);
		int supported=panelGridAppSupported((char *)id,(char *)bin,
			g_desktop_app_info_get_boolean(G_DESKTOP_APP_INFO(info),"DBusActivatable"));
		g_object_set_data(G_OBJECT(info),"mothership-grid-supported",GINT_TO_POINTER(supported));
		g_ptr_array_add(items,g_object_ref(info));
	}
	g_list_free_full(installed,g_object_unref);
	g_ptr_array_sort(items,compare_apps);
	return items;
}
static gboolean matches(GDesktopAppInfo *info,const char *query) {
	g_autofree char *name=g_utf8_casefold(display_name(info),-1);
	g_autofree char *id=g_utf8_casefold(app_id(info),-1);
	return strstr(name,query) || strstr(id,query);
}

static void chrome(cairo_t *cr,int w,int h,int cut,double inset) {
	const double c=cut-inset*(2.-G_SQRT2);
	double l=inset,t=inset,r=w-inset,b=h-inset;
	cairo_move_to(cr,l+c,t);cairo_line_to(cr,r-c,t);cairo_line_to(cr,r,t+c);
	cairo_line_to(cr,r,b-c);cairo_line_to(cr,r-c,b);cairo_line_to(cr,l+c,b);
	cairo_line_to(cr,l,b-c);cairo_line_to(cr,l,t+c);cairo_close_path(cr);
}
static void tile_face(GtkDrawingArea *a,cairo_t *cr,int w,int h,gpointer data) {
	(void)a;
	struct app_tile *tile=data;
	gboolean hot=tile->hover || tile->focussed;
	chrome(cr,w,h,8,.5);
	cairo_set_source_rgba(cr,hot?.10:.04,hot?.25:.105,hot?.30:.15,hot?.85:.50);
	cairo_fill_preserve(cr);
	cairo_set_line_width(cr,1);
	cairo_set_source_rgba(cr,hot?.49:.32,hot?.84:.58,hot?.91:.66,hot?.92:.48);
	cairo_stroke(cr);
}
static void fallback_icon(GtkDrawingArea *a,cairo_t *cr,int w,int h,gpointer unused) {
	(void)a;(void)unused;
	cairo_translate(cr,(w-20)*.5,(h-16)*.5);
	cairo_set_source_rgb(cr,.58,.70,.76);cairo_set_line_width(cr,1.3);
	cairo_rectangle(cr,.7,.7,18.6,14.6);
	cairo_move_to(cr,1,5);cairo_line_to(cr,19,5);
	cairo_move_to(cr,4,3);cairo_line_to(cr,5,3);
	cairo_move_to(cr,7,3);cairo_line_to(cr,8,3);
	cairo_stroke(cr);
}
static void search_face(GtkDrawingArea *a,cairo_t *cr,int w,int h,gpointer unused) {
	(void)a;(void)unused;
	chrome(cr,w,h,9,.5);
	cairo_set_source_rgba(cr,.035,.08,.12,.66);
	cairo_fill_preserve(cr);
	cairo_set_line_width(cr,1);
	cairo_set_source_rgba(cr,.37,.67,.74,.66);
	cairo_stroke(cr);
}
static void pin_face(GtkDrawingArea *a,cairo_t *cr,int w,int h,gpointer data) {
	(void)a;
	struct app_tile *tile=data;
	gboolean yes=is_pinned(tile->info);
	cairo_translate(cr,w*.5,h*.5);
	cairo_rotate(cr,.45);
	cairo_set_source_rgb(cr,yes?.76:.64,yes?.97:.81,.92);
	cairo_move_to(cr,-2.9,-3.9);cairo_line_to(cr,2.9,-3.9);
	cairo_line_to(cr,1.2,-2.1);cairo_line_to(cr,1.2,.5);
	cairo_line_to(cr,3,2);cairo_line_to(cr,-3,2);
	cairo_line_to(cr,-1.2,.5);cairo_line_to(cr,-1.2,-2.1);cairo_close_path(cr);
	if(yes) cairo_fill(cr);else cairo_stroke(cr);
	cairo_move_to(cr,0,2);cairo_line_to(cr,0,5.1);cairo_stroke(cr);
}
static void draw_label(struct app_tile *tile) {
	gboolean active=tile->hover || tile->focussed;
	gtk_widget_set_visible(tile->pin,active);
	gtk_widget_queue_draw(tile->face);
	gtk_widget_queue_draw(gtk_button_get_child(GTK_BUTTON(tile->pin)));
	if (active) {
		if(pango_layout_is_ellipsized(gtk_label_get_layout(GTK_LABEL(tile->label)))) {
			gtk_label_set_text(GTK_LABEL(full_name),display_name(tile->info));
			gtk_widget_set_visible(full_name,TRUE);
		} else gtk_widget_set_visible(full_name,FALSE);
	} else if(g_strcmp0(gtk_label_get_text(GTK_LABEL(full_name)),display_name(tile->info))==0) {
		gtk_widget_set_visible(full_name,FALSE);
	}
}
static void tile_enter(GtkEventControllerMotion *m,double x,double y,gpointer data) {
	(void)m;(void)x;(void)y;
	struct app_tile *tile=data;tile->hover=TRUE;draw_label(tile);
}
static void tile_leave(GtkEventControllerMotion *m,gpointer data) {
	(void)m;
	struct app_tile *tile=data;tile->hover=FALSE;draw_label(tile);
}
static void tile_focus(GtkWidget *button,GParamSpec *p,gpointer data) {
	(void)p;
	struct app_tile *tile=data;tile->focussed=gtk_widget_has_focus(button);draw_label(tile);
	gtk_widget_queue_draw(guide);
}
static void pins_changed(const char *focused_id,int slot);
static void clear_tiles(void) {
	for(int i=0;i<count;i++){
		struct app_tile *t=&shown[i];
		g_signal_handlers_disconnect_by_data(t->button,t);
		g_signal_handlers_disconnect_by_data(t->pin,t);
		g_signal_handlers_disconnect_by_data(t->motion,t);
	}
	GtkWidget *child;
	while((child=gtk_widget_get_first_child(tiles))) gtk_grid_remove(GTK_GRID(tiles),child);
	count=0;
}
static void toggle_pin(struct app_tile *tile) {
	int slot=(int)(tile-shown);
	g_autofree char *id=g_strdup(app_id(tile->info));
	gboolean pin=!is_pinned(tile->info);
	char *error=panelSaveAppPin(id,pin);
	if(error){panel_home_notice(error);g_free(error);return;}
	if(pin) g_hash_table_add(pinned,g_strdup(id));
	else g_hash_table_remove(pinned,id);
	pins_changed(id,slot);
}
static void pin_clicked(GtkButton *b,gpointer data) {(void)b;toggle_pin(data);}
static void desktop_launch_pid(GDesktopAppInfo *app,GPid pid,gpointer data) {
	(void)app;
	*(int *)data=(int)pid;
	g_spawn_close_pid(pid);
}

gboolean app_catalog_launch_native(GDesktopAppInfo *app,const char *display,
		const char *bus,const char *token,int *pid,GError **error) {
	// Launch Exec with GIO's desktop-entry argument handling. Do not ask
	// an existing desktop's activation service to open the app for us.
	if(!g_app_info_should_show(G_APP_INFO(app)) ||
			g_desktop_app_info_get_boolean(app,"DBusActivatable") ||
			g_desktop_app_info_get_boolean(app,"Terminal") ||
			!g_app_info_get_executable(G_APP_INFO(app)) ||
			!*g_app_info_get_executable(G_APP_INFO(app))) {
		g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NOT_SUPPORTED,
			"No standalone Wayland launch for this desktop entry.");
		return FALSE;
	}
	g_autoptr(GAppLaunchContext) context=g_app_launch_context_new();
	const char *unset[]={"DISPLAY","WAYLAND_SOCKET","XDG_ACTIVATION_TOKEN",
		"DESKTOP_STARTUP_ID","QT_QPA_PLATFORM","MOTHERSHIP_GRID_LAUNCH",NULL};
	for(int i=0;unset[i];i++) g_app_launch_context_unsetenv(context,unset[i]);
	if(*token)g_app_launch_context_setenv(context,"MOTHERSHIP_GRID_LAUNCH",token);
	g_app_launch_context_setenv(context,"WAYLAND_DISPLAY",display);
	g_app_launch_context_setenv(context,"GDK_BACKEND","wayland");
	g_app_launch_context_setenv(context,"QT_QPA_PLATFORM","wayland");
	g_app_launch_context_setenv(context,"DBUS_SESSION_BUS_ADDRESS",
		*bus ? bus : "unix:path=/dev/null");
	*pid=0;
	return g_desktop_app_info_launch_uris_as_manager(app,NULL,context,
		G_SPAWN_SEARCH_PATH_FROM_ENVP,NULL,NULL,desktop_launch_pid,pid,error);
}

gboolean app_catalog_launch(GDesktopAppInfo *app) {
	GAppInfo *info=G_APP_INFO(app);
	char *error=panelLaunchGridApp(app,(char *)app_id(app),(char *)g_app_info_get_executable(info),
		g_desktop_app_info_get_boolean(app,"DBusActivatable"));
	if(!error) return TRUE;
	g_autofree char *notice=g_strdup_printf("%s: %s",display_name(app),error);
	panel_home_notice(notice);g_free(error);return FALSE;
}
static void launch_tile(struct app_tile *tile) {
	if(app_catalog_launch(tile->info)) close_grid();
}
static void tile_clicked(GtkButton *b,gpointer data) {(void)b;launch_tile(data);}

static void make_tile(int i,GDesktopAppInfo *info) {
	struct app_tile *t=&shown[i];
	*t=(struct app_tile){.info=info};
	t->wrap=gtk_overlay_new();
	gtk_widget_set_size_request(t->wrap,56,tile_height);
	gtk_widget_set_hexpand(t->wrap,TRUE);
	t->button=gtk_button_new();
	gtk_widget_add_css_class(t->button,"app-grid-click");
	gtk_overlay_set_child(GTK_OVERLAY(t->wrap),t->button);
	t->face=gtk_drawing_area_new();
	gtk_widget_set_can_target(t->face,FALSE);
	gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(t->face),tile_face,t,NULL);
	// Face beneath the actual owned label and full-color desktop icon.
	gtk_button_set_child(GTK_BUTTON(t->button),gtk_overlay_new());
	GtkOverlay *front=GTK_OVERLAY(gtk_button_get_child(GTK_BUTTON(t->button)));
	gtk_overlay_set_child(front,t->face);
	GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,7);
	gtk_widget_set_halign(box,GTK_ALIGN_FILL);gtk_widget_set_valign(box,GTK_ALIGN_CENTER);
	gtk_widget_set_can_target(box,FALSE);
	GIcon *icon=g_app_info_get_icon(G_APP_INFO(info));
	GtkIconTheme *theme=gtk_icon_theme_get_for_display(gtk_widget_get_display(launcher));
	if(icon && G_IS_THEMED_ICON(icon) && !gtk_icon_theme_has_gicon(theme,icon)) icon=NULL;
	if(icon && G_IS_FILE_ICON(icon)) {
		GFile *f=g_file_icon_get_file(G_FILE_ICON(icon));
		if(!g_file_is_native(f) || !g_file_query_exists(f,NULL)) icon=NULL;
	}
	GtkWidget *image;
	if(icon) {
		image=gtk_image_new_from_gicon(icon);
		gtk_image_set_pixel_size(GTK_IMAGE(image),28);
	} else {
		image=gtk_drawing_area_new();
		gtk_widget_set_size_request(image,23,20);
		gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(image),fallback_icon,NULL,NULL);
	}
	gtk_widget_set_can_target(image,FALSE);
	gtk_widget_set_halign(image,GTK_ALIGN_CENTER);gtk_widget_set_valign(image,GTK_ALIGN_CENTER);
	GtkWidget *icon_plate=gtk_overlay_new();
	gtk_widget_set_halign(icon_plate,GTK_ALIGN_CENTER);
	gtk_widget_set_size_request(icon_plate,39,39);
	gtk_widget_set_can_target(icon_plate,FALSE);
	gtk_overlay_set_child(GTK_OVERLAY(icon_plate),image);
	gtk_box_append(GTK_BOX(box),icon_plate);
	GtkWidget *label=t->label=gtk_label_new(display_name(info));
	gtk_label_set_ellipsize(GTK_LABEL(label),PANGO_ELLIPSIZE_END);
	gtk_label_set_single_line_mode(GTK_LABEL(label),TRUE);
	gtk_label_set_width_chars(GTK_LABEL(label),1);
	gtk_widget_set_margin_start(label,8);gtk_widget_set_margin_end(label,8);
	gtk_widget_add_css_class(label,"app-grid-label");
	gtk_box_append(GTK_BOX(box),label);
	gtk_overlay_add_overlay(front,box);

	t->pin=gtk_button_new();
	gtk_widget_set_focusable(t->pin,FALSE);gtk_widget_set_focus_on_click(t->pin,FALSE);
	gtk_widget_set_halign(t->pin,GTK_ALIGN_END);gtk_widget_set_valign(t->pin,GTK_ALIGN_START);
	gtk_widget_set_margin_end(t->pin,5);gtk_widget_set_margin_top(t->pin,5);
	gtk_widget_set_size_request(t->pin,22,22);
	GtkWidget *ink=gtk_drawing_area_new();
	gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(ink),pin_face,t,NULL);
	gtk_button_set_child(GTK_BUTTON(t->pin),ink);
	analog_button_style(t->pin,0,0);
	analog_button_on(t->pin,is_pinned(info));
	gtk_accessible_update_property(GTK_ACCESSIBLE(t->pin),GTK_ACCESSIBLE_PROPERTY_LABEL,
		is_pinned(info)?"Unpin app":"Pin app",-1);
	gtk_accessible_update_property(GTK_ACCESSIBLE(t->button),GTK_ACCESSIBLE_PROPERTY_LABEL,display_name(info),-1);
	gtk_overlay_add_overlay(GTK_OVERLAY(t->wrap),t->pin);
	gtk_widget_set_visible(t->pin,FALSE);
	g_signal_connect(t->pin,"clicked",G_CALLBACK(pin_clicked),t);
	g_signal_connect(t->button,"clicked",G_CALLBACK(tile_clicked),t);
	g_signal_connect(t->button,"notify::has-focus",G_CALLBACK(tile_focus),t);
	GtkEventController *motion=t->motion=gtk_event_controller_motion_new();
	g_signal_connect(motion,"enter",G_CALLBACK(tile_enter),t);
	g_signal_connect(motion,"leave",G_CALLBACK(tile_leave),t);
	gtk_widget_add_controller(t->wrap,motion);
	gtk_grid_attach(GTK_GRID(tiles),t->wrap,i%cols,i/cols,1,1);
}
static int focused_index(void) {
	for(int i=0;i<count;i++) if(gtk_widget_has_focus(shown[i].button)) return i;
	return -1;
}
static GPtrArray *section_apps(enum grid_section which) {
	switch(which) {
	case GRID_PINNED: return installed_pins;
	case GRID_SEARCH: return results;
	case GRID_ALL: return catalog;
	}
	g_assert_not_reached();
}
static int page_size(void) {return cols*rows;}
static int page_count(void) {
	return MAX(1,((int)section_apps(section)->len+page_size()-1)/page_size());
}
static gboolean can_page(int direction) {
	if(direction<0) return page>0 || (section==GRID_ALL && installed_pins->len>0);
	return page+1<page_count() || (section==GRID_PINNED && catalog->len>0);
}
static void focus_tile(int slot) {
	if(count>0) gtk_widget_grab_focus(shown[CLAMP(slot,0,count-1)].button);
}
static void render_page(int slot) {
	GPtrArray *apps=section_apps(section);
	page=CLAMP(page,0,page_count()-1);
	if(focused_index()>=0) gtk_entry_grab_focus_without_selecting(GTK_ENTRY(search));
	clear_tiles();
	gtk_widget_set_visible(full_name,FALSE);
	for(guint i=page*page_size();i<apps->len && count<page_size();i++) {
		make_tile(count,g_ptr_array_index(apps,i));
		count++;
	}
	// Keep the search and tile geometry stable while filtering down to
	// one or zero matches. The empty slots have no paint or input.
	for(int i=count;i<page_size();i++) {
		GtkWidget *spacer=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0);
		gtk_widget_set_size_request(spacer,56,tile_height);
		gtk_widget_set_hexpand(spacer,TRUE);
		gtk_widget_set_can_target(spacer,FALSE);
		gtk_grid_attach(GTK_GRID(tiles),spacer,i%cols,i/cols,1,1);
	}
	const char *names[]={"Pinned","All Apps","Search"};
	g_autofree char *label=apps->len?g_strdup_printf("%s %d / %d",names[section],page+1,page_count()):
		g_strdup(section==GRID_SEARCH?"Search · No results":"All Apps · No apps");
	gtk_label_set_text(GTK_LABEL(page_label),label);
	gtk_widget_set_sensitive(previous,can_page(-1));
	gtk_widget_set_sensitive(next,can_page(1));
	if(slot>=0) focus_tile(slot);
	gtk_widget_queue_draw(guide);
}
static void collect_pins(void) {
	g_ptr_array_set_size(installed_pins,0);
	for(guint i=0;i<catalog->len;i++) {
		GDesktopAppInfo *info=g_ptr_array_index(catalog,i);
		if(is_pinned(info)) g_ptr_array_add(installed_pins,info);
	}
}
static enum grid_section opening_section(void) {
	return installed_pins->len>0?GRID_PINNED:GRID_ALL;
}
static void pins_changed(const char *focused_id,int slot) {
	collect_pins();
	if(section==GRID_PINNED && installed_pins->len==0) {
		section=GRID_ALL;
		page=0;
	}
	if(slot>=0 && page>=page_count()) slot=page_size()-1;
	if(focused_id) {
		GPtrArray *apps=section_apps(section);
		for(guint i=0;i<apps->len;i++) {
			if(!g_str_equal(focused_id,app_id(g_ptr_array_index(apps,i)))) continue;
			page=i/page_size();
			slot=i%page_size();
			break;
		}
	}
	render_page(slot);
}
static void edited(GtkEditable *e,gpointer unused) {
	(void)unused;
	if(!catalog || resetting) return;
	g_autofree char *text=g_strdup(gtk_editable_get_text(e));
	g_strstrip(text);
	section=opening_section();
	page=0;
	g_ptr_array_set_size(results,0);
	if(*text) {
		section=GRID_SEARCH;
		g_autofree char *query=g_utf8_casefold(text,-1);
		for(guint i=0;i<catalog->len;i++) {
			GDesktopAppInfo *info=g_ptr_array_index(catalog,i);
			if(matches(info,query)) g_ptr_array_add(results,info);
		}
	}
	render_page(-1);
}
static void search_activate(GtkEntry *e,gpointer unused) {(void)e;(void)unused;if(count) launch_tile(&shown[0]);}
static void step_page(int direction) {
	if(!can_page(direction)) return;
	if(section==GRID_ALL && page==0 && direction<0) {
		section=GRID_PINNED;
		page=page_count()-1;
	} else if(section==GRID_PINNED && page+1==page_count() && direction>0) {
		section=GRID_ALL;
		page=0;
	} else page+=direction;
	render_page(direction<0?page_size()-1:0);
}
static void page_clicked(GtkButton *button,gpointer data) {
	(void)button;
	step_page(GPOINTER_TO_INT(data));
}
static void grid_guide(GtkDrawingArea *area,cairo_t *cr,int width,int height,gpointer unused) {
	(void)area;(void)height;(void)unused;
	app_grid_guide_draw(cr,width,focused_index()>=0);
}
static gboolean grid_key(GtkEventControllerKey *ctrl,guint key,guint code,GdkModifierType mods,gpointer data) {
	(void)ctrl;(void)code;(void)data;
	// Hold the focus-transfer key until release so repeat cannot edit Search.
	if(search_return_key && gdk_keyval_to_lower(key)==search_return_key) return TRUE;
	if(mods & (GDK_SHIFT_MASK|GDK_CONTROL_MASK|GDK_ALT_MASK|GDK_SUPER_MASK|GDK_META_MASK|GDK_HYPER_MASK)) return FALSE;
	int i=focused_index(), target=-1;
	if(i<0) {
		// Search keeps normal editing keys. Down is the explicit handoff
		// to the first result; W/A/S/D are still ordinary text here.
		if(key!=GDK_KEY_Down && key!=GDK_KEY_KP_Down) return FALSE;
		GtkRoot *root=gtk_widget_get_root(search);
		GtkWidget *focus=root?gtk_root_get_focus(root):NULL;
		if(!focus || (focus!=search && !gtk_widget_is_ancestor(focus,search))) return FALSE;
		focus_tile(0);
		return TRUE;
	}
	switch(key) {
	case GDK_KEY_s: case GDK_KEY_Down: case GDK_KEY_KP_Down:
		target=i+cols;break;
	case GDK_KEY_BackSpace:
		search_return_key=key;
		gtk_entry_grab_focus_without_selecting(GTK_ENTRY(search));
		return TRUE;
	case GDK_KEY_w: case GDK_KEY_Up: case GDK_KEY_KP_Up:
		if(i<cols) {
			search_return_key=key;
			gtk_entry_grab_focus_without_selecting(GTK_ENTRY(search));
			return TRUE;
		}
		target=i-cols;break;
	case GDK_KEY_a: case GDK_KEY_Left: case GDK_KEY_KP_Left:
		if(i==0) step_page(-1);
		else if(i%cols>0) target=i-1;
		break;
	case GDK_KEY_d: case GDK_KEY_Right: case GDK_KEY_KP_Right:
		if(i==count-1) step_page(1);
		else if(i%cols<cols-1) target=i+1;
		break;
	case GDK_KEY_space:
		if(space_held) return TRUE;
		space_held=TRUE;
		toggle_pin(&shown[i]);
		return TRUE;
	default:return FALSE;
	}
	if(target>=0 && target<count) gtk_widget_grab_focus(shown[target].button);
	return TRUE;
}
static void grid_release(GtkEventControllerKey *c,guint key,guint code,GdkModifierType mods,gpointer unused) {
	(void)c;(void)code;(void)mods;(void)unused;
	if(key==GDK_KEY_space) space_held=FALSE;
	if(gdk_keyval_to_lower(key)==search_return_key) search_return_key=0;
}
static GtkWidget *pager_button(const char *label,int direction) {
	GtkWidget *button=gtk_button_new_with_label(label);
	gtk_widget_set_size_request(button,84,PAGER_HEIGHT);
	gtk_widget_set_focus_on_click(button,FALSE);
	gtk_accessible_update_property(GTK_ACCESSIBLE(button),GTK_ACCESSIBLE_PROPERTY_LABEL,label,-1);
	analog_button_style(button,10,4);
	g_signal_connect(button,"clicked",G_CALLBACK(page_clicked),GINT_TO_POINTER(direction));
	return button;
}
GtkWidget *app_grid_new(void (*dismissed)(void)) {
	close_grid=dismissed;
	pinned=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);
	installed_pins=g_ptr_array_new();
	results=g_ptr_array_new();
	launcher=gtk_overlay_new();
	gtk_widget_set_halign(launcher,GTK_ALIGN_CENTER);
	gtk_widget_set_valign(launcher,GTK_ALIGN_CENTER);
	gtk_overlay_set_child(GTK_OVERLAY(launcher),analog_glass_new());
	GtkWidget *stack=gtk_box_new(GTK_ORIENTATION_VERTICAL,GRID_SPACING);
	gtk_widget_set_margin_top(stack,GRID_PADDING);gtk_widget_set_margin_bottom(stack,GRID_PADDING);
	gtk_widget_set_margin_start(stack,GRID_PADDING);gtk_widget_set_margin_end(stack,GRID_PADDING);
	gtk_overlay_add_overlay(GTK_OVERLAY(launcher),stack);
	gtk_overlay_set_measure_overlay(GTK_OVERLAY(launcher),stack,TRUE);
	GtkWidget *well=gtk_overlay_new();
	GtkWidget *search_chrome=gtk_drawing_area_new();
	gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(search_chrome),search_face,NULL,NULL);
	gtk_widget_set_can_target(search_chrome,FALSE);
	gtk_overlay_set_child(GTK_OVERLAY(well),search_chrome);
	search=gtk_entry_new();
	gtk_widget_add_css_class(search,"app-grid-search");
	gtk_entry_set_icon_from_icon_name(GTK_ENTRY(search),GTK_ENTRY_ICON_PRIMARY,"system-search-symbolic");
	gtk_entry_set_icon_activatable(GTK_ENTRY(search),GTK_ENTRY_ICON_PRIMARY,FALSE);
	gtk_entry_set_placeholder_text(GTK_ENTRY(search),"Search apps…");
	gtk_editable_set_width_chars(GTK_EDITABLE(search),1);
	gtk_entry_set_max_length(GTK_ENTRY(search),128);
	gtk_accessible_update_property(GTK_ACCESSIBLE(search),GTK_ACCESSIBLE_PROPERTY_LABEL,"Search installed apps",-1);
	gtk_overlay_add_overlay(GTK_OVERLAY(well),search);
	gtk_overlay_set_measure_overlay(GTK_OVERLAY(well),search,TRUE);
	gtk_box_append(GTK_BOX(stack),well);
	g_signal_connect(search,"changed",G_CALLBACK(edited),NULL);
	g_signal_connect(search,"activate",G_CALLBACK(search_activate),NULL);
	GtkWidget *divider=gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_widget_add_css_class(divider,"app-grid-divider");
	gtk_box_append(GTK_BOX(stack),divider);
	tiles=gtk_grid_new();
	gtk_grid_set_column_homogeneous(GTK_GRID(tiles),TRUE);
	gtk_grid_set_column_spacing(GTK_GRID(tiles),TILE_GAP);
	gtk_grid_set_row_spacing(GTK_GRID(tiles),TILE_GAP);
	gtk_box_append(GTK_BOX(stack),tiles);
	GtkWidget *pager=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,GRID_SPACING);
	gtk_widget_set_halign(pager,GTK_ALIGN_CENTER);
	previous=pager_button("Previous",-1);
	next=pager_button("Next",1);
	page_label=gtk_label_new(NULL);
	gtk_label_set_width_chars(GTK_LABEL(page_label),17);
	gtk_widget_add_css_class(page_label,"app-grid-label");
	gtk_box_append(GTK_BOX(pager),previous);
	gtk_box_append(GTK_BOX(pager),page_label);
	gtk_box_append(GTK_BOX(pager),next);
	gtk_box_append(GTK_BOX(stack),pager);
	guide=gtk_drawing_area_new();
	gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(guide),19);
	gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(guide),grid_guide,NULL,NULL);
	gtk_widget_set_can_target(guide,FALSE);
	gtk_box_append(GTK_BOX(stack),guide);
	gtk_accessible_update_property(GTK_ACCESSIBLE(launcher),GTK_ACCESSIBLE_PROPERTY_DESCRIPTION,
		"Down enters results. Use arrow keys or W, A, S, and D to move among apps and across page boundaries. Backspace returns to Search. Enter launches. Escape closes.",-1);
	full_name=gtk_label_new("");
	gtk_widget_add_css_class(full_name,"app-grid-full-name");
	gtk_widget_set_halign(full_name,GTK_ALIGN_CENTER);
	gtk_widget_set_valign(full_name,GTK_ALIGN_START);
	gtk_widget_set_margin_top(full_name,3);
	gtk_widget_set_can_target(full_name,FALSE);
	gtk_overlay_add_overlay(GTK_OVERLAY(launcher),full_name);
	gtk_widget_set_visible(full_name,FALSE);
	GtkEventController *keys=gtk_event_controller_key_new();
	gtk_event_controller_set_propagation_phase(keys,GTK_PHASE_CAPTURE);
	g_signal_connect(keys,"key-pressed",G_CALLBACK(grid_key),NULL);
	g_signal_connect(keys,"key-released",G_CALLBACK(grid_release),NULL);
	gtk_widget_add_controller(launcher,keys);
	gtk_widget_set_visible(launcher,FALSE);
	return launcher;
}
void app_grid_resize(int w,int h) {
	if(!launcher || w<1) return;
	int width=MAX(1,MIN(w-24,GRID_WIDTH));
	int margin=CLAMP(h-420,100,142);
	int fitted_cols=CLAMP((width-2*GRID_PADDING+TILE_GAP)/(100+TILE_GAP),1,GRID_COLS);
	int available=h-margin-GRID_FIXED_HEIGHT;
	int fitted_rows=CLAMP((available+TILE_GAP)/(MIN_TILE_HEIGHT+TILE_GAP),1,GRID_ROWS);
	int height=CLAMP((available+TILE_GAP)/fitted_rows-TILE_GAP,MIN_TILE_HEIGHT,TILE_HEIGHT);
	gtk_widget_set_size_request(launcher,width,-1);
	gtk_widget_set_margin_bottom(launcher,margin);
	if(fitted_cols==cols && height==tile_height && fitted_rows==rows) return;
	int i=focused_index();
	int anchor=page*page_size()+MAX(i,0);
	cols=fitted_cols;rows=fitted_rows;tile_height=height;
	page=anchor/page_size();
	if(catalog && grid_open) render_page(i<0?-1:anchor%page_size());
}
static void finish_grid_fade(GtkWidget *widget) {
	gtk_widget_set_opacity(widget,grid_to);
	if(!grid_open) gtk_widget_set_visible(widget,FALSE);
}

static gboolean grid_fade_frame(GtkWidget *widget,GdkFrameClock *clock,gpointer data) {
	(void)data;
	const double duration=(grid_open?180000.:140000.)*fabs(grid_to-grid_from);
	double t=duration>0?CLAMP((gdk_frame_clock_get_frame_time(clock)-grid_started)/duration,0.,1.):1.;
	gboolean enabled=TRUE;
	g_object_get(gtk_widget_get_settings(widget),"gtk-enable-animations",&enabled,NULL);
	if(t>=1. || !enabled) {
		grid_tick=0;
		finish_grid_fade(widget);
		return G_SOURCE_REMOVE;
	}
	double eased=t*t*(3.-2.*t);
	gtk_widget_set_opacity(widget,grid_from+(grid_to-grid_from)*eased);
	return G_SOURCE_CONTINUE;
}

static void animate_grid(gboolean instant) {
	if(grid_tick) gtk_widget_remove_tick_callback(launcher,grid_tick);
	grid_tick=0;
	grid_from=gtk_widget_get_opacity(launcher);
	grid_to=grid_open?1.:0.;
	gboolean enabled=TRUE;
	g_object_get(gtk_widget_get_settings(launcher),"gtk-enable-animations",&enabled,NULL);
	if(instant || !enabled || !gtk_widget_get_mapped(launcher) || grid_from==grid_to) {
		finish_grid_fade(launcher);
		return;
	}
	grid_started=g_get_monotonic_time();
	grid_tick=gtk_widget_add_tick_callback(launcher,grid_fade_frame,NULL,NULL);
}

void app_grid_show(gboolean in_node) {
	analog_glass_set_scene_backdrop(gtk_overlay_get_child(GTK_OVERLAY(launcher)),in_node?SCENE_GLASS_GRID:-1);
	grid_open=TRUE;
	resetting=TRUE;
	// Gtk owns focus and hits. The list is a snapshot of installed desktop
	// metadata; no desktop entry is activated while loading it.
	clear_tiles();
	g_ptr_array_set_size(results,0);
	g_ptr_array_set_size(installed_pins,0);
	g_clear_pointer(&catalog,g_ptr_array_unref);
	catalog=app_catalog_snapshot();
	collect_pins();
	section=opening_section();
	page=0;
	gtk_editable_set_text(GTK_EDITABLE(search),"");
	resetting=FALSE;
	render_page(-1);
	if(!gtk_widget_get_visible(launcher)) gtk_widget_set_opacity(launcher,0.);
	gtk_widget_set_sensitive(launcher,TRUE);
	gtk_widget_set_visible(launcher,TRUE);
	gtk_widget_grab_focus(search);
	animate_grid(FALSE);
}
void app_grid_hide(gboolean instant) {
	if(!launcher) return;
	grid_open=FALSE;
	space_held=FALSE;
	search_return_key=0;
	// The exit is paint only: release keyboard and pointer routing now.
	gtk_widget_set_sensitive(launcher,FALSE);
	animate_grid(instant);
}
void app_grid_set_pins(const char *list) {
	if(!pinned) return;
	g_hash_table_remove_all(pinned);
	g_auto(GStrv) ids=g_strsplit(list,"\n",-1);
	for(size_t i=0;ids[i];i++) if (*ids[i]) g_hash_table_add(pinned,g_strdup(ids[i]));
	if(grid_open) {
		int slot=focused_index();
		pins_changed(slot<0?NULL:app_id(shown[slot].info),slot);
	}
}
