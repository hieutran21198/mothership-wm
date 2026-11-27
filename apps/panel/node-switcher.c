//go:build linux && cgo && panel

#include "node-switcher.h"
#include <math.h>
#include "map-fleet.h"
#include "shell-paint.h"
#include "shell-type.h"

enum {
	STRIP_MAX_WIDTH = 630, STRIP_MIN_WIDTH = 380, STRIP_MAX_TILES = 5,
	STRIP_MARGIN = 12, STRIP_PADDING = 17, TILE_GAP = 6, TILE_MIN_WIDTH = 91
};

struct switcher_node {
	char *name, *description;
	int window_count;
};

static GtkWindow *layer;
static GtkWidget *canvas;
static struct switcher_node *nodes;
static size_t node_count, current_node, selected_node;
static gboolean beam;
static double beam_from_x, beam_from_y, beam_x, beam_y;
enum { TILE_TOP = 59, TILE_HEIGHT = 91, FULL_NODE = 8 };

// Shared by drawing and the beam hit test, so a drop lands on what is drawn.
struct strip_geometry {
	double ox, oy, scale, tile;
	int width, height, padding, inside;
	size_t first, visible;
	gboolean stacked;
};
static gboolean strip_geometry(int w, int h, struct strip_geometry *g) {
	if (!node_count || w < 260 || h < 160) return FALSE;
	const int desired = CLAMP(STRIP_PADDING * 2 + MIN(node_count,STRIP_MAX_TILES) * 120,
		STRIP_MIN_WIDTH,STRIP_MAX_WIDTH);
	g->width = MIN(w - STRIP_MARGIN * 2,desired);
	g->padding = g->width >= 380 ? STRIP_PADDING : 10;
	g->inside = g->width - g->padding * 2;
	g->stacked = g->inside < 416;
	g->height = g->stacked ? 262 : 238;
	const size_t capacity = MAX(1,(g->inside + TILE_GAP) / (TILE_MIN_WIDTH + TILE_GAP));
	g->visible = MIN(node_count,MIN(STRIP_MAX_TILES,capacity));
	size_t anchor = selected_node < node_count ? selected_node : current_node < node_count ? current_node : 0;
	const size_t centered = anchor > g->visible / 2 ? anchor - g->visible / 2 : 0;
	g->first = MIN(centered,node_count - g->visible);
	g->tile = (g->inside - (g->visible - 1) * TILE_GAP) / (double)g->visible;
	g->scale = MIN(1.,(h - 2 * STRIP_MARGIN) / (double)g->height);
	g->ox = floor((w - g->width * g->scale) / 2);
	g->oy = MAX(STRIP_MARGIN,floor((h - g->height * g->scale) / 2 - 12));
	return TRUE;
}
static gboolean beam_eligible(size_t i) {
	return i < node_count && i != current_node && nodes[i].window_count < FULL_NODE;
}

static const GdkRGBA primary = {.90,.96,.98,1};
static const GdkRGBA quiet = {.67,.77,.82,1};
static const GdkRGBA selected_ink = {.65,.95,1,1};
static const GdkRGBA divider = {.27,.40,.48,.95};

static void outline(cairo_t *cr, double x, double y, double w, double h,
		double cut, double inset) {
	double left = x + inset, right = x + w - inset;
	double top = y + inset, bottom = y + h - inset;
	cut -= inset * (2 - G_SQRT2);
	cairo_move_to(cr,left + cut,top);
	cairo_line_to(cr,right - cut,top);
	cairo_line_to(cr,right,top + cut);
	cairo_line_to(cr,right,bottom - cut);
	cairo_line_to(cr,right - cut,bottom);
	cairo_line_to(cr,left + cut,bottom);
	cairo_line_to(cr,left,bottom - cut);
	cairo_line_to(cr,left,top + cut);
	cairo_close_path(cr);
}

