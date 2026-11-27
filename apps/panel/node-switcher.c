//go:build linux && cgo && panel

#include "node-switcher.h"
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
	if (!node_count || w < 260 || h < 160) return;
	const int desired = CLAMP(STRIP_PADDING * 2 + MIN(node_count,STRIP_MAX_TILES) * 120,
		STRIP_MIN_WIDTH,STRIP_MAX_WIDTH);
	const int width = MIN(w - STRIP_MARGIN * 2,desired);
	const int padding = width >= 380 ? STRIP_PADDING : 10;
	const int inside = width - padding * 2;
	const gboolean stacked = inside < 416;
	const int height = stacked ? 262 : 238;
	const size_t capacity = MAX(1,(inside + TILE_GAP) / (TILE_MIN_WIDTH + TILE_GAP));
	const size_t visible = MIN(node_count,MIN(STRIP_MAX_TILES,capacity));
	const size_t centered = selected_node > visible / 2 ? selected_node - visible / 2 : 0;
	const size_t first = MIN(centered,node_count - visible);
	const double tile = (inside - (visible - 1) * TILE_GAP) / (double)visible;

	// Fit a short parent output without cropping the instructions.
	const double scale = MIN(1.,(h - 2 * STRIP_MARGIN) / (double)height);
	cairo_save(cr);
	cairo_translate(cr,floor((w - width * scale) / 2),
		MAX(STRIP_MARGIN,floor((h - height * scale) / 2 - 12)));
	cairo_scale(cr,scale,scale);
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
	g_snprintf(position,sizeof(position),"%zu / %zu",selected_node + 1,node_count);
	pango_layout_set_font_description(layout,body);
	const int position_width = text_width(layout,position);
	const int title_width = MAX(0,inside - position_width - 8);
	label(cr,layout,&quiet,width - padding - position_width,14,position_width,
		PANGO_ALIGN_RIGHT,position);
	pango_layout_set_font_description(layout,title_font);
	label(cr,layout,&primary,padding,11,title_width,PANGO_ALIGN_LEFT,"NODE SWITCHER");

	gboolean target_truncated = FALSE;
	for (size_t i = first; i < first + visible; ++i) {
		cairo_save(cr);
		cairo_translate(cr,padding + (i - first) * (tile + TILE_GAP),59);
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
		if (current || !nodes[i].window_count) {
			pango_layout_set_font_description(layout,body);
			pango_layout_set_height(layout,-1);
			label(cr,layout,&quiet,2,MAX(67,name_y + name_height),tile - 4,
				PANGO_ALIGN_CENTER,current ? "Current" : "Empty");
		}
		cairo_restore(cr);
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
	draw_guide(cr,layout,inside,202,stacked);
	cairo_restore(cr);
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
	if (selected >= length) {
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
		layer = paint_only_layer("mothership-node-switcher","Mothership Node Switcher",draw,&canvas);
		g_object_add_weak_pointer(G_OBJECT(layer),(gpointer *)&layer);
	}
	gtk_widget_queue_draw(canvas);
	gtk_widget_set_visible(GTK_WIDGET(layer),TRUE);
}

void node_switcher_hide(void) {
	if (layer) gtk_widget_set_visible(GTK_WIDGET(layer),FALSE);
	clear_nodes();
}
