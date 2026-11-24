//go:build linux && cgo && wlroots

#include "nested-seat.h"

#include <stdint.h>
#include <math.h>
#include <linux/input-event-codes.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/types/wlr_seat.h>
#include <xkbcommon/xkbcommon.h>

struct pending_axis { bool pending; uint32_t time; double delta; int32_t discrete; };

struct nested_seat {
	struct wlr_seat *child;
	struct wlr_keyboard keyboard;
	struct wl_registry *registry;
	struct wl_seat *parent;
	struct wl_pointer *pointer;
	struct wl_keyboard *keys;
	struct wl_surface *output;
	struct wlr_surface *selected, *panel_focus, *blank_shell;
	nested_hit_test hit_test;
	nested_selected select_window;
	nested_cursor_move move_cursor;
	nested_move_start start_move;
	nested_move_update update_move;
	nested_map_scroll map_scroll;
	nested_map_click map_click;
	bool map_pressed;
	double map_pressed_x,map_pressed_y;
	struct pending_axis axes[2];
	enum wl_pointer_axis_source axis_source;
	bool moving;
	bool domain_mode;
	bool keys_held[KEY_MAX + 1], keys_blocked[KEY_MAX + 1];
	bool buttons_held[KEY_MAX + 1], buttons_blocked[KEY_MAX + 1];
	size_t blocked_count;
	void *data;
	double x, y;
	uint32_t parent_name;
	bool pointer_inside, keyboard_inside, has_keymap;
};

static const struct wlr_keyboard_impl keyboard_impl = { .name = "nested-wayland" };

static struct wlr_surface *keyboard_target(struct nested_seat *seat) {
	if (seat->panel_focus) return seat->panel_focus;
	return seat->selected ? seat->selected : seat->blank_shell;
}

static struct wlr_surface *map_panel(struct nested_seat *seat) {
	if (!seat->keyboard_inside) return NULL;
	return seat->panel_focus ? seat->panel_focus : (seat->selected ? NULL : seat->blank_shell);
}

