//go:build linux && cgo && panel

#include <gtk/gtk.h>
#include <librsvg/rsvg.h>
#include <math.h>
#include <string.h>
#include "map_hud.h"

#include "shell-paint.h"
#include "map-camera.h"
#include "retained-paint.h"

static GtkWindow *hud;
static GtkWidget *canvas, *annotations, *gpu_grid;
static char fps_label_next[32];
static GtkWidget *fps;
static guint fps_idle,fps_label_update;
static guint fps_frames;
static gboolean fps_enabled,fps_sample_known;
static gint64 fps_start,fps_latest;
static double fps_x,fps_y;
static GHashTable *icons, *slots;
static char **names;
static char *listed_names;
static char *active_name;
static gboolean in_overview, showing_map, frosted_map, gpu_grid_ready;
static int view_w=MAP_REFERENCE_W,view_h=MAP_REFERENCE_H,anchor_w,anchor_h;
static PangoFontDescription *label_font;
static GdkRGBA tint, grid_color, text_color, stroke_color, border_color;
static void queue_grid(void);
static const GdkRGBA amber = {0.97, 0.69, 0.34, 1};
static const GdkRGBA default_symbol = {237. / 255., 245. / 255., 251. / 255., 1};

static void source(cairo_t *cr, GdkRGBA color, double alpha) {
	cairo_set_source_rgba(cr, color.red, color.green, color.blue, alpha);
}

void map_hud_configure(const char *bg, double opacity, const char *grid,
		const char *text, const char *stroke, const char *border) {
	gdk_rgba_parse(&tint, bg);
	tint.alpha = opacity;
	gdk_rgba_parse(&grid_color, grid);
	gdk_rgba_parse(&text_color, text);
	gdk_rgba_parse(&stroke_color, stroke);
	gdk_rgba_parse(&border_color, border);
	queue_grid();
}

/* Resolve once when loading the theme. Pango still handles per-glyph fallback. */
int map_hud_set_font(const char *family, double size) {
    gboolean known = !g_ascii_strcasecmp(family, "Sans") ||
        !g_ascii_strcasecmp(family, "Sans-serif") ||
        !g_ascii_strcasecmp(family, "Serif") ||
        !g_ascii_strcasecmp(family, "Monospace");
    if (!known) {
        PangoFontFamily **families = NULL;
        int count = 0;
        pango_font_map_list_families(pango_cairo_font_map_get_default(), &families, &count);
        for (int i = 0; i < count && !known; i++)
            known = !g_ascii_strcasecmp(family, pango_font_family_get_name(families[i]));
        g_free(families);
    }
    g_clear_pointer(&label_font, pango_font_description_free);
    label_font = pango_font_description_new();
    pango_font_description_set_family(label_font, known ? family : "Sans");
    pango_font_description_set_weight(label_font, PANGO_WEIGHT_MEDIUM);
    pango_font_description_set_absolute_size(label_font, size * PANGO_SCALE);
    return known;
}

enum { SVG_ICON_HALF = 12, SVG_ICON_RASTER = 48 };
struct map_icon {
	RsvgHandle *image;
	cairo_surface_t *surface;
	int pixels;
};

static void map_icon_free(gpointer data) {
	struct map_icon *icon = data;
	g_clear_pointer(&icon->surface, cairo_surface_destroy);
	g_object_unref(icon->image);
	g_free(icon);
}

static gboolean map_icon_rasterize(struct map_icon *icon, int pixels) {
	if (icon->pixels == pixels) return TRUE;
	cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pixels, pixels);
	cairo_t *cr = cairo_create(surface);
	RsvgRectangle viewport = {0, 0, pixels, pixels};
	GError *err = NULL;
	gboolean ok = rsvg_handle_render_document(icon->image, cr, &viewport, &err);
	g_clear_error(&err);
	cairo_destroy(cr);
	if (!ok || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(surface);
		return FALSE;
	}
	g_clear_pointer(&icon->surface, cairo_surface_destroy);
	icon->surface = surface;
	icon->pixels = pixels;
	return TRUE;
}

