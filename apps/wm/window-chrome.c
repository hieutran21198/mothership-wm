//go:build linux && cgo && wlroots

#include "window-chrome.h"
#include <cairo.h>
#include <drm_fourcc.h>
#include <math.h>
#include <pango/pangocairo.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-util.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>

enum { TOP, BOTTOM, LEFT, RIGHT, STRIPS };
enum { RAIL_Y=24, PANEL_RISE=13, CUT=12, BUTTON_GAP=4, BUTTON_EDGE=25,
	BUTTON_MIN=13, BUTTON_MAX=23, POCKET_PAD=7, DOCK_INSET=12, BUTTON_Y_NUDGE=3 };
#define INNER_FRAME_INSET 3.8
#define INNER_FRAME_STROKE .9
#define BUTTON_FACE_INSET 1.
#define BUTTON_STROKE .8
struct point { double x,y; };

struct chrome_pixels {
	struct wlr_buffer buffer;
	cairo_surface_t *image;
};

struct chrome_strip {
	struct wlr_scene_buffer *scene;
	struct wlr_box box;
	int pixels_w,pixels_h;
};

struct window_chrome {
	struct wlr_scene_tree *tree;
	struct chrome_strip strips[STRIPS];
	int w,h,title_width;
	double scale;
	bool focused,floating,maximized,clipped;
	char *title;
	struct wlr_box aperture;
};

static bool access_pixels(struct wlr_buffer *buffer,uint32_t flags,void **data,uint32_t *format,size_t *stride) {
	if (flags&WLR_BUFFER_DATA_PTR_ACCESS_WRITE) return false;
	const struct chrome_pixels *p=wl_container_of(buffer,p,buffer);
	*data=cairo_image_surface_get_data(p->image);
	*stride=cairo_image_surface_get_stride(p->image);
	*format=DRM_FORMAT_ARGB8888;
	return true;
}

static void end_pixels(struct wlr_buffer *unused) { (void)unused; }

static void free_pixels(struct wlr_buffer *buffer) {
	struct chrome_pixels *p=wl_container_of(buffer,p,buffer);
	cairo_surface_destroy(p->image);
	free(p);
}

static const struct wlr_buffer_impl pixel_impl={
	.destroy=free_pixels,.begin_data_ptr_access=access_pixels,.end_data_ptr_access=end_pixels,
};

static struct wlr_box control(const struct window_chrome *c,int index) {
	// Reserve a level bridge between the dock's downward shoulder and
	// the frame's outer chamfer. Give back only extra inset on tight widths.
	const int room=c->w-2*CUT-2*BUTTON_EDGE-2*BUTTON_GAP;
	if (room<3*BUTTON_MIN) return (struct wlr_box){0};
	const int spare=room-3*BUTTON_MIN;
	const int inset=spare<DOCK_INSET?spare:DOCK_INSET;
	const int cap=(room-inset)/3;
	const int size=cap<BUTTON_MAX?cap:BUTTON_MAX;
	return (struct wlr_box){c->w-CUT-BUTTON_EDGE-inset-3*size-2*BUTTON_GAP+index*(size+BUTTON_GAP),
		RAIL_Y+1+BUTTON_Y_NUDGE-size/2,size,size};
}

