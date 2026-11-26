//go:build linux && cgo && wlroots

#include "group-chrome.h"
#include <cairo.h>
#include <drm_fourcc.h>
#include <math.h>
#include <pango/pangocairo.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_scene.h>

enum { GROUP_CUT=10, GROUP_BUTTON_SIZE=24, GROUP_BUTTON_GAP=8, GROUP_BUTTON_EDGE=16 };
struct group_strip { struct wlr_scene_buffer *image; };
struct group_chrome {
	struct wlr_scene_tree *tree;
	struct group_strip strips[4];
	int w,h;
	double scale;
	unsigned number;
	bool focused;
};
struct group_pixels { struct wlr_buffer buffer; cairo_surface_t *surface; };
static bool read_pixels(struct wlr_buffer *b,uint32_t flags,void **data,uint32_t *fmt,size_t *stride) {
	if(flags&WLR_BUFFER_DATA_PTR_ACCESS_WRITE)return false;
	struct group_pixels *p=wl_container_of(b,p,buffer);
	*data=cairo_image_surface_get_data(p->surface);
	*fmt=DRM_FORMAT_ARGB8888;
	*stride=cairo_image_surface_get_stride(p->surface);
	return true;
}
static void read_end(struct wlr_buffer *b) {(void)b;}
static void pixels_drop(struct wlr_buffer *b) {
	struct group_pixels *p=wl_container_of(b,p,buffer);
	cairo_surface_destroy(p->surface);free(p);
}
static const struct wlr_buffer_impl pixels_impl={
	.destroy=pixels_drop,.begin_data_ptr_access=read_pixels,.end_data_ptr_access=read_end,
};

static void contour(cairo_t *cr,int w,int h,double inset,int cut) {
	double a=inset,b=w-inset,c=h-inset;
	cairo_new_path(cr);
	cairo_move_to(cr,a+cut,a);cairo_line_to(cr,b-cut,a);
	cairo_line_to(cr,b,a+cut);cairo_line_to(cr,b,c-cut);
	cairo_line_to(cr,b-cut,c);cairo_line_to(cr,a+cut,c);
	cairo_line_to(cr,a,c-cut);cairo_line_to(cr,a,a+cut);cairo_close_path(cr);
}
struct wlr_box group_chrome_control_box(const struct group_chrome *g,enum group_control kind) {
	if(!g || kind<GROUP_CONTROL_LAYOUT || kind>GROUP_CONTROL_MINIMIZE)return (struct wlr_box){0};
	int x=g->w-GROUP_BUTTON_EDGE-GROUP_BUTTON_SIZE;
	if(kind==GROUP_CONTROL_LAYOUT)x-=GROUP_BUTTON_SIZE+GROUP_BUTTON_GAP;
	return (struct wlr_box){x,(GROUP_FRAME_RAIL-GROUP_BUTTON_SIZE)/2,GROUP_BUTTON_SIZE,GROUP_BUTTON_SIZE};
}
static PangoFontMap *group_font_map;

bool group_chrome_add_font(const char *path) {
	if(!group_font_map) group_font_map=pango_cairo_font_map_new();
	return pango_font_map_add_font_file(group_font_map,path,NULL);
}

void group_chrome_clear_fonts(void) {
	g_clear_object(&group_font_map);
}

static void text(cairo_t *cr,const char *value,const char *font,double size,int x,int y,int max) {
	PangoLayout *p;
	if(group_font_map) {
		PangoContext *context=pango_font_map_create_context(group_font_map);
		pango_cairo_update_context(cr,context);
		p=pango_layout_new(context);
		g_object_unref(context);
	} else p=pango_cairo_create_layout(cr);
	PangoFontDescription *d=pango_font_description_new();
	pango_font_description_set_family(d,font);
	pango_font_description_set_absolute_size(d,size*PANGO_SCALE);
	pango_layout_set_font_description(p,d);
	pango_layout_set_single_paragraph_mode(p,true);
	pango_layout_set_width(p,max*PANGO_SCALE);
	pango_layout_set_ellipsize(p,PANGO_ELLIPSIZE_END);
	pango_layout_set_text(p,value,-1);
	cairo_move_to(cr,x,y);pango_cairo_show_layout(cr,p);
	pango_font_description_free(d);g_object_unref(p);
}

static void paint_control(cairo_t *cr,struct wlr_box box,enum group_control kind) {
	cairo_save(cr);
	cairo_translate(cr,box.x+box.width*.5,box.y+box.height*.5);
	cairo_set_source_rgb(cr,.82,.93,.96);
	cairo_set_line_width(cr,1.25);
	cairo_set_line_cap(cr,CAIRO_LINE_CAP_SQUARE);
	if(kind==GROUP_CONTROL_LAYOUT) {
		cairo_rectangle(cr,-6,-5,12,10);
		cairo_move_to(cr,-.5,-5);cairo_line_to(cr,-.5,5);
	} else {
		cairo_move_to(cr,-5,2);cairo_line_to(cr,5,2);
	}
	cairo_stroke(cr);
	cairo_restore(cr);
}

