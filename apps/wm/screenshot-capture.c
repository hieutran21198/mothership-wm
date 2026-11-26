//go:build linux && cgo && wlroots

#define _GNU_SOURCE
#include "screenshot-capture.h"

#include <drm.h>
#include <drm_fourcc.h>
#include <fcntl.h>
#include <math.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wlr/render/allocator.h>
#include <wlr/render/color.h>
#include <wlr/render/drm_syncobj.h>
#include <wlr/render/pass.h>
#include <wlr/render/swapchain.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_linux_drm_syncobj_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/transform.h>

static bool valid_size(int width, int height) {
	return width > 0 && height > 0 && width <= 16384 && height <= 16384 &&
		(size_t)width * height <= 64 * 1024 * 1024;
}

static bool fence_ready(struct wlr_drm_syncobj_timeline *timeline, uint64_t point) {
	bool ready = false;
	return !timeline || (wlr_drm_syncobj_timeline_check(timeline, point,
		DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT, &ready) && ready);
}

bool screenshot_buffer_ready(struct wlr_scene_node *node) {
	if (!node->enabled) return true;
	if (node->type == WLR_SCENE_NODE_TREE) {
		struct wlr_scene_node *child;
		wl_list_for_each(child, &wlr_scene_tree_from_node(node)->children, link) {
			if (!screenshot_buffer_ready(child)) return false;
		}
	}
	if (node->type == WLR_SCENE_NODE_BUFFER) {
		struct wlr_scene_buffer *buffer = wlr_scene_buffer_from_node(node);
		return fence_ready(buffer->WLR_PRIVATE.wait_timeline, buffer->WLR_PRIVATE.wait_point);
	}
	return true;
}

static bool scene_node_bounds(struct wlr_scene_node *node, int x, int y, struct wlr_box *box) {
	int w, h;
	if (node->type == WLR_SCENE_NODE_RECT) {
		struct wlr_scene_rect *rect = wlr_scene_rect_from_node(node);
		if (rect->color[3] <= 0) return false;
		w = rect->width;
		h = rect->height;
	} else {
		struct wlr_scene_buffer *buffer = wlr_scene_buffer_from_node(node);
		if ((!buffer->buffer && !buffer->WLR_PRIVATE.texture) || buffer->opacity <= 0) return false;
		w = buffer->dst_width;
		h = buffer->dst_height;
		if (w <= 0 || h <= 0) {
			w = buffer->WLR_PRIVATE.buffer_width;
			h = buffer->WLR_PRIVATE.buffer_height;
			if (buffer->transform & 1) { int swap = w; w = h; h = swap; }
		}
	}
	*box = (struct wlr_box){x, y, w, h};
	return w > 0 && h > 0;
}

static void collect_bounds(struct wlr_scene_node *node, int x, int y, struct wlr_box *bounds) {
	if (!node->enabled) return;
	if (node->type == WLR_SCENE_NODE_TREE) {
		struct wlr_scene_node *child;
		wl_list_for_each(child, &wlr_scene_tree_from_node(node)->children, link) {
			collect_bounds(child, x + child->x, y + child->y, bounds);
		}
		return;
	}
	struct wlr_box box;
	if (!scene_node_bounds(node, x, y, &box)) return;
	if (wlr_box_empty(bounds)) {
		*bounds = box;
		return;
	}
	int left = bounds->x < box.x ? bounds->x : box.x;
	int top = bounds->y < box.y ? bounds->y : box.y;
	int right = bounds->x + bounds->width;
	int bottom = bounds->y + bounds->height;
	if (box.x + box.width > right) right = box.x + box.width;
	if (box.y + box.height > bottom) bottom = box.y + box.height;
	*bounds = (struct wlr_box){left, top, right - left, bottom - top};
}

bool screenshot_window_bounds(struct wlr_scene_node *node, struct wlr_box *bounds) {
	int x, y;
	*bounds = (struct wlr_box){0};
	if (!wlr_scene_node_coords(node, &x, &y)) return false;
	collect_bounds(node, x, y, bounds);
	return !wlr_box_empty(bounds);
}

