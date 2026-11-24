//go:build linux && cgo && panel

#include <gtk/gtk.h>
#include <math.h>
#include "retained-paint.h"
#include "map-camera.h"
#include "shell-paint.h"

typedef struct {
	GtkWidget parent;
	GtkWidget *fallback;
	GdkGLContext *context;
	GdkTexture *texture,*blur;
	cairo_surface_t *image;
	GskPath *frame;
	int margin,padding;
	int last_w,last_h,last_margin,last_padding;
	GdkRGBA grid,tint;
	gboolean wallpaper,contain,frost,blur_attempted;
	gboolean *grid_ready;
} RetainedPaint;

typedef struct { GtkWidgetClass parent_class; } RetainedPaintClass;
G_DEFINE_TYPE(RetainedPaint,retained_paint,GTK_TYPE_WIDGET)

static GskPath *outline(int w,int h,int margin,double pad) {
	cairo_surface_t *surface=cairo_recording_surface_create(CAIRO_CONTENT_ALPHA,NULL);
	cairo_t *cr=cairo_create(surface);
	map_frame_outline(cr,margin,margin,w-2*margin,h-2*margin,pad);
	cairo_path_t *shape=cairo_copy_path(cr);
	GskPathBuilder *b=gsk_path_builder_new();
	gsk_path_builder_add_cairo_path(b,shape);
	cairo_path_destroy(shape);
	cairo_destroy(cr);
	cairo_surface_destroy(surface);
	return gsk_path_builder_free_to_path(b);
}

static void paths(RetainedPaint *p,int w,int h,int margin,int padding) {
	if (p->frame && p->last_w==w && p->last_h==h &&
			p->last_margin==margin && p->last_padding==padding) return;
	g_clear_pointer(&p->frame,gsk_path_unref);
	p->frame=outline(w,h,margin,p->wallpaper?.5:0);
	p->last_w=w;p->last_h=h;p->last_margin=margin;p->last_padding=padding;
}

static void image_released(gpointer p) { cairo_surface_destroy(p); }

static GdkTexture *image_texture(cairo_surface_t *surface) {
	cairo_surface_flush(surface);
	int w=cairo_image_surface_get_width(surface),h=cairo_image_surface_get_height(surface);
	int stride=cairo_image_surface_get_stride(surface);
	GBytes *bytes=g_bytes_new_with_free_func(cairo_image_surface_get_data(surface),(size_t)stride*h,
		image_released,cairo_surface_reference(surface));
	bool opaque=cairo_image_surface_get_format(surface)==CAIRO_FORMAT_RGB24;
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
	GdkMemoryFormat format=opaque?GDK_MEMORY_B8G8R8X8:GDK_MEMORY_B8G8R8A8_PREMULTIPLIED;
#else
	GdkMemoryFormat format=opaque?GDK_MEMORY_X8R8G8B8:GDK_MEMORY_A8R8G8B8_PREMULTIPLIED;
#endif
	GdkTexture *texture=gdk_memory_texture_new(w,h,format,bytes,stride);
	g_bytes_unref(bytes);
	return texture;
}

static void prepare_tile(RetainedPaint *p) {
	enum { pitch=88 };
	unsigned char texels[pitch*pitch*4]={0};
	GdkRGBA dot=map_grid_junction(p->grid);
	for(int y=0;y<pitch;y++) for(int x=0;x<pitch;x++) {
		// The line is centered at .5 logical px, i.e. 1 texel at 2×.
		// Wrap the mark at tile edges so each intersection is complete.
		double ax=fabs(x+.5-1),ay=fabs(y+.5-1);
		double dx=MIN(ax,pitch-ax),dy=MIN(ay,pitch-ay);
		double dot_alpha=dot.alpha*CLAMP(2*MAP_GRID_JUNCTION_RADIUS+.5-hypot(dx,dy),0,1);
		double line_alpha=(x<2 || y<2)?MAP_GRID_LINE_ALPHA*(1-dot_alpha):0;
		if(dot_alpha==0 && line_alpha==0) continue;
		unsigned char *c=texels+4*(y*pitch+x);
		c[0]=(unsigned char)((p->grid.red*line_alpha+dot.red*dot_alpha)*255+.5);
		c[1]=(unsigned char)((p->grid.green*line_alpha+dot.green*dot_alpha)*255+.5);
		c[2]=(unsigned char)((p->grid.blue*line_alpha+dot.blue*dot_alpha)*255+.5);
		c[3]=(unsigned char)((line_alpha+dot_alpha)*255+.5);
	}
	GBytes *bytes=g_bytes_new(texels,sizeof(texels));
	p->texture=gdk_memory_texture_new(pitch,pitch,GDK_MEMORY_R8G8B8A8_PREMULTIPLIED,bytes,pitch*4);
	g_bytes_unref(bytes);
}

