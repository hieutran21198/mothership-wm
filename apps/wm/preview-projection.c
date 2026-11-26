//go:build linux && cgo && wlroots

#include "preview-projection.h"
#include <cairo.h>
#include <drm_fourcc.h>
#include <math.h>
#include <stdlib.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_scene.h>

struct projection_pixels {struct wlr_buffer buffer; cairo_surface_t *image;};
struct xy {double x,y;};

static bool projection_pixels_read(struct wlr_buffer *buffer,uint32_t flags,void **data,uint32_t *format,size_t *stride) {
	if(flags&WLR_BUFFER_DATA_PTR_ACCESS_WRITE) return false;
	struct projection_pixels *pixels=wl_container_of(buffer,pixels,buffer);
	*data=cairo_image_surface_get_data(pixels->image);
	*stride=cairo_image_surface_get_stride(pixels->image);
	*format=DRM_FORMAT_ARGB8888;
	return true;
}
static void projection_pixels_end(struct wlr_buffer *buffer) {(void)buffer;}
static void projection_pixels_destroy(struct wlr_buffer *buffer) {
	struct projection_pixels *pixels=wl_container_of(buffer,pixels,buffer);
	cairo_surface_destroy(pixels->image);
	free(pixels);
}
static const struct wlr_buffer_impl projection_pixels_impl={
	.destroy=projection_pixels_destroy,
	.begin_data_ptr_access=projection_pixels_read,
	.end_data_ptr_access=projection_pixels_end,
};

static void corners(struct xy p[4],struct wlr_box b) {
	p[0]=(struct xy){b.x,b.y};p[1]=(struct xy){b.x+b.width,b.y};
	p[2]=(struct xy){b.x+b.width,b.y+b.height};p[3]=(struct xy){b.x,b.y+b.height};
}
static void path_to(cairo_t *cr,struct xy p) {cairo_line_to(cr,p.x,p.y);}
static void light_gradient(cairo_t *cr,double top,double bottom,double alpha) {
	cairo_pattern_t *g=cairo_pattern_create_linear(0,top,0,bottom);
	cairo_pattern_add_color_stop_rgba(g,0,.39,.75,.82,alpha*.18);
	cairo_pattern_add_color_stop_rgba(g,.55,.39,.75,.82,alpha*.48);
	cairo_pattern_add_color_stop_rgba(g,1,.49,.84,.89,alpha);
	cairo_set_source(cr,g);
	cairo_pattern_destroy(g);
}
static struct projection_pixels *draw_projection(struct wlr_box area,struct wlr_box source,
		struct wlr_box face,unsigned open) {
	// The beam is low-frequency paint only. Bound uploads independently
	// of monitor scale; the app pixels stay in their existing texture.
	int w=area.width>384?384:area.width,h=area.height>160?160:area.height;
	cairo_surface_t *img=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,w,h);
	if(cairo_surface_status(img)!=CAIRO_STATUS_SUCCESS) {cairo_surface_destroy(img);return NULL;}
	cairo_t *cr=cairo_create(img);
	cairo_scale(cr,(double)w/area.width,(double)h/area.height);
	cairo_translate(cr,-area.x,-area.y);
	double top=face.y+face.height,bottom=source.y+source.height;
	cairo_rectangle(cr,area.x,bottom-(bottom-top)*open/24.,area.width,bottom-top);
	cairo_clip(cr);
	struct xy emitter[4],screen[4];
	corners(emitter,source);corners(screen,face);
	// Four connecting faces. The image occludes the back-facing parts;
	// rear edges are fainter than the two nearer edges, not hard wires.
	const double face_alpha[]={.067,.047,.083,.047};
	for(unsigned i=0;i<4;i++) {
		unsigned j=(i+1)%4;
		cairo_move_to(cr,emitter[i].x,emitter[i].y);
		path_to(cr,emitter[j]);path_to(cr,screen[j]);path_to(cr,screen[i]);cairo_close_path(cr);
		light_gradient(cr,top,bottom,face_alpha[i]);cairo_fill(cr);
	}
	for(unsigned i=0;i<4;i++) {
		const bool rear=i<2;
		cairo_move_to(cr,emitter[i].x,emitter[i].y);path_to(cr,screen[i]);
		light_gradient(cr,top,bottom,rear?.036:.060);
		cairo_set_line_width(cr,rear?3.:5.);cairo_stroke_preserve(cr);
		light_gradient(cr,top,bottom,rear?.18:.36);
		cairo_set_line_width(cr,rear?.7:.9);cairo_stroke(cr);
	}
	bool ok=cairo_status(cr)==CAIRO_STATUS_SUCCESS;
	cairo_destroy(cr);
	struct projection_pixels *pixels=ok?calloc(1,sizeof(*pixels)):NULL;
	if(!pixels) {cairo_surface_destroy(img);return NULL;}
	pixels->image=img;
	cairo_surface_flush(img);
	wlr_buffer_init(&pixels->buffer,&projection_pixels_impl,w,h);
	return pixels;
}
static bool same_box(struct wlr_box a,struct wlr_box b) {
	return a.x==b.x && a.y==b.y && a.width==b.width && a.height==b.height;
}

void preview_projection_clear(struct preview_projection *p) {
	if(!p->scene) return;
	wlr_scene_node_set_enabled(&p->scene->node,false);
	if(p->scene->buffer) wlr_scene_buffer_set_buffer(p->scene,NULL);
}

void preview_projection_update(struct preview_projection *p,struct wlr_box source,
		struct wlr_box face,double progress) {
	if(!p->scene) return;
	int left=source.x<face.x?source.x:face.x;
	int right=source.x+source.width>face.x+face.width?source.x+source.width:face.x+face.width;
	int top=face.y+face.height,bottom=source.y+source.height;
	unsigned open=(unsigned)round(fmax(0,fmin(1,progress))*24.);
	if(bottom<=top || !open) {preview_projection_clear(p);return;}
	struct wlr_box area={left-3,top,right-left+6,bottom-top};
	wlr_scene_node_set_position(&p->scene->node,area.x,area.y);
	wlr_scene_buffer_set_dest_size(p->scene,area.width,area.height);
	// The cached texture can move without repaint when both ends move
	// together. Once the flight settles, no raster work or uploads run.
	struct wlr_box local_source=source,local_face=face;
	local_source.x-=area.x;local_source.y-=area.y;
	local_face.x-=area.x;local_face.y-=area.y;
	if(!p->scene->buffer || !same_box(p->source,local_source) || !same_box(p->face,local_face) || p->opening!=open) {
		struct wlr_box local_area={0,0,area.width,area.height};
		struct projection_pixels *pixels=draw_projection(local_area,local_source,local_face,open);
		if(!pixels) {preview_projection_clear(p);return;}
		wlr_scene_buffer_set_buffer(p->scene,&pixels->buffer);
		wlr_buffer_drop(&pixels->buffer);
		p->source=local_source;p->face=local_face;p->opening=open;
	}
	wlr_scene_node_set_enabled(&p->scene->node,true);
}