bool screenshot_read_buffer(struct wlr_renderer *renderer, struct wlr_buffer *buffer,
		struct screenshot_pixels *pixels) {
	if (!valid_size(buffer->width, buffer->height)) return false;
	struct wlr_texture *texture = wlr_texture_from_buffer(renderer, buffer);
	if (!texture) return false;
	int stride = buffer->width * 4;
	size_t size = (size_t)stride * buffer->height;
	int fd = memfd_create("mothership-screenshot", MFD_CLOEXEC | MFD_ALLOW_SEALING);
	void *bytes = MAP_FAILED;
	if (fd >= 0 && ftruncate(fd, (off_t)size) == 0)
		bytes = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	bool ok = false;
	if (bytes != MAP_FAILED) {
		ok = wlr_texture_read_pixels(texture, &(struct wlr_texture_read_pixels_options){
			.data = bytes, .format = DRM_FORMAT_ABGR8888, .stride = stride,
		});
		if (ok) {
			// Output formats with unused alpha and the opaque capture target
			// both travel through the same tight RGBA8 contract.
			unsigned char *rgba = bytes;
			for (size_t i = 3; i < size; i += 4) rgba[i] = 255;
		}
		munmap(bytes, size);
	}
	wlr_texture_destroy(texture);
	if (!ok || fcntl(fd, F_ADD_SEALS, F_SEAL_SEAL | F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE) < 0) {
		if (fd >= 0) close(fd);
		return false;
	}
	*pixels = (struct screenshot_pixels){fd, buffer->width, buffer->height, stride};
	return true;
}

struct window_render {
	struct wlr_renderer *renderer;
	struct wlr_render_pass *pass;
	struct wl_event_loop *loop;
	struct wlr_drm_syncobj_timeline *release;
	const struct wlr_box *bounds;
	float scale;
	bool registered_release;
	struct wl_array owned_textures;
};

static bool render_node(struct window_render *render, struct wlr_scene_node *node, int x, int y) {
	if (!node->enabled) return true;
	if (node->type == WLR_SCENE_NODE_TREE) {
		struct wlr_scene_node *child;
		wl_list_for_each(child, &wlr_scene_tree_from_node(node)->children, link) {
			if (!render_node(render, child, x + child->x, y + child->y)) return false;
		}
		return true;
	}
	struct wlr_box box;
	if (!scene_node_bounds(node, x, y, &box)) return true;
	const float scale = render->scale;
	int left = lroundf((box.x - render->bounds->x) * scale);
	int top = lroundf((box.y - render->bounds->y) * scale);
	struct wlr_box dst = {
		left, top, lroundf((box.x + box.width - render->bounds->x) * scale) - left,
		lroundf((box.y + box.height - render->bounds->y) * scale) - top,
	};
	if (wlr_box_empty(&dst)) return true;
	if (node->type == WLR_SCENE_NODE_RECT) {
		const float *c = wlr_scene_rect_from_node(node)->color;
		wlr_render_pass_add_rect(render->pass, &(struct wlr_render_rect_options){
			.box = dst, .color = {c[0], c[1], c[2], c[3]},
		});
		return true;
	}
	struct wlr_scene_buffer *buffer = wlr_scene_buffer_from_node(node);
	if (buffer->WLR_PRIVATE.is_single_pixel_buffer) {
		const uint32_t *c = buffer->WLR_PRIVATE.single_pixel_buffer_color;
		double alpha = buffer->opacity / (double)UINT32_MAX;
		wlr_render_pass_add_rect(render->pass, &(struct wlr_render_rect_options){
			.box = dst, .color = {c[0] * alpha, c[1] * alpha, c[2] * alpha, c[3] * alpha},
		});
		return true;
	}

	struct wlr_scene_surface *scene_surface = wlr_scene_surface_try_from_buffer(buffer);
	struct wlr_texture *texture = buffer->WLR_PRIVATE.texture;
	if (scene_surface) {
		struct wlr_surface *surface = scene_surface->surface;
		struct wlr_linux_drm_syncobj_surface_v1_state *sync = wlr_linux_drm_syncobj_v1_get_surface_state(surface);
		if (sync && sync->acquire_timeline) {
			if (!render->release || !sync->WLR_PRIVATE.release_merger || !surface->buffer ||
					buffer->buffer != &surface->buffer->base ||
					!wlr_linux_drm_syncobj_v1_state_add_release_point(sync, render->release, 1, render->loop)) return false;
			render->registered_release = true;
		}
		if (!texture) texture = wlr_surface_get_texture(surface);
	} else if (buffer->buffer && wlr_client_buffer_get(buffer->buffer)) {
		return false;
	}
	if (!texture && buffer->buffer) {
		struct wlr_texture **owned = wl_array_add(&render->owned_textures, sizeof(*owned));
		if (!owned) return false;
		texture = *owned = wlr_texture_from_buffer(render->renderer, buffer->buffer);
	}
	if (!texture) return false;
	struct wlr_color_primaries primaries;
	if (buffer->primaries) wlr_color_primaries_from_named(&primaries, buffer->primaries);
	wlr_render_pass_add_texture(render->pass, &(struct wlr_render_texture_options){
		.texture = texture, .src_box = buffer->src_box, .dst_box = dst,
		.transform = wlr_output_transform_invert(buffer->transform), .alpha = &buffer->opacity,
		.filter_mode = buffer->filter_mode, .transfer_function = buffer->transfer_function,
		.primaries = buffer->primaries ? &primaries : NULL,
		.color_encoding = buffer->color_encoding, .color_range = buffer->color_range,
		.wait_timeline = buffer->WLR_PRIVATE.wait_timeline, .wait_point = buffer->WLR_PRIVATE.wait_point,
	});
	return true;
}

