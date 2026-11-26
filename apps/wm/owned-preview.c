//go:build linux && cgo && wlroots

#include "owned-preview.h"
#include <math.h>
#include <wlr/render/drm_syncobj.h>
#include <wlr/render/pass.h>
#include <wlr/render/swapchain.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_linux_drm_syncobj_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

void owned_preview_clear(struct owned_preview *p) {
	if (p->buffer) wlr_buffer_unlock(p->buffer);
	if (p->swapchain) wlr_swapchain_destroy(p->swapchain);
	wlr_drm_syncobj_timeline_unref(p->timeline);
	*p=(struct owned_preview){0};
}

struct wlr_buffer *owned_preview_update(struct owned_preview *p, struct wlr_output *out,
		struct wlr_surface *surface,uint64_t generation,struct wl_event_loop *loop) {
	if (p->broken) return NULL;
	if (p->buffer && p->generation==generation) return p->buffer;
	if (p->attempted && p->attempted_generation==generation) return NULL;
	p->attempted=true;p->attempted_generation=generation;
	struct wlr_linux_drm_syncobj_surface_v1_state *sync=
		wlr_linux_drm_syncobj_v1_get_surface_state(surface);
	// In pinned wlroots 0.20.2 add_release_point returns true for a null
	// merger. Refuse that no-op; in particular, do not read after an OOM
	// prevented wlroots from constructing the release merger.
	if (!sync || !sync->acquire_timeline || !sync->WLR_PRIVATE.release_merger ||
			!out->allocator || !out->swapchain ||
			!out->renderer->features.timeline || !surface->buffer ||
			surface->current.width<1 || surface->current.height<1) return NULL;
	struct wlr_texture *texture=wlr_surface_get_texture(surface);
	if (!texture) return NULL;
	const struct wlr_drm_format *format=&out->swapchain->format;
	double s=fmin(1.,fmin(640./surface->current.width,400./surface->current.height));
	int w=(int)fmax(1.,round(surface->current.width*s));
	int h=(int)fmax(1.,round(surface->current.height*s));
	if (!p->timeline) {
		int fd=wlr_renderer_get_drm_fd(out->renderer);
		if (fd<0 || !(p->timeline=wlr_drm_syncobj_timeline_create(fd))) return NULL;
	}
	if (!p->swapchain || p->width!=w || p->height!=h || p->format!=format->format) {
		struct wlr_swapchain *chain=wlr_swapchain_create(out->allocator,w,h,format);
		if (!chain) return NULL;
		if (p->swapchain) wlr_swapchain_destroy(p->swapchain);
		p->swapchain=chain;p->width=w;p->height=h;p->format=format->format;
	}
	struct wlr_buffer *target=wlr_swapchain_acquire(p->swapchain);
	if (!target) return NULL;
	uint64_t point=++p->next_point;
	struct wlr_render_pass *pass=wlr_renderer_begin_buffer_pass(out->renderer,target,
		&(struct wlr_buffer_pass_options){.signal_timeline=p->timeline,.signal_point=point});
	if (!pass) {wlr_buffer_unlock(target);return NULL;}
	// Register completion before enqueueing a read of producer-owned memory.
	bool attached=wlr_linux_drm_syncobj_v1_state_add_release_point(sync,p->timeline,point,loop);
	if (attached) {
		struct wlr_fbox source;
		wlr_surface_get_buffer_source_box(surface,&source);
		// A backend can reject a draw (its API is void). Never present
		// old pixels from a previously leased swapchain target.
		wlr_render_pass_add_rect(pass,&(struct wlr_render_rect_options){
			.box={0,0,w,h},.color={.04,.06,.08,1},.blend_mode=WLR_RENDER_BLEND_MODE_NONE,
		});
		wlr_render_pass_add_texture(pass,&(struct wlr_render_texture_options){
			.texture=texture,.src_box=source,.dst_box={0,0,w,h},
			.transform=surface->current.transform,.filter_mode=WLR_SCALE_FILTER_BILINEAR,
			.blend_mode=WLR_RENDER_BLEND_MODE_NONE,
			.wait_timeline=sync->acquire_timeline,.wait_point=sync->acquire_point,
		});
	}
	bool submitted=wlr_render_pass_submit(pass);
	if (!submitted) {
		// A failed fence export/import may follow a submitted GPU read. Do
		// not signal that point or advance the timeline through a later copy.
		p->broken=true;
		wlr_log(WLR_ERROR,"Preview GPU fence submission failed; disabled for this window");
	}
	if (!attached || !submitted) {wlr_buffer_unlock(target);return NULL;}
	if (p->buffer) wlr_buffer_unlock(p->buffer);
	p->buffer=target;p->generation=generation;p->point=point;
	return target;
}

void owned_preview_bind(struct owned_preview *p, struct wlr_scene_buffer *image) {
	if (image->buffer==p->buffer) return;
	wlr_scene_buffer_set_buffer_with_options(image,p->buffer,
		&(struct wlr_scene_buffer_set_buffer_options){.wait_timeline=p->timeline,.wait_point=p->point});
}
