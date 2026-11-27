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
enum { CUT=9, CORNER_ARM=17, FOCUS_DASH=6, BUTTON_GAP=5, BUTTON_EDGE=14,
	BUTTON_MIN=14, BUTTON_MAX=25, CONTROL_CENTER_Y=22 };
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
	int w,h;
	double scale;
	bool focused,float_action,floating,maximized,clipped,small_actions,minimize,resizable;
	bool pinned;
	char *title,*hints;
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

static bool group_member(const struct window_chrome *c) {
	return c->small_actions && !c->minimize;
}

static int first_control(const struct window_chrome *c) {
	return c->small_actions?(c->minimize?2:3):c->float_action?0:1;
}

static struct wlr_box control(const struct window_chrome *c,int index) {
	const int first=first_control(c),count=4-first,gap=(count-1)*BUTTON_GAP;
	if(index<first) return (struct wlr_box){0};
	const int room=c->w-2*BUTTON_EDGE-gap;
	if(room<count*BUTTON_MIN) return (struct wlr_box){0};
	const int cap=room/count,size=cap<BUTTON_MAX?cap:BUTTON_MAX;
	const int base=c->w-BUTTON_EDGE-count*size-gap;
	return (struct wlr_box){base+(index-first)*(size+BUTTON_GAP),
		CONTROL_CENTER_Y-size/2,size,size};
}

static void contour(const struct window_chrome *c,struct point p[8]) {
	const double w=c->w,h=c->h,k=group_member(c)?6:CUT;
	const struct point points[]={
		{.75,k+.75},{k+.75,.75},{w-k-.75,.75},{w-.75,k+.75},
		{w-.75,h-k-.75},{w-k-.75,h-.75},{k+.75,h-.75},{.75,h-k-.75},
	};
	memcpy(p,points,sizeof(points));
}

static void path(cairo_t *cr,const struct point *p,size_t n) {
	cairo_new_path(cr);
	cairo_move_to(cr,p[0].x,p[0].y);
	for (size_t i=1;i<n;i++) cairo_line_to(cr,p[i].x,p[i].y);
	cairo_close_path(cr);
}

static void button(cairo_t *cr,struct wlr_box b,int which,bool focused,bool active) {
	if (!b.width) return;
	// Idle reads as a flat glyph on the header. A selected placement
	// receives a restrained tint, never a raised button face.
	if(active) {
		const double x=b.x+2,y=b.y+2,w=b.width-4,h=b.height-4,k=3;
		const struct point p[]={{x+k,y},{x+w-k,y},{x+w,y+k},{x+w,y+h-k},
			{x+w-k,y+h},{x+k,y+h},{x,y+h-k},{x,y+k}};
		path(cr,p,sizeof(p)/sizeof(p[0]));
		cairo_set_source_rgba(cr,.19,.56,.68,focused?.24:.15);
		cairo_fill(cr);
	}

	cairo_save(cr);
	cairo_translate(cr,b.x+b.width*.5,b.y+b.height*.5);
	cairo_scale(cr,(b.width-1)/22.,(b.height-1)/22.);
	cairo_set_source_rgba(cr,active?.72:.76,active?.98:.88,.94,focused?.95:.70);
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
	} else if(which==2) {
		cairo_move_to(cr,-5,2);cairo_line_to(cr,5,2);
	} else {
		cairo_move_to(cr,-4,-4);cairo_line_to(cr,4,4);
		cairo_move_to(cr,-4,4);cairo_line_to(cr,4,-4);
	}
	cairo_stroke(cr);
	cairo_restore(cr);
}

static int title_end(const struct window_chrome *c) {
	const struct wlr_box first=control(c,first_control(c));
	return first.width?first.x-12:c->w-CUT-6;
}

static void title(cairo_t *cr,const struct window_chrome *c) {
	const int x=CUT+8;
	const int space=title_end(c)-x;
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
	pango_layout_get_pixel_size(layout,NULL,&height);
	cairo_set_source_rgba(cr,.84,.93,.98,c->focused?1:.74);
	cairo_move_to(cr,x,CONTROL_CENTER_Y-height*.5);
	pango_cairo_show_layout(cr,layout);
	g_free(line);
	pango_font_description_free(font);
	g_object_unref(layout);
}

// Footer keycaps match the shell guides (see apps/panel/map-guide.c):
// a 17px chamfered cap with the chord, then its quiet action label.
enum { KEYCAP_H=17, KEYCAP_CUT=4, KEYCAP_MIN=19, KEYCAP_PAD=12,
	KEYCAP_LABEL_GAP=6, KEYCAP_ITEM_GAP=14 };

