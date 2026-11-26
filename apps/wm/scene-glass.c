//go:build linux && cgo && wlroots

#include "scene-glass.h"
#include <drm_fourcc.h>
#include <math.h>
#include <stdlib.h>
#include <wlr/render/allocator.h>
#include <wlr/render/gles2.h>
#include <wlr/render/drm_syncobj.h>
#include <wlr/render/pass.h>
#include <wlr/render/swapchain.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_linux_drm_syncobj_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

enum { MAX_DRAWS=128, MAX_WIDTH=304, MAX_HEIGHT=192, HALO=8, CORNER=9 };
struct scene_glass {
	struct wlr_output *output;
	struct wlr_scene_buffer *layer;
	struct wlr_buffer *source, *middle;
	struct wlr_texture *source_texture,*middle_texture;
	struct wlr_swapchain *outputs;
	struct wlr_drm_syncobj_timeline *capture_done;
	uint64_t signature,capture_point;
	int width,height;
	uint32_t format;
	bool visible,broken;
};

struct draw {
	struct wlr_box dst;
	struct wlr_scene_rect *rect;
	struct wlr_scene_buffer *buffer;
	struct wlr_texture *texture;
	struct wlr_single_pixel_buffer_v1 *pixel;
	struct wlr_linux_drm_syncobj_surface_v1_state *sync;
	bool own_texture;
};
struct draw_list {
	struct draw entries[MAX_DRAWS];
	struct wlr_box source;
	int width,height,n;
	uint64_t signature;
	bool failed,explicit;
};

static bool no_input(struct wlr_scene_buffer *buffer,double *x,double *y) {
	(void)buffer;(void)x;(void)y;return false;
}

struct scene_glass *scene_glass_create(struct wlr_output *output,struct wlr_scene_tree *shell) {
	struct wlr_renderer *renderer=output->renderer;
	if(!renderer || !wlr_renderer_is_gles2(renderer)) return NULL;
	struct scene_glass *f=calloc(1,sizeof(*f));
	if(!f) return NULL;
	f->output=output;
	f->layer=wlr_scene_buffer_create(shell->node.parent,NULL);
	if(!f->layer) {free(f);return NULL;}
	wlr_scene_node_place_below(&f->layer->node,&shell->node);
	f->layer->point_accepts_input=no_input;
	wlr_scene_buffer_set_filter_mode(f->layer,WLR_SCALE_FILTER_BILINEAR);
	wlr_scene_node_set_enabled(&f->layer->node,false);
	return f;
}

static void hide(struct scene_glass *f) {
	if(!f->visible) return;
	f->visible=false;
	wlr_scene_node_set_enabled(&f->layer->node,false);
	wlr_scene_buffer_set_buffer(f->layer,NULL);
}

static void release_targets(struct scene_glass *f) {
	hide(f);
	if(f->source_texture) wlr_texture_destroy(f->source_texture);
	if(f->middle_texture) wlr_texture_destroy(f->middle_texture);
	if(f->source) wlr_buffer_drop(f->source);
	if(f->middle) wlr_buffer_drop(f->middle);
	if(f->outputs) wlr_swapchain_destroy(f->outputs);
	f->source_texture=NULL;f->middle_texture=NULL;f->source=NULL;f->middle=NULL;f->outputs=NULL;
}

void scene_glass_destroy(struct scene_glass *f) {
	if(!f) return;
	release_targets(f);
	wlr_drm_syncobj_timeline_unref(f->capture_done);
	wlr_scene_node_destroy(&f->layer->node);
	free(f);
}

static bool prepare(struct scene_glass *f,int w,int h,const struct wlr_drm_format *format) {
	if(f->width==w && f->height==h && f->format==format->format && f->outputs) return true;
	release_targets(f);
	f->width=w;f->height=h;f->format=format->format;
	struct wlr_allocator *a=f->output->allocator;
	struct wlr_renderer *r=f->output->renderer;
	f->source=wlr_allocator_create_buffer(a,w+2*HALO,h+2*HALO,format);
	f->middle=wlr_allocator_create_buffer(a,w,h+2*HALO,format);
	if(f->source) f->source_texture=wlr_texture_from_buffer(r,f->source);
	if(f->middle) f->middle_texture=wlr_texture_from_buffer(r,f->middle);
	if(f->source_texture && f->middle_texture) {
		struct wlr_drm_format alpha=*format;
		alpha.format=DRM_FORMAT_ARGB8888;
		f->outputs=wlr_swapchain_create(a,w,h,&alpha);
	}
	if(f->outputs) return true;
	release_targets(f);
	return false;
}

static void hash(struct draw_list *list,const void *data,size_t n) {
	const unsigned char *p=data;
	for(size_t i=0;i<n;i++) list->signature=(list->signature^p[i])*UINT64_C(1099511628211);
}
#define HASH(list, obj) hash(list,&(obj),sizeof(obj))