int map_hud_icon(const char *name, const unsigned char *data, size_t size) {
	// No base URI for external files. Validate once, then cache just one
	// raster per SVG, with 2× sampling at larger zoom levels.
	GError *err = NULL;
	RsvgHandle *image = rsvg_handle_new_from_data(data, size, &err);
	if (image == NULL) {
		g_clear_error(&err);
		return 0;
	}
	struct map_icon *icon = g_new0(struct map_icon, 1);
	icon->image = image;
	if (!map_icon_rasterize(icon, SVG_ICON_RASTER)) {
		map_icon_free(icon);
		return 0;
	}
	if (icons == NULL) {
		icons = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, map_icon_free);
	}
	g_hash_table_replace(icons, g_strdup(name), icon);
	return 1;
}

struct plane { double x, y, width, height, step; };

static int outer_margin = 24, inner_padding = 12;
static struct map_camera camera;
static struct map_zoom map_zoom={.scale=1,.grid_x=40,.grid_y=38};
static struct {
	double from,to,x,y;
	int direction,animation;
	gint64 start,last;
	guint tick;
} zoom_motion;

static void queue_grid(void) {
	retained_grid_update(gpu_grid,grid_color,tint,outer_margin,inner_padding,
		map_zoom.scale,map_zoom.grid_x,map_zoom.grid_y,map_zoom.anchored);
}

static void camera_redraw(void) {
	if (showing_map && annotations) gtk_widget_queue_draw(annotations);
}

GtkWidget *map_hud_clock(void) { return wallpaper_clock(); }

static gboolean place_camera(struct map_camera *at,double x,double y) {
	struct map_camera next=map_tabletop_clamp((struct map_camera){x,y},view_w/map_zoom.scale,view_h/map_zoom.scale);
	if (at->x==next.x && at->y==next.y) return FALSE;
	*at=next;
	return TRUE;
}

void map_hud_camera_viewport(int width,int height) {
	if (width<120 || height<100) return;
	view_w=width;view_h=height;
	if (!anchor_w) { anchor_w=width;anchor_h=height; }
	if (place_camera(&camera,camera.x,camera.y)) camera_redraw();
}

int map_hud_outer_margin(void) { return outer_margin; }

static void apply_zoom(double x,double y,double to) {
	if (!map_zoom.anchored) map_zoom.grid_x=view_w<480?24:40;
	if (!map_zoom_to(&camera,&map_zoom,view_w,view_h,x,y,to)) return;
	fps_sample_known=FALSE;
	queue_grid();
	if (!gpu_grid_ready) gtk_widget_queue_draw(canvas);
	gtk_widget_queue_draw(annotations);
}

void map_hud_cancel_zoom(void) {
	if (!zoom_motion.tick) return;
	gtk_widget_remove_tick_callback(map_hud_clock(),zoom_motion.tick);
	zoom_motion.tick=0;
}

static gboolean advance_zoom(GtkWidget *widget,GdkFrameClock *clock,gpointer unused) {
	(void)widget;(void)unused;
	zoom_motion.last=gdk_frame_clock_get_frame_time(clock);
	double progress=(zoom_motion.last-zoom_motion.start)/(double)MAP_ZOOM_DURATION_US;
	apply_zoom(zoom_motion.x,zoom_motion.y,map_zoom_sample(zoom_motion.from,zoom_motion.to,progress,zoom_motion.animation));
	if (progress<1) return G_SOURCE_CONTINUE;
	zoom_motion.tick=0;
	return G_SOURCE_REMOVE;
}

