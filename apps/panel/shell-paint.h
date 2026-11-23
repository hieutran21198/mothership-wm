#ifndef MOTHERSHIP_SHELL_PAINT_H
#define MOTHERSHIP_SHELL_PAINT_H

#include <gtk/gtk.h>
#include "../../internal/wayland/map-frame.h"

GtkWindow *paint_only_layer(const char *name, const char *title,
	GtkDrawingAreaDrawFunc draw, GtkWidget **canvas);

/* One physical outline for the HUD and a matching clip in the wallpaper. */
static inline int map_frame_margin(int w, int h, int margin) {
	return map_frame_effective_margin(w,h,margin);
}

static inline void map_frame_outline(cairo_t *cr, double x, double y, double w, double h, double inset) {
	double l = x + .5 + inset, t = y + .5 + inset;
	double r = x + w - .5 - inset, b = y + h - .5 - inset;
	double cut = MAX(0, MAP_FRAME_CUT - inset * (2 - G_SQRT2));
	cairo_new_path(cr);
	cairo_move_to(cr, l + cut, t);
	cairo_line_to(cr, r - cut, t);
	cairo_line_to(cr, r, t + cut);
	cairo_line_to(cr, r, b - cut);
	cairo_line_to(cr, r - cut, b);
	cairo_line_to(cr, l + cut, b);
	cairo_line_to(cr, l, b - cut);
	cairo_line_to(cr, l, t + cut);
	cairo_close_path(cr);
}

GtkWidget *wallpaper_clock(void);
void wallpaper_update_frost(gboolean enabled, int outer_margin);

#endif
