//go:build linux && cgo && panel

#include <gtk/gtk.h>
#include <librsvg/rsvg.h>
#include <math.h>
#include <string.h>
#include "map_hud.h"
#include "map-profile.h"
#include "map-intro.h"
#include "map-radar.h"

#include "shell-paint.h"
#include "map-camera.h"
#include "retained-paint.h"

static GtkWindow *hud;
static GtkWidget *canvas, *annotations, *gpu_grid, *intro_layer;
static char fps_label_next[32];
static GtkWidget *fps;
static guint fps_idle,fps_label_update;
static guint fps_frames;
static gboolean fps_enabled,fps_sample_known;
static gint64 fps_start,fps_latest;
static double fps_x,fps_y;
static GHashTable *icons, *slots, *intro_labels;
static char **names;
static char *listed_names;
static char *active_name, *profile_node, *profile_rows;
static GHashTable *profile_data;
static guint profile_refresh;
static gboolean in_overview, showing_map, frosted_map, gpu_grid_ready;
static int view_w=MAP_REFERENCE_W,view_h=MAP_REFERENCE_H,anchor_w,anchor_h;
static PangoFontDescription *label_font;
static GdkRGBA tint, grid_color, text_color, stroke_color, border_color;
static void queue_grid(void);

static void clear_intro_labels(void) {
	if (intro_labels) g_hash_table_remove_all(intro_labels);
}
static const GdkRGBA amber = {0.97, 0.69, 0.34, 1};
static const GdkRGBA selected_outline = {.43,.85,.94,1};
static const GdkRGBA selected_text = {.82,.98,1,1};


static void source(cairo_t *cr, GdkRGBA color, double alpha) {
	cairo_set_source_rgba(cr, color.red, color.green, color.blue, alpha);
}

