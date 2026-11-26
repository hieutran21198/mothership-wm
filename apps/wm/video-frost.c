//go:build linux && cgo && wlroots

#include "video-frost.h"
#include "surface-sync.h"
#include "video-frost-sample.h"
#include "../../internal/wayland/map-frame.h"
#include <drm_fourcc.h>
#include <math.h>
#include <stdlib.h>
#include <wlr/render/allocator.h>
#include <wlr/render/gles2.h>
#include <wlr/render/pass.h>
#include <wlr/render/swapchain.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

struct video_frost {
	struct wlr_output *output;
	struct wlr_scene_buffer *layer;
	struct wlr_swapchain *outputs;
	struct wlr_buffer *scratch;
	struct wlr_texture *input;
	int width,height,view_w,view_h,margin;
	uint32_t seq,format;
	struct wlr_surface *wallpaper;
	bool visible,failed;
};

struct video_frost *video_frost_create(struct wlr_scene_tree *tree,struct wlr_output *output) {
	if (!output || !output->renderer || !wlr_renderer_is_gles2(output->renderer)) return NULL;
	struct video_frost *f=calloc(1,sizeof(*f));
	if (!f) return NULL;
	f->output=output;f->layer=wlr_scene_buffer_create(tree,NULL);
	if (!f->layer) {free(f);return NULL;}
	wlr_scene_buffer_set_opacity(f->layer,.82f);
	wlr_scene_node_set_enabled(&f->layer->node,false);
	return f;
}

void video_frost_raise(struct video_frost *f) {
	if (f) wlr_scene_node_raise_to_top(&f->layer->node);
}

void video_frost_hide(struct video_frost *f) {
	if (!f || !f->visible) return;
	f->visible=false;
	wlr_scene_buffer_set_buffer(f->layer,NULL);
	wlr_scene_node_set_enabled(&f->layer->node,false);
}

static void release_targets(struct video_frost *f) {
	video_frost_hide(f);
	if (f->input) wlr_texture_destroy(f->input);
	if (f->scratch) wlr_buffer_drop(f->scratch);
	if (f->outputs) wlr_swapchain_destroy(f->outputs);
	f->input=NULL;f->scratch=NULL;f->outputs=NULL;
}

void video_frost_destroy(struct video_frost *f) {
	if (!f) return;
	release_targets(f);
	wlr_scene_node_destroy(&f->layer->node);
	free(f);
}

static bool prepare_targets(struct video_frost *f,int w,int h,const struct wlr_drm_format *native) {
	if (f->width==w && f->height==h && f->format==native->format)
		return f->outputs && f->input;
	release_targets(f);
	f->width=w;f->height=h;f->format=native->format;
	// Reuse the output's renderable modifiers: LINEAR may be import-only.
	f->scratch=wlr_allocator_create_buffer(f->output->allocator,w,h+16,native);
	if (f->scratch) f->input=wlr_texture_from_buffer(f->output->renderer,f->scratch);
	if (!f->input) return false;
	struct wlr_drm_format alpha=*native;
	alpha.format=DRM_FORMAT_ARGB8888;
	f->outputs=wlr_swapchain_create(f->output->allocator,w,h,&alpha);
	return f->outputs!=NULL;
}

// Average five taps on each axis. For an opaque source, blend tap i by
// 1/(i+1) over the previous running mean. No readback or source re-upload.
static bool blur_pass(struct wlr_renderer *r,struct wlr_buffer *out,struct wlr_texture *source,
		struct wlr_fbox visible,struct wlr_fbox roi,double sx,double sy,pixman_region32_t *clip) {
	struct wlr_fbox samples[5];
	for (int i=0;i<5;i++) {
		struct wlr_fbox box=roi;
		box.x+=(i-2)*sx;box.y+=(i-2)*sy;
		if (!video_frost_sample(&samples[i],box,visible,source->width,source->height)) return false;
	}
	struct wlr_render_pass *pass=wlr_renderer_begin_buffer_pass(r,out,NULL);
	if (!pass) return false;
	if (clip) wlr_render_pass_add_rect(pass,&(struct wlr_render_rect_options){
		.box={0,0,out->width,out->height},.color={0,0,0,0},
		.blend_mode=WLR_RENDER_BLEND_MODE_NONE,
	});
	for (int i=0;i<5;i++) {
		const float alpha=1.f/(i+1);
		wlr_render_pass_add_texture(pass,&(struct wlr_render_texture_options){
			.texture=source,.src_box=samples[i],.dst_box={0,0,out->width,out->height},
			.alpha=&alpha,.clip=clip,
			.blend_mode=i?WLR_RENDER_BLEND_MODE_PREMULTIPLIED:WLR_RENDER_BLEND_MODE_NONE,
		});
	}
	return wlr_render_pass_submit(pass);
}

