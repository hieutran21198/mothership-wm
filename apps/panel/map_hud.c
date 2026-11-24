//go:build linux && cgo && panel

#include <gtk/gtk.h>
#include <librsvg/rsvg.h>
#include <math.h>
#include <string.h>
#include "map_hud.h"
#include "map-profile.h"
#include "map-relay.h"
#include "map-intro.h"
#include "map-radar.h"

#include "shell-paint.h"
#include "map-camera.h"
#include "retained-paint.h"
#include "shell-type.h"

static GtkWindow *hud;
static GtkWidget *canvas, *annotations, *gpu_grid, *intro_layer, *relay_layer, *origin_layer, *minimap_layer, *detail_layer, *route_layer;
static struct map_relay_motion relay_motion;
static char *relay_node;
static guint relay_tick;
static guint route_tick;
static gint64 route_last;
static gboolean route_allowed;
static double route_phase;
enum { ROUTE_DASH_PITCH=10, ROUTE_PERIOD_US=460000 };
static void (*relay_finish)(void);
static GtkWidget *stage_widget;
static guint retract_tick;
static gint64 retract_start;
static double retract_scale=1;
static void (*retract_finish)(void);
static char fps_label_next[32];
static GtkWidget *fps;
static guint fps_idle,fps_label_update;
static guint fps_frames;
static gboolean fps_enabled,fps_sample_known;
static gint64 fps_start,fps_latest;
static double fps_x,fps_y;
static GHashTable *icons, *slots, *intro_labels, *map_labels;
static char **names;
static char *listed_names;
static char *active_name, *profile_node, *profile_rows;
static GHashTable *profile_data;
static guint profile_refresh;
static gboolean profile_open;
static gboolean in_overview, showing_map, frosted_map, gpu_grid_ready;
static int view_w=MAP_REFERENCE_W,view_h=MAP_REFERENCE_H,anchor_w,anchor_h;
static PangoFontDescription *label_font;
static GdkRGBA tint, grid_color, text_color, border_color;
static void queue_grid(void);
static void clear_marker_labels(void);
static void sync_route_motion(void);
static gboolean route_source_visible(void);
static void map_origin_point(double *x,double *y);


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
	(void)stroke; // Legacy theme field; Node names have no leader.
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
    clear_marker_labels();
    if(origin_layer) gtk_widget_queue_draw(origin_layer);
    g_clear_pointer(&label_font, pango_font_description_free);
    const char *effective=known?family:shell_font_family();
    label_font=shell_font_description(effective,size,
        !g_ascii_strcasecmp(effective,shell_font_family())?PANGO_WEIGHT_SEMIBOLD:PANGO_WEIGHT_MEDIUM);
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
static const graphene_rect_t *radar_occluder(struct plane frame,graphene_rect_t *exclusion);

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
	if (!showing_map) return;
	if (annotations) gtk_widget_queue_draw(annotations);
	if (origin_layer) gtk_widget_queue_draw(origin_layer);
	if (route_layer && gtk_widget_get_visible(route_layer)) gtk_widget_queue_draw(route_layer);
	sync_route_motion();
	if (relay_node && relay_layer) gtk_widget_queue_draw(relay_layer);
	if (minimap_layer && gtk_widget_get_visible(minimap_layer)) gtk_widget_queue_draw(minimap_layer);
	if (detail_layer && gtk_widget_get_visible(detail_layer)) gtk_widget_queue_draw(detail_layer);
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
	else sync_route_motion();
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
	if(origin_layer) gtk_widget_queue_draw(origin_layer);
	if (canvas) gtk_widget_queue_draw(canvas);
	if (annotations) gtk_widget_queue_draw(annotations);
	if (route_layer) gtk_widget_queue_draw(route_layer);
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
	source(cr,grid_color,MAP_GRID_LINE_ALPHA);
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
	GdkRGBA junction=map_grid_junction(grid_color);
	source(cr,junction,junction.alpha);
	for(double x=ox;x<border.x+border.width;x+=pitch)
		for(double y=oy;y<border.y+border.height;y+=pitch) {
			cairo_new_sub_path(cr);
			cairo_arc(cr,x+.5,y+.5,MAP_GRID_JUNCTION_RADIUS,0,2*G_PI);
		}
	cairo_fill(cr);
	cairo_restore(cr);
outline:
	map_frame_strokes(cr,border.x,border.y,border.width,border.height,&border_color);
}

static struct plane layout_plane(int width, int height) {
	double inset = width < 480 ? 24 : 40;
	return (struct plane){inset, 38, width - inset * 2, height - 76, 44};
}

static struct plane node_plane(int w,int h) {
	return anchor_w?layout_plane(anchor_w,anchor_h):layout_plane(w,h);
}

