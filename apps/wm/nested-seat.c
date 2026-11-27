//go:build linux && cgo && wlroots

#include "nested-seat.h"
#include "render-debug.h"
#include "generated-map-pointer.h"

#include <stdint.h>
#include <math.h>
#include <linux/input-event-codes.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wlr/backend.h>
#include <wlr/backend/session.h>
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_seat.h>
#include <xkbcommon/xkbcommon.h>

struct local_input {
	struct nested_seat *seat;
	struct wl_list link;
	struct wl_listener destroy,key,motion,absolute,button,axis,frame;
};

struct pending_axis { bool pending; uint32_t time; double delta; int32_t discrete; };

struct nested_seat {
	struct wlr_seat *child;
	struct render_debug *render_debug;
	struct wl_listener render_focus_change;
	struct wl_listener selection_request;
	struct wlr_keyboard keyboard;
	struct wl_registry *registry;
	struct wl_seat *parent;
	struct wl_pointer *pointer;
	struct wl_keyboard *keys;
	struct wl_surface *output;
	struct wlr_surface *selected, *panel_focus, *blank_shell;
	struct wlr_surface *screenshot_panel;
	nested_hit_test hit_test;
	nested_selected select_window;
	nested_cursor_move move_cursor;
	nested_move_start start_move;
	nested_move_update update_move;
	nested_map_scroll map_scroll;
	nested_strip_scroll strip_scroll;
	nested_map_contact map_contact;
	nested_map_weapon map_weapon;
	uint32_t arcade_buttons;
	nested_chrome_action chrome;
	nested_flow_scroll flow_scroll;
	nested_flow_cycle cycle;
	void (*home_tap)(void *);
	struct wlr_surface *(*node_panel_key)(void *,uint32_t);
	bool (*node_number)(void *,uint32_t);
	nested_node_switch node_switch;
	bool (*close_window)(void *,struct wlr_surface *);
	bool (*reorder_window)(void *,struct wlr_surface *,uint32_t);
	bool (*move_window)(void *,struct wlr_surface *,uint32_t);
	bool (*render_capture)(void *);
	bool (*screenshot_open)(void *);
	void (*screenshot_abort)(void *);
	void *screenshot_data;
	uint32_t home_tap_key;
	uint32_t close_key,apps_key,settings_key,node_first_key;
	bool map_home;
	int cycle_steps;
	bool cycle_tab,cycle_active,cycle_visible,cycle_escape;
	bool node_switch_active, node_switch_tab, node_switch_escape;
	bool map_pressed, map_release_pending;
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
	bool local_attached;
	int local_width,local_height,local_pointers,local_keyboards;
	struct wlr_session *local_session;
	struct wl_listener local_device;
	struct wl_list local_inputs;
	struct xkb_state *local_xkb;
	void (*local_logout)(void *);

};

static const struct wlr_keyboard_impl keyboard_impl = { .name = "nested-wayland" };

static struct wlr_surface *keyboard_target(struct nested_seat *seat) {
	if (seat->screenshot_panel) return seat->screenshot_panel;
	if (seat->panel_focus) return seat->panel_focus;
	return seat->selected ? seat->selected : seat->blank_shell;
}

static struct wlr_surface *map_panel(struct nested_seat *seat) {
	if (!seat->keyboard_inside || seat->screenshot_panel) return NULL;
	return seat->panel_focus ? seat->panel_focus : (seat->selected ? NULL : seat->blank_shell);
}

struct wlr_surface *nested_seat_map_position(struct nested_seat *seat,double *x,double *y) {
	if(!seat || !seat->pointer_inside) return NULL;
	*x=seat->x;*y=seat->y;
	return map_panel(seat);
}

void nested_seat_map_home(struct nested_seat *seat,bool enabled) {
	if (!seat || seat->map_home==enabled) return;
	seat->map_home=enabled;
	seat->home_tap_key=0;
}

