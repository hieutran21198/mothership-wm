//go:build linux && cgo && panel

#include <gdk/wayland/gdkwayland.h>
#include <string.h>
#include "generated-map-pointer.h"
#include "map-pointer.h"
#include "display-settings.h"
#include "screenshot.h"
#include "slash-ui.h"

static GtkWindow *command;
static struct wl_registry *registry;
static struct mothership_map_pointer_v1 *pointer;
static gboolean zoom_enabled,aim_enabled;
static int zoom_margin,frost_margin;
static gboolean video_frost;
static uint32_t pointer_name;
static int node_x,node_y,node_w,node_h;
static gboolean node_motion;
static int picker_scale=150,strip_hover_scale=100,cursor_size=24;
static struct glass_state {int x,y,w,h;unsigned alpha;} glass[SCENE_GLASS_COUNT];
static unsigned glass_slots;

static void zoom(void *data,struct mothership_map_pointer_v1 *p,wl_fixed_t x,wl_fixed_t y,wl_fixed_t delta) {
	(void)data;(void)p;
	if (zoom_enabled) panel_map_scroll(wl_fixed_to_double(x),wl_fixed_to_double(y),wl_fixed_to_double(delta));
}

static void click(void *data,struct mothership_map_pointer_v1 *p,wl_fixed_t x,wl_fixed_t y) {
	(void)data;(void)p;
	if (zoom_enabled) panel_map_click(wl_fixed_to_double(x),wl_fixed_to_double(y));
}

static void contact(void *data,struct mothership_map_pointer_v1 *p,uint32_t phase,wl_fixed_t x,wl_fixed_t y) {
	(void)data;(void)p;
	if (zoom_enabled || phase==MOTHERSHIP_MAP_POINTER_V1_CONTACT_PHASE_CANCEL)
		panel_map_contact(phase,wl_fixed_to_double(x),wl_fixed_to_double(y));
}