static size_t contour(const struct window_chrome *c,struct point p[12]) {
	const double w=c->w,h=c->h;
	const struct wlr_box first=control(c,0),last=control(c,2);
	const double x0=first.x-POCKET_PAD,x1=last.x+last.width+POCKET_PAD;
	size_t n=0;
	p[n++]=(struct point){.75,RAIL_Y+CUT};
	p[n++]=(struct point){CUT+.75,RAIL_Y};
	// Collapse only the raised pocket, not its controls, at truly narrow sizes.
	if (first.width && x0-PANEL_RISE>CUT+3 && x1+PANEL_RISE<w-CUT-3) {
		p[n++]=(struct point){x0-PANEL_RISE,RAIL_Y};
		p[n++]=(struct point){x0,RAIL_Y-PANEL_RISE};
		p[n++]=(struct point){x1,RAIL_Y-PANEL_RISE};
		p[n++]=(struct point){x1+PANEL_RISE,RAIL_Y};
	}
	p[n++]=(struct point){w-CUT-.75,RAIL_Y};
	p[n++]=(struct point){w-.75,RAIL_Y+CUT};
	p[n++]=(struct point){w-.75,h-CUT-.75};
	p[n++]=(struct point){w-CUT-.75,h-.75};
	p[n++]=(struct point){CUT+.75,h-.75};
	p[n++]=(struct point){.75,h-CUT-.75};
	return n;
}

static struct point normal(struct point a,struct point b) {
	double x=b.x-a.x,y=b.y-a.y,length=hypot(x,y);
	return (struct point){-y/length,x/length};
}

// Intersect offset edge lines. The normal distance is shared by the
// straight runs and every 45-degree side of the outline, even at the pocket.
static void path(cairo_t *cr,const struct point *p,size_t n,double inset) {
	cairo_new_path(cr);
	for (size_t i=0;i<n;i++) {
		struct point a=normal(p[(i+n-1)%n],p[i]),b=normal(p[i],p[(i+1)%n]);
		double m=inset/(1+a.x*b.x+a.y*b.y);
		double x=p[i].x+(a.x+b.x)*m,y=p[i].y+(a.y+b.y)*m;
		if (!i) cairo_move_to(cr,x,y);
		else cairo_line_to(cr,x,y);
	}
	cairo_close_path(cr);
}

static void button(cairo_t *cr,struct wlr_box b,int which,bool focused,bool active) {
	if (!b.width) return;
	// The smaller painted face is carried inside the unchanged hit size.
	// Keep visible clearance to the inner roof even in the focused/active ink.
	const double x=b.x+.5+BUTTON_FACE_INSET,y=b.y+.5+BUTTON_FACE_INSET;
	const double w=b.width-1-2*BUTTON_FACE_INSET,h=b.height-1-2*BUTTON_FACE_INSET,k=3;
	struct point p[]={{x+k,y},{x+w-k,y},{x+w,y+k},{x+w,y+h-k},
		{x+w-k,y+h},{x+k,y+h},{x,y+h-k},{x,y+k}};
	path(cr,p,sizeof(p)/sizeof(p[0]),0);
	cairo_set_source_rgb(cr,active?.135:.085,active?.24:.13,active?.29:.17);
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr,.39,.58,.66,focused?.62:.36);
	cairo_set_line_width(cr,BUTTON_STROKE);
	cairo_stroke(cr);

	cairo_save(cr);
	cairo_translate(cr,b.x+b.width*.5,b.y+b.height*.5);
	cairo_scale(cr,(b.width-1)/22.,(b.height-1)/22.);
	cairo_set_source_rgb(cr,which==2?.87:.62,which==2?.63:.79,which==2?.65:.84);
	cairo_set_line_width(cr,1.25);
	cairo_set_line_cap(cr,CAIRO_LINE_CAP_SQUARE);
	cairo_set_line_join(cr,CAIRO_LINE_JOIN_MITER);
	if (which==0 && !active) {
		cairo_move_to(cr,1,-6);cairo_line_to(cr,6,-6);cairo_line_to(cr,6,-1);
		cairo_move_to(cr,6,-6);cairo_line_to(cr,-2,2);
		cairo_move_to(cr,-.5,-5.5);cairo_line_to(cr,-6,-5.5);cairo_line_to(cr,-6,6);
		cairo_line_to(cr,5.5,6);cairo_line_to(cr,5.5,.5);
	} else if (which==0) {
		cairo_move_to(cr,5,-5);cairo_line_to(cr,-1,1);
		cairo_move_to(cr,-1,-3);cairo_line_to(cr,-1,1);cairo_line_to(cr,3,1);
		cairo_move_to(cr,-1,-6);cairo_line_to(cr,-6,-6);cairo_line_to(cr,-6,6);
		cairo_line_to(cr,6,6);cairo_line_to(cr,6,1);
	} else if (which==1 && active) {
		cairo_rectangle(cr,-6,-3,9,9);
		cairo_move_to(cr,-3,-3);cairo_line_to(cr,-3,-6);cairo_line_to(cr,6,-6);
		cairo_line_to(cr,6,3);cairo_line_to(cr,3,3);
	} else if (which==1) {
		cairo_rectangle(cr,-5,-5,10,10);
	} else {
		cairo_move_to(cr,-4,-4);cairo_line_to(cr,4,4);
		cairo_move_to(cr,-4,4);cairo_line_to(cr,4,-4);
	}
	cairo_stroke(cr);
	cairo_restore(cr);
}