static struct group_pixels *paint(struct group_chrome *g,struct wlr_box b,bool header) {
	struct group_pixels *p=calloc(1,sizeof(*p));
	if(!p)return NULL;
	int pw=ceil(b.width*g->scale),ph=ceil(b.height*g->scale);
	p->surface=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,pw,ph);
	if(cairo_surface_status(p->surface)!=CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(p->surface);free(p);return NULL;
	}
	cairo_t *cr=cairo_create(p->surface);
	cairo_scale(cr,g->scale,g->scale);cairo_translate(cr,-b.x,-b.y);
	contour(cr,g->w,g->h,.5,GROUP_CUT);
	cairo_set_source_rgba(cr,.39,.68,.76,g->focused?.83:.52);
	cairo_set_line_width(cr,1);cairo_stroke(cr);
	if(header) {
		char name[32];snprintf(name,sizeof(name),"GROUP %02u",g->number);
		cairo_set_source_rgb(cr,.82,.94,.96);
		int title_end=group_chrome_control_box(g,GROUP_CONTROL_LAYOUT).x-GROUP_BUTTON_EDGE;
		text(cr,name,"Black Ops One, Chakra Petch, Sans",15,GROUP_BUTTON_EDGE,12,title_end-GROUP_BUTTON_EDGE);
		for(enum group_control i=GROUP_CONTROL_LAYOUT;i<=GROUP_CONTROL_MINIMIZE;i++)
			paint_control(cr,group_chrome_control_box(g,i),i);
	}
	bool ok=cairo_status(cr)==CAIRO_STATUS_SUCCESS;cairo_destroy(cr);
	if(!ok){cairo_surface_destroy(p->surface);free(p);return NULL;}
	cairo_surface_flush(p->surface);wlr_buffer_init(&p->buffer,&pixels_impl,pw,ph);
	return p;
}

static bool header_accepts_input(struct wlr_scene_buffer *buffer,double *sx,double *sy) {
	const struct group_chrome *g=buffer->node.data;
	return *sy>=GROUP_FRAME_RAIL || group_chrome_control(g,*sx,*sy)!=GROUP_CONTROL_NONE;
}

struct group_chrome *group_chrome_create(struct wlr_scene_tree *parent) {
	struct group_chrome *g=calloc(1,sizeof(*g));if(!g)return NULL;
	g->tree=wlr_scene_tree_create(parent);
	if(!g->tree){free(g);return NULL;}
	for(int i=0;i<4;i++) {
		g->strips[i].image=wlr_scene_buffer_create(g->tree,NULL);
		if(!g->strips[i].image){group_chrome_destroy(g);return NULL;}
	}
	g->strips[0].image->node.data=g;
	g->strips[0].image->point_accepts_input=header_accepts_input;
	wlr_scene_node_set_enabled(&g->tree->node,false);
	return g;
}
void group_chrome_destroy(struct group_chrome *g) {
	if(g){wlr_scene_node_destroy(&g->tree->node);free(g);}
}
struct wlr_scene_tree *group_chrome_tree(struct group_chrome *g){return g?g->tree:NULL;}
void group_chrome_place(struct group_chrome *g,int x,int y,int w,int h,double scale,
		unsigned number,bool focus) {
	if(!g || w<260 || h<GROUP_FRAME_TOP+20)return;
	wlr_scene_node_set_position(&g->tree->node,x,y);
	wlr_scene_node_set_enabled(&g->tree->node,true);
	if(g->w==w && g->h==h && g->scale==scale && g->number==number && g->focused==focus)return;
	g->w=w;g->h=h;g->scale=scale;g->number=number;g->focused=focus;
	const int cap=GROUP_FRAME_PAD+1;
	struct wlr_box boxes[]={{0,0,w,GROUP_FRAME_TOP},{0,h-cap,w,cap},
		{0,GROUP_FRAME_TOP,cap,h-GROUP_FRAME_TOP-cap},
		{w-cap,GROUP_FRAME_TOP,cap,h-GROUP_FRAME_TOP-cap}};
	for(int i=0;i<4;i++) {
		struct group_pixels *p=paint(g,boxes[i],i==0);if(!p)continue;
		struct wlr_scene_buffer *b=g->strips[i].image;
		wlr_scene_node_set_position(&b->node,boxes[i].x,boxes[i].y);
		wlr_scene_buffer_set_dest_size(b,boxes[i].width,boxes[i].height);
		wlr_scene_buffer_set_buffer(b,&p->buffer);wlr_buffer_drop(&p->buffer);
	}
}
bool group_chrome_owns_node(const struct group_chrome *g,const struct wlr_scene_node *n) {
	return g && n && n->parent==g->tree;
}
enum group_control group_chrome_control(const struct group_chrome *g,double x,double y) {
	if(!g)return GROUP_CONTROL_NONE;
	for(enum group_control i=GROUP_CONTROL_LAYOUT;i<=GROUP_CONTROL_MINIMIZE;i++) {
		struct wlr_box b=group_chrome_control_box(g,i);
		if(wlr_box_contains_point(&b,x,y))return i;
	}
	return GROUP_CONTROL_NONE;
}