static void collect(struct draw_list *list,struct wlr_scene_node *node,int x,int y) {
	if(!node->enabled || list->failed) return;
	x+=node->x;y+=node->y;
	if(node->type==WLR_SCENE_NODE_TREE) {
		struct wlr_scene_node *child;
		wl_list_for_each(child,&wlr_scene_tree_from_node(node)->children,link) collect(list,child,x,y);
		return;
	}
	struct draw draw={0};
	int w,h;
	if(node->type==WLR_SCENE_NODE_RECT) {
		draw.rect=wlr_scene_rect_from_node(node);
		w=draw.rect->width;h=draw.rect->height;
		if(draw.rect->color[3]<=0) return;
	} else {
		draw.buffer=wlr_scene_buffer_from_node(node);
		if(!draw.buffer->buffer || draw.buffer->opacity<=0) return;
		w=draw.buffer->dst_width;
		h=draw.buffer->dst_height;
		if(w<1 || h<1) {
			w=draw.buffer->buffer->width; h=draw.buffer->buffer->height;
			if(draw.buffer->transform&1) {int swap=w;w=h;h=swap;}
		}
	}
	struct wlr_box overlap,box={x,y,w,h};
	if(w<1 || h<1 || !wlr_box_intersection(&overlap,&box,&list->source)) return;
	if(list->n==MAX_DRAWS) {list->failed=true;return;}
	double sx=(double)list->width/list->source.width,sy=(double)list->height/list->source.height;
	draw.dst.x=lround((x-list->source.x)*sx);
	draw.dst.y=lround((y-list->source.y)*sy);
	draw.dst.width=lround((x+w-list->source.x)*sx)-draw.dst.x;
	draw.dst.height=lround((y+h-list->source.y)*sy)-draw.dst.y;
	if(draw.dst.width<1 || draw.dst.height<1) return;
	HASH(list,node);
	HASH(list,draw.dst);
	if(draw.rect) HASH(list,draw.rect->color);
	else {
		struct wlr_scene_buffer *b=draw.buffer;
		HASH(list,b->buffer);HASH(list,b->opacity);HASH(list,b->transform);HASH(list,b->src_box);
		struct wlr_scene_surface *s=wlr_scene_surface_try_from_buffer(b);
		if(s) {
			struct wlr_surface *surface=s->surface;
			HASH(list,surface->current.seq);
			draw.texture=wlr_surface_get_texture(surface);
			struct wlr_linux_drm_syncobj_surface_v1_state *sync=
				wlr_linux_drm_syncobj_v1_get_surface_state(surface);
			if(sync && sync->acquire_timeline) {
				// wlroots 0.20.2 returns success even without a release merger.
				// Verify that this draw refers to the same committed buffer.
				if(!draw.texture || !surface->buffer || b->buffer!=&surface->buffer->base ||
						!sync->WLR_PRIVATE.release_merger) {list->failed=true;return;}
				draw.sync=sync;
				list->explicit=true;
				HASH(list,sync->acquire_timeline);HASH(list,sync->acquire_point);
			}
		} else if(wlr_client_buffer_get(b->buffer) || b->WLR_PRIVATE.wait_timeline) {
			// A client buffer without its scene surface does not expose a
			// commit release merger here. Never borrow an untracked texture.
			list->failed=true;
			return;
		}
		draw.pixel=wlr_single_pixel_buffer_v1_try_from_buffer(b->buffer);
	}
	list->entries[list->n++]=draw;
}