static void ink(cairo_t *cr,unsigned rgb) {
	cairo_set_source_rgb(cr,((rgb>>16)&255)/255.,((rgb>>8)&255)/255.,(rgb&255)/255.);
}

static void keycap(cairo_t *cr,double x,double y,double w) {
	const double i=.5,k=KEYCAP_CUT-i*(2-M_SQRT2);
	const double l=x+i,r=x+w-i,t=y+i,b=y+KEYCAP_H-i;
	const struct point p[]={{l+k,t},{r-k,t},{r,t+k},{r,b-k},{r-k,b},{l+k,b},{l,b-k},{l,t+k}};
	path(cr,p,sizeof(p)/sizeof(p[0]));
	ink(cr,0x162934);
	cairo_fill_preserve(cr);
	ink(cr,0x425d69);
	cairo_set_line_width(cr,1);
	cairo_stroke(cr);
}

static int text_width(PangoLayout *layout,const char *text) {
	int w;
	pango_layout_set_text(layout,text,-1);
	pango_layout_get_pixel_size(layout,&w,NULL);
	return w;
}

static void footer_hints(cairo_t *cr,const struct window_chrome *c) {
	if(!c->hints || !*c->hints) return;
	const int x0=CUT+8;
	const int right=c->w-(c->resizable?2*WINDOW_CHROME_EDGE:CUT+8);
	if(right-x0<KEYCAP_MIN) return;
	gchar *valid=g_utf8_make_valid(c->hints,-1);
	gchar **items=g_strsplit(valid,"|",8);
	g_free(valid);
	PangoLayout *layout=pango_cairo_create_layout(cr);
	PangoFontDescription *font=pango_font_description_new();
	pango_font_description_set_family(font,"Chakra Petch,Sans");
	pango_font_description_set_weight(font,PANGO_WEIGHT_SEMIBOLD);
	pango_font_description_set_absolute_size(font,10*PANGO_SCALE);
	pango_layout_set_font_description(layout,font);
	pango_layout_set_single_paragraph_mode(layout,TRUE);
	int text_h;
	pango_layout_set_text(layout,"Ag",-1);
	pango_layout_get_pixel_size(layout,NULL,&text_h);
	// Center between the separator and the inner edge of the focused outline.
	const double y=c->h-WINDOW_CHROME_BOTTOM+floor((WINDOW_CHROME_BOTTOM-3-KEYCAP_H)*.5);
	int x=x0;
	// Drop whole instructions from the end rather than clipping a key.
	for(int i=0;items[i];i++) {
		char *split=strrchr(items[i],' ');
		if(!split) continue;
		*split='\0';
		const char *chord=items[i],*label=split+1;
		const int key_w=MAX(KEYCAP_MIN,text_width(layout,chord)+KEYCAP_PAD);
		const int label_w=text_width(layout,label);
		if(x+key_w+KEYCAP_LABEL_GAP+label_w>right) break;
		keycap(cr,x,y,key_w);
		ink(cr,0xc0d3de);
		cairo_move_to(cr,x+(key_w-text_width(layout,chord))*.5,y+(KEYCAP_H-text_h)*.5);
		pango_cairo_show_layout(cr,layout);
		x+=key_w+KEYCAP_LABEL_GAP;
		pango_layout_set_text(layout,label,-1);
		cairo_move_to(cr,x,y+(KEYCAP_H-text_h)*.5);
		pango_cairo_show_layout(cr,layout);
		x+=label_w+KEYCAP_ITEM_GAP;
	}
	g_strfreev(items);
	pango_font_description_free(font);
	g_object_unref(layout);
}

static void dash_edge(cairo_t *cr,double x,double y,double dx,double dy,double length) {
	const double dash=FOCUS_DASH;
	const int count=(int)floor((length-dash)/(2*dash));
	if(count<1) return;
	// Center complete dashes and leave a gap at each solid corner arm.
	const double span=(2*count-1)*dash;
	const double start=(length-span)/2;
	cairo_move_to(cr,x+dx*start,y+dy*start);
	cairo_rel_line_to(cr,dx*span,dy*span);
}