void map_hud_zoom(double x,double y,double delta,int animation) {
	if (!showing_map || !isfinite(delta) || delta==0 ||
			!map_frame_contains(view_w,view_h,outer_margin,x,y)) return;
	if (animation==MAP_ANIMATION_OFF) {
		map_hud_cancel_zoom();
		apply_zoom(x,y,map_wheel_target(map_zoom.scale,delta));
		return;
	}
	int direction=delta<0 ? 1 : -1;
	// Accumulate wheel pulses in the same direction; reverse from the
	// displayed frame, not the unrendered destination of the last pulse.
	double base=zoom_motion.tick && zoom_motion.direction==direction ? zoom_motion.to : map_zoom.scale;
	double to=map_wheel_target(base,delta);
	if (to==base) return;
	zoom_motion.from=map_zoom.scale;
	zoom_motion.to=to;
	zoom_motion.x=x;
	zoom_motion.y=y;
	gint64 now=g_get_monotonic_time();
	// Bursts must not push the first frame farther away with every pulse.
	// Start at the last displayed sample while not borrowing a stale frame.
	zoom_motion.start=zoom_motion.tick ? CLAMP(zoom_motion.last,now-16000,now) : now;
	if (!zoom_motion.tick) zoom_motion.last=now;
	zoom_motion.animation=animation;
	zoom_motion.direction=direction;
	if (!zoom_motion.tick)
		zoom_motion.tick=gtk_widget_add_tick_callback(map_hud_clock(),advance_zoom,NULL,NULL);
}

void map_hud_translate(double x,double y) {
	if (place_camera(&camera,camera.x+x/map_zoom.scale,camera.y+y/map_zoom.scale)) camera_redraw();
}

void map_hud_set_spacing(const char *border, int outer, int inner, gboolean frosted) {
	gdk_rgba_parse(&border_color, border);
	outer_margin = outer;
	inner_padding = inner;
	frosted_map = frosted;
	wallpaper_update_frost(showing_map && frosted_map, outer_margin);
	queue_grid();
	if (canvas) gtk_widget_queue_draw(canvas);
	if (annotations) gtk_widget_queue_draw(annotations);
}

static void plane_outline(cairo_t *cr, struct plane p, double inset) {
	map_frame_outline(cr, p.x, p.y, p.width, p.height, inset);
}

static void draw_plane(cairo_t *cr, struct plane p, struct plane border, double content_inset) {
	if (gpu_grid_ready) goto outline;
	if (tint.alpha > 0) {
		plane_outline(cr, border, 0);
		source(cr, tint, tint.alpha);
		cairo_fill(cr);
	}
	cairo_save(cr);
	plane_outline(cr, border, content_inset);
	cairo_clip(cr);
	// Keep screen-space grid phase independent of the Map camera.
	cairo_set_line_width(cr, 1);
	source(cr, grid_color, .32);
	double pitch=p.step*map_zoom.scale;
	double ox=map_zoom.anchored?map_zoom.grid_x:p.x;
	double oy=map_zoom.anchored?map_zoom.grid_y:p.y;
	ox+=ceil((border.x-ox)/pitch)*pitch;
	oy+=ceil((border.y-oy)/pitch)*pitch;
	cairo_set_line_width(cr,map_zoom.scale);
	for (double x = ox; x < border.x + border.width; x += pitch) {
		cairo_move_to(cr, x + .5, border.y);
		cairo_line_to(cr, x + .5, border.y + border.height);
	}
	for (double y = oy; y < border.y + border.height; y += pitch) {
		cairo_move_to(cr, border.x, y + .5);
		cairo_line_to(cr, border.x + border.width, y + .5);
	}
	cairo_stroke(cr);
	cairo_restore(cr);
outline:
	plane_outline(cr, border, 0);
	source(cr, border_color, border_color.alpha);
	cairo_set_line_width(cr, 1);
	cairo_stroke(cr);
}

static struct plane layout_plane(int width, int height) {
	double inset = width < 480 ? 24 : 40;
	return (struct plane){inset, 38, width - inset * 2, height - 76, 44};
}

static struct plane node_plane(int w,int h) {
	return anchor_w?layout_plane(anchor_w,anchor_h):layout_plane(w,h);
}

