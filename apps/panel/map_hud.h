#ifndef MOTHERSHIP_MAP_HUD_H
#define MOTHERSHIP_MAP_HUD_H
#include <gtk/gtk.h>

void map_hud_init(void);
void map_hud_camera_viewport(int width,int height);
int map_hud_outer_margin(void);
void map_hud_zoom(double x,double y,double delta,int animation);
void map_hud_cancel_zoom(void);
void map_hud_open_effect(int animation);
void map_hud_finish_effect(void);
void map_hud_dismiss_effect(void);
gboolean map_hud_close_profile(void);
const char *map_hud_shortcut(int slot);
void map_hud_restore_selection(const char *name);
gboolean map_hud_relay_begin(const char *name,void (*finish)(void));
void map_hud_relay_ack(void);
void map_hud_relay_cancel(void);
void map_hud_retract(void (*finish)(void));
void map_hud_pause_route(void);
void map_hud_allow_route(gboolean enable);
gboolean map_hud_drag_inside(double x,double y);
gboolean map_hud_drag_can_start(double x,double y);
void map_hud_click(double x,double y,void (*fly)(const char *));
void map_hud_configure(const char *, double, const char *, const char *, const char *, const char *);
void map_hud_set_spacing(const char *, int outer, int inner, gboolean frosted);
int map_hud_set_font(const char *, double);
void *map_hud_icons_new(void);
void map_hud_icons_free(void *);
int map_hud_icon_add(void *,const char *,const unsigned char *,size_t);
void map_hud_icons_replace(void *);
void map_hud_update(const char *names, GHashTable *positions, const char *node, const char *profile_data,
	gboolean overview, gboolean visible);
void map_hud_show_fps(gboolean enabled);
GtkWidget *map_hud_clock(void);
void map_hud_translate(double x, double y);

#endif
