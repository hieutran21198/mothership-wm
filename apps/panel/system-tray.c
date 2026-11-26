//go:build linux && cgo && panel

#include "system-tray.h"
#include "analog-button.h"
#include "analog-glass.h"
#include "map-pointer.h"
#include <string.h>

#define DESK "org.mothership.SystemTray1"
#define PATH "/org/mothership/SystemTray"
#define CALL_TIMEOUT 1900

enum { TRAY_ICON=24, TRAY_ITEM_PAD=10, TRAY_ITEM=TRAY_ICON+2*TRAY_ITEM_PAD, TRAY_ITEM_GAP=5 };

static GtkWidget *pane,*glass,*body,*icons,*grid,*menu_scroll,*menu_rows,*status;
static GtkWidget *trigger;
static GDBusConnection *bus;
static GCancellable *calls;
static GVariant *entries;
static void (*updated)(void);
static char *menu_item;
static gboolean fetching,fetch_again,choose_pending;
static guint service_watch,changes;
static int menu_parent,columns;
static int viewport_width,viewport_height,anchor,bottom;
static guint action_serial;
static GArray *parents;

static void render(void);
static void focus_first(void);
static void menu_fetch(gboolean prepare);

static void clear_box(GtkWidget *box) {
	GtkWidget *child;
	while((child=gtk_widget_get_first_child(box)))gtk_box_remove(GTK_BOX(box),child);
}

static void clear_menu(void) {
	++action_serial;
	choose_pending=FALSE;
	g_clear_pointer(&menu_item,g_free);
	g_array_set_size(parents,0);
	menu_parent=0;
	gtk_widget_set_visible(icons,TRUE);
	gtk_widget_set_visible(menu_scroll,FALSE);
	gtk_widget_set_visible(status,FALSE);
}

static void show_error(const char *detail) {
	gtk_label_set_text(GTK_LABEL(status),detail);
	gtk_widget_set_visible(status,TRUE);
}

gboolean system_tray_has_items(void) {return entries && g_variant_n_children(entries)>0;}
GtkWidget *system_tray_widget(void) {return pane;}
GtkWidget *system_tray_trigger(void) {return trigger;}

gboolean system_tray_close(void) {
	if(!pane || !gtk_widget_get_visible(pane))return FALSE;
	clear_menu();
	if(trigger){
		gtk_accessible_update_state(GTK_ACCESSIBLE(trigger),GTK_ACCESSIBLE_STATE_EXPANDED,FALSE,-1);
		trigger=NULL;
	}
	gtk_widget_set_visible(pane,FALSE);
	updated();
	return TRUE;
}

gboolean system_tray_open(GtkWidget *from) {
	if(!system_tray_has_items() || gtk_widget_get_visible(pane))return FALSE;
	trigger=from;
	gtk_accessible_update_state(GTK_ACCESSIBLE(trigger),GTK_ACCESSIBLE_STATE_EXPANDED,TRUE,-1);
	clear_menu();
	gtk_widget_set_visible(pane,TRUE);
	render();
	focus_first();
	updated();
	return TRUE;
}

static void position(void) {
	int count=entries?(int)g_variant_n_children(entries):0;
	int cells=MAX(1,MIN(6,count));
	int preferred=menu_item?260:MAX(145,24+cells*TRAY_ITEM+(cells-1)*TRAY_ITEM_GAP);
	int width=MIN(preferred,MAX(64,viewport_width-24));
	int height=MAX(45,MIN(menu_item?290:222,viewport_height-bottom-24));
	gtk_widget_set_size_request(pane,width,-1);
	gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(icons),height);
	gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(menu_scroll),height);
	gtk_widget_set_margin_start(pane,CLAMP(anchor-width/2,12,MAX(12,viewport_width-12-width)));
	gtk_widget_set_margin_bottom(pane,bottom);
}
void system_tray_place(int w,int h,int x,int b,gboolean live) {
	viewport_width=w;viewport_height=h;anchor=x;bottom=b;
	if(!pane)return;
	// This rail popup is exclusive with Notification Log, so it can use the
	// same live scene-glass channel while in a Node.
	analog_glass_set_scene_backdrop(glass,live?SCENE_GLASS_LOG:-1);
	position();
	int fit=CLAMP((w-48+TRAY_ITEM_GAP)/(TRAY_ITEM+TRAY_ITEM_GAP),1,6);
	if(columns!=fit){columns=fit;render();}
}