void map_hud_configure(const char *bg, double opacity, const char *grid,
		const char *text, const char *stroke, const char *border) {
	gdk_rgba_parse(&tint, bg);
	tint.alpha = opacity;
	clear_intro_labels();
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
    clear_intro_labels();
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
static struct map_zoom map_zoom={.scale=1};
static guint intro_tick;
static gint64 intro_start, intro_elapsed, intro_scan_us;
static gboolean intro_suppressed,native_radar,radar_tried;
static struct {
	double from,to,x,y;
	int direction,animation;
	gint64 start,last;
	guint tick;
} zoom_motion;

static void queue_grid(void) {
	retained_grid_update(gpu_grid,grid_color,tint,outer_margin,inner_padding);
}

static void camera_redraw(void) {
	clear_intro_labels();
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
	if (!map_zoom_to(&camera,&map_zoom,view_w,view_h,x,y,to)) return;
	fps_sample_known=FALSE;
	camera_redraw();
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
	clear_intro_labels();
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
	double pitch=p.step;
	double ox=p.x,oy=p.y;
	ox+=ceil((border.x-ox)/pitch)*pitch;
	oy+=ceil((border.y-oy)/pitch)*pitch;
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
	plane_outline(cr, border, MAP_PERIMETER_INSET);
	source(cr, border_color, border_color.alpha);
	cairo_set_line_width(cr, MAP_PERIMETER_WIDTH);
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

static gboolean is_selected(const char *name) {
	return profile_node && g_strcmp0(name,profile_node)==0;
}

static void diamond(cairo_t *cr, double x, double y, double r) {
	cairo_move_to(cr, x, y - r);
	cairo_line_to(cr, x + r, y);
	cairo_line_to(cr, x, y + r);
	cairo_line_to(cr, x - r, y);
	cairo_close_path(cr);
}

static void default_marker(cairo_t *cr,double x,double y,double r,gboolean active,gboolean selected) {
	const GdkRGBA muted_edge = {0x91/255.,0xad/255.,0xbd/255.,1};
	const GdkRGBA focus_edge = {0x75/255.,0xe3/255.,0xdd/255.,1};
	const GdkRGBA focus_center = {0x57/255.,0xd4/255.,0xd2/255.,1};
	if (!selected) {
		diamond(cr,x,y,r*16/26);
		source(cr,active?amber:muted_edge,active?.82:.64);
		cairo_set_line_width(cr,1);
		cairo_stroke(cr);
		return;
	}
	// Paint only the outlines: the gaps and hollow center reveal the map.
	diamond(cr,x,y,r);
	source(cr,focus_edge,.94);
	cairo_set_line_width(cr,1.2);
	cairo_stroke(cr);
	cairo_save(cr);
	cairo_set_fill_rule(cr,CAIRO_FILL_RULE_EVEN_ODD);
	diamond(cr,x,y,r*16/26);
	diamond(cr,x,y,r*7/26);
	source(cr,focus_center,1);
	cairo_fill(cr);
	cairo_restore(cr);
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
	gboolean selected = is_selected(name);
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
	} else {
		default_marker(cr,x,y,half,active,selected);
		return;
	}
	if (active && !selected) {
		diamond(cr, x, y, half);
		source(cr, amber, .7);
		cairo_set_line_width(cr, 1);
		cairo_stroke(cr);
	}
	if (selected) {
		double r=half+4,c=5;
		cairo_move_to(cr,x-r+c,y-r); cairo_line_to(cr,x+r-c,y-r);
		cairo_line_to(cr,x+r,y-r+c); cairo_line_to(cr,x+r,y+r-c);
		cairo_line_to(cr,x+r-c,y+r); cairo_line_to(cr,x-r+c,y+r);
		cairo_line_to(cr,x-r,y+r-c); cairo_line_to(cr,x-r,y-r+c);
		cairo_close_path(cr);
		source(cr,selected_outline,.94);
		cairo_set_line_width(cr,1.1);
		cairo_stroke(cr);
	}
}

struct annotation_geometry {double start,bend,reach;};

static struct annotation_geometry annotation_geometry(struct plane p,const char *name) {
	double base=MAX(17,MIN(p.width*.056,38)),shift=annotation_shift();
	struct annotation_geometry g={9+shift,base*.58+shift,base+shift};
	if (is_selected(name)) {
		g.start=symbol_half()+5;
		g.bend=MAX(g.bend,g.start+4);
		g.reach=MAX(g.reach,g.bend+4);
	}
	return g;
}

static void annotation_layout(PangoLayout *layout,struct plane p,const char *name,double x,double reach) {
	if (label_font == NULL) map_hud_set_font("Sans", 13);
	pango_layout_set_font_description(layout, label_font);
	pango_layout_set_text(layout, name, -1);
	pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
	pango_layout_set_width(layout, (int)MAX(8, MIN(p.width * .30 - reach - 8,
		p.x + p.width - x - reach - 7)) * PANGO_SCALE);
}

static void annotation_measure(struct plane p,const char *name,double x,double reach,int *w,int *h) {
	PangoContext *context=pango_font_map_create_context(pango_cairo_font_map_get_default());
	PangoLayout *layout=pango_layout_new(context);
	g_object_unref(context);
	annotation_layout(layout,p,name,x,reach);
	pango_layout_get_pixel_size(layout,w,h);
	g_object_unref(layout);
}

static void draw_leader(cairo_t *cr,struct annotation_geometry g,const char *name,
		double x,double y,double progress) {
	if (progress<=0) return;
	const gboolean active=is_active(name),selected=is_selected(name);
	double first=hypot(g.bend-g.start,6),length=first+g.reach-g.bend;
	double drawn=length*progress;
	cairo_move_to(cr,x+g.start,y);
	if (drawn<first) {
		double t=drawn/first;
		cairo_line_to(cr,x+g.start+(g.bend-g.start)*t,y-6*t);
	} else {
		cairo_line_to(cr,x+g.bend,y-6);
		cairo_line_to(cr,x+g.bend+drawn-first,y-6);
	}
	source(cr,selected?selected_outline:active?amber:stroke_color,.94);
	cairo_set_line_width(cr,1);
	cairo_stroke(cr);
}

static double draw_label(cairo_t *cr,struct plane p,const char *name,double x,double y,double reach) {
	const gboolean active=is_active(name),selected=is_selected(name);
	PangoLayout *layout=pango_cairo_create_layout(cr);
	annotation_layout(layout,p,name,x,reach);
	PangoRectangle ink,logical;
	pango_layout_get_pixel_extents(layout,&ink,&logical);
	cairo_move_to(cr,x+reach+6,y-6-logical.height*.5);
	source(cr,selected?selected_text:active?amber:text_color,1);
	pango_cairo_show_layout(cr,layout);
	g_object_unref(layout);
	return MAX(0,ink.x+ink.width+2);
}

static void draw_annotation(cairo_t *cr,struct plane p,const char *name,double x,double y) {
	struct annotation_geometry g=annotation_geometry(p,name);
	draw_leader(cr,g,name,x,y,1);
	draw_label(cr,p,name,x,y,g.reach);
}

struct intro_label {
	GskRenderNode *node;
	double width;
};

static void intro_label_free(gpointer data) {
	struct intro_label *label=data;
	g_clear_pointer(&label->node,gsk_render_node_unref);
	g_free(label);
}

static void snapshot_intro_label(GtkSnapshot *snap,const graphene_rect_t *bounds,
		struct plane plane,struct plane border,double inset,
		const char *name,double x,double y,double reach,double progress) {
	if (progress<=0) return;
	if (!intro_labels)
		intro_labels=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,intro_label_free);
	struct intro_label *label=g_hash_table_lookup(intro_labels,name);
	double left=x+reach+6;
	if (!label) {
		label=g_new0(struct intro_label,1);
		GtkSnapshot *retained=gtk_snapshot_new();
		cairo_t *cr=gtk_snapshot_append_cairo(retained,bounds);
		plane_outline(cr,border,inset);
		cairo_clip(cr);
		label->width=draw_label(cr,plane,name,x,y,reach)+2;
		cairo_destroy(cr);
		label->node=gtk_snapshot_free_to_node(retained);
		g_hash_table_insert(intro_labels,g_strdup(name),label);
	}
	if (!label->node) return;
	graphene_rect_t wipe=GRAPHENE_RECT_INIT(left-2,bounds->origin.y,label->width*progress,bounds->size.height);
	gtk_snapshot_push_clip(snap,&wipe);
	gtk_snapshot_append_node(snap,label->node);
	gtk_snapshot_pop(snap);
}