static int title_end(const struct window_chrome *c) {
	const struct wlr_box first=control(c,0);
	return first.width?first.x-24:c->w-CUT-6;
}

static void title(cairo_t *cr,struct window_chrome *c) {
	const int x=CUT+8;
	const int space=title_end(c)-x;
	c->title_width=0;
	if (space<12 || !c->title || !*c->title) return;
	gchar *valid=g_utf8_make_valid(c->title,-1),*line=g_strdup(valid);
	g_free(valid);
	for (char *cursor=line;*cursor;cursor++) if (*cursor=='\n'||*cursor=='\r'||*cursor=='\t') *cursor=' ';
	PangoLayout *layout=pango_cairo_create_layout(cr);
	PangoFontDescription *font=pango_font_description_new();
	pango_font_description_set_family(font,"Chakra Petch,Monospace");
	pango_font_description_set_weight(font,PANGO_WEIGHT_SEMIBOLD);
	pango_font_description_set_absolute_size(font,13*PANGO_SCALE);
	pango_layout_set_font_description(layout,font);
	pango_layout_set_single_paragraph_mode(layout,TRUE);
	pango_layout_set_width(layout,space*PANGO_SCALE);
	pango_layout_set_ellipsize(layout,PANGO_ELLIPSIZE_END);
	pango_layout_set_text(layout,line,-1);
	int height;
	pango_layout_get_pixel_size(layout,&c->title_width,&height);
	cairo_set_source_rgba(cr,.84,.93,.98,c->focused?1:.74);
	cairo_move_to(cr,x,RAIL_Y-height-3);
	pango_cairo_show_layout(cr,layout);
	g_free(line);
	pango_font_description_free(font);
	g_object_unref(layout);
}

static void render(cairo_t *cr,struct window_chrome *c,int strip) {
	struct point p[12];
	size_t n=contour(c,p);
	path(cr,p,n,0);
	cairo_set_source_rgb(cr,.058,.101,.139);
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr,.48,.72,.80,c->focused?.90:.46);
	cairo_set_line_join(cr,CAIRO_LINE_JOIN_MITER);
	cairo_set_line_width(cr,1.15);
	cairo_stroke(cr);
	path(cr,p,n,INNER_FRAME_INSET);
	cairo_set_source_rgba(cr,.29,.48,.57,c->focused?.86:.52);
	cairo_set_line_width(cr,INNER_FRAME_STROKE);
	cairo_stroke(cr);
	if (strip==TOP) {
		for (int i=0;i<3;i++) button(cr,control(c,i),i,c->focused,
			(i==0&&c->floating)||(i==1&&c->maximized));
		title(cr,c);
		// Close the lower rail with a continuous seam in compositor-owned
		// pixels, leaving the app's first row and its input region intact.
		cairo_set_source_rgba(cr,.29,.48,.57,c->focused?.58:.40);
		cairo_set_line_width(cr,.75);
		cairo_move_to(cr,WINDOW_CHROME_EDGE,WINDOW_CHROME_TOP-.5);
		cairo_line_to(cr,c->w-WINDOW_CHROME_EDGE,WINDOW_CHROME_TOP-.5);
		cairo_stroke(cr);
	}
}