static void request(const char *method,GVariant *params,const GVariantType *type,GAsyncReadyCallback done,gpointer data) {
	g_dbus_connection_call(bus,DESK,PATH,DESK,method,params,type,G_DBUS_CALL_FLAGS_NO_AUTO_START,CALL_TIMEOUT,calls,done,data);
}

static GtkWidget *item_icon(const char *icon,int w,int h,GVariant *pixels) {
	GtkIconTheme *theme=gtk_icon_theme_get_for_display(gtk_widget_get_display(pane));
	GtkWidget *image=NULL;
	if(*icon && gtk_icon_theme_has_icon(theme,icon))image=gtk_image_new_from_icon_name(icon);
	if(!image && w>0 && h>0 && w<=128 && h<=128) {
		g_autoptr(GBytes) bytes=g_variant_get_data_as_bytes(pixels);
		if(g_bytes_get_size(bytes)==(gsize)(4*w*h)){
			g_autoptr(GdkPixbuf) src=gdk_pixbuf_new_from_bytes(bytes,GDK_COLORSPACE_RGB,TRUE,8,w,h,w*4);
			// GtkImage pixel-size only constrains named icons, not textures.
			int longest=MAX(w,h);
			int fit_w=MAX(1,(w*TRAY_ICON+longest/2)/longest);
			int fit_h=MAX(1,(h*TRAY_ICON+longest/2)/longest);
			g_autoptr(GdkPixbuf) fitted=gdk_pixbuf_scale_simple(src,fit_w,fit_h,GDK_INTERP_BILINEAR);
			g_autoptr(GdkTexture) texture=gdk_texture_new_for_pixbuf(fitted);
			image=gtk_image_new_from_paintable(GDK_PAINTABLE(texture));
		}
	}
	if(!image)image=gtk_image_new_from_icon_name("application-x-executable-symbolic");
	gtk_image_set_pixel_size(GTK_IMAGE(image),TRAY_ICON);
	return image;
}

static void choose_done(GObject *b,GAsyncResult *result,gpointer data) {
	guint serial=GPOINTER_TO_UINT(data);
	g_autoptr(GVariant) reply=g_dbus_connection_call_finish(G_DBUS_CONNECTION(b),result,NULL);
	if(serial!=action_serial || !gtk_widget_get_visible(pane))return;
	choose_pending=FALSE;
	if(reply)system_tray_close();
	else show_error("The application did not accept the menu action.");
}

struct menu_button {int id,kind;};
static void menu_click(GtkButton *button,gpointer data) {
	(void)button;
	struct menu_button *info=data;
	if(!menu_item || !bus || choose_pending)return;
	if(info->kind>0){
		g_array_append_val(parents,menu_parent);
		menu_parent=info->id;
		menu_fetch(TRUE);
	} else if(info->kind<0) {
		if(parents->len){
			menu_parent=g_array_index(parents,int,parents->len-1);
			g_array_set_size(parents,parents->len-1);
			menu_fetch(TRUE);
		} else {
			clear_menu();position();focus_first();updated();
		}
	} else {
		choose_pending=TRUE;
		guint stamp=(guint)(g_get_monotonic_time()/1000);
		request("Choose",g_variant_new("(siu)",menu_item,info->id,stamp),G_VARIANT_TYPE_UNIT,choose_done,GUINT_TO_POINTER(++action_serial));
	}
}

static GtkWidget *menu_button(const char *label,int id,int kind,gboolean enabled) {
	GtkWidget *button=gtk_button_new_with_mnemonic(label);
	gtk_widget_set_halign(gtk_button_get_child(GTK_BUTTON(button)),GTK_ALIGN_START);
	gtk_label_set_ellipsize(GTK_LABEL(gtk_button_get_child(GTK_BUTTON(button))),PANGO_ELLIPSIZE_END);
	analog_button_style(button,7,5);
	gtk_widget_set_sensitive(button,enabled);
	struct menu_button *info=g_new(struct menu_button,1);
	*info=(struct menu_button){id,kind};
	g_object_set_data_full(G_OBJECT(button),"menu-action",info,g_free);
	g_signal_connect(button,"clicked",G_CALLBACK(menu_click),info);
	return button;
}