static gboolean marker(struct plane p, const char *name, double *x, double *y) {
	unsigned at = slots == NULL ? 0 : GPOINTER_TO_UINT(g_hash_table_lookup(slots, name));
	if (at == 0) return FALSE;
	// Keep each slot stable when another row is added. The existing picker
	// still reaches Nodes outside the current fixed viewport.
	*x = p.x + p.width * (.14 + (at - 1) % 3 * .30);
	*y = p.y + MIN(p.height * .24, 82) + (at - 1) / 3 * 72;
	return TRUE;
}

static gboolean is_active(const char *name) {
	return g_strcmp0(name, active_name) == 0;
}

static void diamond(cairo_t *cr, double x, double y, double r) {
	cairo_move_to(cr, x, y - r);
	cairo_line_to(cr, x + r, y);
	cairo_line_to(cr, x, y + r);
	cairo_line_to(cr, x - r, y);
	cairo_close_path(cr);
}

static double symbol_half(void) {
	return SVG_ICON_HALF * map_zoom.scale;
}

static double annotation_shift(void) {
	return symbol_half() - SVG_ICON_HALF;
}

static void draw_symbol(cairo_t *cr, const char *name, double x, double y) {
	gboolean active = is_active(name);
	double half = symbol_half();
	struct map_icon *icon = icons == NULL ? NULL : g_hash_table_lookup(icons, name);
	if (icon != NULL) {
		// Keep the original 48px raster at or below 1×, and render
		// extra SVG detail instead of enlarging that raster above 1×.
		// Reuse one high-resolution raster over the animated range.
		// Do not rerasterize on each interpolated scale sample.
		int pixels = map_zoom.scale <= 1 ? SVG_ICON_RASTER : (int)ceil(SVG_ICON_RASTER * MAP_ZOOM_MAX);
		map_icon_rasterize(icon, pixels);
		cairo_save(cr);
		cairo_translate(cr, x - half, y - half);
		cairo_scale(cr, 2*half/icon->pixels, 2*half/icon->pixels);
		cairo_set_source_surface(cr, icon->surface, 0, 0);
		cairo_paint(cr);
		cairo_restore(cr);
		if (!active) return;
	} else {
		diamond(cr, x, y, 8 * map_zoom.scale);
		source(cr, active ? amber : default_symbol, 1);
		cairo_fill(cr);
	}
	if (active) {
		diamond(cr, x, y, half);
		source(cr, amber, .7);
		cairo_set_line_width(cr, 1);
		cairo_stroke(cr);
	}
}

static void draw_annotation(cairo_t *cr, struct plane p, const char *name, double x, double y) {
	const double base_reach = MAX(17, MIN(p.width * .056, 38));
	const double shift = annotation_shift(), reach = base_reach + shift;
	const gboolean active = is_active(name);
	cairo_move_to(cr, x + 9 + shift, y);
	cairo_line_to(cr, x + base_reach * .58 + shift, y - 6);
	cairo_line_to(cr, x + reach, y - 6);
	source(cr, active ? amber : stroke_color, .94);
	cairo_set_line_width(cr, 1);
	cairo_stroke(cr);
	PangoLayout *layout = pango_cairo_create_layout(cr);
	if (label_font == NULL) map_hud_set_font("Sans", 13);
	pango_layout_set_font_description(layout, label_font);
	pango_layout_set_text(layout, name, -1);
	pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
	pango_layout_set_width(layout, (int)MAX(8, MIN(p.width * .30 - reach - 8,
		p.x + p.width - x - reach - 7)) * PANGO_SCALE);
	int label_height;
	pango_layout_get_pixel_size(layout, NULL, &label_height);
	cairo_move_to(cr, x + reach + 6, y - 6 - label_height * .5);
	source(cr, active ? amber : text_color, 1);
	pango_cairo_show_layout(cr, layout);
	g_object_unref(layout);
}

static struct plane border_plane(int w,int h,double *inset) {
	int margin=map_frame_margin(w,h,outer_margin);
	int padding=MIN(inner_padding,(MIN(w,h)-2*margin-16)/2);
	*inset=padding+.5;
	return (struct plane){margin,margin,w-2*margin,h-2*margin,0};
}

