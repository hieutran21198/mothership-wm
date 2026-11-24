#ifndef MOTHERSHIP_PANEL_MAP_POINTER_H
#define MOTHERSHIP_PANEL_MAP_POINTER_H
#include <gtk/gtk.h>
void map_pointer_init(GtkWindow *window);
void map_pointer_zoom_enabled(gboolean enabled,int margin);
void panel_map_click(double x,double y);
void panel_map_contact(uint32_t phase,double x,double y);
void panel_map_scroll(double x,double y,double delta);
#endif