static void grid_repeat(GtkSnapshot *snap,GdkTexture *texture,const graphene_rect_t *tile,
		double x,double y,double w,double h) {
	if (w<=0 || h<=0) return;
	graphene_rect_t box=GRAPHENE_RECT_INIT(x,y,w,h);
	gtk_snapshot_push_repeat(snap,&box,tile);
	gtk_snapshot_append_texture(snap,texture,tile);
	gtk_snapshot_pop(snap);
}

static gboolean grid_hits(double x,double size,double origin) {
	double phase=fmod(x-origin,44);
	if (phase<0) phase+=44;
	// Include linear texture sampling at the edges.
	return phase<2 || phase+size>43;
}

static void grid_corners(GtkSnapshot *snap,GdkTexture *texture,const graphene_rect_t *tile,
		double left,double top,double right,double bottom,double cut) {
	if (cut<=0) return;
	for (int i=0;i<4;i++) {
		double x=i%2?right-cut:left,y=i/2?bottom-cut:top;
		if (!grid_hits(x,cut,tile->origin.x) && !grid_hits(y,cut,tile->origin.y)) continue;
		GskPathBuilder *builder=gsk_path_builder_new();
		gsk_path_builder_move_to(builder,x+(i%2?0:cut),y+(i/2?0:cut));
		gsk_path_builder_line_to(builder,x+(i%2?cut:0),y+(i/2?0:cut));
		gsk_path_builder_line_to(builder,x+(i%2?0:cut),y+(i/2?cut:0));
		gsk_path_builder_close(builder);
		GskPath *path=gsk_path_builder_free_to_path(builder);
		gtk_snapshot_push_fill(snap,path,GSK_FILL_RULE_WINDING);
		grid_repeat(snap,texture,tile,x,y,cut,cut);
		gtk_snapshot_pop(snap);
		gsk_path_unref(path);
	}
}

static void retained_snapshot(GtkWidget *widget,GtkSnapshot *snap) {
	RetainedPaint *p=(RetainedPaint *)widget;
	if(!p->context) return;
	int w=gtk_widget_get_width(widget),h=gtk_widget_get_height(widget);
	if(w<1||h<1) return;
	graphene_rect_t screen=GRAPHENE_RECT_INIT(0,0,w,h);
	int margin=map_frame_margin(w,h,p->margin);
	int padding=MIN(p->padding,(MIN(w,h)-2*margin-16)/2);
	if (p->wallpaper) {
		const GdkRGBA bg={.023,.047,.081,1};
		gtk_snapshot_append_color(snap,&bg,&screen);
		double iw=gdk_texture_get_width(p->texture), ih=gdk_texture_get_height(p->texture);
		struct map_wallpaper_fit fit=map_screen_wallpaper(w,h,iw,ih,p->contain);
		graphene_rect_t box=GRAPHENE_RECT_INIT(fit.x,fit.y,fit.width,fit.height);
		gtk_snapshot_push_clip(snap,&screen);
		gtk_snapshot_append_texture(snap,p->texture,&box);
		if (p->frost && p->blur && w>=120 && h>=100) {
			paths(p,w,h,margin,0);
			gtk_snapshot_push_fill(snap,p->frame,GSK_FILL_RULE_WINDING);
			gtk_snapshot_push_opacity(snap,.82);
			gtk_snapshot_append_texture(snap,p->blur,&box);
			gtk_snapshot_pop(snap);
			gtk_snapshot_pop(snap);
		}
		gtk_snapshot_pop(snap);
	} else if (w>=120 && h>=100) {
		if(p->tint.alpha>0) {
			paths(p,w,h,margin,padding);
			gtk_snapshot_push_fill(snap,p->frame,GSK_FILL_RULE_WINDING);
			gtk_snapshot_append_color(snap,&p->tint,&screen);
			gtk_snapshot_pop(snap);
		}
		if(!p->texture) prepare_tile(p);
		float x=w<480?24:40, y=38;
		float pitch=44;
		graphene_rect_t tile=GRAPHENE_RECT_INIT(x,y,pitch,pitch);
		// The content's long edges are rectangular; mask only its tiny corners.
		// A full output-sized mask is needless GPU work during the radar pass.
		double inset=map_frame_content_inset(padding+.5),edge=margin+.5+inset,cut=map_frame_cut_at(inset);
		double right=w-edge,bottom=h-edge;
		grid_repeat(snap,p->texture,&tile,edge,edge+cut,right-edge,bottom-edge-2*cut);
		grid_repeat(snap,p->texture,&tile,edge+cut,edge,right-edge-2*cut,cut);
		grid_repeat(snap,p->texture,&tile,edge+cut,bottom-cut,right-edge-2*cut,cut);
		grid_corners(snap,p->texture,&tile,edge,edge,right,bottom,cut);
	}
}