static void keyboard_focus(struct nested_seat *seat) {
	struct wlr_keyboard *kb = &seat->keyboard;
	struct wlr_surface *target = keyboard_target(seat);
	if (seat->keyboard_inside && seat->has_keymap && target != NULL) {
		wlr_seat_keyboard_notify_enter(seat->child, target,
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

static void isolate_keys(struct nested_seat *seat) {
	memcpy(seat->keys_blocked, seat->keys_held, sizeof(seat->keys_held));
	seat->blocked_count = 0;
	for (size_t i = 0; i <= KEY_MAX; ++i) {
		seat->blocked_count += seat->keys_blocked[i];
	}
	wlr_seat_keyboard_end_grab(seat->child);
	wlr_seat_keyboard_clear_focus(seat->child);
	seat->keyboard.num_keycodes = 0;
	seat->keyboard.modifiers.depressed = 0;
	seat->keyboard.modifiers.latched = 0;
}

void nested_seat_blank_shell(struct nested_seat *seat,struct wlr_surface *panel) {
	if (!seat || seat->blank_shell==panel) return;
	struct wlr_surface *before=keyboard_target(seat);
	seat->blank_shell=panel;
	if (keyboard_target(seat)!=before) {
		isolate_keys(seat);
		keyboard_focus(seat);
	}
}

void nested_seat_panel(struct nested_seat *seat, struct wlr_surface *panel, bool acquire) {
	if (seat == NULL || panel == NULL) {
		return;
	}
	if (acquire) {
		// The already-focused blank shell may enter Map without a pointer.
		// Elsewhere only intentional pointer focus can select the shell.
		bool owns_blank=seat->blank_shell==panel && seat->selected==NULL;
		bool pointer_over=seat->pointer_inside &&
			seat->hit_test(seat->data,seat->x,seat->y).panel_keyboard==panel;
		if ((!owns_blank && !pointer_over) || seat->panel_focus==panel) return;
	} else if (seat->panel_focus != panel) {
		return;
	}
	struct wlr_surface *before=keyboard_target(seat);
	seat->map_pressed = false;
	seat->panel_focus = acquire ? panel : NULL;
	if (keyboard_target(seat)!=before) {
		isolate_keys(seat);
		keyboard_focus(seat);
	}
}

void nested_seat_switch(struct nested_seat *seat, struct wlr_surface *target, bool keep_panel) {
	if (seat == NULL) {
		return;
	}
	seat->domain_mode = true;
	if (seat->moving) {
		seat->update_move(seat->data, false, seat->x, seat->y);
		seat->moving = false;
	}
	// The explicit Map View action can carry the already-focused shell
	// across projection. No app focus is kept and no shell focus is granted.
	bool stays_blank=target==NULL && seat->blank_shell && keyboard_target(seat)==seat->blank_shell;
	if ((!keep_panel || !seat->panel_focus) && !stays_blank) isolate_keys(seat);
	memcpy(seat->buttons_blocked, seat->buttons_held, sizeof(seat->buttons_held));
	// A button released after switching cannot complete an old grab.
	wlr_seat_pointer_end_grab(seat->child);
	wlr_seat_pointer_clear_focus(seat->child);
	seat->child->pointer_state.button_count = 0;
	seat->child->pointer_state.grab_serial = 0;
	seat->map_pressed = false;
	if (!keep_panel) seat->panel_focus = NULL;
	seat->selected = target;
	keyboard_focus(seat);
	pointer_focus(seat);
}

void nested_seat_forget(struct nested_seat *seat, struct wlr_surface *gone,
		struct wlr_surface *fallback) {
	if (seat == NULL) {
		return;
	}
	if (seat->selected == gone) {
		if (seat->domain_mode) {
			nested_seat_switch(seat, fallback, false);
			return;
		}
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
	seat->move_cursor(seat->data, seat->pointer_inside, seat->x, seat->y);
	pointer_focus(seat);
}

static void pointer_leave(void *data, struct wl_pointer *pointer, uint32_t serial,
		struct wl_surface *surface) {
	struct nested_seat *seat = data;
	if (surface == seat->output) {
		seat->pointer_inside = false;
		seat->map_pressed = false;
		if (seat->moving) {
			seat->update_move(seat->data, false, seat->x, seat->y);
			seat->moving = false;
			seat->buttons_blocked[BTN_LEFT] = seat->buttons_held[BTN_LEFT];
		}
		seat->move_cursor(seat->data, false, seat->x, seat->y);
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
	seat->move_cursor(seat->data, true, seat->x, seat->y);
	if (seat->moving) {
		seat->update_move(seat->data, true, seat->x, seat->y);
		return;
	}
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
	if (button > KEY_MAX) {
		return;
	}
	bool was_held = seat->buttons_held[button];
	seat->buttons_held[button] = state == WL_POINTER_BUTTON_STATE_PRESSED;
	if (seat->buttons_blocked[button]) {
		seat->buttons_blocked[button] = seat->buttons_held[button];
		return;
	}
	if (seat->domain_mode && !was_held && !seat->buttons_held[button]) {
		return;
	}
	if (seat->moving) {
		if (button != BTN_LEFT) {
			seat->buttons_blocked[button] = seat->buttons_held[button];
		}
		if (button == BTN_LEFT && state == WL_POINTER_BUTTON_STATE_RELEASED) {
			seat->update_move(seat->data, false, seat->x, seat->y);
			seat->moving = false;
			pointer_focus(seat);
		}
		return;
	}
	struct nested_hit hit = pointer_focus(seat);
	struct wlr_surface *map=map_panel(seat);
	if (hit.handle != NULL && button == BTN_LEFT && state == WL_POINTER_BUTTON_STATE_PRESSED) {
		if (seat->panel_focus != NULL) {
			isolate_keys(seat);
			seat->panel_focus = NULL;
		}
		wlr_seat_pointer_end_grab(seat->child);
		wlr_seat_pointer_clear_focus(seat->child);
		seat->selected = hit.toplevel;
		seat->select_window(seat->data, hit.toplevel);
		keyboard_focus(seat);
		seat->start_move(seat->data, hit, seat->x, seat->y);
		seat->moving = true;
		return;
	}
	if (button == BTN_LEFT && seat->map_pressed && state == WL_POINTER_BUTTON_STATE_RELEASED) {
		seat->map_pressed = false;
		if (!hit.surface && map &&
				fabs(seat->x-seat->map_pressed_x)<6 && fabs(seat->y-seat->map_pressed_y)<6)
			seat->map_click(seat->data,map,seat->x,seat->y,true);
		return;
	}
	if (button == BTN_LEFT && state == WL_POINTER_BUTTON_STATE_PRESSED && !hit.surface &&
			map &&
			seat->map_click(seat->data,map,seat->x,seat->y,false)) {
		seat->map_pressed = true;
		seat->map_pressed_x = seat->x;
		seat->map_pressed_y = seat->y;
		return;
	}
	/* Other empty space has no input target. Cross-window drags are out of scope. */
	if (hit.surface == NULL) {
		return;
	}
	if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
		if (hit.toplevel != NULL) {
			if (seat->panel_focus != NULL) {
				isolate_keys(seat);
				seat->panel_focus = NULL;
			}
			seat->selected = hit.toplevel;
			seat->select_window(seat->data, hit.toplevel);
			keyboard_focus(seat);
		} else if (hit.panel_keyboard != NULL) {
			nested_seat_panel(seat, hit.panel_keyboard, true);
		}
	}
	wlr_seat_pointer_notify_button(seat->child, time, button, state);
}

static void flush_axis(struct nested_seat *seat,unsigned orientation) {
	struct pending_axis e=seat->axes[orientation];
	seat->axes[orientation]=(struct pending_axis){0};
	if (!e.pending || !seat->pointer_inside || seat->moving) return;
	struct wlr_surface *map=map_panel(seat);
	if (orientation==WL_POINTER_AXIS_VERTICAL_SCROLL && map &&
			seat->map_scroll(seat->data,map,seat->x,seat->y,e.delta)) return;
	if (pointer_focus(seat).surface)
		wlr_seat_pointer_notify_axis(seat->child,e.time,orientation,e.delta,e.discrete,
			seat->axis_source,WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL);
}

static void pointer_axis(void *data, struct wl_pointer *pointer,uint32_t time,uint32_t axis,wl_fixed_t value) {
	struct nested_seat *seat=data;
	if (axis>=2) return;
	seat->axes[axis].pending=true;
	seat->axes[axis].time=time;
	seat->axes[axis].delta+=wl_fixed_to_double(value);
	if (wl_pointer_get_version(pointer)<WL_POINTER_FRAME_SINCE_VERSION) {
		flush_axis(seat,axis);
		wlr_seat_pointer_notify_frame(seat->child);
	}
}

static void pointer_frame(void *data, struct wl_pointer *pointer) {
	(void)pointer;
	struct nested_seat *seat=data;
	flush_axis(seat,0);flush_axis(seat,1);
	seat->axis_source=WL_POINTER_AXIS_SOURCE_WHEEL;
	wlr_seat_pointer_notify_frame(seat->child);
}

static void pointer_axis_source(void *data,struct wl_pointer *p,uint32_t source) {
	(void)p;
	struct nested_seat *seat=data;
	seat->axis_source=source;
}
static void pointer_axis_stop(void *data,struct wl_pointer *p,uint32_t time,uint32_t axis) {
	struct nested_seat *seat=data;
	if (axis>=2) return;
	if (seat->axes[axis].pending) flush_axis(seat,axis);
	pointer_axis(data,p,time,axis,0);
}
static void pointer_axis_discrete(void *data,struct wl_pointer *p,uint32_t axis,int32_t discrete) {
	(void)p;
	struct nested_seat *seat=data;
	if (axis<2) seat->axes[axis].discrete=(int32_t)((int64_t)discrete*120);
}

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
	memset(seat->keys_held, 0, sizeof(seat->keys_held));
	memset(seat->keys_blocked, 0, sizeof(seat->keys_blocked));
	seat->blocked_count = 0;
	if (seat->keyboard_inside) {
		uint32_t *key;
		wl_array_for_each(key, held) {
			if (*key > KEY_MAX) {
				continue;
			}
			seat->keys_held[*key] = true;
			if (seat->domain_mode) {
				seat->keys_blocked[*key] = true;
				seat->blocked_count++;
				continue;
			}
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
		seat->map_pressed = false;
		seat->keyboard.num_keycodes = 0;
		keyboard_focus(seat);
	}
}

static void keyboard_key(void *data, struct wl_keyboard *keys, uint32_t serial,
		uint32_t time, uint32_t key, uint32_t state) {
	struct nested_seat *seat = data;
	if (!seat->keyboard_inside || !seat->has_keymap || key > KEY_MAX) {
		return;
	}
	bool was_held = seat->keys_held[key];
	seat->keys_held[key] = state == WL_KEYBOARD_KEY_STATE_PRESSED;
	if (seat->keys_blocked[key]) {
		if (!seat->keys_held[key]) {
			seat->keys_blocked[key] = false;
			seat->blocked_count--;
		}
		return;
	}
	if (seat->domain_mode && !was_held && !seat->keys_held[key]) {
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
		if (seat->blocked_count > 0) {
			depressed = latched = 0;
		}
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
	memset(seat->buttons_held, 0, sizeof(seat->buttons_held));
	memset(seat->buttons_blocked, 0, sizeof(seat->buttons_blocked));
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
	memset(seat->keys_held, 0, sizeof(seat->keys_held));
	memset(seat->keys_blocked, 0, sizeof(seat->keys_blocked));
	seat->blocked_count = 0;
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
		nested_hit_test hit_test, nested_selected selected, nested_cursor_move move_cursor,
		nested_move_start start_move, nested_move_update update_move,nested_map_scroll scroll,nested_map_click click,void *data) {
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
	seat->select_window = selected;
	seat->move_cursor = move_cursor;
	seat->start_move = start_move;
	seat->update_move = update_move;
	seat->map_scroll=scroll;
	seat->map_click=click;
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
