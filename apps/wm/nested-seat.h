#ifndef MOTHERSHIP_NESTED_SEAT_H
#define MOTHERSHIP_NESTED_SEAT_H

#include <stdbool.h>

struct wl_display;
struct wl_surface;
struct wlr_surface;
struct nested_seat;

struct nested_hit {
	struct wlr_surface *surface;
	struct wlr_surface *toplevel;
	double x, y;
};

typedef struct nested_hit (*nested_hit_test)(void *data, double x, double y);
typedef void (*nested_selected)(void *data, struct wlr_surface *target);

struct nested_seat *nested_seat_create(struct wl_display *server,
	struct wl_display *parent, struct wl_surface *output,
	nested_hit_test hit_test, nested_selected selected, void *data);
void nested_seat_forget(struct nested_seat *seat, struct wlr_surface *gone,
	struct wlr_surface *fallback);
void nested_seat_switch(struct nested_seat *seat, struct wlr_surface *target);
void nested_seat_refresh(struct nested_seat *seat);
void nested_seat_destroy(struct nested_seat *seat);

#endif