static void prepare_frost(RetainedPaint *p) {
	if (!p->context || !p->frost || p->blur_attempted) return;
	p->blur_attempted=TRUE;
	p->blur=map_gl_frost_texture(p->context,p->image);
}

static void retained_realize(GtkWidget *widget) {
	GTK_WIDGET_CLASS(retained_paint_parent_class)->realize(widget);
	RetainedPaint *p=(RetainedPaint *)widget;
	p->context=map_gpu_context(widget,p->wallpaper?"wallpaper":"grid");
	if (!p->context) return;
	if (p->wallpaper) {
		if (!p->texture) p->texture=image_texture(p->image);
		prepare_frost(p);
		gtk_widget_set_opacity(p->fallback,0);
	} else {
		*p->grid_ready=TRUE;
		gtk_widget_queue_draw(p->fallback);
	}
}

static void retained_unrealize(GtkWidget *widget) {
	RetainedPaint *p=(RetainedPaint *)widget;
	g_clear_object(&p->texture);
	g_clear_object(&p->blur);
	g_clear_object(&p->context);
	p->blur_attempted=FALSE;
	if(p->wallpaper) gtk_widget_set_opacity(p->fallback,1);
	else *p->grid_ready=FALSE;
	GTK_WIDGET_CLASS(retained_paint_parent_class)->unrealize(widget);
}

static void retained_dispose(GObject *object) {
	RetainedPaint *p=(RetainedPaint *)object;
	g_clear_object(&p->fallback);
	g_clear_pointer(&p->image,cairo_surface_destroy);
	g_clear_pointer(&p->frame,gsk_path_unref);
	G_OBJECT_CLASS(retained_paint_parent_class)->dispose(object);
}

static void retained_paint_class_init(RetainedPaintClass *class) {
	GtkWidgetClass *w=GTK_WIDGET_CLASS(class);
	w->realize=retained_realize;w->unrealize=retained_unrealize;w->snapshot=retained_snapshot;
	G_OBJECT_CLASS(class)->dispose=retained_dispose;
}
static void retained_paint_init(RetainedPaint *p) {
	gtk_widget_set_can_target(GTK_WIDGET(p),FALSE);
	gtk_widget_set_hexpand(GTK_WIDGET(p),TRUE);
	gtk_widget_set_vexpand(GTK_WIDGET(p),TRUE);
}

GtkWidget *retained_wallpaper_new(cairo_surface_t *image,gboolean contain,GtkWidget *fallback) {
	RetainedPaint *p=g_object_new(retained_paint_get_type(),NULL);
	p->image=cairo_surface_reference(image);
	p->wallpaper=TRUE;p->contain=contain;
	p->fallback=g_object_ref(fallback);
	return GTK_WIDGET(p);
}

void retained_wallpaper_update(GtkWidget *widget,gboolean frost,int margin) {
	if(!widget) return;
	RetainedPaint *p=(RetainedPaint *)widget;
	p->frost=frost;p->margin=margin;
	prepare_frost(p);
	gtk_widget_queue_draw(widget);
}

GtkWidget *retained_grid_new(gboolean *ready,GtkWidget *fallback) {
	RetainedPaint *p=g_object_new(retained_paint_get_type(),NULL);
	p->grid_ready=ready;p->fallback=g_object_ref(fallback);
	return GTK_WIDGET(p);
}

void retained_grid_update(GtkWidget *widget,GdkRGBA grid,GdkRGBA tint,int margin,int padding) {
	if(!widget) return;
	RetainedPaint *p=(RetainedPaint *)widget;
	if (!gdk_rgba_equal(&p->grid,&grid)) g_clear_object(&p->texture);
	p->grid=grid;p->tint=tint;p->margin=margin;p->padding=padding;
	gtk_widget_queue_draw(widget);
}