static void face(cairo_t *cr, int w, int h) {
	outline(cr,0,0,w,h,9,.5);
	cairo_pattern_t *glass = cairo_pattern_create_linear(0,0,w*.45,h);
	cairo_pattern_add_color_stop_rgba(glass,0,.078,.165,.216,.93);
	cairo_pattern_add_color_stop_rgba(glass,1,.039,.090,.141,.97);
	cairo_set_source(cr,glass);
	cairo_fill_preserve(cr);
	cairo_pattern_destroy(glass);
	cairo_set_line_width(cr,1);
	cairo_set_source_rgba(cr,.48,.61,.67,.95);
	cairo_stroke(cr);
}

static void selection(cairo_t *cr, double w) {
	const int h = 91;
	const double inset = .5, cut = 6 - inset * (2 - G_SQRT2), arm = 13;
	outline(cr,0,0,w,h,6,0);
	cairo_set_source_rgba(cr,.17,.41,.47,.40);
	cairo_fill(cr);
	for (int sx = -1; sx <= 1; sx += 2) {
		for (int sy = -1; sy <= 1; sy += 2) {
			double x = sx > 0 ? inset : w - inset;
			double y = sy > 0 ? inset : h - inset;
			cairo_move_to(cr,x + sx * arm,y);
			cairo_line_to(cr,x + sx * cut,y);
			cairo_line_to(cr,x,y + sy * cut);
			cairo_line_to(cr,x,y + sy * arm);
		}
	}
	cairo_set_line_width(cr,1);
	cairo_set_source_rgba(cr,.55,.86,.92,1);
	cairo_stroke(cr);
}

static void ship(cairo_t *cr, double x, gboolean selected, gboolean current) {
	double alpha = selected ? 1 : current ? .82 : .55;
	cairo_save(cr);
	cairo_translate(cr,x,25);
	cairo_scale(cr,1.15,1.15);
	cairo_move_to(cr,fleet_node[0].x,fleet_node[0].y);
	for (size_t i = 1; i < FLEET_COUNT(fleet_node); ++i)
		cairo_line_to(cr,fleet_node[i].x,fleet_node[i].y);
	cairo_close_path(cr);
	cairo_set_source_rgba(cr,.13,.27,.34,alpha);
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr,.56,.81,.87,alpha);
	cairo_set_line_width(cr,1);
	cairo_set_line_join(cr,CAIRO_LINE_JOIN_ROUND);
	cairo_stroke(cr);
	cairo_move_to(cr,0,-7.5);
	cairo_line_to(cr,2.2,-4.9);
	cairo_line_to(cr,1.7,-2.5);
	cairo_line_to(cr,-1.7,-2.5);
	cairo_line_to(cr,-2.2,-4.9);
	cairo_close_path(cr);
	cairo_set_source_rgba(cr,.70,.89,.93,alpha);
	cairo_fill(cr);
	cairo_restore(cr);
}

static int text_width(PangoLayout *layout, const char *text) {
	int w;
	pango_layout_set_width(layout,-1);
	pango_layout_set_text(layout,text,-1);
	pango_layout_get_pixel_size(layout,&w,NULL);
	return w;
}

static void label(cairo_t *cr, PangoLayout *layout, const GdkRGBA *ink,
		double x, double y, int width, PangoAlignment alignment, const char *text) {
	pango_layout_set_alignment(layout,alignment);
	pango_layout_set_width(layout,width * PANGO_SCALE);
	pango_layout_set_text(layout,text,-1);
	gdk_cairo_set_source_rgba(cr,ink);
	cairo_move_to(cr,x,y);
	pango_cairo_show_layout(cr,layout);
}

static void rule(cairo_t *cr, int width, double y, int padding) {
	cairo_move_to(cr,padding,y + .5);
	cairo_line_to(cr,width - padding,y + .5);
	cairo_set_line_width(cr,1);
	gdk_cairo_set_source_rgba(cr,&divider);
	cairo_stroke(cr);
}

static void keycap(cairo_t *cr, PangoLayout *layout,
		double x, double y, int width, const char *text) {
	outline(cr,x,y,width,18,2,.5);
	cairo_set_source_rgb(cr,.035,.082,.133);
	cairo_fill_preserve(cr);
	cairo_set_line_width(cr,1);
	cairo_set_source_rgb(cr,.34,.45,.52);
	cairo_stroke(cr);
	label(cr,layout,&primary,x,y + 1,width,PANGO_ALIGN_CENTER,text);
}