static void draw_outline(cairo_t *cr,const struct window_chrome *c) {
	const bool member=group_member(c);
	const double cut=member?6:CUT;
	const double arm=member?12:CORNER_ARM;
	// Keep the whole stroke inside the 12px chrome edge.
	const double line=c->focused?2.25:1.5;
	const double dt=line/2+.6;
	for(int y=0;y<2;y++) for(int x=0;x<2;x++) {
		cairo_save(cr);
		cairo_translate(cr,x?c->w-dt:dt,y?c->h-dt:dt);
		cairo_scale(cr,x?-1:1,y?-1:1);
		cairo_move_to(cr,0,arm);
		cairo_line_to(cr,0,cut);
		cairo_line_to(cr,cut,0);
		cairo_line_to(cr,arm,0);
		cairo_restore(cr);
	}
	cairo_set_line_join(cr,CAIRO_LINE_JOIN_MITER);
	cairo_set_line_cap(cr,CAIRO_LINE_CAP_BUTT);
	cairo_set_line_width(cr,line);
	const double alpha=c->focused?(member?.61:.96):(member?.30:.48);
	cairo_set_source_rgba(cr,c->focused?.32:.35,c->focused?.90:.70,.92,alpha);
	cairo_stroke(cr);
	if(!c->focused) return;

	const double inset=dt+arm;
	const double pattern[]={FOCUS_DASH,FOCUS_DASH};
	cairo_set_dash(cr,pattern,2,0);
	cairo_set_source_rgba(cr,.32,.90,.92,alpha*.70);
	dash_edge(cr,inset,dt,1,0,c->w-2*inset);
	dash_edge(cr,inset,c->h-dt,1,0,c->w-2*inset);
	dash_edge(cr,dt,inset,0,1,c->h-2*inset);
	dash_edge(cr,c->w-dt,inset,0,1,c->h-2*inset);
	cairo_stroke(cr);
	cairo_set_dash(cr,NULL,0,0);
}

static void render(cairo_t *cr,struct window_chrome *c,int strip) {
	const bool member=group_member(c);
	struct point p[8];
	contour(c,p);
	path(cr,p,8);
	cairo_save(cr);
	cairo_clip(cr);
	// The retained strips never cover the app rectangle. Translucent ink
	// lets the compositor supply a backdrop without a separate window surface.
	cairo_set_source_rgba(cr,.023,.069,.104,member?.49:.57);
	cairo_paint(cr);
	if(strip==TOP) {
		cairo_rectangle(cr,0,0,c->w,WINDOW_CHROME_TOP);
		cairo_set_source_rgba(cr,.038,.097,.128,member?.35:.42);
		cairo_fill(cr);
		cairo_set_source_rgba(cr,.31,.59,.68,member?.14:.25);
		cairo_set_line_width(cr,.75);
		cairo_move_to(cr,WINDOW_CHROME_EDGE,WINDOW_CHROME_TOP-.5);
		cairo_line_to(cr,c->w-WINDOW_CHROME_EDGE,WINDOW_CHROME_TOP-.5);
		cairo_stroke(cr);
	} else if(strip==BOTTOM) {
		// The footer mirrors the header band and its separator.
		const double top=c->h-WINDOW_CHROME_BOTTOM;
		cairo_rectangle(cr,0,top,c->w,WINDOW_CHROME_BOTTOM);
		cairo_set_source_rgba(cr,.038,.097,.128,member?.35:.42);
		cairo_fill(cr);
		cairo_set_source_rgba(cr,.31,.59,.68,member?.14:.25);
		cairo_set_line_width(cr,.75);
		cairo_move_to(cr,WINDOW_CHROME_EDGE,top+.5);
		cairo_line_to(cr,c->w-WINDOW_CHROME_EDGE,top+.5);
		cairo_stroke(cr);
	}
	cairo_restore(cr);
	draw_outline(cr,c);
	if(strip==BOTTOM) {
		footer_hints(cr,c);
		if(c->resizable) {
			cairo_set_source_rgba(cr,.45,.81,.86,c->focused?.82:.55);
			cairo_set_line_width(cr,1.25);
			cairo_move_to(cr,c->w-20,c->h-7);
			cairo_line_to(cr,c->w-7,c->h-20);
			cairo_move_to(cr,c->w-14,c->h-7);
			cairo_line_to(cr,c->w-7,c->h-14);
			cairo_stroke(cr);
		}
	}
	if(strip!=TOP) return;
	if(c->pinned) {
		cairo_set_source_rgb(cr,1,.77,.46);
		cairo_set_line_width(cr,1.5);
		for(int x=24;x<60 && x+4<c->w-8;x+=6) {
			cairo_move_to(cr,x,8.5);
			cairo_line_to(cr,x+4,4.5);
		}
		cairo_stroke(cr);
	}
	for(int i=0;i<4;i++) button(cr,control(c,i),i,c->focused,
		(i==0&&c->floating)||(i==1&&c->maximized));
	title(cr,c);
}