static void aim(void *data,struct mothership_map_pointer_v1 *p,uint32_t valid,wl_fixed_t x,wl_fixed_t y) {
	(void)data;(void)p;
	if(aim_enabled) panel_map_aim(valid!=0,wl_fixed_to_double(x),wl_fixed_to_double(y));
}
static void weapon_button(void *data,struct mothership_map_pointer_v1 *p,uint32_t weapon,uint32_t pressed) {
	(void)data;(void)p;
	if(aim_enabled) panel_map_weapon(weapon==MOTHERSHIP_MAP_POINTER_V1_WEAPON_PLASMA,pressed!=0);
}
static void home_tap(void *data,struct mothership_map_pointer_v1 *p) {
	(void)data;(void)p;panel_home_tap();
}
static void node_apps(void *data,struct mothership_map_pointer_v1 *p) {
	(void)data;(void)p;panel_node_apps();
}
static void node_slash(void *data,struct mothership_map_pointer_v1 *p) {
	(void)data;(void)p;panel_node_slash();
}
static void node_notifications(void *data,struct mothership_map_pointer_v1 *p) {
	(void)data;(void)p;panel_node_notifications();
}
static void node_settings(void *data,struct mothership_map_pointer_v1 *p) {
	(void)data;(void)p;panel_open_node_settings();
}
static void node_number(void *data,struct mothership_map_pointer_v1 *p,uint32_t slot) {
	(void)data;(void)p;panel_node_number(slot);
}
static void node_move(void *d,struct mothership_map_pointer_v1 *p,uint32_t slot,uint32_t hi,uint32_t lo) {
	(void)d;(void)p;panel_node_move(slot,((uint64_t)hi<<32)|lo);
}
static void window_list_row(void *d,struct mothership_map_pointer_v1 *p,uint32_t serial,uint32_t hi,uint32_t lo,
		const char *title,const char *app) {
	(void)d;(void)p;slash_ui_window_row(serial,((uint64_t)hi<<32)|lo,title,app);
}
static void window_list_end(void *d,struct mothership_map_pointer_v1 *p,uint32_t serial) {
	(void)d;(void)p;slash_ui_window_end(serial);
}
gboolean panel_request_window_list(uint32_t serial) {
	if(!pointer || mothership_map_pointer_v1_get_version(pointer)<MOTHERSHIP_MAP_POINTER_V1_WINDOW_LIST_SINCE_VERSION)
		return FALSE;
	mothership_map_pointer_v1_window_list(pointer,serial);
	return TRUE;
}
static void node_switch_step(void *d,struct mothership_map_pointer_v1 *p,int32_t direction) {
	(void)d;(void)p;panel_node_switch_step(direction);
}
static void node_switch_finish(void *d,struct mothership_map_pointer_v1 *p,uint32_t commit) {
	(void)d;(void)p;panel_node_switch_finish(commit);
}
static void window_count(void *d,struct mothership_map_pointer_v1 *p,uint32_t count) {
	(void)d;(void)p;panel_window_count(count);
}
static void window_overflow(void *d,struct mothership_map_pointer_v1 *p,uint32_t count) {
	(void)d;(void)p;panel_window_overflow(count);
}
static void picker_begin(void *d,struct mothership_map_pointer_v1 *p,uint32_t serial,uint32_t keyboard) {
	(void)d;(void)p;panel_picker_begin(serial,keyboard!=0);
}
static void picker_row(void *d,struct mothership_map_pointer_v1 *p,uint32_t hi,uint32_t lo,const char *title,const char *app) {
	(void)d;(void)p;panel_picker_row(((uint64_t)hi<<32)|lo,title,app);
}
static void picker_highlight(void *d,struct mothership_map_pointer_v1 *p,uint32_t serial,uint32_t hi,uint32_t lo) {
	(void)d;(void)p;panel_picker_highlight(serial,((uint64_t)hi<<32)|lo);
}
static void picker_end(void *d,struct mothership_map_pointer_v1 *p,uint32_t serial) {
	(void)d;(void)p;panel_picker_end(serial);
}
void panel_request_window_picker(void) {
	if(pointer && mothership_map_pointer_v1_get_version(pointer)>=13)
		mothership_map_pointer_v1_window_picker(pointer);
}
void panel_pick_window(uint32_t serial,uint64_t id) {
	if(pointer && mothership_map_pointer_v1_get_version(pointer)>=11)
		mothership_map_pointer_v1_window_pick(pointer,serial,id>>32,id);
}
void panel_place_window_preview(uint32_t serial,uint64_t id,int x,int y,int w,int h) {
	if(pointer && mothership_map_pointer_v1_get_version(pointer)>=13)
		mothership_map_pointer_v1_window_preview(pointer,serial,id>>32,id,x,y,w,h);
}
static void display_available(void *d,struct mothership_map_pointer_v1 *p,uint32_t available) {
	(void)d;(void)p;display_settings_available(available!=0);
}
static void display_current(void *d,struct mothership_map_pointer_v1 *p,const char *name,const char *connector,
		int32_t w,int32_t h,int32_t hz,int32_t scale) {
	(void)d;(void)p;display_settings_current(name,connector,w,h,hz,scale);
}
static void display_mode(void *d,struct mothership_map_pointer_v1 *p,int32_t w,int32_t h,int32_t hz) {
	(void)d;(void)p;display_settings_mode(w,h,hz);
}
static void display_done(void *d,struct mothership_map_pointer_v1 *p) {
	(void)d;(void)p;display_settings_done();
}
static void display_result(void *d,struct mothership_map_pointer_v1 *p,uint32_t status,const char *message) {
	(void)d;(void)p;display_settings_result(status,message);
}
void panel_display_query(void) {
	if(pointer && mothership_map_pointer_v1_get_version(pointer)>=MOTHERSHIP_MAP_POINTER_V1_DISPLAY_QUERY_SINCE_VERSION)
		mothership_map_pointer_v1_display_query(pointer);
}
gboolean panel_display_apply(int w,int h,int hz,int scale) {
	if(!pointer || mothership_map_pointer_v1_get_version(pointer)<MOTHERSHIP_MAP_POINTER_V1_DISPLAY_APPLY_SINCE_VERSION)
		return FALSE;
	mothership_map_pointer_v1_display_apply(pointer,w,h,hz,scale);
	return TRUE;
}