static void project_map_point(double *x,double *y) {
	*x=(*x-camera.x)*map_zoom.scale;
	*y=(*y-camera.y)*map_zoom.scale;
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

static struct profile_data *profile_for(const char *name) {
	return profile_data && name ? g_hash_table_lookup(profile_data,name) : NULL;
}

const char *map_hud_shortcut(int slot) {
	if(slot<1 || slot>9) return NULL;
	for(char **n=names; n && *n; ++n) {
		struct profile_data *record=profile_for(*n);
		if(record && record->shortcut==slot) return *n;
	}
	return NULL;
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

// The base ring/diamond has a shared axis-aligned painted footprint.
#define FOCUS_DIAMOND_FACTOR 1.35
#define NODE_IDLE_WIDTH 1.35
#define NODE_FOCUS_WIDTH 1.2
#define CUSTOM_FOCUS_WIDTH 1.1
#define SHIP_RIM_WIDTH 1.0

static double symbol_half(void);
static gboolean custom_marker(const char *name) {
	return icons && g_hash_table_contains(icons,name);
}
static double focus_marker_radius(const char *name) {
	double half=symbol_half();
	return custom_marker(name)?half+4*map_zoom.scale:half*FOCUS_DIAMOND_FACTOR;
}
static double painted_marker_radius(const char *name) {
	return is_selected(name)?focus_marker_radius(name)+1: symbol_half()+2;
}

static void default_marker(cairo_t *cr,double x,double y,double r,gboolean active,gboolean selected) {
	const GdkRGBA muted_edge = {0xaa/255.,0xc4/255.,0xd1/255.,1};
	const GdkRGBA focus_edge = {0x75/255.,0xe3/255.,0xdd/255.,1};
	const GdkRGBA focus_center = {0x57/255.,0xd4/255.,0xd2/255.,1};
	if (!selected) {
		// Compensate the mitered stroke so the visible blue edge, not the
		// path centerline, has the same extent as the Mothership ring.
		diamond(cr,x,y,r-NODE_IDLE_WIDTH/G_SQRT2);
		cairo_set_source_rgba(cr,.018,.045,.058,.82);
		cairo_set_line_width(cr,3);
		cairo_stroke_preserve(cr);
		source(cr,active?amber:muted_edge,.92);
		cairo_set_line_width(cr,NODE_IDLE_WIDTH);
		cairo_stroke(cr);
		return;
	}
	// Paint only the outlines: the gaps and hollow center reveal the map.
	diamond(cr,x,y,r*FOCUS_DIAMOND_FACTOR);
	source(cr,focus_edge,.98);
	cairo_set_line_width(cr,NODE_FOCUS_WIDTH);
	cairo_stroke(cr);
	cairo_save(cr);
	cairo_set_fill_rule(cr,CAIRO_FILL_RULE_EVEN_ODD);
	diamond(cr,x,y,r);
	diamond(cr,x,y,r*7/16);
	source(cr,focus_center,1);
	cairo_fill(cr);
	cairo_restore(cr);
}

static double symbol_half(void) {
	return SVG_ICON_HALF * map_zoom.scale;
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
		double r=focus_marker_radius(name),c=5*map_zoom.scale;
		cairo_move_to(cr,x-r+c,y-r); cairo_line_to(cr,x+r-c,y-r);
		cairo_line_to(cr,x+r,y-r+c); cairo_line_to(cr,x+r,y+r-c);
		cairo_line_to(cr,x+r-c,y+r); cairo_line_to(cr,x-r+c,y+r);
		cairo_line_to(cr,x-r,y+r-c); cairo_line_to(cr,x-r,y-r+c);
		cairo_close_path(cr);
		source(cr,selected_outline,.94);
		cairo_set_line_width(cr,CUSTOM_FOCUS_WIDTH);
		cairo_stroke(cr);
	}
}

static double annotation_reach(const char *name) {
	return MAX(17,painted_marker_radius(name)+3);
}

struct map_label {
	PangoLayout *layout;
	PangoRectangle ink,logical;
};

static struct map_label *mothership_label;

static void free_map_label(gpointer data) {
	struct map_label *label=data;
	g_object_unref(label->layout);
	g_free(label);
}

static void clear_marker_labels(void) {
	if(map_labels) g_hash_table_remove_all(map_labels);
	g_clear_pointer(&mothership_label,free_map_label);
}

static struct map_label *new_marker_label(const char *text) {
	if(!label_font) map_hud_set_font(shell_font_family(),13);
	struct map_label *label=g_new0(struct map_label,1);
	PangoContext *ctx=pango_font_map_create_context(pango_cairo_font_map_get_default());
	label->layout=pango_layout_new(ctx);
	g_object_unref(ctx);
	pango_layout_set_font_description(label->layout,label_font);
	pango_layout_set_single_paragraph_mode(label->layout,TRUE);
	pango_layout_set_text(label->layout,text,-1);
	pango_layout_get_pixel_extents(label->layout,&label->ink,&label->logical);
	return label;
}

static double marker_label_top(const struct map_label *label,double y) {
	return y-6-label->logical.height*.5;
}

// Shape the complete name once at its screen font size. Camera projection
// moves the ink, but does not change the label's width or layout.
static struct map_label *map_label_for(const char *name) {
	if (!label_font) map_hud_set_font(shell_font_family(),13);
	if (!map_labels)
		map_labels=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,free_map_label);
	struct map_label *label=g_hash_table_lookup(map_labels,name);
	if(label) return label;
	struct profile_data *record=profile_for(name);
	g_autofree char *with_slot=record && record->shortcut?
		g_strdup_printf("%d  %s",record->shortcut,name):NULL;
	label=new_marker_label(with_slot?with_slot:name);
	g_hash_table_insert(map_labels,g_strdup(name),label);
	return label;
}

static double draw_label(cairo_t *cr,const char *name,double x,double y,double reach) {
	const gboolean active=is_active(name),selected=is_selected(name);
	const struct map_label *label=map_label_for(name);
	cairo_move_to(cr,x+reach+6,marker_label_top(label,y));
	source(cr,selected?selected_text:active?amber:text_color,1);
	pango_cairo_show_layout(cr,label->layout);
	return MAX(0,label->ink.x+label->ink.width+2);
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
		struct plane border,double inset,
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
		label->width=draw_label(cr,name,x,y,reach)+2;
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
	*inset=map_frame_content_inset(padding+.5);
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
	gtk_widget_set_visible(route_layer,paint && (!intro ||
		(reveal && map_intro_beats_at(intro_elapsed-reveal_start).icon>=1)));
	gtk_widget_set_visible(origin_layer,paint && (!intro ||
		(reveal && map_intro_beats_at(intro_elapsed-reveal_start).icon>=1)));
	// The fast scan has its own surface. Hide, rather than just stop
	// invalidating, the opening widget so GTK cannot keep a cached frame.
	gtk_widget_set_visible(intro_layer,intro && !reveal && (!frame || !native_radar));
	gtk_widget_set_visible(minimap_layer,frame && in_overview);
	gtk_widget_set_visible(detail_layer,paint && !intro);
	gtk_widget_set_visible(fps,fps_enabled && paint && !intro);
	gtk_widget_set_visible(GTK_WIDGET(hud),paint);
	sync_route_motion();
}