void nested_seat_cancel_map_pointer(struct nested_seat *seat) {
	if (!seat || !seat->map_pressed) return;
	seat->map_pressed = false;
	seat->map_release_pending = seat->buttons_held[BTN_LEFT];
	seat->map_contact(seat->data,NULL,seat->x,seat->y,
		MOTHERSHIP_MAP_POINTER_V1_CONTACT_PHASE_CANCEL);
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
		if (seat->screenshot_panel) {
			hit = (struct nested_hit){.surface=seat->screenshot_panel,.x=seat->x,.y=seat->y};
		} else {
			hit = seat->hit_test(seat->data, seat->x, seat->y);
		}
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

static void cancel_cycle(struct nested_seat *seat) {
	if(seat->cycle_active) seat->cycle(seat->data,CYCLE_CANCEL,0);
	seat->cycle_active=seat->cycle_visible=false;
	seat->cycle_steps=0;
}

static void finish_node_switch(struct nested_seat *seat, bool commit) {
	if (!seat || !seat->node_switch_active) return;
	seat->node_switch_active = false;
	seat->node_switch(seat->data, commit ? NODE_SWITCH_COMMIT : NODE_SWITCH_CANCEL);
}

void nested_seat_cancel_node_switch(struct nested_seat *seat) {
	finish_node_switch(seat, false);
}

static void isolate_keys(struct nested_seat *seat) {
	seat->home_tap_key=0;
	cancel_cycle(seat);
	nested_seat_cancel_node_switch(seat);
	seat->node_switch_tab = seat->node_switch_escape = false;
	seat->cycle_tab=seat->cycle_escape=false;
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

void nested_seat_screenshot_bind(struct nested_seat *seat,bool (*open)(void *),
		void (*abort)(void *),void *data) {
	if (!seat) return;
	seat->screenshot_open=open;
	seat->screenshot_abort=abort;
	seat->screenshot_data=data;
}

void nested_seat_screenshot_modal(struct nested_seat *seat,struct wlr_surface *panel) {
	if (!seat || seat->screenshot_panel==panel) return;
	struct wlr_surface *previous=keyboard_target(seat);
	seat->screenshot_panel=panel;
	if (seat->moving) {
		seat->update_move(seat->data,MOVE_CANCEL,seat->x,seat->y);
		seat->moving=false;
	}
	nested_seat_cancel_map_pointer(seat);
	memcpy(seat->buttons_blocked,seat->buttons_held,sizeof(seat->buttons_held));
	wlr_seat_pointer_end_grab(seat->child);
	wlr_seat_pointer_clear_focus(seat->child);
	seat->child->pointer_state.button_count=0;
	seat->child->pointer_state.grab_serial=0;
	seat->home_tap_key=0;
	if (keyboard_target(seat)!=previous) {
		isolate_keys(seat);
		keyboard_focus(seat);
	}
	pointer_focus(seat);
}

void nested_seat_blank_shell(struct nested_seat *seat,struct wlr_surface *panel) {
	if (!seat || seat->blank_shell==panel) return;
	struct wlr_surface *before=keyboard_target(seat);
	seat->blank_shell=panel;
	if (keyboard_target(seat)!=before) {
		nested_seat_cancel_map_pointer(seat);
		isolate_keys(seat);
		keyboard_focus(seat);
	}
}

void nested_seat_shortcut_panel(struct nested_seat *seat,struct wlr_surface *panel) {
	if(!seat || !panel || !seat->keyboard_inside) return;
	nested_seat_cancel_map_pointer(seat);
	seat->panel_focus=panel;
	isolate_keys(seat);
	keyboard_focus(seat);
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
	nested_seat_cancel_map_pointer(seat);
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
	nested_seat_cancel_node_switch(seat);
	seat->domain_mode = true;
	if (seat->moving) {
		seat->update_move(seat->data, MOVE_CANCEL, seat->x, seat->y);
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
	nested_seat_cancel_map_pointer(seat);
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
		nested_seat_cancel_map_pointer(seat);
		if (seat->moving) {
			seat->update_move(seat->data, MOVE_CANCEL, seat->x, seat->y);
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
		seat->update_move(seat->data, MOVE_UPDATE, seat->x, seat->y);
		return;
	}
	struct nested_hit hit = pointer_focus(seat);
	if (seat->map_pressed) {
		if (hit.surface || !seat->map_contact(seat->data,map_panel(seat),seat->x,seat->y,
				MOTHERSHIP_MAP_POINTER_V1_CONTACT_PHASE_MOTION))
			nested_seat_cancel_map_pointer(seat);
	}
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
	uint32_t weapon=button==BTN_LEFT?MOTHERSHIP_MAP_POINTER_V1_WEAPON_NORMAL:
		button==BTN_RIGHT?MOTHERSHIP_MAP_POINTER_V1_WEAPON_PLASMA:0;
	if(weapon && (seat->arcade_buttons&weapon)) {
		seat->arcade_buttons&=~weapon;
		seat->buttons_blocked[button]=false;
		if(!seat->buttons_held[button]) {
			// Consume the paired release even if the pointer left the Map,
			// the mode changed, or an app is now underneath.
			seat->map_weapon(seat->data,weapon,false);
			return;
		}
		// A fresh press means the old release happened off our surface.
		was_held=false;
	}
	if (button == BTN_LEFT && seat->map_release_pending) {
		seat->map_release_pending = false;
		seat->buttons_blocked[button] = false;
		if (!seat->buttons_held[button]) return;
		// A fresh press means the previous release happened outside our
		// surface. Never require an extra click to rearm navigation.
		was_held = false;
	}
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
			seat->moving = false;
			seat->update_move(seat->data, MOVE_DROP, seat->x, seat->y);
			pointer_focus(seat);
		}
		return;
	}
	struct nested_hit hit = pointer_focus(seat);
	struct wlr_surface *map=map_panel(seat);
	if(weapon && !was_held && seat->buttons_held[button] && !hit.surface &&
	   seat->map_weapon(seat->data,weapon,true)) {
		seat->arcade_buttons|=weapon;
		return;
	}
	if (hit.handle != NULL && button == BTN_LEFT && state == WL_POINTER_BUTTON_STATE_PRESSED) {
		if (seat->panel_focus != NULL) {
			isolate_keys(seat);
			seat->panel_focus = NULL;
		}
		wlr_seat_pointer_end_grab(seat->child);
		wlr_seat_pointer_clear_focus(seat->child);
		if(!hit.defer_move_focus) {
			seat->selected = hit.toplevel;
			seat->select_window(seat->data, hit.toplevel);
			keyboard_focus(seat);
		}
		seat->start_move(seat->data, hit, seat->x, seat->y);
		seat->moving = true;
		return;
	}
	if (button == BTN_LEFT && seat->map_pressed && state == WL_POINTER_BUTTON_STATE_RELEASED) {
		if (hit.surface || !seat->map_contact(seat->data,map,seat->x,seat->y,
				MOTHERSHIP_MAP_POINTER_V1_CONTACT_PHASE_END)) {
			nested_seat_cancel_map_pointer(seat);
		} else {
			seat->map_pressed = false;
			if (fabs(seat->x-seat->map_pressed_x)<6 && fabs(seat->y-seat->map_pressed_y)<6)
				seat->map_contact(seat->data,map,seat->x,seat->y,NESTED_MAP_LEGACY_CLICK);
		}
		return;
	}
	if (button == BTN_LEFT && !was_held && state == WL_POINTER_BUTTON_STATE_PRESSED && !hit.surface &&
			seat->map_contact(seat->data,map,seat->x,seat->y,
				MOTHERSHIP_MAP_POINTER_V1_CONTACT_PHASE_BEGIN)) {
		seat->map_pressed = true;
		seat->map_pressed_x = seat->x;
		seat->map_pressed_y = seat->y;
		return;
	}
	if (hit.decoration && button==BTN_LEFT && state==WL_POINTER_BUTTON_STATE_PRESSED) {
		seat->buttons_blocked[BTN_LEFT]=true;
		seat->chrome(seat->data,hit);
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
	// A moving window can change surface-local coordinates under a still pointer.
	if(hit.toplevel) wlr_seat_pointer_notify_motion(seat->child,time,hit.x,hit.y);
	wlr_seat_pointer_notify_button(seat->child, time, button, state);
}

static void flush_axis(struct nested_seat *seat,unsigned orientation) {
	struct pending_axis e=seat->axes[orientation];
	seat->axes[orientation]=(struct pending_axis){0};
	if (!e.pending || !seat->pointer_inside || seat->moving) return;
	struct wlr_surface *map=map_panel(seat);
	if (orientation==WL_POINTER_AXIS_VERTICAL_SCROLL && map &&
			seat->map_scroll(seat->data,map,seat->x,seat->y,e.delta)) return;
	struct nested_hit hit=pointer_focus(seat);
	if(hit.surface && !seat->screenshot_panel && seat->strip_scroll(seat->data,hit.surface,seat->x,seat->y,e.delta,
			seat->axis_source==WL_POINTER_AXIS_SOURCE_WHEEL ||
			seat->axis_source==WL_POINTER_AXIS_SOURCE_WHEEL_TILT)) return;
	if (!hit.surface && seat->flow_scroll(seat->data,seat->x,seat->y,orientation,e.delta)) return;
	if (hit.surface)
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
	nested_seat_cancel_node_switch(seat);
	seat->home_tap_key=0;
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
	nested_seat_cancel_node_switch(seat);
	seat->node_switch_tab = seat->node_switch_escape = false;
	seat->home_tap_key=0;
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
		nested_seat_cancel_node_switch(seat);
		if (seat->screenshot_abort) seat->screenshot_abort(seat->screenshot_data);
		seat->home_tap_key=0;
		cancel_cycle(seat);
		seat->cycle_tab=seat->cycle_escape=false;
		nested_seat_cancel_map_pointer(seat);
		seat->keyboard.num_keycodes = 0;
		keyboard_focus(seat);
	}
}

static bool valid_shortcut_key(uint64_t key) {
	return (key>=KEY_Q && key<=KEY_P) || (key>=KEY_A && key<=KEY_L) ||
		(key>=KEY_Z && key<=KEY_M) || key==KEY_COMMA;
}
void nested_seat_move_bind(struct nested_seat *seat,bool (*move)(void *,struct wlr_surface *,uint32_t)) {
	if(seat) seat->move_window=move;
}
bool nested_seat_set_shortcuts(struct nested_seat *seat,uint64_t close,uint64_t apps,uint64_t settings,uint64_t node) {
	if(!seat || !valid_shortcut_key(close) || !valid_shortcut_key(apps) || !valid_shortcut_key(settings) ||
		close==apps || close==settings || apps==settings || (node!=KEY_1 && node!=KEY_F1))return false;
	seat->close_key=close;seat->apps_key=apps;seat->settings_key=settings;seat->node_first_key=node;
	return true;
}

static bool screenshot_chord(struct nested_seat *seat,uint32_t key) {
	if (!seat->screenshot_open || seat->keyboard.modifiers.latched) return false;
	bool win_shift=key==KEY_S;
	if (win_shift) {
		if (!(seat->keys_held[KEY_LEFTMETA] || seat->keys_held[KEY_RIGHTMETA]) ||
				!(seat->keys_held[KEY_LEFTSHIFT] || seat->keys_held[KEY_RIGHTSHIFT])) return false;
	} else if (key!=KEY_SYSRQ && key!=KEY_PRINT) {
		return false;
	}
	for (uint32_t k=0;k<=KEY_MAX;k++) {
		if (!seat->keys_held[k] || k==key) continue;
		if (win_shift && (k==KEY_LEFTMETA || k==KEY_RIGHTMETA || k==KEY_LEFTSHIFT || k==KEY_RIGHTSHIFT)) continue;
		return false;
	}
	return seat->screenshot_open(seat->screenshot_data);
}

static bool meta_shift_chord(const struct nested_seat *seat,uint32_t key) {
	if(seat->keys_held[KEY_LEFTMETA]==seat->keys_held[KEY_RIGHTMETA] ||
			seat->keys_held[KEY_LEFTSHIFT]==seat->keys_held[KEY_RIGHTSHIFT] ||
			seat->keyboard.modifiers.latched) return false;
	for(uint32_t k=0;k<=KEY_MAX;k++) {
		if(k!=key && k!=KEY_LEFTMETA && k!=KEY_RIGHTMETA &&
				k!=KEY_LEFTSHIFT && k!=KEY_RIGHTSHIFT && seat->keys_held[k]) return false;
	}
	return true;
}

static bool any_meta_held(const struct nested_seat *seat) {
	return seat->keys_held[KEY_LEFTMETA] || seat->keys_held[KEY_RIGHTMETA];
}

static bool node_switch_available(struct nested_seat *seat) {
	return !seat->blocked_count && !seat->screenshot_panel && !seat->moving &&
		!seat->map_pressed && !seat->child->pointer_state.button_count &&
		!seat->keyboard.modifiers.latched &&
		!wlr_seat_keyboard_has_grab(seat->child) && !wlr_seat_pointer_has_grab(seat->child);
}

static bool node_switch_key(struct nested_seat *seat, uint32_t key, bool was_held) {
	bool down = seat->keys_held[key];
	if (key == KEY_ESC && (seat->node_switch_active || seat->node_switch_escape)) {
		if (down) nested_seat_cancel_node_switch(seat);
		seat->node_switch_escape = down;
		return true;
	}
	bool meta = key == KEY_LEFTMETA || key == KEY_RIGHTMETA;
	bool shift = key == KEY_LEFTSHIFT || key == KEY_RIGHTSHIFT;
	if (key != KEY_TAB) {
		if (down && !meta && !shift) nested_seat_cancel_node_switch(seat);
		return false;
	}
	// The paired release stays with the gesture after commit or cancel.
	if (!down || was_held) {
		bool handled = seat->node_switch_tab;
		if (!down) seat->node_switch_tab = false;
		return handled;
	}
	if (!seat->node_switch || !any_meta_held(seat) || !node_switch_available(seat)) {
		nested_seat_cancel_node_switch(seat);
		return false;
	}
	for (uint32_t k = 0; k <= KEY_MAX; ++k) {
		if (seat->keys_held[k] && k != KEY_TAB && k != KEY_LEFTMETA && k != KEY_RIGHTMETA &&
				k != KEY_LEFTSHIFT && k != KEY_RIGHTSHIFT) return false;
	}
	bool reverse = seat->keys_held[KEY_LEFTSHIFT] || seat->keys_held[KEY_RIGHTSHIFT];
	if (!seat->node_switch(seat->data, reverse ? NODE_SWITCH_PREVIOUS : NODE_SWITCH_NEXT)) {
		nested_seat_cancel_node_switch(seat);
		return false;
	}
	cancel_cycle(seat);
	seat->node_switch_active = seat->node_switch_tab = true;
	return true;
}

static void forward_key(struct nested_seat *seat,uint32_t time,uint32_t key,uint32_t state) {
	struct wlr_keyboard_key_event e={.time_msec=time,.keycode=key,.state=state};
	wlr_keyboard_notify_key(&seat->keyboard,&e);
	wlr_seat_keyboard_notify_key(seat->child,time,key,state);
	if (seat->child->keyboard_state.focused_surface) render_debug_activity(seat->render_debug,RENDER_KEY);
}

static void keyboard_key(void *data, struct wl_keyboard *keys, uint32_t serial,
		uint32_t time, uint32_t key, uint32_t state) {
	struct nested_seat *seat = data;
	if (!seat->keyboard_inside || !seat->has_keymap || key > KEY_MAX) {
		return;
	}
	bool was_held = seat->keys_held[key];
	seat->keys_held[key] = state == WL_KEYBOARD_KEY_STATE_PRESSED;
	bool home_tap=false,meta=key==KEY_LEFTMETA || key==KEY_RIGHTMETA;
	bool node_scope=!seat->screenshot_panel && seat->node_panel_key && seat->node_panel_key(seat->data,0)!=NULL;
	bool home_allowed=!seat->screenshot_panel && (!seat->panel_focus || seat->map_home || node_scope);
	if (seat->keys_held[key]) {
		if (seat->home_tap_key && key!=seat->home_tap_key) seat->home_tap_key=0;
		if (meta && !was_held && home_allowed && !seat->blocked_count &&
				!seat->keyboard.modifiers.latched) {
			xkb_mod_index_t logo=xkb_keymap_mod_get_index(seat->keyboard.keymap,XKB_MOD_NAME_LOGO);
			uint32_t meta_mask=logo<32?1u<<logo:0;
			bool alone=true;
			for(uint32_t k=0;k<=KEY_MAX;k++) if(k!=key && seat->keys_held[k]) {alone=false;break;}
			if(alone && !(seat->keyboard.modifiers.depressed & ~meta_mask)) seat->home_tap_key=key;
		}
	} else if (seat->home_tap_key==key) {
		home_tap=was_held && home_allowed;
		seat->home_tap_key=0;
	}
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
	if (node_switch_key(seat, key, was_held)) return;
	if (!was_held && seat->keys_held[key] && screenshot_chord(seat,key)) {
		seat->keys_blocked[key]=true;
		seat->blocked_count++;
		return;
	}
	if (seat->screenshot_panel) {
		forward_key(seat,time,key,state);
		return;
	}
	bool arrow=key==KEY_LEFT || key==KEY_RIGHT || key==KEY_UP || key==KEY_DOWN;
	if((arrow || key==KEY_F12) && seat->keys_held[key] && !was_held && meta_shift_chord(seat,key)) {
		bool handled=false;
		if(key==KEY_F12) {
			handled=seat->render_capture && seat->render_capture(seat->data);
		} else if(seat->reorder_window && seat->selected && !seat->panel_focus &&
				!seat->moving && !seat->cycle_active && !seat->blocked_count &&
				seat->child->keyboard_state.focused_surface==seat->selected) {
			handled=seat->reorder_window(seat->data,seat->selected,key);
		}
		if(handled) {
			seat->keys_blocked[key]=true;
			seat->blocked_count++;
			return;
		}
	}
	// Meta+Shift with a Node number moves the focused app window there.
	bool move_number=key>=seat->node_first_key && key<=seat->node_first_key+8;
	if(move_number && seat->move_window && seat->keys_held[key] && !was_held &&
			meta_shift_chord(seat,key) && seat->selected && !seat->panel_focus &&
			!seat->moving && !seat->cycle_active && !seat->blocked_count &&
			seat->child->keyboard_state.focused_surface==seat->selected &&
			seat->move_window(seat->data,seat->selected,key-seat->node_first_key+1)) {
		seat->keys_blocked[key]=true;
		seat->blocked_count++;
		return;
	}
	// A Node jump is shell-wide. Close only an app that has keyboard
	// focus; a panel overlay must not close the remembered app beneath it.
	bool node_number=key>=seat->node_first_key && key<=seat->node_first_key+8;
	bool node_notifications=node_scope && key==KEY_N;
	bool node_panel_key=node_notifications || key==seat->apps_key || key==seat->settings_key || key==KEY_SLASH;
	if((key==seat->close_key || node_number || node_panel_key) && seat->keys_held[key] && !was_held &&
			(seat->keys_held[KEY_LEFTMETA]!=seat->keys_held[KEY_RIGHTMETA])) {
		bool alone=true;
		for(uint32_t k=0;k<=KEY_MAX;k++)
			if(k!=key && k!=KEY_LEFTMETA && k!=KEY_RIGHTMETA && seat->keys_held[k]) {alone=false;break;}
		if(alone && !seat->keyboard.modifiers.latched) {
			bool handled=false;
			if(key==seat->close_key && !node_notifications) handled=!seat->blocked_count && !seat->panel_focus && seat->selected &&
				seat->close_window && seat->close_window(seat->data,seat->selected);
			else if(node_number)handled=seat->node_number && seat->node_number(seat->data,key-seat->node_first_key+1);
			else if(node_scope && !seat->blocked_count) {
				// Use the existing action identifiers regardless of the bound key.
				struct wlr_surface *panel=seat->node_panel_key(seat->data,
					node_notifications?KEY_N:key==KEY_SLASH?KEY_SLASH:key==seat->apps_key?KEY_A:KEY_COMMA);
				if(panel && panel==keyboard_target(seat)) {
					// In an empty Default Node the panel already has focus.
					// Do not send leave/enter on the same surface: a GTK
					// popup dismisses itself as soon as it sees the leave.
					seat->panel_focus=panel;
					handled=true;
				} else if(panel) {
					nested_seat_shortcut_panel(seat,panel);
					return;
				}
			}
			if(handled) {
				seat->keys_blocked[key]=true;
				seat->blocked_count++;
				return;
			}
		}
	}
	// The parent may reserve Alt+Tab; only handle the delivered chord.
	bool alt=seat->keys_held[KEY_LEFTALT];
	bool reserved=seat->keys_held[KEY_LEFTCTRL] || seat->keys_held[KEY_RIGHTCTRL] ||
		seat->keys_held[KEY_LEFTMETA] || seat->keys_held[KEY_RIGHTMETA];
	if(reserved || seat->panel_focus) cancel_cycle(seat);
	if(key==KEY_ESC && (seat->cycle_active || seat->cycle_escape)) {
		if(seat->keys_held[key]) {
			cancel_cycle(seat);
			seat->cycle_escape=true;
		} else seat->cycle_escape=false;
		return;
	}
	if (key==KEY_TAB) {
		if (seat->cycle_tab && !seat->keys_held[key]) {
			seat->cycle_tab=false;
			return;
		}
		if (seat->keys_held[key] && alt && !reserved && !seat->panel_focus) {
			bool started=seat->cycle_active;
			if(!started && !was_held)
				seat->cycle_active=seat->cycle(seat->data,CYCLE_BEGIN,0);
			if(seat->cycle_active) {
				if(!was_held) {
					bool reverse=seat->keys_held[KEY_LEFTSHIFT] || seat->keys_held[KEY_RIGHTSHIFT];
					if(labs(seat->cycle_steps)<1024) seat->cycle_steps+=reverse?-1:1;
					seat->cycle_visible|=started;
					if(seat->cycle_visible) seat->cycle(seat->data,CYCLE_PREVIEW,seat->cycle_steps);
				}
				seat->cycle_tab=true;
				return;
			}
		}
	}

	forward_key(seat,time,key,state);
	if (meta && !seat->keys_held[key] && !any_meta_held(seat))
		finish_node_switch(seat, node_switch_available(seat));
	if(home_tap && seat->home_tap) seat->home_tap(seat->data);
	if (key==KEY_LEFTALT && !alt && seat->cycle_active) {
		int steps=seat->cycle_steps;
		seat->cycle_active=seat->cycle_visible=false;
		seat->cycle_steps=0;
		seat->cycle(seat->data,CYCLE_COMMIT,steps);
	}
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
	nested_seat_cancel_map_pointer(seat);
	if (seat->pointer != NULL) {
		if (wl_pointer_get_version(seat->pointer) >= WL_POINTER_RELEASE_SINCE_VERSION) {
			wl_pointer_release(seat->pointer);
		} else {
			wl_pointer_destroy(seat->pointer);
		}
		seat->pointer = NULL;
	}
	seat->pointer_inside = false;
	seat->map_release_pending = false;
	memset(seat->buttons_held, 0, sizeof(seat->buttons_held));
	memset(seat->buttons_blocked, 0, sizeof(seat->buttons_blocked));
	wlr_seat_pointer_notify_clear_focus(seat->child);
}

static void drop_keyboard(struct nested_seat *seat) {
	nested_seat_cancel_map_pointer(seat);
	nested_seat_cancel_node_switch(seat);
	seat->node_switch_tab = seat->node_switch_escape = false;
	cancel_cycle(seat);
	seat->cycle_tab=seat->cycle_escape=false;
	if (seat->keys != NULL) {
		if (wl_keyboard_get_version(seat->keys) >= WL_KEYBOARD_RELEASE_SINCE_VERSION) {
			wl_keyboard_release(seat->keys);
		} else {
			wl_keyboard_destroy(seat->keys);
		}
		seat->keys = NULL;
	}
	seat->keyboard_inside = false;
	if (seat->screenshot_abort) seat->screenshot_abort(seat->screenshot_data);
	seat->home_tap_key=0;
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

static void local_capabilities(struct nested_seat *seat) {
	uint32_t caps=0;
	if(seat->local_pointers) caps|=WL_SEAT_CAPABILITY_POINTER;
	if(seat->local_keyboards) caps|=WL_SEAT_CAPABILITY_KEYBOARD;
	wlr_seat_set_capabilities(seat->child,caps);
}

static void local_modifiers(struct nested_seat *seat) {
	struct xkb_state *xs=seat->local_xkb;
	keyboard_modifiers(seat,NULL,0,xkb_state_serialize_mods(xs,XKB_STATE_MODS_DEPRESSED),
		xkb_state_serialize_mods(xs,XKB_STATE_MODS_LATCHED),
		xkb_state_serialize_mods(xs,XKB_STATE_MODS_LOCKED),
		xkb_state_serialize_layout(xs,XKB_STATE_LAYOUT_EFFECTIVE));
}

static void local_key(struct wl_listener *listener,void *data) {
	struct local_input *dev=wl_container_of(listener,dev,key);
	struct nested_seat *seat=dev->seat;
	struct wlr_keyboard_key_event *event=data;
	uint32_t key=event->keycode;
	if(!seat->keyboard_inside || key>KEY_MAX) return;
	bool pressed=event->state==WL_KEYBOARD_KEY_STATE_PRESSED;
	if(pressed && (seat->keys_held[KEY_LEFTCTRL] || seat->keys_held[KEY_RIGHTCTRL]) &&
			seat->keys_held[KEY_LEFTALT]) {
		unsigned vt=key>=KEY_F1 && key<=KEY_F10?key-KEY_F1+1:key==KEY_F11?11:key==KEY_F12?12:0;
		if(key==KEY_ESC || (vt && seat->local_session)) {
			seat->keys_held[key]=true;
			if(!seat->keys_blocked[key]) {seat->keys_blocked[key]=true;seat->blocked_count++;}
			if(key==KEY_ESC) seat->local_logout(seat->data);
			else wlr_session_change_vt(seat->local_session,vt);
			return;
		}
	}
	if(seat->keys_held[key]!=pressed)
		xkb_state_update_key(seat->local_xkb,key+8,pressed?XKB_KEY_DOWN:XKB_KEY_UP);
	keyboard_key(seat,NULL,0,event->time_msec,key,event->state);
	local_modifiers(seat);
}

static void local_motion_at(struct nested_seat *seat,uint32_t ms,double x,double y) {
	x=fmax(0,fmin(x,seat->local_width-1));
	y=fmax(0,fmin(y,seat->local_height-1));
	pointer_motion(seat,NULL,ms,wl_fixed_from_double(x),wl_fixed_from_double(y));
}

static void local_motion(struct wl_listener *listener,void *data) {
	struct local_input *dev=wl_container_of(listener,dev,motion);
	struct nested_seat *seat=dev->seat;
	struct wlr_pointer_motion_event *e=data;
	local_motion_at(seat,e->time_msec,seat->x+e->delta_x,seat->y+e->delta_y);
}

static void local_absolute(struct wl_listener *listener,void *data) {
	struct local_input *dev=wl_container_of(listener,dev,absolute);
	struct nested_seat *seat=dev->seat;
	struct wlr_pointer_motion_absolute_event *e=data;
	local_motion_at(seat,e->time_msec,isfinite(e->x)?e->x*seat->local_width:seat->x,
		isfinite(e->y)?e->y*seat->local_height:seat->y);
}

static void local_button(struct wl_listener *listener,void *data) {
	struct local_input *dev=wl_container_of(listener,dev,button);
	struct wlr_pointer_button_event *e=data;
	pointer_button(dev->seat,NULL,0,e->time_msec,e->button,e->state);
}

static void local_axis(struct wl_listener *listener,void *data) {
	struct local_input *dev=wl_container_of(listener,dev,axis);
	struct wlr_pointer_axis_event *e=data;
	struct nested_seat *seat=dev->seat;
	if(!seat->pointer_inside || e->orientation>=2) return;
	seat->axis_source=e->source;
	seat->axes[e->orientation]=(struct pending_axis){true,e->time_msec,e->delta,e->delta_discrete};
	flush_axis(seat,e->orientation);
}

static void local_frame(struct wl_listener *listener,void *data) {
	(void)data;
	struct local_input *dev=wl_container_of(listener,dev,frame);
	if(dev->seat->pointer_inside) pointer_frame(dev->seat,NULL);
}

static void local_input_destroy(struct wl_listener *listener,void *data) {
	(void)data;
	struct local_input *dev=wl_container_of(listener,dev,destroy);
	struct nested_seat *seat=dev->seat;
	if(dev->key.notify) {
		wl_list_remove(&dev->key.link);
		seat->local_keyboards--;
		if(seat->local_attached) {
			bool active=seat->keyboard_inside;
			nested_seat_local_active(seat,false);
			nested_seat_local_active(seat,active);
		}
	} else {
		wl_list_remove(&dev->motion.link);
		wl_list_remove(&dev->absolute.link);
		wl_list_remove(&dev->button.link);
		wl_list_remove(&dev->axis.link);
		wl_list_remove(&dev->frame.link);
		if(--seat->local_pointers==0) pointer_leave(seat,NULL,0,NULL);
	}
	wl_list_remove(&dev->destroy.link);
	wl_list_remove(&dev->link);
	local_capabilities(seat);
	free(dev);
}

static void local_input_added(struct wl_listener *listener,void *data) {
	struct nested_seat *seat=wl_container_of(listener,seat,local_device);
	struct wlr_input_device *input=data;
	if(input->type!=WLR_INPUT_DEVICE_KEYBOARD && input->type!=WLR_INPUT_DEVICE_POINTER) return;
	struct local_input *dev=calloc(1,sizeof(*dev));
	if(!dev) return;
	dev->seat=seat;
	if(input->type==WLR_INPUT_DEVICE_KEYBOARD) {
		struct wlr_keyboard *kb=wlr_keyboard_from_input_device(input);
		dev->key.notify=local_key;
		wl_signal_add(&kb->events.key,&dev->key);
		seat->local_keyboards++;
	} else {
		struct wlr_pointer *p=wlr_pointer_from_input_device(input);
		dev->motion.notify=local_motion;
		dev->absolute.notify=local_absolute;
		dev->button.notify=local_button;
		dev->axis.notify=local_axis;
		dev->frame.notify=local_frame;
		wl_signal_add(&p->events.motion,&dev->motion);
		wl_signal_add(&p->events.motion_absolute,&dev->absolute);
		wl_signal_add(&p->events.button,&dev->button);
		wl_signal_add(&p->events.axis,&dev->axis);
		wl_signal_add(&p->events.frame,&dev->frame);
		seat->local_pointers++;
		seat->pointer_inside=seat->keyboard_inside;
	}
	dev->destroy.notify=local_input_destroy;
	wl_signal_add(&input->events.destroy,&dev->destroy);
	wl_list_insert(&seat->local_inputs,&dev->link);
	local_capabilities(seat);
	keyboard_focus(seat);
}

bool nested_seat_attach_local(struct nested_seat *seat,struct wlr_backend *backend,
		struct wlr_session *session,void (*logout)(void *)) {
	if(!seat || seat->registry) return false;
	struct xkb_context *context=xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if(!context) return false;
	struct xkb_keymap *map=xkb_keymap_new_from_names(context,NULL,XKB_KEYMAP_COMPILE_NO_FLAGS);
	xkb_context_unref(context);
	if(!map) return false;
	bool ok=wlr_keyboard_set_keymap(&seat->keyboard,map);
	if(ok) seat->local_xkb=xkb_state_new(map);
	xkb_keymap_unref(map);
	if(!seat->local_xkb) return false;
	seat->has_keymap=true;
	seat->local_width=seat->local_height=1;
	seat->local_session=session;
	seat->local_logout=logout;
	seat->local_attached=true;
	wl_list_init(&seat->local_inputs);
	seat->local_device.notify=local_input_added;
	wl_signal_add(&backend->events.new_input,&seat->local_device);
	wlr_keyboard_set_repeat_info(&seat->keyboard,25,400);
	wlr_seat_set_keyboard(seat->child,&seat->keyboard);
	nested_seat_local_active(seat,!session || session->active);
	return true;
}

void nested_seat_local_extent(struct nested_seat *seat,int width,int height) {
	if(!seat || !seat->local_attached || width<=0 || height<=0) return;
	seat->local_width=width;
	seat->local_height=height;
	seat->x=width/2.;
	seat->y=height/2.;
	if(seat->pointer_inside) seat->move_cursor(seat->data,true,seat->x,seat->y);
}

void nested_seat_local_active(struct nested_seat *seat,bool active) {
	if(!seat || !seat->local_attached) return;
	if(!active) {
		keyboard_leave(seat,NULL,0,NULL);
		pointer_leave(seat,NULL,0,NULL);
		struct xkb_state *next=xkb_state_new(seat->keyboard.keymap);
		if(next) {
			xkb_state_unref(seat->local_xkb);
			seat->local_xkb=next;
		}
		memset(seat->buttons_held,0,sizeof(seat->buttons_held));
		memset(seat->buttons_blocked,0,sizeof(seat->buttons_blocked));
		seat->map_release_pending=false;
		wlr_seat_pointer_end_grab(seat->child);
		seat->child->pointer_state.button_count=0;
		seat->child->pointer_state.grab_serial=0;
		return;
	}
	struct wl_array held;
	wl_array_init(&held);
	keyboard_enter(seat,NULL,0,NULL,&held);
	local_modifiers(seat);
	if(seat->local_pointers)
		pointer_enter(seat,NULL,0,NULL,wl_fixed_from_double(seat->x),wl_fixed_from_double(seat->y));
}

static void render_focus_change(struct wl_listener *listener,void *data) {
	(void)data;
	struct nested_seat *seat=wl_container_of(listener,seat,render_focus_change);
	nested_seat_cancel_node_switch(seat);
	render_debug_activity(seat->render_debug,RENDER_FOCUS);
}

static void set_selection(struct wl_listener *listener,void *data) {
	struct nested_seat *seat=wl_container_of(listener,seat,selection_request);
	struct wlr_seat_request_set_selection_event *event=data;
	wlr_seat_set_selection(seat->child,event->source,event->serial);
}

void nested_seat_set_render_debug(struct nested_seat *seat,struct render_debug *debug) {
	if (seat) seat->render_debug=debug;
}

struct nested_seat *nested_seat_create(struct wl_display *server,
		struct wl_display *parent, struct wl_surface *output,
		nested_hit_test hit_test, nested_selected selected, nested_cursor_move move_cursor,
		nested_move_start start_move, nested_move_update update_move,nested_map_scroll scroll,nested_map_contact contact,
		nested_map_weapon weapon,
		nested_chrome_action chrome,nested_flow_scroll flow_scroll,nested_strip_scroll strip_scroll,nested_flow_cycle cycle,void (*home_tap)(void *),
		struct wlr_surface *(*node_panel_key)(void *,uint32_t),bool (*node_number)(void *,uint32_t),
		nested_node_switch node_switch,
		bool (*close_window)(void *,struct wlr_surface *),
		bool (*reorder_window)(void *,struct wlr_surface *,uint32_t),bool (*render_capture)(void *),void *data) {
	struct nested_seat *seat = calloc(1, sizeof(*seat));
	if (seat == NULL) {
		return NULL;
	}
	seat->child = wlr_seat_create(server, "mothership");
	if (seat->child == NULL) {
		free(seat);
		return NULL;
	}
	seat->render_focus_change.notify=render_focus_change;
	wl_signal_add(&seat->child->keyboard_state.events.focus_change,&seat->render_focus_change);
	seat->selection_request.notify=set_selection;
	wl_signal_add(&seat->child->events.request_set_selection,&seat->selection_request);
	wlr_keyboard_init(&seat->keyboard, &keyboard_impl, "nested-wayland");
	seat->output = output;
	seat->hit_test = hit_test;
	seat->select_window = selected;
	seat->move_cursor = move_cursor;
	seat->start_move = start_move;
	seat->update_move = update_move;
	seat->map_scroll=scroll;
	seat->strip_scroll=strip_scroll;
	seat->map_contact=contact;
	seat->map_weapon=weapon;
	seat->chrome=chrome;
	seat->flow_scroll=flow_scroll;
	seat->cycle=cycle;
	seat->home_tap=home_tap;
	seat->node_panel_key=node_panel_key;
	seat->node_number=node_number;
	seat->node_switch=node_switch;
	seat->close_window=close_window;
	seat->reorder_window=reorder_window;
	seat->render_capture=render_capture;
	nested_seat_set_shortcuts(seat,KEY_Q,KEY_A,KEY_COMMA,KEY_1);
	seat->data = data;
	if(!parent) return seat;
	seat->registry = wl_display_get_registry(parent);
	if (seat->registry == NULL) {
		nested_seat_destroy(seat);
		return NULL;
	}
	wl_registry_add_listener(seat->registry, &registry_listener, seat);
	return seat;
}

void nested_seat_detach_local(struct nested_seat *seat) {
	if(!seat || !seat->local_attached) return;
	seat->local_attached=false;
	wl_list_remove(&seat->local_device.link);
	struct local_input *entry,*tmp;
	wl_list_for_each_safe(entry,tmp,&seat->local_inputs,link)
		local_input_destroy(&entry->destroy,NULL);
	xkb_state_unref(seat->local_xkb);
	seat->local_xkb=NULL;
}

void nested_seat_destroy(struct nested_seat *seat) {
	if (seat == NULL) {
		return;
	}
	nested_seat_detach_local(seat);
	drop_parent(seat);
	if (seat->registry != NULL) {
		wl_registry_destroy(seat->registry);
	}
	wlr_keyboard_finish(&seat->keyboard);
	wl_list_remove(&seat->render_focus_change.link);
	wl_list_remove(&seat->selection_request.link);
	wlr_seat_destroy(seat->child);
	free(seat);
}