static struct chrome_pixels *draw_strip(struct window_chrome *c,struct wlr_box box,int slot) {
	int w=(int)ceil(box.width*c->scale),h=(int)ceil(box.height*c->scale);
	cairo_surface_t *img=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,w,h);
	if (cairo_surface_status(img)!=CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(img);
		return NULL;
	}
	cairo_t *cr=cairo_create(img);
	cairo_scale(cr,(double)w/box.width,(double)h/box.height);
	cairo_translate(cr,-box.x,-box.y);
	render(cr,c,slot);
	bool ok=cairo_status(cr)==CAIRO_STATUS_SUCCESS;
	cairo_destroy(cr);
	struct chrome_pixels *pixels=ok?calloc(1,sizeof(*pixels)):NULL;
	if (!pixels) {
		cairo_surface_destroy(img);
		return NULL;
	}
	pixels->image=img;
	cairo_surface_flush(img);
	wlr_buffer_init(&pixels->buffer,&pixel_impl,w,h);
	return pixels;
}

static bool inside(struct wlr_box b,double x,double y) {
	return b.width>0 && b.height>0 && x>=b.x && y>=b.y && x<b.x+b.width && y<b.y+b.height;
}

static bool painted(const struct window_chrome *c,double x,double y) {
	if (x>=CUT+7 && x<CUT+9+c->title_width && y>=3 && y<RAIL_Y && c->title && *c->title) return true;
	for (int i=0;i<3;i++) if (inside(control(c,i),x,y)) return true;
	struct point p[12];
	size_t n=contour(c,p);
	bool yes=false;
	for (size_t i=0,j=n-1;i<n;j=i++)
		if ((p[i].y>y)!=(p[j].y>y) && x<(p[j].x-p[i].x)*(y-p[i].y)/(p[j].y-p[i].y)+p[i].x) yes=!yes;
	return yes;
}

static bool accepts_input(struct wlr_scene_buffer *buffer,double *sx,double *sy) {
	const struct window_chrome *c=buffer->node.data;
	return c && painted(c,buffer->node.x+*sx,buffer->node.y+*sy);
}

static void crop(struct window_chrome *c) {
	for (int i=0;i<STRIPS;i++) {
		struct chrome_strip *s=&c->strips[i];
		struct wlr_box visible=s->box;
		bool show=s->pixels_w>0 && (!c->clipped || wlr_box_intersection(&visible,&s->box,&c->aperture));
		wlr_scene_node_set_enabled(&s->scene->node,show);
		if (!show) continue;
		double sx=(double)s->pixels_w/s->box.width,sy=(double)s->pixels_h/s->box.height;
		struct wlr_fbox src={(visible.x-s->box.x)*sx,(visible.y-s->box.y)*sy,visible.width*sx,visible.height*sy};
		wlr_scene_node_set_position(&s->scene->node,visible.x,visible.y);
		wlr_scene_buffer_set_dest_size(s->scene,visible.width,visible.height);
		wlr_scene_buffer_set_source_box(s->scene,c->clipped?&src:NULL);
	}
}

struct window_chrome *window_chrome_create(struct wlr_scene_tree *parent) {
	struct window_chrome *c=calloc(1,sizeof(*c));
	if (!c) return NULL;
	c->tree=wlr_scene_tree_create(parent);
	if (!c->tree) { free(c);return NULL; }
	wlr_scene_node_set_enabled(&c->tree->node,false);
	for (int i=0;i<STRIPS;i++) {
		struct wlr_scene_buffer *s=wlr_scene_buffer_create(c->tree,NULL);
		if (!s) { window_chrome_destroy(c);return NULL; }
		c->strips[i].scene=s;
		s->node.data=c;
		s->point_accepts_input=accepts_input;
	}
	return c;
}

