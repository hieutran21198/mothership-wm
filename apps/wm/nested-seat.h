#ifndef MOTHERSHIP_NESTED_SEAT_H
#define MOTHERSHIP_NESTED_SEAT_H

#include <stdbool.h>
#include <stdint.h>

struct wl_display;
struct wl_surface;
struct wlr_surface;
struct nested_seat;

struct nested_hit {
	struct wlr_surface *surface;
	struct wlr_surface *toplevel;
	struct wlr_surface *panel_keyboard;
	void *handle, *decoration;
	int chrome_action;
	bool resize;
	double x, y;
};

typedef struct nested_hit (*nested_hit_test)(void *data, double x, double y);
typedef void (*nested_cursor_move)(void *data, bool inside, double x, double y);
typedef void (*nested_selected)(void *data, struct wlr_surface *target);
#define NESTED_MAP_LEGACY_CLICK UINT32_MAX
typedef bool (*nested_map_contact)(void *data,struct wlr_surface *panel,double x,double y,uint32_t phase);
typedef bool (*nested_map_scroll)(void *data,struct wlr_surface *panel,double x,double y,double delta);
typedef void (*nested_move_start)(void *data, struct nested_hit hit, double x, double y);
typedef void (*nested_move_update)(void *data, bool moving, double x, double y);
typedef void (*nested_chrome_action)(void *data,struct nested_hit hit);
typedef bool (*nested_flow_scroll)(void *data,double x,double y,unsigned axis,double delta);
typedef bool (*nested_flow_cycle)(void *data,int steps);

struct nested_seat *nested_seat_create(struct wl_display *server,
	struct wl_display *parent, struct wl_surface *output,
	nested_hit_test hit_test, nested_selected selected, nested_cursor_move move_cursor,
	nested_move_start start_move, nested_move_update update_move, nested_map_scroll scroll,nested_map_contact contact,
	nested_chrome_action chrome,nested_flow_scroll flow_scroll,nested_flow_cycle cycle,void *data);
void nested_seat_forget(struct nested_seat *seat, struct wlr_surface *gone,
	struct wlr_surface *fallback);
void nested_seat_switch(struct nested_seat *seat, struct wlr_surface *target, bool keep_panel);
void nested_seat_panel(struct nested_seat *seat, struct wlr_surface *panel, bool acquire);
void nested_seat_blank_shell(struct nested_seat *seat,struct wlr_surface *panel);
void nested_seat_cancel_map_pointer(struct nested_seat *seat);
void nested_seat_refresh(struct nested_seat *seat);
void nested_seat_destroy(struct nested_seat *seat);

#endif