static struct plane border_plane(int w,int h,double *inset) {
	int margin=map_frame_margin(w,h,outer_margin);
	int padding=MIN(inner_padding,(MIN(w,h)-2*margin-16)/2);
	*inset=padding+.5;
	return (struct plane){margin,margin,w-2*margin,h-2*margin,0};
}

static void sync_layers(void) {
	if (!intro_layer) return;
	gboolean paint=showing_map && !intro_suppressed;
	gboolean intro=paint && intro_tick!=0;
	gint64 reveal_start=MAP_INTRO_FRAME_US+intro_scan_us;
	gboolean frame=paint && (!intro || intro_elapsed>=MAP_INTRO_FRAME_US);
	gboolean reveal=intro && intro_elapsed>=reveal_start;
	gtk_widget_set_visible(canvas,frame);
	gtk_widget_set_visible(gpu_grid,frame);
	gtk_widget_set_visible(annotations,paint && (!intro || reveal));
	gtk_widget_set_visible(intro_layer,intro && !reveal);
	gtk_widget_set_visible(fps,fps_enabled && paint && !intro);
	gtk_widget_set_visible(GTK_WIDGET(hud),paint);
}

typedef GtkWidget MapOpening;
typedef GtkWidgetClass MapOpeningClass;
G_DEFINE_TYPE(MapOpening,map_opening,GTK_TYPE_WIDGET)

