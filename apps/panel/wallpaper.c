//go:build linux && cgo && panel

#include <gtk/gtk.h>
#include <stdbool.h>
#include <string.h>

#include "shell-paint.h"
#include "retained-paint.h"
#include "map-camera.h"
#include "map_hud.h"

static cairo_surface_t *wallpaper;
static GtkWidget *wallpaper_canvas, *wallpaper_accelerated;
static bool contain_image;
static gboolean frost_enabled;
static int frost_margin;

void wallpaper_update_frost(gboolean enabled, int outer_margin) {
	bool changed = frost_enabled != enabled || (enabled && frost_margin != outer_margin);
	frost_enabled = enabled;
	frost_margin = outer_margin;
	if (changed) retained_wallpaper_update(wallpaper_accelerated, frost_enabled, frost_margin);
}

GtkWidget *wallpaper_clock(void) { return wallpaper_canvas; }
struct png_stream {
	const unsigned char *data;
	size_t left;
};

static cairo_status_t read_png(void *data, unsigned char *dest, unsigned int len) {
	struct png_stream *png = data;
	if (len > png->left) return CAIRO_STATUS_READ_ERROR;
	memcpy(dest, png->data, len);
	png->data += len;
	png->left -= len;
	return CAIRO_STATUS_SUCCESS;
}

int wallpaper_set(const unsigned char *data, size_t size, int contain) {
	struct png_stream png = {data, size};
	cairo_surface_t *image = cairo_image_surface_create_from_png_stream(read_png, &png);
	g_clear_pointer(&wallpaper, cairo_surface_destroy);
	if (cairo_surface_status(image) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(image);
		return 0;
	}
	wallpaper = image;
	contain_image = contain != 0;
	return 1;
}

static void wallpaper_render(cairo_t *cr, int w, int h) {
	if (wallpaper != NULL) {
		int iw = cairo_image_surface_get_width(wallpaper);
		int ih = cairo_image_surface_get_height(wallpaper);
		struct map_wallpaper_fit fit=map_screen_wallpaper(w,h,iw,ih,contain_image);
		double scale=fit.width/iw,x=fit.x,y=fit.y;
		cairo_set_source_rgb(cr, .023, .047, .081);
		cairo_paint(cr);
		cairo_save(cr);
		cairo_rectangle(cr, x, y, iw * scale, ih * scale);
		cairo_clip(cr);
		cairo_translate(cr, x, y);
		cairo_scale(cr, scale, scale);
		cairo_set_source_surface(cr, wallpaper, 0, 0);
		cairo_pattern_set_extend(cairo_get_source(cr),CAIRO_EXTEND_PAD);
		cairo_pattern_set_filter(cairo_get_source(cr),CAIRO_FILTER_BILINEAR);
		cairo_paint(cr);
		cairo_restore(cr);

		return;
	}
	// A static procedural backdrop when a chosen wallpaper cannot be loaded.
	cairo_pattern_t *sky = cairo_pattern_create_linear(0, 0, w * .55, h);
	cairo_pattern_add_color_stop_rgb(sky, 0, .023, .047, .081);
	cairo_pattern_add_color_stop_rgb(sky, 1, .052, .087, .144);
	cairo_set_source(cr, sky);
	cairo_paint(cr);
	cairo_pattern_destroy(sky);
	for (unsigned i = 1; i < 55; ++i) {
		double x = ((i * 751U) % 997U) * w / 997.;
		double y = ((i * 359U) % 991U) * h / 991.;
		cairo_set_source_rgba(cr, .68, .79, .91, i % 7 == 0 ? .38 : .17);
		cairo_rectangle(cr, x, y, i % 7 == 0 ? 1.4 : .8, i % 7 == 0 ? 1.4 : .8);
		cairo_fill(cr);
	}
	cairo_pattern_t *planet = cairo_pattern_create_linear(0, h * .48, 0, h);
	cairo_pattern_add_color_stop_rgba(planet, 0, .20, .39, .56, .7);
	cairo_pattern_add_color_stop_rgb(planet, 1, .049, .092, .153);
	cairo_move_to(cr, 0, h * .99);
	cairo_curve_to(cr, w * .29, h * .72, w * .62, h * .41, w, h * .57);
	cairo_line_to(cr, w, h);
	cairo_line_to(cr, 0, h);
	cairo_close_path(cr);
	cairo_set_source(cr, planet);
	cairo_fill(cr);
	cairo_pattern_destroy(planet);
	cairo_move_to(cr, 0, h * .99);
	cairo_curve_to(cr, w * .29, h * .72, w * .62, h * .41, w, h * .57);
	cairo_set_source_rgba(cr, .42, .65, .79, .45);
	cairo_set_line_width(cr, 1.3);
	cairo_stroke(cr);
}

static void draw_wallpaper(GtkDrawingArea *area, cairo_t *cr, int w, int h, gpointer data) {
	(void)area; (void)data;
	wallpaper_render(cr, w, h);
}

static void viewport_resized(GtkDrawingArea *unused,int width,int height,gpointer data) {
	(void)unused; (void)data;
	map_hud_camera_viewport(width,height);
}

void wallpaper_show(void) {
	GtkWindow *window=paint_only_layer("mothership-wallpaper",
		"Mothership Wallpaper", draw_wallpaper, &wallpaper_canvas);
	g_signal_connect(wallpaper_canvas,"resize",G_CALLBACK(viewport_resized),NULL);
	if (wallpaper) {
		GtkWidget *overlay=gtk_overlay_new();
		g_object_ref(wallpaper_canvas);
		gtk_window_set_child(window, overlay);
		gtk_overlay_set_child(GTK_OVERLAY(overlay),wallpaper_canvas);
		g_object_unref(wallpaper_canvas);
		wallpaper_accelerated=retained_wallpaper_new(wallpaper,contain_image,wallpaper_canvas);
		gtk_overlay_add_overlay(GTK_OVERLAY(overlay),wallpaper_accelerated);
		retained_wallpaper_update(wallpaper_accelerated, frost_enabled, frost_margin);
	}
	gtk_widget_set_visible(GTK_WIDGET(window), TRUE);
}