static struct chrome_pixels *finish_pixels(cairo_t *cr,cairo_surface_t *img,int w,int h) {
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

bool window_chrome_pin_face(struct wlr_scene_buffer *scene,int width,int height,
		double scale,int header,int row,bool highlight) {
	int w=(int)ceil(width*scale),h=(int)ceil(height*scale);
	cairo_surface_t *image=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,w,h);
	cairo_t *cr=cairo_create(image);
	cairo_scale(cr,(double)w/width,(double)h/height);
	// The 1px stroke sits inside the outer 5px cut, including its diagonal.
	const double inset=.5,cut=5+inset*(sqrt(2.)-1.);
	const struct point contour[]={
		{cut,inset},{width-cut,inset},{width-inset,cut},{width-inset,height-cut},
		{width-cut,height-inset},{cut,height-inset},{inset,height-cut},{inset,cut},
	};
	path(cr,contour,8);
	cairo_save(cr);
	cairo_clip_preserve(cr);
	if(!highlight) {
		cairo_set_source_rgb(cr,.047,.086,.118);
		cairo_paint(cr);
	}
	if(header) {
		cairo_set_source_rgb(cr,.071,.133,.176);
		cairo_rectangle(cr,0,0,width,header);
		cairo_fill(cr);
	}
	if(row) {
		cairo_set_source_rgb(cr,.16,.25,.30);
		for(int y=row;y<height;y+=row) cairo_rectangle(cr,1,y,width-2,1);
		cairo_fill(cr);
	}
	cairo_restore(cr);
	path(cr,contour,8);
	if(highlight) cairo_set_source_rgb(cr,.55,.81,.86);
	else cairo_set_source_rgb(cr,.35,.51,.56);
	cairo_set_line_width(cr,1);
	cairo_stroke(cr);
	struct chrome_pixels *pixels=finish_pixels(cr,image,w,h);
	if(!pixels) return false;
	wlr_scene_buffer_set_buffer(scene,&pixels->buffer);
	wlr_buffer_drop(&pixels->buffer);
	wlr_scene_buffer_set_dest_size(scene,width,height);
	return true;
}

static struct chrome_pixels *draw_strip(struct window_chrome *c,struct wlr_box box,int slot) {
	int w=(int)ceil(box.width*c->scale),h=(int)ceil(box.height*c->scale);
	cairo_surface_t *img=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,w,h);
	cairo_t *cr=cairo_create(img);
	cairo_scale(cr,(double)w/box.width,(double)h/box.height);
	cairo_translate(cr,-box.x,-box.y);
	render(cr,c,slot);
	return finish_pixels(cr,img,w,h);
}

static bool inside(struct wlr_box b,double x,double y) {
	return b.width>0 && b.height>0 && x>=b.x && y>=b.y && x<b.x+b.width && y<b.y+b.height;
}