static void menu_loaded(GObject *b,GAsyncResult *result,gpointer data) {
	guint serial=GPOINTER_TO_UINT(data);
	g_autoptr(GVariant) reply=g_dbus_connection_call_finish(G_DBUS_CONNECTION(b),result,NULL);
	if(serial!=action_serial || !menu_item || !gtk_widget_get_visible(pane))return;
	clear_box(menu_rows);
	gtk_box_append(GTK_BOX(menu_rows),menu_button("‹  Back",0,-1,TRUE));
	gtk_widget_set_visible(status,FALSE);
	if(!reply) {show_error("This app’s menu is unavailable.");updated();return;}
	g_autoptr(GVariant) list=g_variant_get_child_value(reply,0);
	GVariantIter iter;
	int id,state;gboolean enabled,child;
	const char *label,*type;
	g_variant_iter_init(&iter,list);
	int count=0;
	while(g_variant_iter_next(&iter,"(i&sbb&si)",&id,&label,&enabled,&child,&type,&state)){
		if(g_str_equal(type,"separator")){
			gtk_box_append(GTK_BOX(menu_rows),gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
			continue;
		}
		g_autofree char *caption=g_strdup_printf("%s%s%s",
			g_str_equal(type,"checkmark")?(state==1?"✓  ":"□  "):
			g_str_equal(type,"radio")?(state==1?"●  ":"○  "):"",
			*label?label:"Menu item",child?"  ›":"");
		gtk_box_append(GTK_BOX(menu_rows),menu_button(caption,id,child?1:0,enabled));
		count++;
	}
	if(count==0)show_error("No actions from this application.");
	gtk_widget_grab_focus(gtk_widget_get_first_child(menu_rows));
	updated();
}

static void menu_fetch(gboolean prepare) {
	if(!bus || !menu_item)return;
	gtk_widget_set_visible(icons,FALSE);
	gtk_widget_set_visible(menu_scroll,TRUE);
	gtk_widget_set_visible(status,FALSE);
	position();
	request("GetMenu",g_variant_new("(sib)",menu_item,menu_parent,prepare),G_VARIANT_TYPE("(a(isbbsi))"),
		menu_loaded,GUINT_TO_POINTER(++action_serial));
}

struct open_result {char *id;guint serial;};
static void item_opened(GObject *b,GAsyncResult *result,gpointer data) {
	struct open_result *op=data;
	g_autoptr(GVariant) reply=g_dbus_connection_call_finish(G_DBUS_CONNECTION(b),result,NULL);
	if(op->serial==action_serial && gtk_widget_get_visible(pane)){
		gboolean wants_menu=FALSE;
		if(!reply)show_error("The application did not respond.");
		else {
			g_variant_get(reply,"(b)",&wants_menu);
			if(wants_menu){
				menu_item=g_steal_pointer(&op->id);
				menu_fetch(TRUE);
			} else system_tray_close();
		}
	}
	g_free(op->id);g_free(op);
}

static void open_item(GtkWidget *button,gboolean context) {
	const char *id=g_object_get_data(G_OBJECT(button),"tray-id");
	if(!id || !bus)return;
	struct open_result *op=g_new0(struct open_result,1);
	op->id=g_strdup(id);op->serial=++action_serial;
	graphene_rect_t pos=GRAPHENE_RECT_INIT(0,0,0,0);
	GtkRoot *root=gtk_widget_get_root(button);
	gboolean have_pos=root && gtk_widget_compute_bounds(button,GTK_WIDGET(root),&pos);
	int x=have_pos?(int)(pos.origin.x+pos.size.width/2):0;
	int y=have_pos?(int)(pos.origin.y+pos.size.height/2):0;
	request("Open",g_variant_new("(sbii)",id,context,x,y),G_VARIANT_TYPE("(b)"),item_opened,op);
}
static void item_click(GtkButton *button,gpointer data) {(void)data;open_item(GTK_WIDGET(button),FALSE);}
static void item_right(GtkGestureClick *click,int count,double x,double y,gpointer data) {
	(void)count;(void)x;(void)y;(void)data;
	gtk_gesture_set_state(GTK_GESTURE(click),GTK_EVENT_SEQUENCE_CLAIMED);
	open_item(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(click)),TRUE);
}
static gboolean item_key(GtkEventControllerKey *keys,guint key,guint code,GdkModifierType mods,gpointer data) {
	(void)code;(void)data;
	if(key!=GDK_KEY_Menu && !(key==GDK_KEY_F10 && (mods&GDK_SHIFT_MASK)))return FALSE;
	open_item(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(keys)),TRUE);
	return TRUE;
}

static void focus_first(void) {
	GtkWidget *first=gtk_grid_get_child_at(GTK_GRID(grid),0,0);
	if(first)gtk_widget_grab_focus(first);
}

