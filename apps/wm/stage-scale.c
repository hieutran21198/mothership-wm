//go:build linux && cgo && wlroots

#include "stage-scale.h"
#include "surface-sync.h"
#include "owned-preview.h"

#include <math.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_compositor.h>

static void retain_layer(struct wlr_scene_buffer *src,int x,int y,void *data) {
	struct stage_scale *scale=data;
	if(scale->failed) return;
	struct wlr_scene_surface *surface=wlr_scene_surface_try_from_buffer(src);
	bool explicit=surface && surface_has_explicit_sync(surface->surface);
	// Borrow only compositor-owned copies of explicit-sync app pixels.
	// Popups and subsurfaces without such a copy stay out of the transient sprite.
	if(explicit && surface->surface!=scale->owned_surface)return;
	if(explicit && (!scale->owned || !scale->owned->buffer)) {scale->failed=true;return;}
	struct wlr_buffer *buffer=src->buffer;
	if(!buffer && surface && surface->surface->buffer) buffer=&surface->surface->buffer->base;
	if(!buffer) return;
	if(scale->count==STAGE_SCALE_LAYERS) {scale->failed=true;return;}
	struct wlr_scene_buffer *sprite=wlr_scene_buffer_create(scale->tree,explicit?NULL:buffer);
	if(!sprite) {scale->failed=true;return;}
	int w=src->dst_width,h=src->dst_height;
	if(w<=0 || h<=0) {
		w=buffer->width;h=buffer->height;
		if(src->transform%2) {int t=w;w=h;h=t;}
	}
	scale->has_surface|=surface!=NULL;
	int i=scale->count++;
	scale->layers[i].buffer=sprite;
	scale->layers[i].box=(struct wlr_box){x-scale->full.x,y-scale->full.y,w,h};
	if(explicit) {
		owned_preview_bind(scale->owned,sprite);
	} else {
		wlr_scene_buffer_set_source_box(sprite,&src->src_box);
		wlr_scene_buffer_set_transform(sprite,src->transform);
	}
	wlr_scene_buffer_set_opacity(sprite,src->opacity);
	wlr_scene_buffer_set_color_encoding(sprite,src->color_encoding);
	wlr_scene_buffer_set_color_range(sprite,src->color_range);
}

bool stage_scale_capture(struct stage_scale *scale,struct wlr_scene_tree *parent,
		struct wlr_scene_tree *source,struct wlr_box full,
		struct wlr_surface *owned_surface,struct owned_preview *owned) {
	stage_scale_clear(scale);
	if(full.width<=0 || full.height<=0) return false;
	scale->full=full;
	scale->owned_surface=owned_surface;
	scale->owned=owned;
	scale->tree=wlr_scene_tree_create(parent);
	if(!scale->tree) return false;
	wlr_scene_node_set_enabled(&scale->tree->node,false);
	wlr_scene_node_for_each_buffer(&source->node,retain_layer,scale);
	scale->owned_surface=NULL;scale->owned=NULL;
	if(scale->failed || !scale->has_surface) {stage_scale_clear(scale);return false;}
	return true;
}

static int blend(int from,int to,double t) {
	return from+(int)round((to-from)*t);
}

void stage_scale_draw_box(struct stage_scale *scale,struct wlr_box box) {
	struct wlr_box f=scale->full;
	int w=box.width,h=box.height;
	for(int i=0;i<scale->count;i++) {
		const struct wlr_box b=scale->layers[i].box;
		struct wlr_scene_buffer *sprite=scale->layers[i].buffer;
		int left=(int)round(b.x*(double)w/f.width),top=(int)round(b.y*(double)h/f.height);
		int right=(int)round((b.x+b.width)*(double)w/f.width);
		int bottom=(int)round((b.y+b.height)*(double)h/f.height);
		wlr_scene_node_set_enabled(&sprite->node,right>left && bottom>top);
		if(right<=left || bottom<=top) continue;
		wlr_scene_buffer_set_dest_size(sprite,right-left,bottom-top);
		wlr_scene_node_set_position(&sprite->node,left,top);
	}
	wlr_scene_node_set_position(&scale->tree->node,box.x,box.y);
	wlr_scene_node_set_enabled(&scale->tree->node,true);
}

void stage_scale_draw(struct stage_scale *scale,struct wlr_box thumbnail,double amount) {
	struct wlr_box f=scale->full;
	stage_scale_draw_box(scale,(struct wlr_box){
		blend(thumbnail.x,f.x,amount),blend(thumbnail.y,f.y,amount),
		blend(thumbnail.width,f.width,amount),blend(thumbnail.height,f.height,amount),
	});
}

void stage_scale_clear(struct stage_scale *scale) {
	if(scale->tree) wlr_scene_node_destroy(&scale->tree->node);
	*scale=(struct stage_scale){0};
}