static const graphene_rect_t *radar_occluder(struct plane frame,graphene_rect_t *exclusion);

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
	graphene_rect_t exclusion;
	map_intro_snapshot(snap,bounds,intro_elapsed,intro_scan_us,&border_color,
		radar_occluder(frame,&exclusion));
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
			int w=gtk_widget_get_width(stage_widget),h=gtk_widget_get_height(stage_widget);
			double inset;
			struct plane frame=border_plane(w,h,&inset);
			graphene_rect_t bounds=GRAPHENE_RECT_INIT(frame.x,frame.y,frame.width,frame.height);
			graphene_rect_t exclusion;
			if (w<120 || h<100 || !map_radar_step(hud,bounds,intro_elapsed-MAP_INTRO_FRAME_US,
					intro_scan_us,&border_color,radar_occluder(frame,&exclusion))) {
				map_radar_end();
				native_radar=FALSE;
				sync_layers();
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
	wallpaper_update_frost(FALSE,outer_margin);
}

static gboolean content_contains(struct plane frame,double inset,double x,double y) {
	double m=.5+inset;
	double l=x-frame.x-m,t=y-frame.y-m,r=frame.x+frame.width-x-m,b=frame.y+frame.height-y-m;
	double c=MAX(0,MAP_FRAME_CUT-inset*(2-G_SQRT2));
	return l>=0 && t>=0 && r>=0 && b>=0 && l+t>=c && l+b>=c && r+t>=c && r+b>=c;
}

static gboolean profile_layout_at(int w,int h,struct profile_layout *layout,struct profile_data **record) {
	if (!profile_node || !profile_open) return FALSE;
	double x,y,inset;
	struct plane frame=border_plane(w,h,&inset), nodes=node_plane(w,h);
	if (!marker(nodes,profile_node,&x,&y)) return FALSE;
	project_map_point(&x,&y);
	if (!content_contains(frame,inset,x,y)) return FALSE;
	double margin=inset+9+PROFILE_RIM_GAP;
	struct profile_rect safe={frame.x+margin,frame.y+margin,frame.width-2*margin,frame.height-2*margin};
	*record=profile_for(profile_node);
	// Keep the card next to the selected Node on the right when it fits.
	// Its painted rim must clear the visible name as well as the symbol.
	double reach=annotation_reach(profile_node);
	const struct map_label *label=map_label_for(profile_node);
	int width=MAX(label->logical.width,label->ink.x+label->ink.width);
	for(enum profile_side side=PROFILE_RIGHT; side<PROFILE_SIDE_COUNT; ++side) {
		double radius=painted_marker_radius(profile_node);
		double reserve=side==PROFILE_RIGHT?MAX(radius,reach+6+width):radius;
		if (profile_place_on(layout,safe,x,y,reserve,profile_height(*record),side)) return TRUE;
	}
	return FALSE;
}

static gboolean refresh_profile_age(gpointer unused) {
	(void)unused;
	if(detail_layer && profile_node) gtk_widget_queue_draw(detail_layer);
	return G_SOURCE_CONTINUE;
}

static void watch_profile_age(void) {
	g_clear_handle_id(&profile_refresh,g_source_remove);
	if(profile_node && profile_open) profile_refresh=g_timeout_add_seconds(60,refresh_profile_age,NULL);
}

void map_hud_pause_route(void) {
	if(route_tick) gtk_widget_remove_tick_callback(route_layer,route_tick);
	route_tick=0;route_last=0;
}

static gboolean route_can_move(void) {
	return route_allowed && showing_map && profile_node && !relay_node &&
		route_layer && gtk_widget_get_mapped(route_layer) && route_source_visible();
}

static gboolean move_route(GtkWidget *widget,GdkFrameClock *clock,gpointer unused) {
	(void)unused;
	if(!route_can_move()) {
		route_tick=0;route_last=0;
		return G_SOURCE_REMOVE;
	}
	gint64 now=gdk_frame_clock_get_frame_time(clock),elapsed=now-route_last;
	// If a hidden/occluded surface stops receiving frames, do not jump
	// through the paused interval when the compositor resumes its clock.
	if(route_last && elapsed>0 && elapsed<=250000)
		route_phase=fmod(route_phase+elapsed*((double)ROUTE_DASH_PITCH/ROUTE_PERIOD_US),ROUTE_DASH_PITCH);
	route_last=now;
	gtk_widget_queue_draw(widget);
	return G_SOURCE_CONTINUE;
}

static void sync_route_motion(void) {
	if(!route_can_move()) map_hud_pause_route();
	else if(!route_tick) route_tick=gtk_widget_add_tick_callback(route_layer,move_route,NULL,NULL);
}

void map_hud_allow_route(gboolean enable) {
	route_allowed=enable;
	sync_route_motion();
}

static void reset_route(void) {
	map_hud_pause_route();
	route_phase=0;
}

gboolean map_hud_close_profile(void) {
	if (!profile_node) return FALSE;
	g_clear_pointer(&profile_node,g_free);
	profile_open=FALSE;
	reset_route();
	watch_profile_age();
	camera_redraw();
	return TRUE;
}

void map_hud_restore_selection(const char *name) {
	if (!name || !*name || !names || !g_strv_contains((const char *const *)names,name)) return;
	if(g_strcmp0(profile_node,name)==0) map_hud_pause_route();
	else reset_route();
	g_free(profile_node);
	profile_node=g_strdup(name);
	profile_open=FALSE;
	sync_route_motion();
	watch_profile_age();
	camera_redraw();
}

static const char *node_at(int w,int h,double px,double py) {
	double inset;
	struct plane frame=border_plane(w,h,&inset), plane=node_plane(w,h);
	if (!content_contains(frame,inset,px,py)) return NULL;
	for (int pass=0; pass<2; ++pass) for (char **n=names; n && *n; ++n) {
		double x,y;
		if (!marker(plane,*n,&x,&y)) continue;
		project_map_point(&x,&y);
		if (!content_contains(frame,inset,x,y)) continue;
		double radius=MAX(12,painted_marker_radius(*n));
		if (pass==0) {
			if (fabs(px-x)<=radius && fabs(py-y)<=radius) return *n;
			continue;
		}
		double left=x+annotation_reach(*n)+6;
		const struct map_label *label=map_label_for(*n);
		if (px>=left+MIN(0,label->ink.x) &&
			px<=left+MAX(label->logical.width,label->ink.x+label->ink.width) &&
			fabs(py-y+6)<=label->logical.height*.5) return *n;
	}
	return NULL;
}

