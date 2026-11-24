//go:build linux && cgo && panel

#include <gdk/wayland/gdkwayland.h>
#include <string.h>
#include "generated-map-pointer.h"
#include "map-pointer.h"

static GtkWindow *command;
static struct wl_registry *registry;
static struct mothership_map_pointer_v1 *pointer;
static gboolean zoom_enabled;
static int zoom_margin;
static uint32_t pointer_name;

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

static const struct mothership_map_pointer_v1_listener events={.zoom=zoom,.click=click,.contact=contact};

static void watch(void) {
	if(!pointer || !gtk_widget_get_mapped(GTK_WIDGET(command))) return;
	GdkSurface *surface=gtk_native_get_surface(GTK_NATIVE(command));
	if(!surface || !GDK_IS_WAYLAND_SURFACE(surface)) return;
	struct wl_surface *wl=gdk_wayland_surface_get_wl_surface(surface);
	if(!wl) return;
	mothership_map_pointer_v1_watch(pointer,wl);
	mothership_map_pointer_v1_set_zoom(pointer,zoom_enabled,zoom_margin);
}

static void command_mapped(GtkWidget *widget,gpointer data) {
	(void)widget;(void)data;
	watch();
}

static void global(void *data,struct wl_registry *r,uint32_t name,const char *interface,uint32_t version) {
	(void)data;(void)version;
	if(pointer || strcmp(interface,mothership_map_pointer_v1_interface.name)!=0) return;
	pointer=wl_registry_bind(r,name,&mothership_map_pointer_v1_interface,MIN(version,3));
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
		mothership_map_pointer_v1_destroy(pointer);
		pointer=NULL;
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