static void opening_snapshot(GtkWidget *widget,GtkSnapshot *snap) {
	if (native_radar && intro_elapsed>=MAP_INTRO_FRAME_US) return;
	int w=gtk_widget_get_width(widget),h=gtk_widget_get_height(widget);
	if (w<120 || h<100) return;
	double inset;
	struct plane frame=border_plane(w,h,&inset);
	graphene_rect_t bounds=GRAPHENE_RECT_INIT(frame.x,frame.y,frame.width,frame.height);
	map_intro_snapshot(snap,bounds,intro_elapsed,intro_scan_us,&border_color);
}
static void map_opening_class_init(MapOpeningClass *class) {
	GTK_WIDGET_CLASS(class)->snapshot=opening_snapshot;
}
static void map_opening_init(MapOpening *self) {
	gtk_widget_set_can_target(GTK_WIDGET(self),FALSE);
	gtk_widget_set_hexpand(GTK_WIDGET(self),TRUE);
	gtk_widget_set_vexpand(GTK_WIDGET(self),TRUE);
}

static gboolean intro_frame(GtkWidget *widget,GdkFrameClock *clock,gpointer data) {
	(void)widget;(void)data;
	gint64 now=gdk_frame_clock_get_frame_time(clock);
	if (!intro_start) intro_start=now;
	intro_elapsed=now-intro_start;
	if (intro_elapsed>=MAP_INTRO_FRAME_US+intro_scan_us+MAP_INTRO_REVEAL_US) {
		intro_tick=0;
		if (native_radar) { map_radar_end();native_radar=FALSE; }
		clear_intro_labels();
		sync_layers();
		gtk_widget_queue_draw(annotations);
		return G_SOURCE_REMOVE;
	}
	sync_layers();
	if (intro_elapsed<MAP_INTRO_FRAME_US) {
		if (!radar_tried && gtk_widget_get_mapped(GTK_WIDGET(hud))) {
			radar_tried=TRUE;
			native_radar=map_radar_begin(hud);
		}
		gtk_widget_queue_draw(intro_layer);
	} else if (intro_elapsed<MAP_INTRO_FRAME_US+intro_scan_us) {
		if (native_radar) {
			int w=gtk_widget_get_width(intro_layer),h=gtk_widget_get_height(intro_layer);
			double inset;
			struct plane frame=border_plane(w,h,&inset);
			graphene_rect_t bounds=GRAPHENE_RECT_INIT(frame.x,frame.y,frame.width,frame.height);
			if (w<120 || h<100 || !map_radar_step(hud,bounds,intro_elapsed-MAP_INTRO_FRAME_US,
					intro_scan_us,&border_color)) {
				map_radar_end();
				native_radar=FALSE;
			}
		}
		if (!native_radar) gtk_widget_queue_draw(intro_layer);
	} else {
		if (native_radar) { map_radar_end();native_radar=FALSE; }
		gtk_widget_queue_draw(annotations);
	}
	return G_SOURCE_CONTINUE;
}

static void stop_intro(void) {
	if (native_radar) map_radar_end();
	native_radar=radar_tried=FALSE;
	if (intro_tick) gtk_widget_remove_tick_callback(map_hud_clock(),intro_tick);
	intro_tick=0;
	intro_start=intro_elapsed=0;
	clear_intro_labels();
}

void map_hud_open_effect(int animation) {
	stop_intro();
	intro_suppressed=FALSE;
	int margin=map_frame_margin(view_w,view_h,outer_margin);
	intro_scan_us=map_intro_scan_us(view_w-2*margin);
	if (animation!=MAP_ANIMATION_OFF)
		intro_tick=gtk_widget_add_tick_callback(map_hud_clock(),intro_frame,NULL,NULL);
	sync_layers();
}

void map_hud_finish_effect(void) {
	stop_intro();
	sync_layers();
	if (annotations) gtk_widget_queue_draw(annotations);
}

void map_hud_dismiss_effect(void) {
	intro_suppressed=TRUE;
	map_hud_finish_effect();
}

static gboolean content_contains(struct plane frame,double inset,double x,double y) {
	double m=.5+inset;
	double l=x-frame.x-m,t=y-frame.y-m,r=frame.x+frame.width-x-m,b=frame.y+frame.height-y-m;
	double c=MAX(0,MAP_FRAME_CUT-inset*(2-G_SQRT2));
	return l>=0 && t>=0 && r>=0 && b>=0 && l+t>=c && l+b>=c && r+t>=c && r+b>=c;
}