static void frame_clip(pixman_region32_t *clip,int w,int h,double scale) {
	pixman_region32_init(clip);
	int cut=(int)ceil(MAP_FRAME_CUT*scale);
	for (int y=0;y<cut;y++) {
		int inset=cut-y;
		pixman_region32_union_rect(clip,clip,inset,y,w-2*inset,1);
		pixman_region32_union_rect(clip,clip,inset,h-1-y,w-2*inset,1);
	}
	pixman_region32_union_rect(clip,clip,0,cut,w,h-2*cut);
}

enum video_frost_result video_frost_update(struct video_frost *f,struct wlr_surface *wallpaper,int w,int h,int margin) {
	if (!f) return FROST_FALLBACK;
	if (!wallpaper || !wallpaper->mapped || !f->output->swapchain || !map_frame_available(w,h) ||
		surface_has_explicit_sync(wallpaper) ||
		wallpaper->current.transform!=WL_OUTPUT_TRANSFORM_NORMAL) {video_frost_hide(f);return FROST_FALLBACK;}
	if (f->wallpaper==wallpaper && f->view_w==w && f->view_h==h && f->margin==margin &&
		(f->failed || (f->visible && f->seq==wallpaper->current.seq)))
		return f->failed?FROST_FALLBACK:FROST_HIT;
	f->wallpaper=wallpaper;f->view_w=w;f->view_h=h;f->margin=margin;
	struct wlr_texture *src=wlr_surface_get_texture(wallpaper);
	if (!src) {video_frost_hide(f);return FROST_FALLBACK;}
	struct map_frame_box view=map_frame_layout(w,h,margin);
	double scale=fmin(960./w,540./h);
	int ow=(int)(view.width*scale),oh=(int)(view.height*scale);
	int cut=(int)ceil(MAP_FRAME_CUT*scale);
	if (ow<=2*cut || oh<=2*cut) {video_frost_hide(f);return FROST_FALLBACK;}
	if (!prepare_targets(f,ow,oh,&f->output->swapchain->format)) goto fail;

	struct wlr_buffer *out=wlr_swapchain_acquire(f->outputs);
	if (!out) {video_frost_hide(f);return FROST_FALLBACK;}
	struct wlr_fbox full;wlr_surface_get_buffer_source_box(wallpaper,&full);
	double px=full.width/w,py=full.height/h,pad=8*view.height/oh;
	struct wlr_fbox region={full.x+view.x*px,full.y+(view.y-pad)*py,
		view.width*px,(view.height+2*pad)*py};
	pixman_region32_t clip;frame_clip(&clip,ow,oh,scale);
	bool ok=blur_pass(f->output->renderer,f->scratch,src,full,region,view.width/ow*px*2,0,NULL) &&
		blur_pass(f->output->renderer,out,f->input,(struct wlr_fbox){0,0,ow,oh+16},
			(struct wlr_fbox){0,8,ow,oh},0,2,&clip);
	pixman_region32_fini(&clip);
	if (ok) {
		if (!f->visible) wlr_log(WLR_INFO,"Mothership compositor video frost: %dx%d",ow,oh);
		wlr_scene_node_set_position(&f->layer->node,(int)view.x,(int)view.y);
		wlr_scene_buffer_set_dest_size(f->layer,(int)view.width,(int)view.height);
		wlr_scene_buffer_set_buffer(f->layer,out);
		wlr_scene_node_set_enabled(&f->layer->node,true);
		f->seq=wallpaper->current.seq;f->visible=true;f->failed=false;
	}
	wlr_buffer_unlock(out);
	if (ok) return FROST_REBUILT;
fail:
	release_targets(f);
	f->failed=true;
	wlr_log(WLR_ERROR,"Mothership video frost target unavailable; using clear glass");
	return FROST_FALLBACK;
}