static bool render_source(struct scene_glass *f,struct draw_list *list,struct wl_event_loop *loop) {
	struct wlr_renderer *r=f->output->renderer;
	struct wlr_buffer *target=f->source;
	struct wlr_buffer_pass_options options={0};
	bool ok=false;
	if(list->explicit) {
		if(!loop || !r->features.timeline || f->capture_point==UINT64_MAX) return false;
		if(!f->capture_done) {
			int fd=wlr_renderer_get_drm_fd(r);
			if(fd<0 || !(f->capture_done=wlr_drm_syncobj_timeline_create(fd))) return false;
		}
		options.signal_timeline=f->capture_done;
		options.signal_point=++f->capture_point;
	}
	for(int i=0;i<list->n;i++) {
		struct draw *d=&list->entries[i];
		if(!d->buffer || d->texture || d->pixel) continue;
		// Other scene-owned nodes (like window chrome) have compositor buffers.
		d->texture=wlr_texture_from_buffer(r,d->buffer->buffer);
		if(!d->texture) goto done;
		d->own_texture=true;
	}
	struct wlr_render_pass *pass=wlr_renderer_begin_buffer_pass(r,target,list->explicit?&options:NULL);
	if(!pass) goto done;
	// A partial registration is possible on allocation failure. Submit the
	// empty pass to complete any points already registered, without sampling.
	if(list->explicit) {
		for(int i=0;i<list->n;i++) {
			struct wlr_linux_drm_syncobj_surface_v1_state *sync=list->entries[i].sync;
			if(!sync) continue;
			bool duplicate=false;
			for(int j=0;j<i;j++) if(list->entries[j].sync==sync) {duplicate=true;break;}
			if(!duplicate && !wlr_linux_drm_syncobj_v1_state_add_release_point(sync,
					f->capture_done,options.signal_point,loop)) goto submit;
		}
	}
	wlr_render_pass_add_rect(pass,&(struct wlr_render_rect_options){
		.box={0,0,target->width,target->height},.color={.07,.10,.14,1},
		.blend_mode=WLR_RENDER_BLEND_MODE_NONE,
	});
	for(int i=0;i<list->n;i++) {
		struct draw *d=&list->entries[i];
		if(d->rect) {
			const float *c=d->rect->color;
			wlr_render_pass_add_rect(pass,&(struct wlr_render_rect_options){
				.box=d->dst,.color={c[0],c[1],c[2],c[3]},
			});
		} else if(d->pixel) {
			const struct wlr_single_pixel_buffer_v1 *c=d->pixel;
			const double k=d->buffer->opacity/UINT32_MAX;
			wlr_render_pass_add_rect(pass,&(struct wlr_render_rect_options){
				.box=d->dst,.color={c->r*k,c->g*k,c->b*k,c->a*k},
			});
		} else {
			struct wlr_scene_buffer *b=d->buffer;
			wlr_render_pass_add_texture(pass,&(struct wlr_render_texture_options){
				.texture=d->texture,.dst_box=d->dst,.src_box=b->src_box,.alpha=&b->opacity,
				.transform=b->transform,.filter_mode=b->filter_mode,
				.transfer_function=b->transfer_function,.color_encoding=b->color_encoding,.color_range=b->color_range,
				.wait_timeline=d->sync?d->sync->acquire_timeline:NULL,
				.wait_point=d->sync?d->sync->acquire_point:0,
			});
		}
	}
	ok=true;
submit:
	if(!wlr_render_pass_submit(pass)) {
		// A fence export/import failure may follow an already issued GPU read.
		// Never signal or advance this timeline. Leave release unsignalled.
		if(list->explicit) {
			f->broken=true;
			wlr_log(WLR_ERROR,"Scene glass source fence submission failed; disabled for this slot");
		}
		ok=false;
	}
done:
	for(int i=0;i<list->n;i++)
		if(list->entries[i].own_texture) wlr_texture_destroy(list->entries[i].texture);
	return ok;
}

static bool blur(struct wlr_renderer *r,struct wlr_buffer *target,struct wlr_texture *src,
		bool vertical,const pixman_region32_t *clip) {
	struct wlr_render_pass *pass=wlr_renderer_begin_buffer_pass(r,target,NULL);
	if(!pass) return false;
	if(clip) wlr_render_pass_add_rect(pass,&(struct wlr_render_rect_options){
		.box={0,0,target->width,target->height},.color={0,0,0,0},.blend_mode=WLR_RENDER_BLEND_MODE_NONE,
	});
	// Gaussian with sigma 2.8, truncated at six source texels. Each
	// fractional sample combines two adjacent texels by linear filtering.
	// Seven draws cover all thirteen texels, without the old two-texel gaps.
	static const double positions[]={0,-1.452313745,1.452313745,
		-3.390210240,3.390210240,-5.331472959,5.331472959};
	static const float weights[]={1.f,1.713053025f,1.713053025f,
		.923727139f,.923727139f,.303701696f,.303701696f};
	float total=0;
	for(size_t i=0;i<sizeof(positions)/sizeof(positions[0]);i++) {
		double sample=HALO+positions[i];
		total+=weights[i];
		const float alpha=weights[i]/total;
		wlr_render_pass_add_texture(pass,&(struct wlr_render_texture_options){
			.texture=src,.src_box={vertical?0:sample,vertical?sample:0,target->width,target->height},
			.dst_box={0,0,target->width,target->height},.clip=clip,.alpha=&alpha,
			.filter_mode=WLR_SCALE_FILTER_BILINEAR,
			.blend_mode=i?WLR_RENDER_BLEND_MODE_PREMULTIPLIED:WLR_RENDER_BLEND_MODE_NONE,
		});
	}
	return wlr_render_pass_submit(pass);
}