static void recover_release(struct window_render *render, struct wlr_buffer *target) {
	if (!render->registered_release) return;
	// Fence export failure can occur after the GPU read was queued. A
	// synchronous read from the destination proves that it completed
	// before we signal the already registered release point.
	unsigned char pixel[4];
	struct wlr_texture *texture = wlr_texture_from_buffer(render->renderer, target);
	if (texture && wlr_texture_read_pixels(texture, &(struct wlr_texture_read_pixels_options){
		.data = pixel, .format = DRM_FORMAT_ABGR8888, .stride = sizeof(pixel), .src_box = {0, 0, 1, 1},
	})) wlr_drm_syncobj_timeline_signal(render->release, 1);
	if (texture) wlr_texture_destroy(texture);
}

bool screenshot_render_window(struct wlr_output *output, struct wlr_scene_node *node,
		const struct wlr_box *bounds, struct wl_event_loop *loop, struct screenshot_pixels *pixels) {
	long width = lroundf(bounds->width * output->scale), height = lroundf(bounds->height * output->scale);
	if (width > 16384 || height > 16384 || !valid_size(width, height) || !output->swapchain ||
			!screenshot_buffer_ready(node)) return false;
	int x, y;
	if (!wlr_scene_node_coords(node, &x, &y)) return false;
	struct wlr_buffer *target = wlr_allocator_create_buffer(output->allocator, width, height, &output->swapchain->format);
	if (!target) return false;
	struct window_render render = {
		.renderer = output->renderer, .loop = loop, .bounds = bounds, .scale = output->scale,
	};
	wl_array_init(&render.owned_textures);
	if (render.renderer->features.timeline) {
		int drm_fd = wlr_renderer_get_drm_fd(render.renderer);
		if (drm_fd >= 0) render.release = wlr_drm_syncobj_timeline_create(drm_fd);
	}
	render.pass = wlr_renderer_begin_buffer_pass(render.renderer, target,
		&(struct wlr_buffer_pass_options){.signal_timeline = render.release, .signal_point = 1});
	bool ok = false;
	if (render.pass) {
		wlr_render_pass_add_rect(render.pass, &(struct wlr_render_rect_options){
			.box = {0, 0, width, height}, .color = {.07f, .10f, .14f, 1},
			.blend_mode = WLR_RENDER_BLEND_MODE_NONE,
		});
		ok = render_node(&render, node, x, y);
		if (!wlr_render_pass_submit(render.pass)) {
			recover_release(&render, target);
			ok = false;
		}
	}
	if (ok) ok = screenshot_read_buffer(render.renderer, target, pixels);
	struct wlr_texture **texture;
	wl_array_for_each(texture, &render.owned_textures) if (*texture) wlr_texture_destroy(*texture);
	wl_array_release(&render.owned_textures);
	wlr_drm_syncobj_timeline_unref(render.release);
	wlr_buffer_drop(target);
	return ok;
}