static void draw_guide(cairo_t *cr, PangoLayout *layout, int width, int y,
		gboolean stacked) {
	int actions = stacked ? 0 : width - 216;
	int action_y = stacked ? y + 24 : y;
	keycap(cr,layout,0,y,31,"Tab");
	label(cr,layout,&quiet,37,y + 1,31,PANGO_ALIGN_LEFT,"Next");
	keycap(cr,layout,74,y,38,"Shift");
	keycap(cr,layout,116,y,31,"Tab");
	label(cr,layout,&quiet,153,y + 1,35,PANGO_ALIGN_LEFT,"Back");
	keycap(cr,layout,actions,action_y,88,"Release Meta");
	label(cr,layout,&selected_ink,actions + 94,action_y + 1,41,PANGO_ALIGN_LEFT,"Switch");
	keycap(cr,layout,actions + 141,action_y,31,"Esc");
	label(cr,layout,&quiet,actions + 178,action_y + 1,38,PANGO_ALIGN_LEFT,"Cancel");
}

static void draw(GtkDrawingArea *area, cairo_t *cr, int w, int h, gpointer data) {
	(void)area;
	(void)data;
	struct strip_geometry g;
	if (!strip_geometry(w,h,&g)) return;
	const int width = g.width, padding = g.padding, inside = g.inside, height = g.height;
	const gboolean stacked = g.stacked;
	const size_t visible = g.visible, first = g.first;
	const double tile = g.tile;

	// Fit a short parent output without cropping the instructions.
	cairo_save(cr);
	cairo_translate(cr,g.ox,g.oy);
	cairo_scale(cr,g.scale,g.scale);
	face(cr,width,height);
	rule(cr,width,43,padding);
	rule(cr,width,189,padding);

	g_autoptr(PangoLayout) layout = pango_cairo_create_layout(cr);
	g_autoptr(PangoFontDescription) title_font = shell_font_description(SHELL_DISPLAY_FALLBACK,
		19,PANGO_WEIGHT_NORMAL);
	g_autoptr(PangoFontDescription) node_font = shell_font_description(SHELL_DISPLAY_FALLBACK,
		17,PANGO_WEIGHT_NORMAL);
	g_autoptr(PangoFontDescription) body = shell_font_description(NULL,13,PANGO_WEIGHT_NORMAL);
	g_autoptr(PangoAttrList) text_attributes = pango_attr_list_new();
	pango_attr_list_insert(text_attributes,pango_attr_insert_hyphens_new(FALSE));
	pango_layout_set_attributes(layout,text_attributes);
	pango_layout_set_ellipsize(layout,PANGO_ELLIPSIZE_END);
	pango_layout_set_height(layout,-1);
	char position[48];
	if (beam) g_strlcpy(position,"",sizeof(position));
	else g_snprintf(position,sizeof(position),"%zu / %zu",selected_node + 1,node_count);
	pango_layout_set_font_description(layout,body);
	const int position_width = text_width(layout,position);
	const int title_width = MAX(0,inside - position_width - 8);
	label(cr,layout,&quiet,width - padding - position_width,14,position_width,
		PANGO_ALIGN_RIGHT,position);
	pango_layout_set_font_description(layout,title_font);
	label(cr,layout,&primary,padding,11,title_width,PANGO_ALIGN_LEFT,beam ? "MOVE WINDOW TO" : "NODE SWITCHER");

	gboolean target_truncated = FALSE;
	for (size_t i = first; i < first + visible; ++i) {
		cairo_save(cr);
		cairo_translate(cr,padding + (i - first) * (tile + TILE_GAP),TILE_TOP);
		gboolean full = beam && i != current_node && nodes[i].window_count >= FULL_NODE;
		if (full) cairo_push_group(cr);
		gboolean selected = i == selected_node, current = i == current_node;
		if (selected) selection(cr,tile);
		ship(cr,tile / 2,selected,current);
		pango_layout_set_font_description(layout,node_font);
		pango_layout_set_wrap(layout,PANGO_WRAP_WORD_CHAR);
		pango_layout_set_height(layout,-2);
		pango_layout_set_spacing(layout,-3 * PANGO_SCALE);
		const int name_width = tile - 8;
		const int name_y = text_width(layout,nodes[i].name) > name_width ? 37 : 44;
		label(cr,layout,selected ? &selected_ink : &primary,4,name_y,name_width,
			PANGO_ALIGN_CENTER,nodes[i].name);
		if (selected) target_truncated = pango_layout_is_ellipsized(layout);
		int name_height;
		pango_layout_get_pixel_size(layout,NULL,&name_height);
		pango_layout_set_spacing(layout,0);
		if (current || !nodes[i].window_count || full) {
			pango_layout_set_font_description(layout,body);
			pango_layout_set_height(layout,-1);
			label(cr,layout,&quiet,2,MAX(67,name_y + name_height),tile - 4,
				PANGO_ALIGN_CENTER,current ? "Current" : full ? "Full · 8/8" : "Empty");
		}
		if (full) {
			cairo_pop_group_to_source(cr);
			cairo_paint_with_alpha(cr,.45);
		}
		cairo_restore(cr);
	}

	if (beam && selected_node >= node_count) {
		pango_layout_set_font_description(layout,body);
		pango_layout_set_height(layout,-1);
		label(cr,layout,&quiet,padding,159,inside,PANGO_ALIGN_CENTER,"Drop on a Node to move the window there.");
		cairo_restore(cr);
		return;
	}
	const struct switcher_node *target = &nodes[selected_node];
	g_autofree char *window_count = target->window_count == 0 ? g_strdup("No open windows") :
		g_strdup_printf("%d %s",target->window_count,target->window_count == 1 ? "window" : "windows");
	pango_layout_set_font_description(layout,body);
	pango_layout_set_height(layout,-1);
	if (target_truncated) {
		// Make room for the exact name when its tile could not display it.
		pango_font_description_set_absolute_size(body,11 * PANGO_SCALE);
		pango_layout_set_font_description(layout,body);
		pango_layout_set_height(layout,-2);
		pango_layout_set_ellipsize(layout,PANGO_ELLIPSIZE_MIDDLE);
		label(cr,layout,&quiet,padding,152,inside,PANGO_ALIGN_CENTER,target->name);
		pango_layout_set_height(layout,-1);
		pango_layout_set_ellipsize(layout,PANGO_ELLIPSIZE_END);
		label(cr,layout,&quiet,padding,174,inside,PANGO_ALIGN_CENTER,window_count);
	} else if (*target->description) {
		g_autofree char *count_suffix = g_strdup_printf(" · %s",window_count);
		int suffix_width = text_width(layout,count_suffix);
		int description_width = MIN(text_width(layout,target->description),MAX(0,inside - suffix_width));
		int x = (width - suffix_width - description_width) / 2;
		label(cr,layout,&quiet,x,159,description_width,PANGO_ALIGN_LEFT,target->description);
		label(cr,layout,&quiet,x + description_width,159,suffix_width,PANGO_ALIGN_LEFT,count_suffix);
	} else {
		label(cr,layout,&quiet,padding,159,inside,PANGO_ALIGN_CENTER,window_count);
	}

	pango_font_description_set_absolute_size(body,12 * PANGO_SCALE);
	pango_layout_set_font_description(layout,body);
	cairo_translate(cr,padding,0);
	if (!beam) draw_guide(cr,layout,inside,202,stacked);
	else {
		keycap(cr,layout,0,202,40,"Drop");
		label(cr,layout,&selected_ink,46,203,40,PANGO_ALIGN_LEFT,"Move");
		keycap(cr,layout,92,202,31,"Esc");
		label(cr,layout,&quiet,129,203,46,PANGO_ALIGN_LEFT,"Cancel");
	}
	cairo_restore(cr);
}

