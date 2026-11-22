//go:build linux && cgo && wlroots

#include "nested-seat.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/types/wlr_seat.h>
#include <xkbcommon/xkbcommon.h>

struct nested_seat {
	struct wlr_seat *child;
	struct wlr_keyboard keyboard;
	struct wl_registry *registry;
	struct wl_seat *parent;
	struct wl_pointer *pointer;
	struct wl_keyboard *keys;
	struct wl_surface *output;
	struct wlr_surface *selected;
	nested_hit_test hit_test;
	void *data;
	double x, y;
	uint32_t parent_name;
	bool pointer_inside, keyboard_inside, has_keymap;
};

static const struct wlr_keyboard_impl keyboard_impl = { .name = "nested-wayland" };

static void keyboard_focus(struct nested_seat *seat) {
	struct wlr_keyboard *kb = &seat->keyboard;
	if (seat->keyboard_inside && seat->has_keymap && seat->selected != NULL) {
		wlr_seat_keyboard_notify_enter(seat->child, seat->selected,
			kb->keycodes, kb->num_keycodes, &kb->modifiers);
	} else {
		wlr_seat_keyboard_notify_clear_focus(seat->child);
	}
}

static struct nested_hit pointer_focus(struct nested_seat *seat) {
	struct nested_hit hit = {0};
	if (seat->pointer_inside) {
		hit = seat->hit_test(seat->data, seat->x, seat->y);
	}
	if (hit.surface != NULL) {
		wlr_seat_pointer_notify_enter(seat->child, hit.surface, hit.x, hit.y);
	} else {
		wlr_seat_pointer_notify_clear_focus(seat->child);
	}
	return hit;
}

void nested_seat_refresh(struct nested_seat *seat) {
	if (seat != NULL) {
		pointer_focus(seat);
	}
}

void nested_seat_forget(struct nested_seat *seat, struct wlr_surface *gone,
		struct wlr_surface *fallback) {
	if (seat == NULL) {
		return;
	}
	if (seat->selected == gone) {
		seat->selected = fallback;
		keyboard_focus(seat);
	}
	pointer_focus(seat);
}

static void pointer_enter(void *data, struct wl_pointer *pointer, uint32_t serial,
		struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y) {
	struct nested_seat *seat = data;
	seat->pointer_inside = surface == seat->output;
	seat->x = wl_fixed_to_double(x);
	seat->y = wl_fixed_to_double(y);
	pointer_focus(seat);
}

static void pointer_leave(void *data, struct wl_pointer *pointer, uint32_t serial,
		struct wl_surface *surface) {
	struct nested_seat *seat = data;
	if (surface == seat->output) {
		seat->pointer_inside = false;
		pointer_focus(seat);
	}
}

static void pointer_motion(void *data, struct wl_pointer *pointer, uint32_t time,
		wl_fixed_t x, wl_fixed_t y) {
	struct nested_seat *seat = data;
	if (!seat->pointer_inside) {
		return;
	}
	seat->x = wl_fixed_to_double(x);
	seat->y = wl_fixed_to_double(y);
	struct nested_hit hit = pointer_focus(seat);
	if (hit.surface != NULL) {
		wlr_seat_pointer_notify_motion(seat->child, time, hit.x, hit.y);
	}
}

static void pointer_button(void *data, struct wl_pointer *pointer, uint32_t serial,
		uint32_t time, uint32_t button, uint32_t state) {
	struct nested_seat *seat = data;
	if (!seat->pointer_inside) {
		return;
	}
	struct nested_hit hit = pointer_focus(seat);
	/* Empty space has no input target. Cross-window drags are out of scope. */
	if (hit.surface == NULL) {
		return;
	}
	if (state == WL_POINTER_BUTTON_STATE_PRESSED && hit.toplevel != NULL) {
		seat->selected = hit.toplevel;
		keyboard_focus(seat);
	}
	wlr_seat_pointer_notify_button(seat->child, time, button, state);
}

static void pointer_axis(void *data, struct wl_pointer *pointer, uint32_t time,
		uint32_t axis, wl_fixed_t value) {
	/* Scrolling and gesture behavior are outside the focus slice. */
}

static void pointer_frame(void *data, struct wl_pointer *pointer) {
	struct nested_seat *seat = data;
	wlr_seat_pointer_notify_frame(seat->child);
}

static void pointer_axis_source(void *data, struct wl_pointer *p, uint32_t source) {}
static void pointer_axis_stop(void *data, struct wl_pointer *p, uint32_t time, uint32_t axis) {}
static void pointer_axis_discrete(void *data, struct wl_pointer *p, uint32_t axis, int32_t discrete) {}