gboolean map_hud_drag_inside(double px,double py) {
	if (!annotations || !showing_map || intro_tick || intro_suppressed) return FALSE;
	double unused;
	struct plane frame=border_plane(gtk_widget_get_width(annotations),gtk_widget_get_height(annotations),&unused);
	return content_contains(frame,MAP_INNER_INSET+MAP_INNER_WIDTH*.5,px,py);
}

gboolean map_hud_drag_can_start(double px,double py) {
	if (!map_hud_drag_inside(px,py)) return FALSE;
	int w=gtk_widget_get_width(annotations),h=gtk_widget_get_height(annotations);
	struct profile_layout layout;
	struct profile_data *record;
	if (profile_layout_at(w,h,&layout,&record)) {
		double gap=PROFILE_RIM_GAP+2;
		if (px>=layout.card.x-gap && px<=layout.card.x+layout.card.w+gap &&
			py>=layout.card.y-gap && py<=layout.card.y+layout.card.h+gap) return FALSE;
	}
	double inset;
	struct plane frame=border_plane(w,h,&inset);
	graphene_rect_t mini;
	if (radar_occluder(frame,&mini) &&
		graphene_rect_contains_point(&mini,&GRAPHENE_POINT_INIT(px,py))) return FALSE;
	double hx,hy;
	map_origin_point(&hx,&hy);
	if (content_contains(frame,inset,hx,hy)) {
		double half=symbol_half();
		if (hypot(px-hx,py-hy)<=half+1) return FALSE;
		if (!mothership_label) mothership_label=new_marker_label("Mothership");
		const struct map_label *label=mothership_label;
		double left=hx+half+6+label->ink.x, top=marker_label_top(label,hy)+label->ink.y;
		if (px>=left && px<=left+label->ink.width && py>=top && py<=top+label->ink.height) return FALSE;
	}
	return node_at(w,h,px,py)==NULL;
}

