#ifndef MOTHERSHIP_MAP_HUD_H
#define MOTHERSHIP_MAP_HUD_H
#include <gtk/gtk.h>

void map_hud_init(void);
void map_hud_camera_viewport(int width,int height);
int map_hud_outer_margin(void);
void map_hud_zoom(double x,double y,double delta,int animation);
void map_hud_cancel_zoom(void);
void map_hud_configure(const char *, double, const char *, const char *, const char *, const char *);
void map_hud_set_spacing(const char *, int outer, int inner, gboolean frosted);
int map_hud_set_font(const char *, double);
int map_hud_icon(const char *, const unsigned char *, size_t);
void map_hud_update(const char *names, GHashTable *positions, const char *node,
	gboolean overview, gboolean visible);
void map_hud_show_fps(gboolean enabled);
GtkWidget *map_hud_clock(void);
void map_hud_translate(double x, double y);

#endif