static const struct wl_pointer_listener pointer_listener = {
	.enter = pointer_enter, .leave = pointer_leave, .motion = pointer_motion,
	.button = pointer_button, .axis = pointer_axis, .frame = pointer_frame,
	.axis_source = pointer_axis_source, .axis_stop = pointer_axis_stop,
	.axis_discrete = pointer_axis_discrete,
};

static void capabilities(struct nested_seat *seat) {
	uint32_t caps = seat->pointer != NULL ? WL_SEAT_CAPABILITY_POINTER : 0;
	if (seat->keys != NULL && seat->has_keymap) {
		caps |= WL_SEAT_CAPABILITY_KEYBOARD;
	}
	wlr_seat_set_capabilities(seat->child, caps);
}

static void keyboard_keymap(void *data, struct wl_keyboard *keys, uint32_t format,
		int fd, uint32_t size) {
	struct nested_seat *seat = data;
	if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 || size < 2) {
		close(fd);
		return;
	}
	const char *map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (map == MAP_FAILED) {
		return;
	}
	struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	/* The Wayland fd provides an explicit byte length. Ignore a trailing NUL
	 * when present; neither form requires reading beyond the mapping. */
	size_t length = size - (map[size - 1] == '\0');
	struct xkb_keymap *keymap = context == NULL ? NULL :
		xkb_keymap_new_from_buffer(context, map, length,
			XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
	munmap((void *)map, size);
	if (keymap != NULL) {
		if (wlr_keyboard_set_keymap(&seat->keyboard, keymap)) {
			seat->has_keymap = true;
			wlr_seat_set_keyboard(seat->child, &seat->keyboard);
			capabilities(seat);
			keyboard_focus(seat);
		}
		xkb_keymap_unref(keymap);
	}
	if (context != NULL) {
		xkb_context_unref(context);
	}
}

static void keyboard_enter(void *data, struct wl_keyboard *keys, uint32_t serial,
		struct wl_surface *surface, struct wl_array *held) {
	struct nested_seat *seat = data;
	seat->keyboard_inside = surface == seat->output;
	seat->keyboard.num_keycodes = 0;
	if (seat->keyboard_inside) {
		uint32_t *key;
		wl_array_for_each(key, held) {
			struct wlr_keyboard_key_event e = {
				.keycode = *key, .state = WL_KEYBOARD_KEY_STATE_PRESSED,
			};
			wlr_keyboard_notify_key(&seat->keyboard, &e);
		}
	}
	keyboard_focus(seat);
}

static void keyboard_leave(void *data, struct wl_keyboard *keys, uint32_t serial,
		struct wl_surface *surface) {
	struct nested_seat *seat = data;
	if (surface == seat->output) {
		seat->keyboard_inside = false;
		seat->keyboard.num_keycodes = 0;
		keyboard_focus(seat);
	}
}

static void keyboard_key(void *data, struct wl_keyboard *keys, uint32_t serial,
		uint32_t time, uint32_t key, uint32_t state) {
	struct nested_seat *seat = data;
	if (!seat->keyboard_inside || !seat->has_keymap) {
		return;
	}
	struct wlr_keyboard_key_event e = { .time_msec = time, .keycode = key, .state = state };
	wlr_keyboard_notify_key(&seat->keyboard, &e);
	wlr_seat_keyboard_notify_key(seat->child, time, key, state);
}

static void keyboard_modifiers(void *data, struct wl_keyboard *keys, uint32_t serial,
		uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group) {
	struct nested_seat *seat = data;
	if (seat->has_keymap) {
		wlr_keyboard_notify_modifiers(&seat->keyboard, depressed, latched, locked, group);
		if (seat->keyboard_inside) {
			wlr_seat_keyboard_notify_modifiers(seat->child, &seat->keyboard.modifiers);
		}
	}
}

static void keyboard_repeat(void *data, struct wl_keyboard *keys, int32_t rate, int32_t delay) {
	struct nested_seat *seat = data;
	wlr_keyboard_set_repeat_info(&seat->keyboard, rate, delay);
}

static const struct wl_keyboard_listener keyboard_listener = {
	.keymap = keyboard_keymap, .enter = keyboard_enter, .leave = keyboard_leave,
	.key = keyboard_key, .modifiers = keyboard_modifiers, .repeat_info = keyboard_repeat,
};