static void render(void) {
	if(!pane || !gtk_widget_get_visible(pane))return;
	GtkWidget *focused=gtk_root_get_focus(gtk_widget_get_root(pane));
	g_autofree char *last_focus=focused?g_strdup(g_object_get_data(G_OBJECT(focused),"tray-id")):NULL;
	GtkWidget *next_focus=NULL;
	while(gtk_widget_get_first_child(grid))gtk_grid_remove(GTK_GRID(grid),gtk_widget_get_first_child(grid));
	if(!entries)return;
	GVariantIter iter;
	g_variant_iter_init(&iter,entries);
	const char *id,*title,*icon,*level;
	gboolean menu_only,menu_still_present=FALSE;int w,h;
	GVariant *bytes;
	int i=0,cols=MAX(1,columns);
	while(g_variant_iter_next(&iter,"(&s&s&s&sbii@ay)",&id,&title,&icon,&level,&menu_only,&w,&h,&bytes)){
		menu_still_present|=g_strcmp0(id,menu_item)==0;
		GtkWidget *button=gtk_button_new();
		GtkWidget *image=item_icon(icon,w,h,bytes);
		g_variant_unref(bytes);
		gtk_widget_set_halign(image,GTK_ALIGN_CENTER);
		gtk_widget_set_valign(image,GTK_ALIGN_CENTER);
		gtk_button_set_child(GTK_BUTTON(button),image);
		gtk_widget_set_size_request(button,TRAY_ITEM,TRAY_ITEM);
		analog_button_style(button,TRAY_ITEM_PAD,TRAY_ITEM_PAD);
		analog_button_on(button,g_str_equal(level,"NeedsAttention"));
		gtk_accessible_update_property(GTK_ACCESSIBLE(button),GTK_ACCESSIBLE_PROPERTY_LABEL,title,
			GTK_ACCESSIBLE_PROPERTY_DESCRIPTION,menu_only?"Opens the application menu":
				g_str_equal(level,"NeedsAttention")?"Application needs attention":"Activates the application",-1);
		g_object_set_data_full(G_OBJECT(button),"tray-id",g_strdup(id),g_free);
		g_signal_connect(button,"clicked",G_CALLBACK(item_click),NULL);
		GtkGesture *right=gtk_gesture_click_new();
		gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(right),GDK_BUTTON_SECONDARY);
		g_signal_connect(right,"released",G_CALLBACK(item_right),NULL);
		gtk_widget_add_controller(button,GTK_EVENT_CONTROLLER(right));
		GtkEventController *keys=gtk_event_controller_key_new();
		g_signal_connect(keys,"key-pressed",G_CALLBACK(item_key),NULL);
		gtk_widget_add_controller(button,keys);
		gtk_grid_attach(GTK_GRID(grid),button,i%cols,i/cols,1,1);
		if(last_focus && g_str_equal(id,last_focus))next_focus=button;
		i++;
	}
	if(menu_item && !menu_still_present){clear_menu();focus_first();}
	else if(last_focus && !menu_item) {if(next_focus)gtk_widget_grab_focus(next_focus);else focus_first();}
	position();
}

static void refresh(void);
static void apply_items(GVariant *next) {
	if(entries && next && g_variant_equal(entries,next))return;
	g_clear_pointer(&entries,g_variant_unref);
	if(next)entries=g_variant_ref(next);
	if(!system_tray_has_items() && system_tray_close())return;
	render();updated();
}
static void got_items(GObject *b,GAsyncResult *result,gpointer data) {
	(void)data;
	g_autoptr(GVariant) reply=g_dbus_connection_call_finish(G_DBUS_CONNECTION(b),result,NULL);
	fetching=FALSE;
	if(!bus || G_DBUS_CONNECTION(b)!=bus)return;
	g_autoptr(GVariant) next=reply?g_variant_get_child_value(reply,0):NULL;
	apply_items(next);
	if(fetch_again){fetch_again=FALSE;refresh();}
}
static void refresh(void) {
	if(!bus)return;
	if(fetching){fetch_again=TRUE;return;}
	fetching=TRUE;
	request("GetItems",NULL,G_VARIANT_TYPE("(a(ssssbiiay))"),got_items,NULL);
}
static void signal_changed(GDBusConnection *b,const char *sender,const char *path,const char *iface,
	const char *name,GVariant *value,gpointer data) {
	(void)b;(void)sender;(void)path;(void)iface;(void)data;
	if(g_str_equal(name,"Changed"))refresh();
	else if(menu_item && g_str_equal(name,"MenuChanged")){
		const char *id;
		g_variant_get(value,"(&s)",&id);
		if(g_str_equal(id,menu_item) && !choose_pending)menu_fetch(FALSE);
	}
}
static void service_visible(GDBusConnection *b,const char *name,const char *owner,gpointer data) {
	(void)b;(void)name;(void)owner;(void)data;refresh();
}
static void service_gone(GDBusConnection *b,const char *name,gpointer data) {
	(void)b;(void)name;(void)data;apply_items(NULL);
}

