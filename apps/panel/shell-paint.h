#ifndef MOTHERSHIP_SHELL_PAINT_H
#define MOTHERSHIP_SHELL_PAINT_H

#include <gtk/gtk.h>
#include "../../internal/wayland/map-frame.h"

GtkWindow *paint_only_layer(const char *name, const char *title,
	GtkDrawingAreaDrawFunc draw, GtkWidget **canvas);

// Grow toward the outside so the old interior painted edge and grid gap
// remain unchanged on straight segments and on the 45-degree corners.
#define MAP_PERIMETER_WIDTH 1.5
#define MAP_PERIMETER_INSET ((1.0-MAP_PERIMETER_WIDTH)*.5)

static inline double map_frame_cut_at(double inset) {
	return MAX(0,MAP_FRAME_CUT-inset*(2-G_SQRT2));
}

#define MAP_INNER_WIDTH 1.0
#define MAP_INNER_OFFSET 5.0
#define MAP_INNER_INSET (MAP_PERIMETER_INSET + MAP_INNER_OFFSET)

// Keep the inner contour inside, even when a theme asks for zero padding.
static inline double map_frame_content_inset(double inset) {
	return MAX(inset, MAP_INNER_INSET + MAP_INNER_WIDTH*.5 + .5);
}

static inline GdkRGBA map_frame_inner_color(const GdkRGBA *outer) {
	// The default steel #7895A5 gives #263F4C; carry custom RGB and alpha.
	return (GdkRGBA){outer->red*(38./120.),outer->green*(63./149.),
		outer->blue*(76./165.),outer->alpha};
}

// Match the retained repeating tile and Cairo fallback.
#define MAP_GRID_LINE_ALPHA .32
#define MAP_GRID_JUNCTION_RADIUS 1.25
static inline GdkRGBA map_grid_junction(GdkRGBA grid) {
	return (GdkRGBA){grid.red*.42+.50*.58,grid.green*.42+.83*.58,
		grid.blue*.42+.88*.58,.58};
}

static inline void map_frame_outline(cairo_t *cr, double x, double y, double w, double h, double inset) {
	double l = x + .5 + inset, t = y + .5 + inset;
	double r = x + w - .5 - inset, b = y + h - .5 - inset;
	double cut = map_frame_cut_at(inset);
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

static inline void map_frame_strokes(cairo_t *cr,double x,double y,double w,double h,const GdkRGBA *outer) {
	const GdkRGBA inner=map_frame_inner_color(outer);
	map_frame_outline(cr,x,y,w,h,MAP_PERIMETER_INSET);
	cairo_set_source_rgba(cr,outer->red,outer->green,outer->blue,outer->alpha);
	cairo_set_line_width(cr,MAP_PERIMETER_WIDTH);
	cairo_stroke(cr);
	map_frame_outline(cr,x,y,w,h,MAP_INNER_INSET);
	cairo_set_source_rgba(cr,inner.red,inner.green,inner.blue,inner.alpha);
	cairo_set_line_width(cr,MAP_INNER_WIDTH);
	cairo_stroke(cr);
}

GtkWidget *wallpaper_clock(void);
void wallpaper_update_frost(gboolean enabled, int outer_margin);

void wallpaper_add_guide(GtkWidget *guide);

#endif