static void apply_scene_glass(unsigned slot);
static void scene_glass_available(void *data,struct mothership_map_pointer_v1 *p,uint32_t slots) {
	(void)data;(void)p;
	glass_slots=slots;
	for(unsigned i=0;i<SCENE_GLASS_COUNT;i++)
		if(glass[i].alpha) apply_scene_glass(i);
}

static void group_state(void *d,struct mothership_map_pointer_v1 *p,
		uint32_t hi,uint32_t lo,uint32_t count,uint32_t hidden,int32_t sx,int32_t sy,int32_t gap) {
	(void)d;(void)p;
	panel_node_group_state(((uint64_t)hi<<32)|lo,count,hidden!=0,sx,sy,gap);
}

void panel_node_group_query(double x,double y) {
	if(pointer && mothership_map_pointer_v1_get_version(pointer)>=MOTHERSHIP_MAP_POINTER_V1_GROUP_QUERY_SINCE_VERSION)
		mothership_map_pointer_v1_group_query(pointer,wl_fixed_from_double(x),wl_fixed_from_double(y));
}
void panel_node_group_action(uint64_t id,unsigned action,int sx,int sy,int gap) {
	if(pointer && mothership_map_pointer_v1_get_version(pointer)>=MOTHERSHIP_MAP_POINTER_V1_GROUP_ACTION_SINCE_VERSION)
		mothership_map_pointer_v1_group_action(pointer,id>>32,(uint32_t)id,action,sx,sy,gap);
}

static void group_settings_anchor(void *d,struct mothership_map_pointer_v1 *p,int32_t x,int32_t y) {
	(void)d;(void)p;
	panel_node_group_settings_anchor(x,y);
}

static gboolean screenshot_supported(void) {
	return pointer && mothership_map_pointer_v1_get_version(pointer)>=MOTHERSHIP_MAP_POINTER_V1_SCREENSHOT_AUTH_SINCE_VERSION;
}

void map_pointer_screenshot_begin(void) {
	if(screenshot_supported()) mothership_map_pointer_v1_screenshot_begin(pointer);
}

void map_pointer_screenshot_cancel(void) {
	if(screenshot_supported()) mothership_map_pointer_v1_screenshot_cancel(pointer);
}

void map_pointer_screenshot_window(uint32_t hi,uint32_t lo) {
	if(screenshot_supported()) mothership_map_pointer_v1_screenshot_window(pointer,hi,lo);
}

static void screenshot_available(void *d,struct mothership_map_pointer_v1 *p,uint32_t available) {
	(void)d;(void)p;panel_screenshot_available(available!=0);
}

static void screenshot_open(void *d,struct mothership_map_pointer_v1 *p) {
	(void)d;(void)p;panel_screenshot_open();
}

static void screenshot_frame(void *d,struct mothership_map_pointer_v1 *p,int32_t fd,int32_t w,int32_t h,
		int32_t stride,int32_t logical_w,int32_t logical_h) {
	(void)d;(void)p;panel_screenshot_frame(fd,w,h,stride,logical_w,logical_h);
}

static void screenshot_target(void *d,struct mothership_map_pointer_v1 *p,uint32_t hi,uint32_t lo,
		int32_t x,int32_t y,int32_t w,int32_t h,const char *title) {
	(void)d;(void)p;panel_screenshot_target(hi,lo,x,y,w,h,title);
}

static void screenshot_image(void *d,struct mothership_map_pointer_v1 *p,int32_t fd,int32_t w,int32_t h,int32_t stride) {
	(void)d;(void)p;panel_screenshot_image(fd,w,h,stride);
}

static void screenshot_problem(void *d,struct mothership_map_pointer_v1 *p,const char *message,uint32_t terminal) {
	(void)d;(void)p;panel_screenshot_problem(message,terminal!=0);
}