static void click_at(int w,int h,double px,double py,void (*fly)(const char *)) {
	struct profile_layout layout;
	struct profile_data *record;
	if (profile_layout_at(w,h,&layout,&record)) {
		if (profile_contains(layout.fly,PROFILE_FLY_CORNER,px,py)) { fly(profile_node); return; }
		if (profile_contains(layout.card,PROFILE_CORNER,px,py)) return;
	}
	const char *selected=node_at(w,h,px,py);
	if (g_strcmp0(profile_node,selected)==0 && (profile_open || !selected)) return;
	g_free(profile_node);
	profile_node=g_strdup(selected);
	profile_open=selected!=NULL;
	reset_route();
	sync_route_motion();
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
// Home is a virtual coordinate of the map, not a Node or a control.
static void map_origin_point(double *x,double *y) {
	*x=94;
	*y=252;
	project_map_point(x,y);
}

static gboolean route_endpoint(struct plane map,struct plane frame,double inset,const char *name,double *x,double *y) {
	if(!name || !marker(map,name,x,y)) return FALSE;
	project_map_point(x,y);
	return content_contains(frame,inset+3,*x,*y);
}

struct route_span { double first,last; };

// Compute the parameter of the ray where it reaches the padded painted
// outline, not the icon's bounding box or a fixed pixel distance.
static gboolean route_visible_span(const char *name,double hx,double hy,double x,double y,struct route_span *span) {
	double length=hypot(x-hx,y-hy);
	if(length<=0.01) return FALSE;
	double ux=fabs(x-hx)/length,uy=fabs(y-hy)/length,diag=ux+uy;
	double marker=focus_marker_radius(name);
	const double clear=3, half_route=.65;
	double border;
	if(custom_marker(name)) {
		double pad=clear+half_route+CUSTOM_FOCUS_WIDTH*.5, cut=5*map_zoom.scale;
		border=MIN((marker+pad)/MAX(ux,uy),(2*marker-cut+pad*G_SQRT2)/diag);
	} else {
		double pad=clear+half_route+NODE_FOCUS_WIDTH*.5;
		border=(marker+pad*G_SQRT2)/diag;
	}
	span->first=symbol_half()+clear+half_route;
	span->last=length-border;
	return span->last>span->first;
}

static gboolean route_source_visible(void) {
	double inset,hx,hy,x,y;
	struct plane frame=border_plane(view_w,view_h,&inset);
	struct route_span span;
	map_origin_point(&hx,&hy);
	return route_endpoint(node_plane(view_w,view_h),frame,inset,profile_node,&x,&y) &&
		route_visible_span(profile_node,hx,hy,x,y,&span);
}

// Leave the route anchored to the real projected point even offscreen,
// but clip the visible segment and relay glyphs to the Map content.
static void push_route_clip(GtkSnapshot *snap,struct plane frame,double inset) {
	float l=frame.x+.5+inset,r=frame.x+frame.width-.5-inset;
	float t=frame.y+.5+inset,b=frame.y+frame.height-.5-inset;
	float cut=map_frame_cut_at(inset);
	GskPathBuilder *p=gsk_path_builder_new();
	gsk_path_builder_move_to(p,l+cut,t);
	gsk_path_builder_line_to(p,r-cut,t);
	gsk_path_builder_line_to(p,r,t+cut);
	gsk_path_builder_line_to(p,r,b-cut);
	gsk_path_builder_line_to(p,r-cut,b);
	gsk_path_builder_line_to(p,l+cut,b);
	gsk_path_builder_line_to(p,l,b-cut);
	gsk_path_builder_line_to(p,l,t+cut);
	gsk_path_builder_close(p);
	GskPath *path=gsk_path_builder_free_to_path(p);
	gtk_snapshot_push_fill(snap,path,GSK_FILL_RULE_WINDING);
	gsk_path_unref(path);
}

typedef GtkWidget MapMini;
typedef GtkWidgetClass MapMiniClass;
G_DEFINE_TYPE(MapMini,map_mini,GTK_TYPE_WIDGET)

static struct profile_rect minimap_bounds(struct plane frame) {
	// Equal inset from both straight painted edges. Use a smaller footprint
	// on small windows rather than notch or move the underlying Map.
	const double gap=18,extent=PROFILE_RIM_GAP+2;
	double w=MIN(232,frame.width*.30),h=w*.60;
	if(w<104 || h>frame.height*.40) return (struct profile_rect){0};
	return (struct profile_rect){
		frame.x+frame.width-gap-extent-w,frame.y+frame.height-gap-extent-h,w,h};
}

static const graphene_rect_t *radar_occluder(struct plane frame,graphene_rect_t *exclusion) {
	if(!in_overview) return NULL;
	struct profile_rect box=minimap_bounds(frame);
	if(box.w==0) return NULL;
	// Exclude the full painted footprint, including the outer dashed rim
	// and its antialiased edges, in both the GTK and native radar paths.
	double extent=PROFILE_RIM_GAP+2;
	double l=floor(box.x-extent),t=floor(box.y-extent);
	*exclusion=GRAPHENE_RECT_INIT(l,t,ceil(box.x+box.w+extent)-l,
		ceil(box.y+box.h+extent)-t);
	return exclusion;
}

static void minimap_snapshot(GtkWidget *widget,GtkSnapshot *snap) {
	int w=gtk_widget_get_width(widget),h=gtk_widget_get_height(widget);
	double inset;
	struct plane frame=border_plane(w,h,&inset),map=node_plane(w,h);
	struct profile_rect box=minimap_bounds(frame);
	if(box.w==0) return;
	double extent=PROFILE_RIM_GAP+2;
	graphene_rect_t bounds=GRAPHENE_RECT_INIT(box.x-extent,box.y-extent,box.w+2*extent,box.h+2*extent);
	cairo_t *cr=gtk_snapshot_append_cairo(snap,&bounds);
	profile_draw_chrome(cr,box,TRUE);
	double scale=MIN((box.w-18)/MAP_TABLE_W,(box.h-18)/MAP_TABLE_H);
	double left=box.x+(box.w-MAP_TABLE_W*scale)*.5;
	double top=box.y+(box.h-MAP_TABLE_H*scale)*.5;
	double cx=left+(camera.x-MAP_TABLE_MIN_X)*scale;
	double cy=top+(camera.y-MAP_TABLE_MIN_Y)*scale;
	double vw=MIN(MAP_TABLE_W,w/map_zoom.scale)*scale,vh=MIN(MAP_TABLE_H,h/map_zoom.scale)*scale;
	cairo_set_source_rgba(cr,.62,.78,.86,.82);
	cairo_set_line_width(cr,1);
	cairo_rectangle(cr,cx,cy,vw,vh);
	cairo_stroke(cr);
	for(int pass=0;pass<2;pass++) for(char **n=names;n && *n;++n) {
		double nx,ny;
		gboolean selected=is_selected(*n) || is_active(*n);
		if(selected!=pass || !marker(map,*n,&nx,&ny)) continue;
		if(nx<MAP_TABLE_MIN_X || nx>MAP_TABLE_MIN_X+MAP_TABLE_W ||
		   ny<MAP_TABLE_MIN_Y || ny>MAP_TABLE_MIN_Y+MAP_TABLE_H) continue;
		double px=left+(nx-MAP_TABLE_MIN_X)*scale,py=top+(ny-MAP_TABLE_MIN_Y)*scale;
		diamond(cr,px,py,selected?3.2:2.3);
		source(cr,selected?selected_outline:text_color,selected?.95:.78);
		cairo_fill(cr);
	}
	cairo_destroy(cr);
}
static void map_mini_class_init(MapMiniClass *c) { GTK_WIDGET_CLASS(c)->snapshot=minimap_snapshot; }
static void map_mini_init(MapMini *self) {
	gtk_widget_set_can_target(GTK_WIDGET(self),FALSE);
	gtk_widget_set_hexpand(GTK_WIDGET(self),TRUE);
	gtk_widget_set_vexpand(GTK_WIDGET(self),TRUE);
}

typedef GtkWidget MapRelay;
typedef GtkWidgetClass MapRelayClass;
G_DEFINE_TYPE(MapRelay,map_relay,GTK_TYPE_WIDGET)

static void relay_snapshot(GtkWidget *widget,GtkSnapshot *snap) {
	int w=gtk_widget_get_width(widget),h=gtk_widget_get_height(widget);
	if(w<120 || h<100 || !relay_node) return;
	double inset,x,y,hx,hy;
	struct plane frame=border_plane(w,h,&inset),map=node_plane(w,h);
	map_origin_point(&hx,&hy);
	struct route_span span;
	if(route_endpoint(map,frame,inset,relay_node,&x,&y) &&
			route_visible_span(relay_node,hx,hy,x,y,&span)) {
		push_route_clip(snap,frame,inset);
		map_relay_draw(snap,hx,hy,x,y,span.first,span.last,&relay_motion);
		gtk_snapshot_pop(snap);
	}
}
static void map_relay_class_init(MapRelayClass *c) { GTK_WIDGET_CLASS(c)->snapshot=relay_snapshot; }
static void map_relay_init(MapRelay *self) {
	gtk_widget_set_can_target(GTK_WIDGET(self),FALSE);
	gtk_widget_set_hexpand(GTK_WIDGET(self),TRUE);gtk_widget_set_vexpand(GTK_WIDGET(self),TRUE);
}

enum { MAP_RETRACT_US=200000 };

typedef GtkWidget MapStage;
typedef GtkWidgetClass MapStageClass;
G_DEFINE_TYPE(MapStage,map_stage,GTK_TYPE_WIDGET)

static void map_stage_snapshot(GtkWidget *widget,GtkSnapshot *snap) {
	GtkWidget *child=gtk_widget_get_first_child(widget);
	if(!child || retract_scale<=0) return;
	if(retract_scale>=1) {gtk_widget_snapshot_child(widget,child,snap);return;}
	double x=gtk_widget_get_width(widget)*.5,y=gtk_widget_get_height(widget)*.5;
	graphene_point_t center=GRAPHENE_POINT_INIT(x,y),back=GRAPHENE_POINT_INIT(-x,-y);
	gtk_snapshot_save(snap);
	gtk_snapshot_translate(snap,&center);
	gtk_snapshot_scale(snap,retract_scale,retract_scale);
	gtk_snapshot_translate(snap,&back);
	gtk_widget_snapshot_child(widget,child,snap);
	gtk_snapshot_restore(snap);
}

static void map_stage_dispose(GObject *object) {
	GtkWidget *child=gtk_widget_get_first_child(GTK_WIDGET(object));
	if(child) gtk_widget_unparent(child);
	G_OBJECT_CLASS(map_stage_parent_class)->dispose(object);
}
static void map_stage_class_init(MapStageClass *c) {
	GTK_WIDGET_CLASS(c)->snapshot=map_stage_snapshot;
	G_OBJECT_CLASS(c)->dispose=map_stage_dispose;
	gtk_widget_class_set_layout_manager_type(GTK_WIDGET_CLASS(c),GTK_TYPE_BIN_LAYOUT);
}
static void map_stage_init(MapStage *self) {(void)self;}

static gboolean retract_frame(GtkWidget *widget,GdkFrameClock *clock,gpointer data) {
	(void)widget;(void)data;
	gint64 now=gdk_frame_clock_get_frame_time(clock);
	if(!retract_start) retract_start=now;
	double t=CLAMP((now-retract_start)/(double)MAP_RETRACT_US,0,1);
	retract_scale=1-t*t*(3-2*t);
	gtk_widget_queue_draw(stage_widget);
	if(t<1) return G_SOURCE_CONTINUE;
	void (*finish)(void)=retract_finish;
	retract_tick=0;
	if(finish) finish();
	return G_SOURCE_REMOVE;
}

void map_hud_retract(void (*finish)(void)) {
	if(retract_tick) gtk_widget_remove_tick_callback(map_hud_clock(),retract_tick);
	retract_start=0;
	retract_scale=1;
	retract_finish=finish;
	retract_tick=gtk_widget_add_tick_callback(map_hud_clock(),retract_frame,NULL,NULL);
}

void map_hud_relay_cancel(void) {
	if(retract_tick) {
		gtk_widget_remove_tick_callback(map_hud_clock(),retract_tick);
		retract_tick=0;
	}
	if(retract_scale!=1 && stage_widget) gtk_widget_queue_draw(stage_widget);
	retract_start=0;
	retract_scale=1;
	retract_finish=NULL;
	if(relay_tick) gtk_widget_remove_tick_callback(map_hud_clock(),relay_tick);
	relay_tick=0;
	relay_motion=(struct map_relay_motion){0};
	g_clear_pointer(&relay_node,g_free);
	relay_finish=NULL;
	if(relay_layer) gtk_widget_set_visible(relay_layer,FALSE);
	sync_route_motion();
}

static gboolean advance_relay(GtkWidget *widget,GdkFrameClock *clock,gpointer data) {
	(void)widget;(void)data;
	if(relay_motion.phase==RELAY_DONE) {
		void (*finish)(void)=relay_finish;
		relay_tick=0;
		map_hud_relay_cancel();
		if(finish) finish();
		return G_SOURCE_REMOVE;
	}
	map_relay_advance(&relay_motion,gdk_frame_clock_get_frame_time(clock));
	gtk_widget_queue_draw(relay_layer);
	if(relay_motion.phase==RELAY_WAIT) {
		relay_tick=0;
		return G_SOURCE_REMOVE;
	}
	return G_SOURCE_CONTINUE;
}

gboolean map_hud_relay_begin(const char *name,void (*finish)(void)) {
	int w=gtk_widget_get_width(annotations),h=gtk_widget_get_height(annotations);
	if(w<120 || h<100) return FALSE;
	double inset,x,y,hx,hy;
	struct plane frame=border_plane(w,h,&inset);
	map_origin_point(&hx,&hy);
	struct route_span span;
	if(!route_endpoint(node_plane(w,h),frame,inset,name,&x,&y) ||
			!route_visible_span(name,hx,hy,x,y,&span) || span.last-span.first<25) return FALSE;
	map_hud_relay_cancel();
	relay_node=g_strdup(name);
	map_hud_finish_effect();
	map_hud_restore_selection(name);
	relay_motion.phase=RELAY_OUT;
	relay_finish=finish;
	gtk_widget_set_visible(relay_layer,TRUE);
	gtk_widget_queue_draw(relay_layer);
	relay_tick=gtk_widget_add_tick_callback(map_hud_clock(),advance_relay,NULL,NULL);
	return TRUE;
}

void map_hud_relay_ack(void) {
	if(!relay_node) return;
	map_relay_accept(&relay_motion);
	if(!relay_tick && relay_motion.phase==RELAY_IN)
		relay_tick=gtk_widget_add_tick_callback(map_hud_clock(),advance_relay,NULL,NULL);
}

static void map_route(GtkSnapshot *snap,struct plane map,struct plane frame,double inset) {
	double hx,hy,x,y;
	map_origin_point(&hx,&hy);
	if (route_endpoint(map,frame,inset,profile_node,&x,&y)) {
		struct route_span span;
		if(route_visible_span(profile_node,hx,hy,x,y,&span)) {
			graphene_point_t start=GRAPHENE_POINT_INIT(hx,hy);
			graphene_rect_t run=GRAPHENE_RECT_INIT(span.first,-.65,span.last-span.first,1.3);
			double offset=span.first+route_phase;
			graphene_rect_t tile=GRAPHENE_RECT_INIT(offset,-.65,ROUTE_DASH_PITCH,1.3);
			graphene_rect_t dash=GRAPHENE_RECT_INIT(offset,-.65,5.5,1.3);
			const GdkRGBA ink={.47,.74,.80,.73};
			push_route_clip(snap,frame,inset);
			gtk_snapshot_save(snap);gtk_snapshot_translate(snap,&start);
			gtk_snapshot_rotate(snap,atan2(y-hy,x-hx)*180/G_PI);
			gtk_snapshot_push_repeat(snap,&run,&tile);
			gtk_snapshot_append_color(snap,&ink,&dash);
			gtk_snapshot_pop(snap);gtk_snapshot_restore(snap);
			gtk_snapshot_pop(snap);
		}
	}
}

typedef GtkWidget MapRoute;
typedef GtkWidgetClass MapRouteClass;
G_DEFINE_TYPE(MapRoute,map_route_layer,GTK_TYPE_WIDGET)

static void route_snapshot(GtkWidget *widget,GtkSnapshot *snap) {
	int w=gtk_widget_get_width(widget),h=gtk_widget_get_height(widget);
	if(!profile_node || w<120 || h<100) return;
	double inset;
	struct plane frame=border_plane(w,h,&inset);
	map_route(snap,node_plane(w,h),frame,inset);
}
static void map_route_layer_class_init(MapRouteClass *c) { GTK_WIDGET_CLASS(c)->snapshot=route_snapshot; }
static void route_layer_mapped(GtkWidget *widget,gpointer unused) {
	(void)widget;(void)unused;
	sync_route_motion();
}

static void route_layer_unmapped(GtkWidget *widget,gpointer unused) {
	(void)widget;(void)unused;
	map_hud_pause_route();
}

static void map_route_layer_init(MapRoute *self) {
	gtk_widget_set_can_target(GTK_WIDGET(self),FALSE);
	gtk_widget_set_hexpand(GTK_WIDGET(self),TRUE);gtk_widget_set_vexpand(GTK_WIDGET(self),TRUE);
	g_signal_connect(self,"map",G_CALLBACK(route_layer_mapped),NULL);
	g_signal_connect(self,"unmap",G_CALLBACK(route_layer_unmapped),NULL);
}

typedef GtkWidget MapOrigin;
typedef GtkWidgetClass MapOriginClass;
G_DEFINE_TYPE(MapOrigin,map_origin,GTK_TYPE_WIDGET)

static void origin_snapshot(GtkWidget *widget,GtkSnapshot *snap) {
	int w=gtk_widget_get_width(widget),h=gtk_widget_get_height(widget);
	if(w<120||h<100) return;
	double hx,hy,inset;
	struct plane frame=border_plane(w,h,&inset);
	map_origin_point(&hx,&hy);
	if(!content_contains(frame,inset,hx,hy)) return;
	double half=symbol_half();
	if(!mothership_label)mothership_label=new_marker_label("Mothership");
	const struct map_label *label=mothership_label;
	double label_x=hx+half+6,label_y=marker_label_top(label,hy);
	double left=hx-half-2,top=MIN(hy-half-2,label_y+label->ink.y-2);
	double right=MAX(hx+half+2,label_x+label->ink.x+label->ink.width+2);
	double bottom=MAX(hy+half+2,label_y+label->ink.y+label->ink.height+2);
	graphene_rect_t bounds=GRAPHENE_RECT_INIT(left,top,right-left,bottom-top);
	cairo_t *cr=gtk_snapshot_append_cairo(snap,&bounds);
	plane_outline(cr,frame,inset);
	cairo_clip(cr);
	// Connect storyboard: blue-gray circular rim, cyan swept ship and spine.
	cairo_save(cr);
	cairo_translate(cr,hx,hy);
	double scale=(half-SHIP_RIM_WIDTH*.5)/21;
	cairo_scale(cr,scale,scale);
	cairo_arc(cr,0,0,21,0,2*G_PI);
	cairo_set_source_rgb(cr,.080,.142,.181);cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr,.33,.56,.61,.90);
	cairo_set_line_width(cr,SHIP_RIM_WIDTH/scale);cairo_stroke(cr);
	cairo_move_to(cr,0,-14.5);
	cairo_line_to(cr,14,9.4);
	cairo_line_to(cr,5.4,6.4);
	cairo_line_to(cr,0,10.6);
	cairo_line_to(cr,-5.4,6.4);
	cairo_line_to(cr,-14,9.4);
	cairo_close_path(cr);
	cairo_set_source_rgb(cr,.101,.192,.232);cairo_fill_preserve(cr);
	cairo_set_source_rgb(cr,.44,.87,.87);
	cairo_set_line_width(cr,1.75);
	cairo_set_line_join(cr,CAIRO_LINE_JOIN_ROUND);cairo_stroke(cr);
	cairo_move_to(cr,0,-9.7);cairo_line_to(cr,0,6.1);
	cairo_set_line_width(cr,1.4);cairo_set_line_cap(cr,CAIRO_LINE_CAP_ROUND);cairo_stroke(cr);
	cairo_restore(cr);
	cairo_set_source_rgba(cr,.56,.80,.87,.95);
	cairo_move_to(cr,label_x,label_y);
	pango_cairo_show_layout(cr,label->layout);
	cairo_destroy(cr);
}