static GtkWidget *scroll(GtkWidget *child) {
	GtkWidget *s=gtk_scrolled_window_new();
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(s),GTK_POLICY_NEVER,GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(s),TRUE);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(s),child);
	return s;
}

void system_tray_init(GtkOverlay *root,void (*fn)(void)) {
	updated=fn;
	parents=g_array_new(FALSE,FALSE,sizeof(int));
	columns=6;
	pane=gtk_overlay_new();
	gtk_widget_set_halign(pane,GTK_ALIGN_START);
	gtk_widget_set_valign(pane,GTK_ALIGN_END);
	glass=analog_glass_new();
	gtk_overlay_set_child(GTK_OVERLAY(pane),glass);
	GtkCssProvider *skin=gtk_css_provider_new();
	gtk_css_provider_load_from_string(skin,
		".system-tray-title { color: #d7e9f3; font-size: 14px; }"
		".system-tray-menu separator { background: #355660; border: 0; min-height: 1px; }");
	gtk_style_context_add_provider_for_display(gtk_widget_get_display(pane),GTK_STYLE_PROVIDER(skin),GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref(skin);
	body=gtk_box_new(GTK_ORIENTATION_VERTICAL,6);
	gtk_widget_set_margin_start(body,12);gtk_widget_set_margin_end(body,12);
	gtk_widget_set_margin_top(body,10);gtk_widget_set_margin_bottom(body,10);
	gtk_overlay_add_overlay(GTK_OVERLAY(pane),body);
	gtk_overlay_set_measure_overlay(GTK_OVERLAY(pane),body,TRUE);
	GtkWidget *caption=gtk_label_new("SYSTEM TRAY");
	gtk_widget_add_css_class(caption,"system-tray-title");
	gtk_widget_add_css_class(caption,"shell-system-title");
	gtk_widget_set_halign(caption,GTK_ALIGN_START);
	gtk_box_append(GTK_BOX(body),caption);
	grid=gtk_grid_new();
	gtk_grid_set_column_spacing(GTK_GRID(grid),TRAY_ITEM_GAP);
	gtk_grid_set_row_spacing(GTK_GRID(grid),TRAY_ITEM_GAP);
	gtk_widget_set_halign(grid,GTK_ALIGN_CENTER);
	icons=scroll(grid);
	gtk_box_append(GTK_BOX(body),icons);
	menu_rows=gtk_box_new(GTK_ORIENTATION_VERTICAL,3);
	gtk_widget_add_css_class(menu_rows,"system-tray-menu");
	menu_scroll=scroll(menu_rows);
	gtk_box_append(GTK_BOX(body),menu_scroll);
	gtk_widget_set_visible(menu_scroll,FALSE);
	status=gtk_label_new("");
	gtk_label_set_wrap(GTK_LABEL(status),TRUE);
	gtk_widget_add_css_class(status,"notification-meta");
	gtk_box_append(GTK_BOX(body),status);
	gtk_widget_set_visible(status,FALSE);
	gtk_accessible_update_property(GTK_ACCESSIBLE(pane),GTK_ACCESSIBLE_PROPERTY_LABEL,"System Tray",-1);
	gtk_widget_set_visible(pane,FALSE);
	gtk_overlay_add_overlay(root,pane);
}

void system_tray_connect(const char *address) {
	bus=g_dbus_connection_new_for_address_sync(address,G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT|
		G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,NULL,NULL,NULL);
	if(!bus)return;
	g_dbus_connection_set_exit_on_close(bus,FALSE);
	calls=g_cancellable_new();
	changes=g_dbus_connection_signal_subscribe(bus,DESK,DESK,NULL,PATH,NULL,G_DBUS_SIGNAL_FLAGS_NONE,signal_changed,NULL,NULL);
	service_watch=g_bus_watch_name_on_connection(bus,DESK,G_BUS_NAME_WATCHER_FLAGS_NONE,service_visible,service_gone,NULL,NULL);
}
void system_tray_disconnect(void) {
	if(!bus)return;
	g_bus_unwatch_name(service_watch);
	g_dbus_connection_signal_unsubscribe(bus,changes);
	g_cancellable_cancel(calls);
	g_clear_object(&calls);
	g_clear_object(&bus);
}