// The tractor beam: a dashed cyan line from the dragged card to the
// highlighted tile, or to the pointer while nothing is highlighted.
static void draw_beam(cairo_t *cr, int w, int h) {
	struct strip_geometry g;
	if (!beam || !strip_geometry(w,h,&g)) return;
	double tx = beam_x, ty = beam_y;
	if (selected_node < node_count && selected_node >= g.first && selected_node < g.first + g.visible) {
		tx = g.ox + (g.padding + (selected_node - g.first) * (g.tile + TILE_GAP) + g.tile / 2) * g.scale;
		ty = g.oy + (TILE_TOP + TILE_HEIGHT) * g.scale;
	}
	const double dash[] = {5,4};
	cairo_save(cr);
	cairo_set_dash(cr,dash,2,0);
	cairo_set_line_width(cr,1.5);
	cairo_set_source_rgba(cr,.56,.85,.89,.9);
	cairo_move_to(cr,beam_from_x,beam_from_y);
	cairo_line_to(cr,tx,ty);
	cairo_stroke(cr);
	cairo_restore(cr);
}

static void draw_layer(GtkDrawingArea *area, cairo_t *cr, int w, int h, gpointer data) {
	draw(area,cr,w,h,data);
	draw_beam(cr,w,h);
}

static void clear_nodes(void) {
	for (size_t i = 0; i < node_count; ++i) {
		g_free(nodes[i].name);
		g_free(nodes[i].description);
	}
	g_clear_pointer(&nodes,g_free);
	node_count = 0;
}