static void draw(GtkDrawingArea *area, cairo_t *cr, int w, int h, gpointer data) {
	(void)area; (void)data;
	if (w<120 || h<100) return;
	double inset;
	struct plane border=border_plane(w,h,&inset);
	draw_plane(cr,layout_plane(w,h),border,inset);
}

typedef GtkWidget MapAnnotations;
typedef GtkWidgetClass MapAnnotationsClass;
G_DEFINE_TYPE(MapAnnotations,map_annotations,GTK_TYPE_WIDGET)

static void map_hud_rendered(double x,double y);

/* Two passes keep labels above every icon. */
static void annotations_snapshot(GtkWidget *widget,GtkSnapshot *snap) {
	int w=gtk_widget_get_width(widget),h=gtk_widget_get_height(widget);
	if(w<120||h<100) return;
	double inset;
	struct plane plane=node_plane(w,h),border=border_plane(w,h,&inset);
	graphene_rect_t screen=GRAPHENE_RECT_INIT(0,0,w,h);
	gboolean drew=FALSE;
	for (int pass=0;pass<2;pass++) {
		for(char **n=names;n&&*n;++n) {
			double x,y;
			if(!**n || !marker(plane,*n,&x,&y))continue;
			x=(x-camera.x)*map_zoom.scale;y=(y-camera.y)*map_zoom.scale;
			double radius = symbol_half() + 2;
			graphene_rect_t bounds=pass?
				GRAPHENE_RECT_INIT(x+7+annotation_shift(),y-40,MAX(56,plane.width*.30+8),68):
				GRAPHENE_RECT_INIT(x-radius,y-radius,2*radius,2*radius);
			graphene_rect_t clipped;
			if(!graphene_rect_intersection(&bounds,&screen,&clipped))continue;
			cairo_t *cr=gtk_snapshot_append_cairo(snap,&clipped);
			plane_outline(cr,border,inset);
			cairo_clip(cr);
			if(pass)draw_annotation(cr,plane,*n,x,y);
			else draw_symbol(cr,*n,x,y);
			cairo_destroy(cr);
			drew=TRUE;
		}
	}
	if (drew) map_hud_rendered(camera.x,camera.y);
}
static void map_annotations_class_init(MapAnnotationsClass *class) {
	GTK_WIDGET_CLASS(class)->snapshot=annotations_snapshot;
}
static void map_annotations_init(MapAnnotations *self) {
	gtk_widget_set_can_target(GTK_WIDGET(self),FALSE);
	gtk_widget_set_hexpand(GTK_WIDGET(self),TRUE);
	gtk_widget_set_vexpand(GTK_WIDGET(self),TRUE);
}

static gboolean fps_idle_check(gpointer unused) {
	(void)unused;
	fps_idle=0;
	gint64 remaining=350000-(g_get_monotonic_time()-fps_latest);
	if (remaining>0) {
		fps_idle=g_timeout_add((remaining+999)/1000,fps_idle_check,NULL);
		return G_SOURCE_REMOVE;
	}
	gtk_label_set_text(GTK_LABEL(fps),"Render FPS: idle");
	fps_frames=0;
	fps_start=0;
	return G_SOURCE_REMOVE;
}

static gboolean fps_label_apply(gpointer unused) {
	(void)unused;
	fps_label_update=0;
	if (fps_enabled) gtk_label_set_text(GTK_LABEL(fps),fps_label_next);
	return G_SOURCE_REMOVE;
}

static void map_hud_rendered(double x,double y) {
	if (!fps_enabled || (fps_sample_known && x==fps_x && y==fps_y)) return;
	fps_sample_known=TRUE;
	fps_x=x;fps_y=y;
	gint64 now=g_get_monotonic_time();
	fps_latest=now;
	if (!fps_idle) fps_idle=g_timeout_add(350,fps_idle_check,NULL);
	if (!fps_start) {
		fps_start=now;
		return;
	}
	fps_frames++;
	if (now-fps_start>=200000) {
		g_snprintf(fps_label_next,sizeof fps_label_next,"Render FPS: %.0f",
			fps_frames*1000000./(now-fps_start));
		if (!fps_label_update) fps_label_update=g_idle_add(fps_label_apply,NULL);
		fps_frames=0;
		fps_start=now;
	}
}

