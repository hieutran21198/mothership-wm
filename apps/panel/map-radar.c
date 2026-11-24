//go:build linux && cgo && panel

#define _GNU_SOURCE
#include <gdk/wayland/gdkwayland.h>
#include <math.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "generated-viewporter.h"
#include "map-intro.h"
#include "map-radar.h"
#include "shell-paint.h"

enum { EDGE_BUFFERS=3, CORNER_BOX=26, MAX_TILE_BYTES=8*1024*1024 };

struct artwork {
	struct wl_buffer *buffer;
	unsigned char *pixels;
	size_t size;
	int width,height,stride;
	gboolean busy;
};

struct layer {
	struct wl_surface *surface;
	struct wl_subsurface *subsurface;
	struct wp_viewport *viewport;
	struct artwork *shown;
	graphene_rect_t crop;
	int x,y;
};

static struct {
	struct wl_display *display;
	struct wl_compositor *compositor;
	struct wl_subcompositor *subcompositor;
	struct wl_shm *shm;
	struct wp_viewporter *viewporter;
	struct layer anchor,beam[4],locks[4];
	struct artwork blank,strip,edges[EDGE_BUFFERS],corners[4][EDGE_BUFFERS];
	double strip_width,strip_height,strip_scale;
} radar;

static void announce(void *p,struct wl_registry *r,uint32_t n,const char *type,uint32_t v) {
	(void)p;
	if (!radar.compositor && !strcmp(type,wl_compositor_interface.name))
		radar.compositor=wl_registry_bind(r,n,&wl_compositor_interface,MIN(4,v));
	else if (!radar.subcompositor && !strcmp(type,wl_subcompositor_interface.name))
		radar.subcompositor=wl_registry_bind(r,n,&wl_subcompositor_interface,1);
	else if (!radar.shm && !strcmp(type,wl_shm_interface.name))
		radar.shm=wl_registry_bind(r,n,&wl_shm_interface,1);
	else if (!radar.viewporter && !strcmp(type,wp_viewporter_interface.name))
		radar.viewporter=wl_registry_bind(r,n,&wp_viewporter_interface,1);
}
static void removed(void *p,struct wl_registry *r,uint32_t n) { (void)p;(void)r;(void)n; }
static const struct wl_registry_listener globals={.global=announce,.global_remove=removed};

static void release_buffer(void *data,struct wl_buffer *buffer) {
	struct artwork *tile=data;
	if (tile->buffer==buffer) tile->busy=FALSE;
}
static const struct wl_buffer_listener releases={.release=release_buffer};

static void destroy_buffer(struct artwork *tile) {
	// The server has its own SHM pool mapping until it finishes sampling.
	if (tile->buffer) wl_buffer_destroy(tile->buffer);
	if (tile->pixels) munmap(tile->pixels,tile->size);
	*tile=(struct artwork){0};
}

