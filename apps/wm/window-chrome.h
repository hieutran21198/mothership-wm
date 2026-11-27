#ifndef MOTHERSHIP_WINDOW_CHROME_H
#define MOTHERSHIP_WINDOW_CHROME_H
#include <stdbool.h>

struct wlr_box;
struct wlr_scene_buffer;
struct wlr_scene_node;
struct wlr_scene_tree;
struct window_chrome;
struct window_resize_preview;

enum {
	WINDOW_CHROME_TOP=44, WINDOW_CHROME_BOTTOM=28, WINDOW_CHROME_EDGE=12,
	WINDOW_CONTENT_PAD=4,
	WINDOW_CONTENT_TOP=WINDOW_CHROME_TOP+WINDOW_CONTENT_PAD,
	WINDOW_CONTENT_BOTTOM=WINDOW_CHROME_BOTTOM+WINDOW_CONTENT_PAD,
	WINDOW_CONTENT_EDGE=WINDOW_CHROME_EDGE+WINDOW_CONTENT_PAD,
};

enum window_chrome_hit {
	WINDOW_CHROME_NONE,
	WINDOW_CHROME_TITLE,
	WINDOW_CHROME_FLOAT,
	WINDOW_CHROME_MAXIMIZE,
	WINDOW_CHROME_MINIMIZE,
	WINDOW_CHROME_CLOSE,
	WINDOW_CHROME_RESIZE,
};

struct window_chrome *window_chrome_create(struct wlr_scene_tree *);
void window_chrome_destroy(struct window_chrome *);
void window_chrome_show(struct window_chrome *,bool);
void window_chrome_update(struct window_chrome *,int width,int height,double scale,
	const char *title,bool focused,bool pinned,bool float_action,bool floating,bool maximized,
	bool small_actions,bool minimize,bool resizable,const char *hints);
// Hints are "Key Label" items separated by '|'. Each item splits at its
// last space. Items that do not fit the footer are dropped from the end.
// The aperture is relative to the outer frame. NULL restores all strips.
void window_chrome_clip(struct window_chrome *,const struct wlr_box *);
bool window_chrome_owns_node(const struct window_chrome *,const struct wlr_scene_node *);
struct wlr_box window_chrome_maximize_box(const struct window_chrome *);
enum window_chrome_hit window_chrome_hit_at(const struct window_chrome *,double x,double y);
struct window_resize_preview *window_resize_preview_create(struct wlr_scene_tree *);
void window_resize_preview_destroy(struct window_resize_preview *);
void window_resize_preview_hide(struct window_resize_preview *);
void window_resize_preview_place(struct window_resize_preview *,
	const struct wlr_box *original,const struct wlr_box *proposed,int width,int height);
bool window_chrome_pin_face(struct wlr_scene_buffer *, int width, int height,
	double scale, int header, int row, bool highlight);
#endif