static gboolean profile_layout_at(int w,int h,struct profile_layout *layout,struct profile_data **record) {
	if (!profile_node) return FALSE;
	double x,y,inset;
	struct plane frame=border_plane(w,h,&inset), nodes=node_plane(w,h);
	if (!marker(nodes,profile_node,&x,&y)) return FALSE;
	x=(x-camera.x)*map_zoom.scale;y=(y-camera.y)*map_zoom.scale;
	if (!content_contains(frame,inset,x,y)) return FALSE;
	double margin=inset+9+PROFILE_RIM_GAP;
	struct profile_rect safe={frame.x+margin,frame.y+margin,frame.width-2*margin,frame.height-2*margin};
	*record=profile_data?g_hash_table_lookup(profile_data,profile_node):NULL;
	// Keep the selected map label visible even when its profile opens to the right.
	struct annotation_geometry g=annotation_geometry(nodes,profile_node);
	int width=0;
	annotation_measure(nodes,profile_node,x,g.reach,&width,NULL);
	struct profile_layout fallback;
	gboolean got_fallback=FALSE;
	for(enum profile_side side=PROFILE_RIGHT; side<PROFILE_SIDE_COUNT; ++side) {
		double reserve=side==PROFILE_RIGHT?MAX(symbol_half(),g.reach+width+1):symbol_half();
		if (!profile_place_on(layout,safe,x,y,reserve,profile_height(*record),side)) continue;
		if (!got_fallback) { fallback=*layout; got_fallback=TRUE; }
		gboolean overlaps=FALSE;
		for (char **n=names; n && *n; ++n) {
			double a,b;
			if (g_strcmp0(*n,profile_node)==0 || !marker(nodes,*n,&a,&b)) continue;
			a=(a-camera.x)*map_zoom.scale;b=(b-camera.y)*map_zoom.scale;
			double r=symbol_half()+4;
			struct profile_rect card=layout->card;
			if (a+r>card.x && a-r<card.x+card.w && b+r>card.y && b-r<card.y+card.h) {
				overlaps=TRUE;
				break;
			}
		}
		if (!overlaps) return TRUE;
	}
	if (got_fallback) *layout=fallback;
	return got_fallback;
}

static gboolean refresh_profile_age(gpointer unused) {
	(void)unused;
	if(annotations && profile_node) gtk_widget_queue_draw(annotations);
	return G_SOURCE_CONTINUE;
}

static void watch_profile_age(void) {
	g_clear_handle_id(&profile_refresh,g_source_remove);
	if(profile_node) profile_refresh=g_timeout_add_seconds(60,refresh_profile_age,NULL);
}

gboolean map_hud_close_profile(void) {
	if (!profile_node) return FALSE;
	g_clear_pointer(&profile_node,g_free);
	watch_profile_age();
	camera_redraw();
	return TRUE;
}

static void click_at(int w,int h,double px,double py,void (*fly)(const char *)) {
	struct profile_layout layout;
	struct profile_data *record;
	if (profile_layout_at(w,h,&layout,&record)) {
		if (profile_contains(layout.fly,PROFILE_FLY_CORNER,px,py)) { fly(profile_node); return; }
		if (profile_contains(layout.card,PROFILE_CORNER,px,py)) return;
	}
	double inset;
	struct plane frame=border_plane(w,h,&inset), plane=node_plane(w,h);
	const char *selected=NULL;
	if (content_contains(frame,inset,px,py)) {
		for (int pass=0; pass<2 && !selected; ++pass) for (char **n=names; n && *n; ++n) {
			double x,y;
			if (!marker(plane,*n,&x,&y)) continue;
			x=(x-camera.x)*map_zoom.scale;y=(y-camera.y)*map_zoom.scale;
			if (!content_contains(frame,inset,x,y)) continue;
			double radius=MAX(12,symbol_half()+(is_selected(*n)?4:0));
			if (pass==0) {
				if (fabs(px-x)<=radius && fabs(py-y)<=radius) { selected=*n; break; }
				continue;
			}
			double reach=annotation_geometry(plane,*n).reach,left=x+reach+6;
			if (px<left || px>left+plane.width*.30 || fabs(py-y+6)>22) continue;
			int lw,lh;
			annotation_measure(plane,*n,x,reach,&lw,&lh);
			if (px<=left+lw && fabs(py-y+6)<=lh*.5) { selected=*n; break; }
		}
	}
	if (g_strcmp0(profile_node,selected)==0) return;
	g_free(profile_node);
	profile_node=g_strdup(selected);
	watch_profile_age();
	camera_redraw();
}