static void map_origin_class_init(MapOriginClass *c) { GTK_WIDGET_CLASS(c)->snapshot=origin_snapshot; }
static void map_origin_init(MapOrigin *self) {
	gtk_widget_set_can_target(GTK_WIDGET(self),FALSE);
	gtk_widget_set_hexpand(GTK_WIDGET(self),TRUE);
	gtk_widget_set_vexpand(GTK_WIDGET(self),TRUE);
}

static void annotations_snapshot(GtkWidget *widget,GtkSnapshot *snap) {
	int w=gtk_widget_get_width(widget),h=gtk_widget_get_height(widget);
	if(w<120||h<100) return;
	static int last_width,last_height;
	if (w!=last_width || h!=last_height) clear_intro_labels();
	last_width=w;last_height=h;
	gboolean revealing=intro_tick!=0;
	struct map_intro_beats beats=revealing ?
		map_intro_beats_at(intro_elapsed-MAP_INTRO_FRAME_US-intro_scan_us) :
		(struct map_intro_beats){1,1};
	double inset;
	struct plane plane=node_plane(w,h),border=border_plane(w,h,&inset);
	graphene_rect_t screen=GRAPHENE_RECT_INIT(0,0,w,h);
	struct profile_layout profile;
	struct profile_data *record;
	gboolean profile_visible=profile_layout_at(w,h,&profile,&record);
	if (!profile_visible && profile_node && profile_open) {
		g_clear_pointer(&profile_node,g_free);
		profile_open=FALSE;
		reset_route();
		gtk_widget_queue_draw(route_layer);
		watch_profile_age();
	}
	gboolean drew=FALSE;
	for (int pass=0;pass<2;pass++) {
		for(char **n=names;n&&*n;++n) {
			double x,y;
			if(!**n || !marker(plane,*n,&x,&y))continue;
			project_map_point(&x,&y);
			double radius = painted_marker_radius(*n)+1;
			double reach=annotation_reach(*n);
			graphene_rect_t bounds=GRAPHENE_RECT_INIT(x-radius,y-radius,2*radius,2*radius);
			if(pass) {
				const struct map_label *label=map_label_for(*n);
				bounds=GRAPHENE_RECT_INIT(x+reach+6+label->ink.x-2,
					marker_label_top(label,y)+label->ink.y-2,
					label->ink.width+4,label->ink.height+4);
			}
			graphene_rect_t clipped;
			if(!graphene_rect_intersection(&bounds,&screen,&clipped))continue;
			if (!(pass ? beats.label : beats.icon)) continue;
			if (pass && revealing) {
				snapshot_intro_label(snap,&clipped,border,inset,*n,x,y,reach,beats.label);
				drew=TRUE;
				continue;
			}
			cairo_t *cr=gtk_snapshot_append_cairo(snap,&clipped);
			plane_outline(cr,border,inset);
			cairo_clip(cr);
			if (!pass) {
				if (beats.icon<1) {
					cairo_translate(cr,x,y);
					cairo_scale(cr,beats.icon,beats.icon);
					cairo_translate(cr,-x,-y);
				}
				draw_symbol(cr,*n,x,y);
			} else draw_label(cr,*n,x,y,reach);
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

typedef GtkWidget MapDetail;
typedef GtkWidgetClass MapDetailClass;
G_DEFINE_TYPE(MapDetail,map_detail,GTK_TYPE_WIDGET)

static void detail_snapshot(GtkWidget *widget,GtkSnapshot *snap) {
	struct profile_layout profile;
	struct profile_data *record;
	if (profile_layout_at(gtk_widget_get_width(widget),gtk_widget_get_height(widget),
			&profile,&record)) profile_snapshot(snap,widget,profile,profile_node,record);
}

static void map_detail_class_init(MapDetailClass *c) { GTK_WIDGET_CLASS(c)->snapshot=detail_snapshot; }
static void map_detail_init(MapDetail *self) {
	gtk_widget_set_can_target(GTK_WIDGET(self),FALSE);
	gtk_widget_set_hexpand(GTK_WIDGET(self),TRUE);gtk_widget_set_vexpand(GTK_WIDGET(self),TRUE);
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
	stage_widget=g_object_new(map_stage_get_type(),NULL);
	gtk_widget_set_parent(overlay,stage_widget);
	gtk_window_set_child(hud,stage_widget);
	gpu_grid=retained_grid_new(&gpu_grid_ready,canvas);
	gtk_overlay_set_child(GTK_OVERLAY(overlay),gpu_grid);
	gtk_widget_set_hexpand(canvas,TRUE);
	gtk_widget_set_vexpand(canvas,TRUE);
	gtk_overlay_add_overlay(GTK_OVERLAY(overlay),canvas);
	g_object_unref(canvas);
	intro_layer=g_object_new(map_opening_get_type(),NULL);
	gtk_overlay_add_overlay(GTK_OVERLAY(overlay),intro_layer);
	gtk_widget_set_visible(intro_layer,FALSE);
	// One retained stack, bottom to top: grid/frame, radar, world (origin,
	// Nodes/routes, relay), minimap, Detail. The fast radar is a native
	// subsurface; it runs before world/Detail and shares the minimap
	// exclusion with the GTK radar fallback.
	origin_layer=g_object_new(map_origin_get_type(),NULL);
	gtk_overlay_add_overlay(GTK_OVERLAY(overlay),origin_layer);
	gtk_widget_set_visible(origin_layer,FALSE);
	route_layer=g_object_new(map_route_layer_get_type(),NULL);
	gtk_overlay_add_overlay(GTK_OVERLAY(overlay),route_layer);
	gtk_widget_set_visible(route_layer,FALSE);
	gtk_overlay_add_overlay(GTK_OVERLAY(overlay),annotations);
	relay_layer=g_object_new(map_relay_get_type(),NULL);
	gtk_overlay_add_overlay(GTK_OVERLAY(overlay),relay_layer);
	gtk_widget_set_visible(relay_layer,FALSE);
	minimap_layer=g_object_new(map_mini_get_type(),NULL);
	gtk_overlay_add_overlay(GTK_OVERLAY(overlay),minimap_layer);
	gtk_widget_set_visible(minimap_layer,FALSE);
	detail_layer=g_object_new(map_detail_get_type(),NULL);
	gtk_overlay_add_overlay(GTK_OVERLAY(overlay),detail_layer);
	gtk_widget_set_visible(detail_layer,FALSE);
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
		if(map_labels) g_hash_table_remove_all(map_labels);
		g_free(listed_names);
		listed_names = g_strdup(node_names);
		g_strfreev(names);
		names = g_strsplit(node_names, "\n", -1);
	}
	if (g_strcmp0(profile_rows,rows)!=0) {
		if(map_labels) g_hash_table_remove_all(map_labels);
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
		map_hud_relay_cancel();
		map_hud_cancel_zoom();
		map_hud_close_profile();
		stop_intro();
		intro_suppressed=FALSE;
	}
	wallpaper_update_frost(showing_map && frosted_map, outer_margin);
	if (hud == NULL) return;
	gboolean was_visible=gtk_widget_get_visible(annotations);
	sync_layers();
	if(changed) {
		clear_intro_labels();
		if (gtk_widget_get_visible(minimap_layer)) gtk_widget_queue_draw(minimap_layer);
		if (gtk_widget_get_visible(detail_layer)) gtk_widget_queue_draw(detail_layer);
		if (gtk_widget_get_visible(route_layer)) gtk_widget_queue_draw(route_layer);
	}
	if (gtk_widget_get_visible(annotations) && (changed || !was_visible)) gtk_widget_queue_draw(annotations);
}