unsigned scene_glass_update_masked(struct scene_glass *f,struct wlr_box view,unsigned opacity,
		bool dragging,struct wl_event_loop *loop,struct wlr_scene_node *const *sources,size_t count,
		const struct wlr_box *holes,size_t count_holes) {
	if(!f) return SCENE_GLASS_OFF;
	const struct wlr_swapchain *chain=f->output->swapchain;
	if(opacity==0 || view.width<24 || view.height<24 || !chain) {
		// The reduced image is reusable only while the popup is showing.
		if(f->source || f->middle || f->outputs) release_targets(f);
		return SCENE_GLASS_OFF;
	}
	if(dragging || f->broken) {
		hide(f);
		return dragging?SCENE_GLASS_DRAG:SCENE_GLASS_FALLBACK;
	}
	double scale=fmin(1./3.,fmin((double)MAX_WIDTH/view.width,(double)MAX_HEIGHT/view.height));
	int w=lround(view.width*scale),h=lround(view.height*scale);
	bool rebuilt=false;
	int halo=lround(HALO/scale);
	struct draw_list list={
		.source={view.x-halo,view.y-halo,view.width+2*halo,view.height+2*halo},
		.width=w+2*HALO,.height=h+2*HALO,.signature=UINT64_C(14695981039346656037),
	};
	HASH(&list,view);
	for(size_t i=0;i<count_holes;i++)HASH(&list,holes[i]);
	// Submitted in scene order, from mechanical background to visible apps.
	// Never enter the front tree that owns the requesting widget.
	for(size_t i=0;i<count;i++) collect(&list,sources[i],0,0);
	if(list.failed) {hide(f);return SCENE_GLASS_FALLBACK;}
	if(!f->visible || f->signature!=list.signature || f->format!=chain->format.format) {
		if(!prepare(f,w,h,&chain->format)) {hide(f);return SCENE_GLASS_FALLBACK;}
		struct wlr_buffer *out=wlr_swapchain_acquire(f->outputs);
		if(!out) {hide(f);return SCENE_GLASS_FALLBACK;}
		pixman_region32_t cut;
		pixman_region32_init(&cut);
		int rim=(int)ceil(CORNER*scale);
		rim=fmin(rim,fmin((w-1)/2,(h-1)/2));
		for(int y=0;y<rim;y++) {
			int inset=rim-y;
			pixman_region32_union_rect(&cut,&cut,inset,y,w-2*inset,1);
			pixman_region32_union_rect(&cut,&cut,inset,h-y-1,w-2*inset,1);
		}
		pixman_region32_union_rect(&cut,&cut,0,rim,w,h-2*rim);
		for(size_t i=0;i<count_holes;i++) {
			// Round holes outwards so translucent app pixels cannot pick
			// up the frame effect. All coordinates are local to the capture.
			struct wlr_box box;
			if(!wlr_box_intersection(&box,&view,&holes[i]))continue;
			int left=floor((box.x-view.x)*(double)w/view.width);
			int right=ceil((box.x+box.width-view.x)*(double)w/view.width);
			int top=floor((box.y-view.y)*(double)h/view.height);
			int bottom=ceil((box.y+box.height-view.y)*(double)h/view.height);
			pixman_region32_t hole;
			pixman_region32_init_rect(&hole,left,top,right-left,bottom-top);
			pixman_region32_subtract(&cut,&cut,&hole);
			pixman_region32_fini(&hole);
		}
		struct wlr_renderer *r=f->output->renderer;
		bool ok=render_source(f,&list,loop) && blur(r,f->middle,f->source_texture,false,NULL)
			&& blur(r,out,f->middle_texture,true,&cut);
		if(ok) {
			if(!f->visible) wlr_log(WLR_INFO,"Mothership live scene glass: %dx%d",w,h);
			wlr_scene_node_set_position(&f->layer->node,view.x,view.y);
			wlr_scene_buffer_set_dest_size(f->layer,view.width,view.height);
			wlr_scene_buffer_set_buffer(f->layer,out);
			wlr_scene_node_set_enabled(&f->layer->node,true);
			f->signature=list.signature;f->visible=true;rebuilt=true;
		}
		pixman_region32_fini(&cut);
		wlr_buffer_unlock(out);
		if(!ok) {hide(f);return SCENE_GLASS_FALLBACK;}
	}
	wlr_scene_buffer_set_opacity(f->layer,opacity/1000.f);
	return rebuilt?SCENE_GLASS_REBUILD:SCENE_GLASS_HIT;
}


unsigned scene_glass_update(struct scene_glass *f,struct wlr_box view,unsigned opacity,
		bool dragging,struct wl_event_loop *loop,struct wlr_scene_node *const *sources,size_t count) {
	return scene_glass_update_masked(f,view,opacity,dragging,loop,sources,count,NULL,0);
}

void scene_glass_place_below(struct scene_glass *f,struct wlr_scene_node *anchor) {
	if(f && anchor && &f->layer->node!=anchor && f->layer->node.parent==anchor->parent &&
		f->layer->node.link.next!=&anchor->link)
		wlr_scene_node_place_below(&f->layer->node,anchor);
}