void map_hud_click(double px,double py,void (*fly)(const char *)) {
	if (!annotations || !showing_map || intro_tick || intro_suppressed) return;
	click_at(gtk_widget_get_width(annotations),gtk_widget_get_height(annotations),px,py,fly);
}

typedef GtkWidget MapSteadyFrame;
typedef GtkWidgetClass MapSteadyFrameClass;
G_DEFINE_TYPE(MapSteadyFrame,map_steady_frame,GTK_TYPE_WIDGET)
static void steady_frame_snapshot(GtkWidget *widget,GtkSnapshot *snap) {
	int w=gtk_widget_get_width(widget),h=gtk_widget_get_height(widget);
	if (w<120 || h<100) return;
	double inset;
	struct plane frame=border_plane(w,h,&inset);
	if (gpu_grid_ready) {
		graphene_rect_t bounds=GRAPHENE_RECT_INIT(frame.x,frame.y,frame.width,frame.height);
		map_frame_snapshot(snap,bounds,&border_color);
	} else {
		graphene_rect_t bounds=GRAPHENE_RECT_INIT(0,0,w,h);
		cairo_t *cr=gtk_snapshot_append_cairo(snap,&bounds);
		draw_plane(cr,layout_plane(w,h),frame,inset);
		cairo_destroy(cr);
	}
}
static void map_steady_frame_class_init(MapSteadyFrameClass *c) {
	GTK_WIDGET_CLASS(c)->snapshot=steady_frame_snapshot;
}
static void map_steady_frame_init(MapSteadyFrame *self) {
	gtk_widget_set_can_target(GTK_WIDGET(self),FALSE);
}
typedef GtkWidget MapAnnotations;
typedef GtkWidgetClass MapAnnotationsClass;
G_DEFINE_TYPE(MapAnnotations,map_annotations,GTK_TYPE_WIDGET)

static void map_hud_rendered(double x,double y);