static const struct mothership_map_pointer_v1_listener events={
	.zoom=zoom,.click=click,.contact=contact,.aim=aim,.weapon_button=weapon_button,.home_tap=home_tap,.node_apps=node_apps,.node_settings=node_settings,
	.window_count=window_count,.window_overflow=window_overflow,.picker_begin=picker_begin,.picker_row=picker_row,
	.picker_highlight=picker_highlight,.picker_end=picker_end,.node_number=node_number,
	.display_available=display_available,.display_current=display_current,.display_mode=display_mode,
	.display_done=display_done,.display_result=display_result,
	.scene_glass_available=scene_glass_available,.node_slash=node_slash,
	.group_state=group_state,.group_settings_anchor=group_settings_anchor,
	.node_notifications=node_notifications,
	.screenshot_available=screenshot_available,.screenshot_open=screenshot_open,.screenshot_frame=screenshot_frame,
	.screenshot_target=screenshot_target,.screenshot_image=screenshot_image,.screenshot_problem=screenshot_problem,
	.node_switch_step=node_switch_step,.node_switch_finish=node_switch_finish,
	.window_list_row=window_list_row,.window_list_end=window_list_end,.node_move=node_move,
};

static void apply_aim(void) {
	if(pointer && mothership_map_pointer_v1_get_version(pointer)>=MOTHERSHIP_MAP_POINTER_V1_SET_AIM_SINCE_VERSION)
		mothership_map_pointer_v1_set_aim(pointer,aim_enabled);
}

static void apply_video_frost(void) {
	if(pointer && mothership_map_pointer_v1_get_version(pointer)>=MOTHERSHIP_MAP_POINTER_V1_SET_VIDEO_FROST_SINCE_VERSION)
		mothership_map_pointer_v1_set_video_frost(pointer,video_frost,frost_margin);
}

static void apply_scene_glass(unsigned slot) {
	if(pointer && (glass_slots&(1u<<slot))) {
		struct glass_state g=glass[slot];
		mothership_map_pointer_v1_set_scene_glass(pointer,slot,g.x,g.y,g.w,g.h,g.alpha);
	}
}
void map_pointer_scene_glass(unsigned slot,int x,int y,int w,int h,unsigned alpha) {
	if(slot>=SCENE_GLASS_COUNT) return;
	struct glass_state g=glass[slot];
	if(g.x==x && g.y==y && g.w==w && g.h==h && g.alpha==alpha) return;
	glass[slot]=(struct glass_state){x,y,w,h,alpha};
	apply_scene_glass(slot);
}

static void apply_picker_scale(void) {
	if(pointer && mothership_map_pointer_v1_get_version(pointer)>=14)
		mothership_map_pointer_v1_set_picker_scale(pointer,picker_scale);
}
void panel_set_picker_scale(int percent) {
	picker_scale=CLAMP(percent,100,300);
	apply_picker_scale();
}
static void apply_strip_hover_scale(void) {
	if(pointer && mothership_map_pointer_v1_get_version(pointer)>=MOTHERSHIP_MAP_POINTER_V1_SET_STRIP_HOVER_SCALE_SINCE_VERSION)
		mothership_map_pointer_v1_set_strip_hover_scale(pointer,strip_hover_scale);
}
void panel_set_strip_hover_scale(int percent) {
	strip_hover_scale=CLAMP(percent,100,300);
	apply_strip_hover_scale();
}
static void apply_cursor_size(void) {
	if(pointer && mothership_map_pointer_v1_get_version(pointer)>=MOTHERSHIP_MAP_POINTER_V1_SET_CURSOR_SIZE_SINCE_VERSION)
		mothership_map_pointer_v1_set_cursor_size(pointer,cursor_size);
}
void map_pointer_set_cursor_size(int size) {
	if(cursor_size==size)return;
	cursor_size=size;
	apply_cursor_size();
}
static void apply_node_strip(void) {
	if(pointer && mothership_map_pointer_v1_get_version(pointer)>=8)
		mothership_map_pointer_v1_set_node_strip(pointer,node_x,node_y,node_w,node_h,node_motion);
}
void panel_node_strip(int x,int y,int w,int h,gboolean motion) {
	if(node_x==x && node_y==y && node_w==w && node_h==h && node_motion==motion) return;
	node_x=x;node_y=y;node_w=w;node_h=h;node_motion=motion;apply_node_strip();
}
void panel_node_strip_pointer(double x,gboolean focus) {
	if(!pointer || mothership_map_pointer_v1_get_version(pointer)<8) return;
	if(focus) mothership_map_pointer_v1_node_strip_focus(pointer,wl_fixed_from_double(x));
	else mothership_map_pointer_v1_node_strip_hover(pointer,wl_fixed_from_double(x));
}