static void drop_pointer(struct nested_seat *seat) {
	if (seat->pointer != NULL) {
		if (wl_pointer_get_version(seat->pointer) >= WL_POINTER_RELEASE_SINCE_VERSION) {
			wl_pointer_release(seat->pointer);
		} else {
			wl_pointer_destroy(seat->pointer);
		}
		seat->pointer = NULL;
	}
	seat->pointer_inside = false;
	wlr_seat_pointer_notify_clear_focus(seat->child);
}

static void drop_keyboard(struct nested_seat *seat) {
	if (seat->keys != NULL) {
		if (wl_keyboard_get_version(seat->keys) >= WL_KEYBOARD_RELEASE_SINCE_VERSION) {
			wl_keyboard_release(seat->keys);
		} else {
			wl_keyboard_destroy(seat->keys);
		}
		seat->keys = NULL;
	}
	seat->keyboard_inside = false;
	seat->has_keymap = false;
	seat->keyboard.num_keycodes = 0;
	wlr_seat_keyboard_notify_clear_focus(seat->child);
	wlr_seat_set_keyboard(seat->child, NULL);
}

static void seat_caps(void *data, struct wl_seat *parent, uint32_t caps) {
	struct nested_seat *seat = data;
	if (!(caps & WL_SEAT_CAPABILITY_POINTER)) {
		drop_pointer(seat);
	} else if (seat->pointer == NULL) {
		seat->pointer = wl_seat_get_pointer(parent);
		if (seat->pointer != NULL) {
			wl_pointer_add_listener(seat->pointer, &pointer_listener, seat);
		}
	}
	if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD)) {
		drop_keyboard(seat);
	} else if (seat->keys == NULL) {
		seat->keys = wl_seat_get_keyboard(parent);
		if (seat->keys != NULL) {
			wl_keyboard_add_listener(seat->keys, &keyboard_listener, seat);
		}
	}
	capabilities(seat);
}
static void seat_name(void *data, struct wl_seat *seat, const char *name) {}
static const struct wl_seat_listener parent_listener = { .capabilities = seat_caps, .name = seat_name };

static void parent_global(void *data, struct wl_registry *registry, uint32_t name,
		const char *interface, uint32_t version) {
	struct nested_seat *seat = data;
	if (seat->parent == NULL && strcmp(interface, wl_seat_interface.name) == 0) {
		seat->parent = wl_registry_bind(registry, name, &wl_seat_interface, version < 5 ? version : 5);
		if (seat->parent != NULL) {
			seat->parent_name = name;
			wl_seat_add_listener(seat->parent, &parent_listener, seat);
		}
	}
}

static void drop_parent(struct nested_seat *seat) {
	drop_pointer(seat);
	drop_keyboard(seat);
	if (seat->parent != NULL) {
		if (wl_seat_get_version(seat->parent) >= WL_SEAT_RELEASE_SINCE_VERSION) {
			wl_seat_release(seat->parent);
		} else {
			wl_seat_destroy(seat->parent);
		}
		seat->parent = NULL;
	}
	capabilities(seat);
}

static void parent_removed(void *data, struct wl_registry *r, uint32_t name) {
	struct nested_seat *seat = data;
	if (seat->parent != NULL && name == seat->parent_name) {
		drop_parent(seat);
	}
}
static const struct wl_registry_listener registry_listener = {
	.global = parent_global, .global_remove = parent_removed,
};

struct nested_seat *nested_seat_create(struct wl_display *server,
		struct wl_display *parent, struct wl_surface *output,
		nested_hit_test hit_test, void *data) {
	struct nested_seat *seat = calloc(1, sizeof(*seat));
	if (seat == NULL) {
		return NULL;
	}
	seat->child = wlr_seat_create(server, "mothership");
	if (seat->child == NULL) {
		free(seat);
		return NULL;
	}
	wlr_keyboard_init(&seat->keyboard, &keyboard_impl, "nested-wayland");
	seat->output = output;
	seat->hit_test = hit_test;
	seat->data = data;
	seat->registry = wl_display_get_registry(parent);
	if (seat->registry == NULL) {
		nested_seat_destroy(seat);
		return NULL;
	}
	wl_registry_add_listener(seat->registry, &registry_listener, seat);
	return seat;
}

void nested_seat_destroy(struct nested_seat *seat) {
	if (seat == NULL) {
		return;
	}
	drop_parent(seat);
	if (seat->registry != NULL) {
		wl_registry_destroy(seat->registry);
	}
	wlr_keyboard_finish(&seat->keyboard);
	wlr_seat_destroy(seat->child);
	free(seat);
}