static bool painted(const struct window_chrome *c,double x,double y) {
	for (int i=0;i<4;i++) if (inside(control(c,i),x,y)) return true;
	struct point p[8];
	contour(c,p);
	const size_t n=8;
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
	free(c->hints);
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
		const char *name,bool focused,bool pinned,bool float_action,bool floating,bool maximized,bool small_actions,bool minimize,
		bool resizable,const char *hints) {
	if (!c || w<2*CUT+26 || h<WINDOW_CONTENT_TOP+WINDOW_CONTENT_BOTTOM+1 || w>16384 || h>16384) return;
	scale=isfinite(scale)?fmax(1,fmin(4,scale)):1;
	if (!name) name="";
	if (!hints) hints="";
	const bool same_hints=c->hints&&strncmp(c->hints,hints,1024)==0;
	if (c->w==w&&c->h==h&&c->scale==scale&&c->focused==focused&&c->pinned==pinned&&
			c->float_action==float_action&&c->floating==floating&&c->maximized==maximized&&
			c->small_actions==small_actions&&c->minimize==minimize&&c->resizable==resizable&&
			c->title&&strncmp(c->title,name,4096)==0&&
			same_hints) return;
	struct window_chrome next=*c;
	next.w=w;next.h=h;next.scale=scale;next.title=strndup(name,4096);next.hints=strndup(hints,1024);
	next.focused=focused;next.float_action=float_action;next.floating=floating;next.maximized=maximized;
	next.pinned=pinned;
	next.small_actions=small_actions;next.minimize=minimize;next.resizable=resizable;
	if (!next.title || !next.hints) {
		free(next.title);
		free(next.hints);
		return;
	}
	const int top=WINDOW_CONTENT_TOP,bottom=WINDOW_CONTENT_BOTTOM,edge=WINDOW_CONTENT_EDGE;
	struct wlr_box boxes[STRIPS]={{0,0,w,top},{0,h-bottom,w,bottom},
		{0,top,edge,h-top-bottom},{w-edge,top,edge,h-top-bottom}};
	// Focus changes the outline on every strip. Title and controls affect
	// the header; hints and the resize corner affect the footer.
	int dirty=c->w==w&&c->h==h&&c->scale==scale&&c->focused==focused&&
		group_member(c)==group_member(&next)?
		(same_hints&&c->resizable==resizable?1:2):STRIPS;
	struct chrome_pixels *pixels[STRIPS]={0};
	for (int i=0;i<dirty;i++) {
		pixels[i]=draw_strip(&next,boxes[i],i);
		if (pixels[i]) continue;
		for (int j=0;j<i;j++) wlr_buffer_drop(&pixels[j]->buffer);
		free(next.title);
		free(next.hints);
		return;
	}
	free(c->title);
	free(c->hints);
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
	for (int i=0;i<4;i++)
		if (inside(control(c,i),x,y)) return (enum window_chrome_hit)(WINDOW_CHROME_FLOAT+i);
	if (c->resizable&&x>=c->w-2*WINDOW_CHROME_EDGE&&y>=c->h-2*WINDOW_CHROME_EDGE) return WINDOW_CHROME_RESIZE;
	if (y<WINDOW_CHROME_TOP && painted(c,x,y)) return WINDOW_CHROME_TITLE;
	return WINDOW_CHROME_NONE;
}

struct wlr_box window_chrome_maximize_box(const struct window_chrome *c) {
	return c?control(c,1):(struct wlr_box){0};
}

enum {
	RESIZE_MARK=28, RESIZE_MARK_PIXELS=RESIZE_MARK*2,
	RESIZE_STRIPE_PERIOD=12, RESIZE_TILE=240,
	RESIZE_TILE_PIXELS=(RESIZE_TILE+RESIZE_STRIPE_PERIOD)*2,
};

struct window_resize_preview {
	struct wlr_scene_tree *tree, *stripes;
	struct wlr_scene_buffer *marks[4];
	struct wlr_buffer *tile;
};

static struct wlr_buffer *resize_stripe_tile(void) {
	cairo_surface_t *img=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
		RESIZE_TILE_PIXELS,RESIZE_TILE_PIXELS);
	cairo_t *cr=cairo_create(img);
	const int extent=RESIZE_TILE_PIXELS/2;
	cairo_scale(cr,2,2);
	cairo_set_source_rgba(cr,.023,.069,.104,.22);
	cairo_paint(cr);
	cairo_set_source_rgba(cr,.52,.87,.90,.26);
	cairo_set_line_width(cr,1.5);
	for (int p=0;p<=2*extent;p+=RESIZE_STRIPE_PERIOD) {
		cairo_move_to(cr,p,0);
		cairo_line_to(cr,p-extent,extent);
	}
	cairo_stroke(cr);
	struct chrome_pixels *pixels=finish_pixels(cr,img,RESIZE_TILE_PIXELS,RESIZE_TILE_PIXELS);
	return pixels ? &pixels->buffer : NULL;
}

void window_resize_preview_destroy(struct window_resize_preview *preview) {
	if (!preview) return;
	if (preview->tree) wlr_scene_node_destroy(&preview->tree->node);
	if (preview->tile) wlr_buffer_drop(preview->tile);
	free(preview);
}