static void watch(void) {
	if(!pointer || !gtk_widget_get_mapped(GTK_WIDGET(command))) return;
	GdkSurface *surface=gtk_native_get_surface(GTK_NATIVE(command));
	if(!surface || !GDK_IS_WAYLAND_SURFACE(surface)) return;
	struct wl_surface *wl=gdk_wayland_surface_get_wl_surface(surface);
	if(!wl) return;
	mothership_map_pointer_v1_watch(pointer,wl);
	if(screenshot_supported()) mothership_map_pointer_v1_screenshot_auth(pointer);
	mothership_map_pointer_v1_set_zoom(pointer,zoom_enabled,zoom_margin);
	apply_aim();
	apply_video_frost();
	apply_node_strip();
	apply_picker_scale();
	apply_strip_hover_scale();
	apply_cursor_size();
}

static void command_mapped(GtkWidget *widget,gpointer data) {
	(void)widget;(void)data;
	watch();
}

static void global(void *data,struct wl_registry *r,uint32_t name,const char *interface,uint32_t version) {
	(void)data;(void)version;
	if(pointer || strcmp(interface,mothership_map_pointer_v1_interface.name)!=0) return;
	pointer=wl_registry_bind(r,name,&mothership_map_pointer_v1_interface,MIN(version,(uint32_t)mothership_map_pointer_v1_interface.version));
	if(pointer) {
		pointer_name=name;
		mothership_map_pointer_v1_add_listener(pointer,&events,NULL);
		watch();
	}
}

static void removed(void *data,struct wl_registry *r,uint32_t name) {
	(void)data;(void)r;
	if(pointer && name==pointer_name) {
		panel_map_contact(MOTHERSHIP_MAP_POINTER_V1_CONTACT_PHASE_CANCEL,0,0);
		panel_map_aim(FALSE,0,0);
		mothership_map_pointer_v1_destroy(pointer);
		pointer=NULL;
		glass_slots=0;
		panel_screenshot_available(FALSE);
		display_settings_available(FALSE);
		panel_picker_end(0);
		panel_node_switch_finish(0);
		panel_window_count(0);
	}
}

static const struct wl_registry_listener globals={.global=global,.global_remove=removed};

void map_pointer_init(GtkWindow *window) {
	command=window;
	GdkDisplay *display=gtk_widget_get_display(GTK_WIDGET(window));
	if(!GDK_IS_WAYLAND_DISPLAY(display)) return;
	registry=wl_display_get_registry(gdk_wayland_display_get_wl_display(display));
	if(registry) wl_registry_add_listener(registry,&globals,NULL);
	g_signal_connect(window,"map",G_CALLBACK(command_mapped),NULL);
}

void map_pointer_zoom_enabled(gboolean enabled,int margin) {
	if (zoom_enabled==enabled && zoom_margin==margin) return;
	zoom_enabled=enabled; zoom_margin=margin;
	watch();
}

void map_pointer_aim_enabled(gboolean enabled) {
	if(aim_enabled==enabled) return;
	aim_enabled=enabled;
	apply_aim();
}

void map_pointer_video_frost(gboolean enabled,int margin) {
	if(video_frost==enabled && frost_margin==margin) return;
	video_frost=enabled;frost_margin=margin;
	apply_video_frost();
}