void window_chrome_destroy(struct window_chrome *c) {
	if (!c) return;
	wlr_scene_node_destroy(&c->tree->node);
	free(c->title);
	free(c);
}

void window_chrome_show(struct window_chrome *c,bool shown) {
	if (c) wlr_scene_node_set_enabled(&c->tree->node,shown);
}

void window_chrome_clip(struct window_chrome *c,const struct wlr_box *clip) {
	if (!c || (!clip&&!c->clipped) ||
		(clip&&c->clipped&&memcmp(&c->aperture,clip,sizeof(*clip))==0)) return;
	c->clipped=clip!=NULL;
	if (clip) c->aperture=*clip;
	crop(c);
}

void window_chrome_update(struct window_chrome *c,int w,int h,double scale,
		const char *name,bool focused,bool floating,bool maximized) {
	if (!c || w<2*CUT+26 || h<WINDOW_CHROME_TOP+WINDOW_CHROME_EDGE+1 || w>16384 || h>16384) return;
	scale=isfinite(scale)?fmax(1,fmin(4,scale)):1;
	if (!name) name="";
	if (c->w==w&&c->h==h&&c->scale==scale&&c->focused==focused&&
			c->floating==floating&&c->maximized==maximized&&c->title&&strncmp(c->title,name,4096)==0) return;
	struct window_chrome next=*c;
	next.w=w;next.h=h;next.scale=scale;next.title=strndup(name,4096);
	next.focused=focused;next.floating=floating;next.maximized=maximized;
	if (!next.title) return;
	const int top=WINDOW_CHROME_TOP,edge=WINDOW_CHROME_EDGE;
	struct wlr_box boxes[STRIPS]={{0,0,w,top},{0,h-edge,w,edge},
		{0,top,edge,h-top-edge},{w-edge,top,edge,h-top-edge}};
	// Title and control icon changes affect only the top strip. Dimensions,
	// backing scale or focus palette changes affect the entire contour.
	int dirty=c->w==w&&c->h==h&&c->scale==scale&&c->focused==focused?1:STRIPS;
	struct chrome_pixels *pixels[STRIPS]={0};
	for (int i=0;i<dirty;i++) {
		pixels[i]=draw_strip(&next,boxes[i],i);
		if (pixels[i]) continue;
		for (int j=0;j<i;j++) wlr_buffer_drop(&pixels[j]->buffer);
		free(next.title);
		return;
	}
	free(c->title);
	*c=next;
	for (int i=0;i<dirty;i++) {
		struct chrome_strip *s=&c->strips[i];
		s->box=boxes[i];
		s->pixels_w=pixels[i]->buffer.width;
		s->pixels_h=pixels[i]->buffer.height;
		wlr_scene_buffer_set_buffer(s->scene,&pixels[i]->buffer);
		wlr_buffer_drop(&pixels[i]->buffer);
	}
	crop(c);
}

bool window_chrome_owns_node(const struct window_chrome *c,const struct wlr_scene_node *node) {
	return c && node && node->parent==c->tree;
}

enum window_chrome_hit window_chrome_hit_at(const struct window_chrome *c,double x,double y) {
	if (!c||!c->tree->node.enabled||x<0||y<0||x>=c->w||y>=c->h||
			(c->clipped&&!inside(c->aperture,x,y))) return WINDOW_CHROME_NONE;
	for (int i=0;i<3;i++)
		if (inside(control(c,i),x,y)) return (enum window_chrome_hit)(WINDOW_CHROME_FLOAT+i);
	if (c->floating&&!c->maximized&&x>=c->w-2*WINDOW_CHROME_EDGE&&y>=c->h-2*WINDOW_CHROME_EDGE) return WINDOW_CHROME_RESIZE;
	if (y<WINDOW_CHROME_TOP && painted(c,x,y)) return WINDOW_CHROME_TITLE;
	return WINDOW_CHROME_NONE;
}