void node_switcher_show(const char *const *names, const char *const *descriptions,
		const int *counts, size_t length, size_t current, size_t selected) {
	if (selected >= length && !beam) {
		node_switcher_hide();
		return;
	}
	clear_nodes();
	nodes = g_new(struct switcher_node,length);
	for (size_t i = 0; i < length; ++i) {
		nodes[i].name = g_utf8_make_valid(names[i],-1);
		nodes[i].description = g_utf8_make_valid(descriptions[i] ? descriptions[i] : "",-1);
		nodes[i].window_count = counts[i];
	}
	node_count = length;
	current_node = current;
	selected_node = selected;
	if (!layer) {
		layer = paint_only_layer("mothership-node-switcher","Mothership Node Switcher",draw_layer,&canvas);
		g_object_add_weak_pointer(G_OBJECT(layer),(gpointer *)&layer);
	}
	gtk_widget_queue_draw(canvas);
	gtk_widget_set_visible(GTK_WIDGET(layer),TRUE);
}

void node_switcher_hide(void) {
	if (layer) gtk_widget_set_visible(GTK_WIDGET(layer),FALSE);
	clear_nodes();
	beam = FALSE;
}

void node_switcher_beam(const char *const *names, const char *const *descriptions,
		const int *counts, size_t length, size_t current, double from_x, double from_y) {
	if (!length) return;
	beam = TRUE;
	beam_from_x = beam_x = from_x;
	beam_from_y = beam_y = from_y;
	node_switcher_show(names,descriptions,counts,length,current,SIZE_MAX);
}

size_t node_switcher_beam_move(double x, double y) {
	if (!beam || !canvas) return SIZE_MAX;
	beam_x = x;
	beam_y = y;
	struct strip_geometry g;
	size_t hit = SIZE_MAX;
	if (strip_geometry(gtk_widget_get_width(canvas),gtk_widget_get_height(canvas),&g)) {
		double lx = (x - g.ox) / g.scale - g.padding, ly = (y - g.oy) / g.scale;
		if (lx >= 0 && ly >= TILE_TOP && ly <= TILE_TOP + TILE_HEIGHT) {
			size_t i = g.first + (size_t)(lx / (g.tile + TILE_GAP));
			double within = fmod(lx,g.tile + TILE_GAP);
			if (i < g.first + g.visible && within <= g.tile && beam_eligible(i)) hit = i;
		}
	}
	selected_node = hit;
	gtk_widget_queue_draw(canvas);
	return hit;
}
