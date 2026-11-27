#ifndef MOTHERSHIP_MAP_GUIDE_H
#define MOTHERSHIP_MAP_GUIDE_H
#include <gtk/gtk.h>

enum map_guide_input {
	MAP_GUIDE_A=1, MAP_GUIDE_D=2, MAP_GUIDE_W=4, MAP_GUIDE_S=8,
	MAP_GUIDE_SCROLL=16, MAP_GUIDE_GRAB=32, MAP_GUIDE_SPACE=64,
};
enum { MAP_GUIDE_COMPACT_WIDTH=148, MAP_GUIDE_WIDTH=267, MAP_GUIDE_HEIGHT=52 };
void map_guide_draw(cairo_t *,int width,unsigned);
void app_grid_guide_draw(cairo_t *,int width,gboolean results_focused);
enum { HOME_GUIDE_CELL_WIDTH=23, HOME_GUIDE_ROW_HEIGHT=22, HOME_GUIDE_INSET=9 };
// Home keys: A M W / , R N L; W only with the Default Node enabled.
static inline int home_guide_items(gboolean default_node) { return default_node?8:7; }
int home_guide_height(int width,gboolean default_node);
void home_guide_draw(cairo_t *,int width,gboolean default_node);
void slash_guide_draw(cairo_t *,int);
void notification_log_guide_draw(cairo_t *,int);
void mission_log_guide_draw(cairo_t *,int);
#endif