/* Two passes keep labels above every icon. */
static void annotations_snapshot(GtkWidget *widget,GtkSnapshot *snap) {
	int w=gtk_widget_get_width(widget),h=gtk_widget_get_height(widget);
	if(w<120||h<100) return;
	static int last_width,last_height;
	if (w!=last_width || h!=last_height) clear_intro_labels();
	last_width=w;last_height=h;
	gboolean revealing=intro_tick!=0;
	struct map_intro_beats beats=revealing ?
		map_intro_beats_at(intro_elapsed-MAP_INTRO_FRAME_US-intro_scan_us) :
		(struct map_intro_beats){1,1,1};
	double inset;
	struct plane plane=node_plane(w,h),border=border_plane(w,h,&inset);
	graphene_rect_t screen=GRAPHENE_RECT_INIT(0,0,w,h);
	struct profile_layout profile;
	struct profile_data *record;
	gboolean profile_visible=profile_layout_at(w,h,&profile,&record);
	if (!profile_visible && profile_node) {
		g_clear_pointer(&profile_node,g_free);
		watch_profile_age();
	}
	gboolean drew=FALSE;
	for (int pass=0;pass<2;pass++) {
		for(char **n=names;n&&*n;++n) {
			double x,y;
			if(!**n || !marker(plane,*n,&x,&y))continue;
			x=(x-camera.x)*map_zoom.scale;y=(y-camera.y)*map_zoom.scale;
			double radius = symbol_half() + (is_selected(*n)?6:2);
			graphene_rect_t bounds=pass?
				GRAPHENE_RECT_INIT(x+annotation_geometry(plane,*n).start-2,y-40,MAX(56,plane.width*.30+8),68):
				GRAPHENE_RECT_INIT(x-radius,y-radius,2*radius,2*radius);
			graphene_rect_t clipped;
			if(!graphene_rect_intersection(&bounds,&screen,&clipped))continue;
			if (!(pass ? beats.leader : beats.icon)) continue;
			cairo_t *cr=gtk_snapshot_append_cairo(snap,&clipped);
			plane_outline(cr,border,inset);
			cairo_clip(cr);
			struct annotation_geometry g=annotation_geometry(plane,*n);
			if (!pass) {
				if (beats.icon<1) {
					cairo_translate(cr,x,y);
					cairo_scale(cr,beats.icon,beats.icon);
					cairo_translate(cr,-x,-y);
				}
				draw_symbol(cr,*n,x,y);
			} else if (revealing) draw_leader(cr,g,*n,x,y,beats.leader);
			else draw_annotation(cr,plane,*n,x,y);
			cairo_destroy(cr);
			if (pass && revealing)
				snapshot_intro_label(snap,&clipped,plane,border,inset,*n,x,y,g.reach,beats.label);
			drew=TRUE;
		}
	}
	if (profile_visible) profile_snapshot(snap,widget,profile,profile_node,record);
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
	sync_layers();
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
	hud=paint_only_layer("mothership-center-map","Mothership Center Map",NULL,NULL);
	map_radar_init(hud);
	canvas=g_object_new(map_steady_frame_get_type(),NULL);
	g_object_ref_sink(canvas);
	gtk_window_set_child(hud,overlay);
	gpu_grid=retained_grid_new(&gpu_grid_ready,canvas);
	gtk_overlay_set_child(GTK_OVERLAY(overlay),gpu_grid);
	gtk_widget_set_hexpand(canvas,TRUE);
	gtk_widget_set_vexpand(canvas,TRUE);
	gtk_overlay_add_overlay(GTK_OVERLAY(overlay),canvas);
	g_object_unref(canvas);
	intro_layer=g_object_new(map_opening_get_type(),NULL);
	gtk_overlay_add_overlay(GTK_OVERLAY(overlay),intro_layer);
	gtk_widget_set_visible(intro_layer,FALSE);
	gtk_overlay_add_overlay(GTK_OVERLAY(overlay),annotations);
	gtk_widget_set_visible(gpu_grid,FALSE);
	gtk_widget_set_visible(canvas,FALSE);
	gtk_widget_set_visible(annotations,FALSE);
	queue_grid();
	install_fps(GTK_OVERLAY(overlay));
}

void map_hud_update(const char *node_names, GHashTable *positions, const char *node, const char *rows,
		gboolean overview, gboolean visible) {
	gboolean changed = g_strcmp0(listed_names, node_names) != 0 || in_overview != overview;
	if (g_strcmp0(listed_names, node_names) != 0) {
		g_free(listed_names);
		listed_names = g_strdup(node_names);
		g_strfreev(names);
		names = g_strsplit(node_names, "\n", -1);
	}
	if (g_strcmp0(profile_rows,rows)!=0) {
		g_free(profile_rows);
		profile_rows=g_strdup(rows);
		g_clear_pointer(&profile_data,g_hash_table_unref);
		profile_data=profile_parse(rows);
		changed=TRUE;
	}
	slots = positions;
	if (((node != NULL && *node) || !overview) && g_strcmp0(active_name, node) != 0) {
		g_free(active_name);
		active_name = g_strdup(node);
		changed = TRUE;
	}
	in_overview = overview;
	showing_map = visible;
	if (!visible) {
		map_hud_cancel_zoom();
		map_hud_close_profile();
		stop_intro();
		intro_suppressed=FALSE;
	}
	wallpaper_update_frost(showing_map && frosted_map, outer_margin);
	if (hud == NULL) return;
	gboolean was_visible=gtk_widget_get_visible(annotations);
	sync_layers();
	if(changed) clear_intro_labels();
	if (gtk_widget_get_visible(annotations) && (changed || !was_visible)) gtk_widget_queue_draw(annotations);
}
