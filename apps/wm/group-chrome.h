#ifndef MOTHERSHIP_GROUP_CHROME_H
#define MOTHERSHIP_GROUP_CHROME_H
#include <stdbool.h>
#include <wlr/util/box.h>
struct group_chrome;
struct wlr_scene_tree;
struct wlr_scene_node;
enum {
	GROUP_FRAME_RAIL=44,
	GROUP_FRAME_PAD=12,
	GROUP_FRAME_TOP=GROUP_FRAME_RAIL+GROUP_FRAME_PAD,
};
enum group_control { GROUP_CONTROL_NONE,GROUP_CONTROL_LAYOUT,GROUP_CONTROL_MINIMIZE };
bool group_chrome_add_font(const char *path);
void group_chrome_clear_fonts(void);
struct group_chrome *group_chrome_create(struct wlr_scene_tree *);
void group_chrome_destroy(struct group_chrome *);
struct wlr_scene_tree *group_chrome_tree(struct group_chrome *);
void group_chrome_place(struct group_chrome *,int x,int y,int w,int h,double scale,
	unsigned number,bool focus);
bool group_chrome_owns_node(const struct group_chrome *,const struct wlr_scene_node *);
struct wlr_box group_chrome_control_box(const struct group_chrome *,enum group_control);
enum group_control group_chrome_control(const struct group_chrome *,double x,double y);
#endif