struct window_resize_preview *window_resize_preview_create(struct wlr_scene_tree *parent) {
	struct window_resize_preview *preview=calloc(1,sizeof(*preview));
	if (!preview) return NULL;
	preview->tree=wlr_scene_tree_create(parent);
	if (!preview->tree) goto failed;
	wlr_scene_node_set_enabled(&preview->tree->node,false);
	preview->stripes=wlr_scene_tree_create(preview->tree);
	preview->tile=resize_stripe_tile();
	if (!preview->stripes || !preview->tile) goto failed;
	for (int i=0;i<4;i++) {
		cairo_surface_t *img=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
			RESIZE_MARK_PIXELS,RESIZE_MARK_PIXELS);
		cairo_t *cr=cairo_create(img);
		cairo_scale(cr,2,2);
		cairo_translate(cr,(i&1)?RESIZE_MARK:0,(i&2)?RESIZE_MARK:0);
		cairo_scale(cr,(i&1)?-1:1,(i&2)?-1:1);
		cairo_move_to(cr,1.25,RESIZE_MARK);
		cairo_line_to(cr,1.25,CUT+1.25);
		cairo_line_to(cr,CUT+1.25,1.25);
		cairo_line_to(cr,RESIZE_MARK,1.25);
		cairo_set_source_rgba(cr,.62,.94,.96,.96);
		cairo_set_line_width(cr,2);
		cairo_stroke(cr);
		struct chrome_pixels *pixels=finish_pixels(cr,img,RESIZE_MARK_PIXELS,RESIZE_MARK_PIXELS);
		if (!pixels) goto failed;
		struct wlr_scene_buffer *mark=wlr_scene_buffer_create(preview->tree,&pixels->buffer);
		wlr_buffer_drop(&pixels->buffer);
		if (!mark) goto failed;
		wlr_scene_buffer_set_dest_size(mark,RESIZE_MARK,RESIZE_MARK);
		preview->marks[i]=mark;
	}
	return preview;

failed:
	window_resize_preview_destroy(preview);
	return NULL;
}

void window_resize_preview_hide(struct window_resize_preview *preview) {
	wlr_scene_node_set_enabled(&preview->tree->node,false);
}

static void place_resize_stripes(struct window_resize_preview *preview,const cairo_region_t *region) {
	struct wl_list *children=&preview->stripes->children;
	struct wl_list *unused=children->next;
	for (int i=0;i<cairo_region_num_rectangles(region);i++) {
		cairo_rectangle_int_t band;
		cairo_region_get_rectangle(region,i,&band);
		int right=band.x+band.width,bottom=band.y+band.height;
		for (int y=band.y;y<bottom;y+=RESIZE_TILE) {
			for (int x=band.x;x<right;x+=RESIZE_TILE) {
				struct wlr_scene_buffer *tile;
				if (unused==children) {
					tile=wlr_scene_buffer_create(preview->stripes,preview->tile);
					if (!tile) return;
				} else {
					struct wlr_scene_node *node=wl_container_of(unused,node,link);
					unused=unused->next;
					tile=wlr_scene_buffer_from_node(node);
				}
				int width=right-x<RESIZE_TILE ? right-x : RESIZE_TILE;
				int height=bottom-y<RESIZE_TILE ? bottom-y : RESIZE_TILE;
				// Anchor the pattern in output coordinates so it is continuous
				// across bands and does not swim as their edges move.
				struct wlr_fbox source={2*(x%RESIZE_STRIPE_PERIOD),2*(y%RESIZE_STRIPE_PERIOD),
					2*width,2*height};
				wlr_scene_buffer_set_source_box(tile,&source);
				wlr_scene_buffer_set_dest_size(tile,width,height);
				wlr_scene_node_set_position(&tile->node,x,y);
				wlr_scene_node_set_enabled(&tile->node,true);
			}
		}
	}
	for (;unused!=children;unused=unused->next) {
		struct wlr_scene_node *node=wl_container_of(unused,node,link);
		wlr_scene_node_set_enabled(node,false);
	}
}

void window_resize_preview_place(struct window_resize_preview *preview,
		const struct wlr_box *original,const struct wlr_box *proposed,int width,int height) {
	cairo_rectangle_int_t before={original->x,original->y,original->width,original->height};
	cairo_rectangle_int_t after={proposed->x,proposed->y,proposed->width,proposed->height};
	cairo_rectangle_int_t output={0,0,width,height};
	cairo_region_t *changed=cairo_region_create_rectangle(&before);
	cairo_region_xor_rectangle(changed,&after);
	cairo_region_intersect_rectangle(changed,&output);
	place_resize_stripes(preview,changed);
	cairo_region_destroy(changed);
	for (int i=0;i<4;i++) {
		wlr_scene_node_set_position(&preview->marks[i]->node,
			proposed->x+((i&1)?proposed->width-RESIZE_MARK:0),
			proposed->y+((i&2)?proposed->height-RESIZE_MARK:0));
	}
	wlr_scene_node_set_enabled(&preview->tree->node,true);
}