static gboolean reserve_buffer(struct artwork *tile,int width,int height) {
	if (width<1 || height<1 || width>MAX_TILE_BYTES/4/height || tile->busy) return FALSE;
	if (tile->buffer && tile->width==width && tile->height==height) return TRUE;
	destroy_buffer(tile);
	int stride=4*width,bytes=stride*height;
	int fd=memfd_create("mothership-map-radar",MFD_CLOEXEC);
	if (fd<0) return FALSE;
	if (ftruncate(fd,bytes)<0) { close(fd);return FALSE; }
	void *pixels=mmap(NULL,bytes,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
	if (pixels==MAP_FAILED) { close(fd);return FALSE; }
	struct wl_shm_pool *pool=wl_shm_create_pool(radar.shm,fd,bytes);
	struct wl_buffer *buffer=pool?wl_shm_pool_create_buffer(pool,0,width,height,stride,WL_SHM_FORMAT_ARGB8888):NULL;
	if (pool) wl_shm_pool_destroy(pool);
	close(fd);
	if (!buffer) { munmap(pixels,bytes);return FALSE; }
	*tile=(struct artwork){.buffer=buffer,.pixels=pixels,.size=bytes,.width=width,.height=height,.stride=stride};
	wl_buffer_add_listener(buffer,&releases,tile);
	return TRUE;
}

typedef void (*paint_fn)(cairo_t *,const void *);
static gboolean paint_tile(struct artwork *tile,int w,int h,double scale,
		paint_fn paint,const void *data) {
	if (w<1 || h<1 || w>128 || h>8192 || !isfinite(scale) || scale<=0 || scale>4) return FALSE;
	int pw=ceil(w*scale),ph=ceil(h*scale);
	if (!reserve_buffer(tile,pw,ph)) return FALSE;
	cairo_surface_t *image=cairo_image_surface_create_for_data(tile->pixels,CAIRO_FORMAT_ARGB32,pw,ph,tile->stride);
	cairo_t *cr=cairo_create(image);
	cairo_set_operator(cr,CAIRO_OPERATOR_CLEAR);
	cairo_paint(cr);
	cairo_set_operator(cr,CAIRO_OPERATOR_OVER);
	cairo_scale(cr,pw/(double)w,ph/(double)h);
	if (paint) paint(cr,data);
	gboolean ok=cairo_status(cr)==CAIRO_STATUS_SUCCESS;
	cairo_destroy(cr);
	cairo_surface_flush(image);
	cairo_surface_destroy(image);
	return ok;
}

static struct artwork *paint_available(struct artwork list[EDGE_BUFFERS],int w,int h,
		double scale,paint_fn paint,const void *data) {
	for (int i=0;i<EDGE_BUFFERS;i++) {
		if (!list[i].busy && paint_tile(&list[i],w,h,scale,paint,data)) return &list[i];
	}
	return NULL;
}

static gboolean create_layer(struct layer *s,struct wl_surface *parent,gboolean viewport) {
	s->surface=wl_compositor_create_surface(radar.compositor);
	if (!s->surface) return FALSE;
	s->subsurface=wl_subcompositor_get_subsurface(radar.subcompositor,s->surface,parent);
	if (!s->subsurface) return FALSE;
	wl_subsurface_set_desync(s->subsurface);
	struct wl_region *empty=wl_compositor_create_region(radar.compositor);
	if (!empty) return FALSE;
	wl_surface_set_input_region(s->surface,empty);
	wl_region_destroy(empty);
	if (viewport) {
		s->viewport=wp_viewporter_get_viewport(radar.viewporter,s->surface);
		if (!s->viewport) return FALSE;
	}
	return TRUE;
}

static void destroy_layer(struct layer *s) {
	if (s->viewport) wp_viewport_destroy(s->viewport);
	if (s->subsurface) wl_subsurface_destroy(s->subsurface);
	if (s->surface) wl_surface_destroy(s->surface);
	*s=(struct layer){0};
}

void map_radar_end(void) {
	for (int i=0;i<4;i++) destroy_layer(&radar.beam[i]);
	for (int i=0;i<4;i++) destroy_layer(&radar.locks[i]);
	destroy_layer(&radar.anchor);
	destroy_buffer(&radar.blank);
	destroy_buffer(&radar.strip);
	for(int k=0;k<EDGE_BUFFERS;k++) {
		destroy_buffer(&radar.edges[k]);
		for(int i=0;i<4;i++) destroy_buffer(&radar.corners[i][k]);
	}
}

static void hud_unmapped(GtkWidget *widget,gpointer data) {
	(void)widget;(void)data;
	map_radar_end();
}

void map_radar_init(GtkWindow *hud) {
	GdkDisplay *display=gtk_widget_get_display(GTK_WIDGET(hud));
	if (!GDK_IS_WAYLAND_DISPLAY(display)) return;
	radar.display=gdk_wayland_display_get_wl_display(display);
	struct wl_registry *registry=wl_display_get_registry(radar.display);
	if (registry) wl_registry_add_listener(registry,&globals,NULL);
	g_signal_connect(hud,"unmap",G_CALLBACK(hud_unmapped),NULL);
}

gboolean map_radar_begin(GtkWindow *hud) {
	if (radar.anchor.surface) return TRUE;
	if (!radar.compositor || !radar.subcompositor || !radar.shm || !radar.viewporter) return FALSE;
	GdkSurface *surface=gtk_native_get_surface(GTK_NATIVE(hud));
	if (!surface || !GDK_IS_WAYLAND_SURFACE(surface)) return FALSE;
	struct wl_surface *parent=gdk_wayland_surface_get_wl_surface(surface);
	if (!parent || !paint_tile(&radar.blank,1,1,1,NULL,NULL) ||
			!create_layer(&radar.anchor,parent,FALSE)) goto fail;
	// GTK owns the HUD commit. A separate 1 px transparent parent lets
	// position changes latch on our own commit, without touching GTK's state.
	wl_surface_attach(radar.anchor.surface,radar.blank.buffer,0,0);
	radar.blank.busy=TRUE;
	wl_surface_commit(radar.anchor.surface);
	for(int i=0;i<4;i++) {
		if (!create_layer(&radar.beam[i],radar.anchor.surface,TRUE)) goto fail;
		// Crop, attachment and position must all change at the same parent
		// commit; otherwise an old crop can flash at its new position.
		wl_subsurface_set_sync(radar.beam[i].subsurface);
		if (!create_layer(&radar.locks[i],radar.anchor.surface,TRUE)) goto fail;
	}
	return TRUE;
fail:
	map_radar_end();
	return FALSE;
}

static void attach_at(struct layer *s,struct artwork *tile,int w,int h,int x,int y) {
	wl_subsurface_set_position(s->subsurface,x,y);
	wp_viewport_set_source(s->viewport,-256,-256,-256,-256);
	wp_viewport_set_destination(s->viewport,w,h);
	wl_surface_attach(s->surface,tile->buffer,0,0);
	wl_surface_damage(s->surface,0,0,w,h);
	wl_surface_commit(s->surface);
	tile->busy=TRUE;
	s->shown=tile;
}

static void hide_layer(struct layer *s) {
	if (!s->shown) return;
	wl_surface_attach(s->surface,NULL,0,0);
	wl_surface_commit(s->surface);
	s->shown=NULL;
}

static void place_crop(struct layer *layer,struct artwork *tile,graphene_rect_t strip,
		graphene_rect_t piece) {
	int x=(int)piece.origin.x,y=(int)piece.origin.y;
	graphene_rect_t crop=GRAPHENE_RECT_INIT(x-strip.origin.x,y-strip.origin.y,
		piece.size.width,piece.size.height);
	if(layer->shown!=tile || x!=layer->x || y!=layer->y)
		wl_subsurface_set_position(layer->subsurface,x,y);
	if(layer->shown==tile && graphene_rect_equal(&layer->crop,&crop)) {
		layer->x=x;layer->y=y;return;
	}
	if(layer->shown!=tile) wl_surface_attach(layer->surface,tile->buffer,0,0);
	double sx=tile->width/strip.size.width,sy=tile->height/strip.size.height;
	wl_fixed_t l=wl_fixed_from_double(crop.origin.x*sx),t=wl_fixed_from_double(crop.origin.y*sy);
	wl_fixed_t r=wl_fixed_from_double((crop.origin.x+crop.size.width)*sx);
	wl_fixed_t b=wl_fixed_from_double((crop.origin.y+crop.size.height)*sy);
	wp_viewport_set_source(layer->viewport,l,t,r-l,b-t);
	wp_viewport_set_destination(layer->viewport,(int)piece.size.width,(int)piece.size.height);
	wl_surface_damage(layer->surface,0,0,(int)piece.size.width,(int)piece.size.height);
	wl_surface_commit(layer->surface);
	layer->shown=tile;layer->crop=crop;layer->x=x;layer->y=y;
}

struct radar_paint { double left,top,right,bottom,cut,tail,head; const graphene_rect_t *exclude; };
static void paint_beam(cairo_t *cr,const void *arg) {
	const struct radar_paint *p=arg;
	double l=p->left,t=p->top,r=p->right,b=p->bottom,c=p->cut;
	cairo_move_to(cr,l+c,t);cairo_line_to(cr,r-c,t);
	cairo_line_to(cr,r,t+c);cairo_line_to(cr,r,b-c);
	cairo_line_to(cr,r-c,b);cairo_line_to(cr,l+c,b);
	cairo_line_to(cr,l,b-c);cairo_line_to(cr,l,t+c);cairo_close_path(cr);
	cairo_clip(cr);
	cairo_pattern_t *gradient=cairo_pattern_create_linear(p->tail,0,p->head,0);
	for (int i=0;i<MAP_RADAR_STOPS;i++) {
		const struct map_radar_stop *s=&map_radar_stops[i];
		cairo_pattern_add_color_stop_rgba(gradient,s->at,s->color.red,s->color.green,s->color.blue,s->color.alpha);
	}
	cairo_set_source(cr,gradient);
	cairo_paint(cr);
	cairo_pattern_destroy(gradient);
	if(p->exclude) {
		const graphene_rect_t *e=p->exclude;
		cairo_set_operator(cr,CAIRO_OPERATOR_CLEAR);
		cairo_rectangle(cr,e->origin.x,e->origin.y,e->size.width,e->size.height);
		cairo_fill(cr);
	}
}

struct lock_paint { double x,y; int i; GdkRGBA color; };
static void paint_lock(cairo_t *cr,const void *data) {
	const struct lock_paint *p=data;
	double x=p->x,y=p->y,c=map_frame_cut_at(MAP_PERIMETER_INSET);
	int sx=(p->i%2)?-1:1,sy=(p->i/2)?-1:1;
	cairo_set_source_rgba(cr,p->color.red,p->color.green,p->color.blue,p->color.alpha);
	cairo_set_line_width(cr,MAP_PERIMETER_WIDTH);
	cairo_move_to(cr,x+sx*(c+8),y);cairo_line_to(cr,x+sx*c,y);
	cairo_line_to(cr,x,y+sy*c);cairo_line_to(cr,x,y+sy*(c+8));
	cairo_stroke(cr);
}

gboolean map_radar_step(GtkWindow *hud,graphene_rect_t frame,gint64 elapsed,gint64 duration,
		const GdkRGBA *color,const graphene_rect_t *exclude) {
	if (!radar.anchor.surface) return FALSE;
	GdkSurface *surface=gtk_native_get_surface(GTK_NATIVE(hud));
	if (!surface) return FALSE;
	double scale=gdk_surface_get_scale(surface);
	struct map_radar_sample s=map_radar_sample_at(frame,elapsed,duration);
	int by=(int)floor(s.top),bh=(int)ceil(s.bottom)-by,bw=(int)ceil(s.width)+1;
	if (s.head>s.left && s.tail<s.right) {
		gboolean body=s.tail>=s.left+s.cut && s.head<=s.right-s.cut;
		if (body) {
			int bx=(int)round(s.tail);
			double width=s.width,height=s.bottom-s.top;
			if (!radar.strip.buffer || radar.strip_width!=width || radar.strip_height!=height || radar.strip_scale!=scale) {
				for (int i=0;i<4;i++) if(radar.beam[i].shown==&radar.strip) hide_layer(&radar.beam[i]);
				destroy_buffer(&radar.strip);
				struct radar_paint p={0,s.top-by,width,s.bottom-by,0,0,width,NULL};
				if (!paint_tile(&radar.strip,bw,bh,scale,paint_beam,&p)) return FALSE;
				radar.strip_width=width;radar.strip_height=height;radar.strip_scale=scale;
			}
			// All pieces share one immutable pixel buffer. There is no outline,
			// gradient, raster or upload work when the crop moves.
			graphene_rect_t strip=GRAPHENE_RECT_INIT(bx,by,bw,bh),pieces[4];
			int count=map_radar_visible_rects(strip,exclude,pieces);
			for(int i=0;i<4;i++) {
				if(i<count) place_crop(&radar.beam[i],&radar.strip,strip,pieces[i]);
				else hide_layer(&radar.beam[i]);
			}
		} else {
			int bx=(int)floor(s.tail);
			graphene_rect_t e;
			if(exclude) e=GRAPHENE_RECT_INIT(exclude->origin.x-bx,exclude->origin.y-by,
				exclude->size.width,exclude->size.height);
			struct radar_paint p={s.left-bx,s.top-by,s.right-bx,s.bottom-by,s.cut,s.tail-bx,s.head-bx,
				exclude?&e:NULL};
			struct artwork *tile=paint_available(radar.edges,bw,bh,scale,paint_beam,&p);
			if (!tile) return FALSE;
			attach_at(&radar.beam[0],tile,bw,bh,bx,by);
			for(int i=1;i<4;i++) hide_layer(&radar.beam[i]);
		}
	} else for(int i=0;i<4;i++) hide_layer(&radar.beam[i]);
	GdkRGBA lock=map_radar_lock_color(elapsed,color);
	for(int i=0;i<4;i++) {
		if (lock.alpha<=0) { hide_layer(&radar.locks[i]);continue; }
		double x=(i%2?s.right:s.left)+(i%2?-MAP_PERIMETER_INSET:MAP_PERIMETER_INSET);
		double y=(i/2?s.bottom:s.top)+(i/2?-MAP_PERIMETER_INSET:MAP_PERIMETER_INSET);
		int lx=floor(x)-(i%2?24:2),ly=floor(y)-(i/2?24:2);
		struct lock_paint p={x-lx,y-ly,i,lock};
		struct artwork *tile=paint_available(radar.corners[i],CORNER_BOX,CORNER_BOX,scale,paint_lock,&p);
		if (!tile) return FALSE;
		attach_at(&radar.locks[i],tile,CORNER_BOX,CORNER_BOX,lx,ly);
	}
	wl_surface_commit(radar.anchor.surface);
	wl_display_flush(radar.display);
	return TRUE;
}
