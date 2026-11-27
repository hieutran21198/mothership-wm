#ifndef MOTHERSHIP_HOME_PANEL_H
#define MOTHERSHIP_HOME_PANEL_H
#include <gtk/gtk.h>
#include "../../internal/wayland/map-frame.h"
enum home_action { HOME_MAP, HOME_APPS, HOME_CENTER, HOME_RELOAD, HOME_HANGAR, HOME_AUDIO, HOME_LOCK, HOME_TRAY, HOME_NOTIFICATIONS, HOME_ACTIONS };
enum { HOME_WIDTH_DEFAULT=840, HOME_WIDTH_MIN=520, HOME_WIDTH_MAX=1440, HOME_TELEMETRY_WIDTH_MAX=600, HOME_HEIGHT=SHELL_DOCK_HEIGHT, HOME_BOTTOM=SHELL_DOCK_BOTTOM };
enum { NODE_TELEMETRY_SCREEN_HEIGHT=51, NODE_TELEMETRY_FULL_WIDTH=380 };
// Home and Node dock buttons share one fixed face. A wing is sized to hold
// its faces; only an output too narrow for that wing compacts them.
enum { HOME_BUTTON_WIDTH=44, HOME_BUTTON_HEIGHT=36, HOME_BUTTON_GAP=8, HOME_BUTTON_INSET=13 };
struct home_wing_fit { int face,gap,inset; };
int home_wing_width(int faces);
struct home_wing_fit home_wing_fit(int wing,int faces);

GtkWidget *home_dock_new(void (*action)(enum home_action),void (*connect)(const char *),void (*region_changed)(void));
GtkWidget *home_dock_hangar(void);
void home_dock_hangar_toggle(void);
gboolean home_dock_hangar_close(void);
void home_dock_set_nodes(const char *);
void home_dock_layout(int output_width,int output_height,int wanted_width,gboolean map,gboolean apps,gboolean apps_open,gboolean reload,gboolean tray);
int home_dock_center_width(void);
int home_dock_notice_center(void);
GtkWidget *home_dock_notice_button(void);
GtkWidget *home_dock_tray_button(void);
int home_dock_tray_center(void);
int home_dock_systems_center(void);
GtkWidget *home_dock_systems_button(void);
void home_dock_systems_open(gboolean);
void home_dock_systems_mute(gboolean output,gboolean input);
void home_dock_tray_open(gboolean open);
void home_tray_glyph(cairo_t *);
void home_dock_notice_open(gboolean);
void home_dock_notice_count(int);
int home_dock_telemetry_center(void);
int home_dock_telemetry_width(void);
void home_slash_face(GtkDrawingArea *,cairo_t *,int,int,gpointer);
void home_dock_sample(int,const int *,int,double,double,double,double,gboolean,int,int,const char *,const char *);
void home_apps_glyph(cairo_t *);
void home_map_glyph(cairo_t *);
void home_mechanical_button(GtkWidget *,gboolean primary);
void home_node_dock_face(cairo_t *,int w,int tray_x,int tray_width);
GtkWidget *home_node_telemetry_new(void);
#endif
