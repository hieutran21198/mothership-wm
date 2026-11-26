#ifndef MOTHERSHIP_WINDOW_CHROME_H
#define MOTHERSHIP_WINDOW_CHROME_H
#include <stdbool.h>

struct wlr_box;
struct wlr_scene_node;
struct wlr_scene_tree;
struct window_chrome;

enum {
	WINDOW_CHROME_TOP=44, WINDOW_CHROME_EDGE=12,
	WINDOW_CONTENT_PAD=4,
	WINDOW_CONTENT_TOP=WINDOW_CHROME_TOP+WINDOW_CONTENT_PAD,
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
	const char *title,bool focused,bool float_action,bool floating,bool maximized,
	bool small_actions,bool minimize);
// The aperture is relative to the outer frame. NULL restores all strips.
void window_chrome_clip(struct window_chrome *,const struct wlr_box *);
bool window_chrome_owns_node(const struct window_chrome *,const struct wlr_scene_node *);
enum window_chrome_hit window_chrome_hit_at(const struct window_chrome *,double x,double y);
#endif