void map_hud_show_fps(gboolean enabled) {
	if (!fps || fps_enabled==enabled) return;
	fps_enabled=enabled;
	if (fps_idle) g_source_remove(fps_idle);
	if (fps_label_update) g_source_remove(fps_label_update);
	fps_idle=0;fps_label_update=0;
	fps_frames=0;
	fps_start=0;
	fps_sample_known=FALSE;
	gtk_label_set_text(GTK_LABEL(fps),"Render FPS: idle");
	gtk_widget_set_visible(fps,enabled && showing_map);
	gtk_widget_set_visible(GTK_WIDGET(hud),showing_map);
}

static void install_fps(GtkOverlay *overlay) {
	fps=gtk_label_new("Render FPS: idle");
	gtk_widget_set_can_target(fps,FALSE);
	gtk_widget_set_focusable(fps,FALSE);
	gtk_widget_set_halign(fps,GTK_ALIGN_END);
	gtk_widget_set_valign(fps,GTK_ALIGN_START);
	gtk_widget_set_margin_end(fps,24);
	gtk_widget_set_margin_top(fps,24);
	gtk_widget_add_css_class(fps,"mothership-render-fps");
	gtk_overlay_add_overlay(overlay,fps);
	gtk_widget_set_visible(fps,FALSE);
}

void map_hud_init(void) {
	annotations=g_object_new(map_annotations_get_type(),NULL);
	GtkWidget *overlay=gtk_overlay_new();
	hud=paint_only_layer("mothership-center-map","Mothership Center Map",draw,&canvas);
	g_object_ref(canvas);
	gtk_window_set_child(hud,overlay);
	gpu_grid=retained_grid_new(&gpu_grid_ready,canvas);
	gtk_overlay_set_child(GTK_OVERLAY(overlay),gpu_grid);
	gtk_widget_set_hexpand(canvas,TRUE);
	gtk_widget_set_vexpand(canvas,TRUE);
	gtk_overlay_add_overlay(GTK_OVERLAY(overlay),canvas);
	g_object_unref(canvas);
	gtk_overlay_add_overlay(GTK_OVERLAY(overlay),annotations);
	gtk_widget_set_visible(gpu_grid,FALSE);
	gtk_widget_set_visible(canvas,FALSE);
	gtk_widget_set_visible(annotations,FALSE);
	queue_grid();
	install_fps(GTK_OVERLAY(overlay));
}

void map_hud_update(const char *node_names, GHashTable *positions, const char *node,
		gboolean overview, gboolean visible) {
	gboolean changed = g_strcmp0(listed_names, node_names) != 0 || in_overview != overview;
	if (g_strcmp0(listed_names, node_names) != 0) {
		g_free(listed_names);
		listed_names = g_strdup(node_names);
		g_strfreev(names);
		names = g_strsplit(node_names, "\n", -1);
	}
	slots = positions;
	if (((node != NULL && *node) || !overview) && g_strcmp0(active_name, node) != 0) {
		g_free(active_name);
		active_name = g_strdup(node);
		changed = TRUE;
	}
	in_overview = overview;
	showing_map = visible;
	if (!visible) map_hud_cancel_zoom();
	wallpaper_update_frost(showing_map && frosted_map, outer_margin);
	if (hud == NULL) return;
	gboolean was_visible=gtk_widget_get_visible(annotations);
	if (visible!=was_visible) {
		gtk_widget_set_visible(annotations,visible);
		gtk_widget_set_visible(gpu_grid,visible);
		gtk_widget_set_visible(canvas,visible);
	}
	gtk_widget_set_visible(fps,fps_enabled && visible);
	gtk_widget_set_visible(GTK_WIDGET(hud),visible);
	if (visible && (changed || !was_visible)) gtk_widget_queue_draw(annotations);
}
