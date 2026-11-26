//go:build linux && cgo && panel

#include <gtk/gtk.h>
#include <stdbool.h>
#include <string.h>

#include "shell-paint.h"
#include "retained-paint.h"
#include "map-camera.h"
#include "map_hud.h"
#include "wallpaper-video.h"
#include "map-pointer.h"
#include "analog-glass.h"

static cairo_surface_t *wallpaper;
static GtkWidget *wallpaper_canvas, *wallpaper_accelerated, *video_picture, *wallpaper_overlay;
static GtkWidget *pending_guide;
static struct wallpaper_video *video;
static gboolean video_contain;
static GdkTexture *glass_texture;
static GdkPaintable *glass_video_source;
static gulong glass_video_tick;
static bool contain_image;
static gboolean frost_enabled;
static int frost_margin;

void wallpaper_update_frost(gboolean enabled, int outer_margin) {
	bool changed = frost_enabled != enabled || (enabled && frost_margin != outer_margin);
	frost_enabled = enabled;
	frost_margin = outer_margin;
	map_pointer_video_frost(video && frost_enabled,frost_margin);
	if (changed && !video) retained_wallpaper_update(wallpaper_accelerated, frost_enabled, frost_margin);
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

void *wallpaper_prepare(const unsigned char *data,size_t size) {
	struct png_stream png = {data, size};
	cairo_surface_t *image = cairo_image_surface_create_from_png_stream(read_png, &png);
	if (cairo_surface_status(image) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(image);
		return NULL;
	}
	return image;
}

static void video_glass_frame(GdkPaintable *source,gpointer data) {
	(void)source;(void)data;analog_glass_wallpaper_changed();
}
static void unwatch_glass_video(void) {
	if(glass_video_source) {
		g_signal_handler_disconnect(glass_video_source,glass_video_tick);
		g_clear_object(&glass_video_source);
	}
}
static void stop_video(void) {
	if (!video) return;
	unwatch_glass_video();
	if (video_picture) {
		gtk_widget_set_visible(video_picture,FALSE);
		gtk_picture_set_paintable(GTK_PICTURE(video_picture),NULL);
		gtk_widget_set_visible(wallpaper_accelerated,TRUE);
		retained_wallpaper_update(wallpaper_accelerated,frost_enabled,frost_margin);
	}
	g_clear_pointer(&video,wallpaper_video_free);
	map_pointer_video_frost(FALSE,frost_margin);
	if (wallpaper_canvas) gtk_widget_queue_draw(wallpaper_canvas);
}

static void show_video(void) {
	gtk_picture_set_content_fit(GTK_PICTURE(video_picture),
		video_contain?GTK_CONTENT_FIT_CONTAIN:GTK_CONTENT_FIT_COVER);
	gtk_widget_set_visible(wallpaper_accelerated,FALSE);
	gtk_widget_set_visible(video_picture,TRUE);
	wallpaper_video_attach(video,GTK_PICTURE(video_picture),stop_video);
	GdkPaintable *source=gtk_picture_get_paintable(GTK_PICTURE(video_picture));
	if(source) {
		glass_video_source=g_object_ref(source);
		glass_video_tick=g_signal_connect(source,"invalidate-contents",G_CALLBACK(video_glass_frame),NULL);
	}
	analog_glass_wallpaper_changed();
	gtk_widget_queue_draw(wallpaper_canvas);
}

void *wallpaper_prepare_mp4(const char *path) {
	return wallpaper_video_prepare(path);
}

void wallpaper_discard_prepared(void *prepared,int mp4) {
	if (mp4) wallpaper_video_free(prepared);
	else if (prepared) cairo_surface_destroy(prepared);
}

void wallpaper_replace_mp4(void *prepared,int contain) {
	stop_video();
	video=prepared;
	map_pointer_video_frost(video && frost_enabled,frost_margin);
	video_contain=contain!=0;
	if (video_picture) show_video();
}

void wallpaper_replace(void *prepared,int contain) {
	stop_video();
	g_clear_pointer(&wallpaper,cairo_surface_destroy);
	wallpaper=prepared;
	g_clear_object(&glass_texture);
	analog_glass_wallpaper_changed();
	contain_image=contain!=0;
	retained_wallpaper_source(wallpaper_accelerated,wallpaper,contain_image);
	if (wallpaper_canvas) gtk_widget_queue_draw(wallpaper_canvas);
}

int wallpaper_set(const unsigned char *data,size_t size,int contain) {
	void *image=wallpaper_prepare(data,size);
	if (!image) return 0;
	wallpaper_replace(image,contain);
	return 1;
}

static void wallpaper_render(cairo_t *cr, int w, int h) {
	if (video) {cairo_set_source_rgb(cr,0,0,0);cairo_paint(cr);return;}
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

void wallpaper_add_guide(GtkWidget *guide) {
	// This layer is below app surfaces and has an empty input region.
	if(wallpaper_overlay) {
		gtk_overlay_add_overlay(GTK_OVERLAY(wallpaper_overlay),guide);
	} else {
		g_set_object(&pending_guide,guide);
	}
}

void wallpaper_show(void) {
	GtkWindow *window=paint_only_layer("mothership-wallpaper",
		"Mothership Wallpaper", draw_wallpaper, &wallpaper_canvas);
	g_signal_connect(wallpaper_canvas,"resize",G_CALLBACK(viewport_resized),NULL);
	GtkWidget *overlay=wallpaper_overlay=gtk_overlay_new();
	g_object_ref(wallpaper_canvas);
	gtk_window_set_child(window, overlay);
	gtk_overlay_set_child(GTK_OVERLAY(overlay),wallpaper_canvas);
	g_object_unref(wallpaper_canvas);
	wallpaper_accelerated=retained_wallpaper_new(wallpaper,contain_image,wallpaper_canvas);
	gtk_overlay_add_overlay(GTK_OVERLAY(overlay),wallpaper_accelerated);
	retained_wallpaper_update(wallpaper_accelerated, frost_enabled, frost_margin);
	video_picture=gtk_picture_new();
	gtk_picture_set_can_shrink(GTK_PICTURE(video_picture),TRUE);
	gtk_widget_set_can_target(video_picture,FALSE);
	gtk_widget_set_visible(video_picture,FALSE);
	gtk_overlay_add_overlay(GTK_OVERLAY(overlay),video_picture);
	if(pending_guide) {
		wallpaper_add_guide(pending_guide);
		g_clear_object(&pending_guide);
	}
	gtk_widget_set_visible(GTK_WIDGET(window), TRUE);
	if (video) show_video();
}

void wallpaper_analog_backdrop(GtkSnapshot *s,int sw,int sh,double x,double y,int w,int h,GskPath *cut) {
	if(sw<=0||sh<=0) return;
	if(!video && wallpaper && !glass_texture) glass_texture=wallpaper_image_texture(wallpaper);
	GdkPaintable *image=video?glass_video_source:GDK_PAINTABLE(glass_texture);
	if(!image) return;
	int iw=gdk_paintable_get_intrinsic_width(image),ih=gdk_paintable_get_intrinsic_height(image);
	if(iw<1||ih<1) return;
	struct map_wallpaper_fit fit=map_screen_wallpaper(sw,sh,iw,ih,video?video_contain:contain_image);
	gtk_snapshot_push_fill(s,cut,GSK_FILL_RULE_WINDING);
	gtk_snapshot_push_blur(s,10);
	gtk_snapshot_push_clip(s,&GRAPHENE_RECT_INIT(-24,-24,w+48,h+48));
	gtk_snapshot_save(s);
	gtk_snapshot_translate(s,&GRAPHENE_POINT_INIT(fit.x-x,fit.y-y));
	gdk_paintable_snapshot(image,GDK_SNAPSHOT(s),fit.width,fit.height);
	gtk_snapshot_restore(s);
	gtk_snapshot_pop(s);
	gtk_snapshot_pop(s);
	gtk_snapshot_pop(s);
}
