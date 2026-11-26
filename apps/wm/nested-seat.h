#ifndef MOTHERSHIP_NESTED_SEAT_H
#define MOTHERSHIP_NESTED_SEAT_H

#include <stdbool.h>
#include <stdint.h>

struct wl_display;
struct wl_surface;
struct wlr_surface;
struct nested_seat;
struct render_debug;

struct nested_hit {
	struct wlr_surface *surface;
	struct wlr_surface *toplevel;
	struct wlr_surface *panel_keyboard;
	void *handle, *decoration;
	int chrome_action;
	bool resize, tray, defer_move_focus;
	double x, y;
};

typedef struct nested_hit (*nested_hit_test)(void *data, double x, double y);
typedef void (*nested_cursor_move)(void *data, bool inside, double x, double y);
typedef void (*nested_selected)(void *data, struct wlr_surface *target);
#define NESTED_MAP_LEGACY_CLICK UINT32_MAX
typedef bool (*nested_map_contact)(void *data,struct wlr_surface *panel,double x,double y,uint32_t phase);
typedef bool (*nested_map_weapon)(void *data,uint32_t weapon,bool pressed);
typedef bool (*nested_map_scroll)(void *data,struct wlr_surface *panel,double x,double y,double delta);
typedef void (*nested_move_start)(void *data, struct nested_hit hit, double x, double y);
enum nested_move_phase { MOVE_CANCEL, MOVE_UPDATE, MOVE_DROP };
typedef void (*nested_move_update)(void *data, enum nested_move_phase phase, double x, double y);
typedef void (*nested_chrome_action)(void *data,struct nested_hit hit);
typedef bool (*nested_flow_scroll)(void *data,double x,double y,unsigned axis,double delta);
typedef bool (*nested_strip_scroll)(void *data,struct wlr_surface *surface,double x,double y,
	double delta,bool wheel);
enum cycle_phase { CYCLE_BEGIN, CYCLE_PREVIEW, CYCLE_COMMIT, CYCLE_CANCEL };
typedef bool (*nested_flow_cycle)(void *data,enum cycle_phase phase,int steps);

struct nested_seat *nested_seat_create(struct wl_display *server,
	struct wl_display *parent, struct wl_surface *output,
	nested_hit_test hit_test, nested_selected selected, nested_cursor_move move_cursor,
	nested_move_start start_move, nested_move_update update_move, nested_map_scroll scroll,nested_map_contact contact,
	nested_map_weapon weapon,
	nested_chrome_action chrome,nested_flow_scroll flow_scroll,nested_strip_scroll strip_scroll,nested_flow_cycle cycle,void (*home_tap)(void *),
	struct wlr_surface *(*node_panel_key)(void *,uint32_t),bool (*node_number)(void *,uint32_t),
	bool (*close_window)(void *,struct wlr_surface *),bool (*render_capture)(void *),void *data);
struct wlr_backend;
struct wlr_session;
bool nested_seat_attach_local(struct nested_seat *,struct wlr_backend *,struct wlr_session *,void (*logout)(void *));
void nested_seat_detach_local(struct nested_seat *);
void nested_seat_local_extent(struct nested_seat *,int width,int height);
void nested_seat_local_active(struct nested_seat *,bool active);
void nested_seat_set_render_debug(struct nested_seat *,struct render_debug *);
bool nested_seat_set_shortcuts(struct nested_seat *,uint64_t,uint64_t,uint64_t,uint64_t);
void nested_seat_shortcut_panel(struct nested_seat *,struct wlr_surface *);
void nested_seat_forget(struct nested_seat *seat, struct wlr_surface *gone,
	struct wlr_surface *fallback);
void nested_seat_switch(struct nested_seat *seat, struct wlr_surface *target, bool keep_panel);
void nested_seat_panel(struct nested_seat *seat, struct wlr_surface *panel, bool acquire);
void nested_seat_blank_shell(struct nested_seat *seat,struct wlr_surface *panel);
void nested_seat_cancel_map_pointer(struct nested_seat *seat);
void nested_seat_map_home(struct nested_seat *seat,bool enabled);
struct wlr_surface *nested_seat_map_position(struct nested_seat *,double *x,double *y);
void nested_seat_refresh(struct nested_seat *seat);
void nested_seat_destroy(struct nested_seat *seat);

#endif
