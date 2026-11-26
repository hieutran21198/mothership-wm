#ifndef MOTHERSHIP_VIDEO_FROST_H
#define MOTHERSHIP_VIDEO_FROST_H
#include <stdbool.h>
struct wlr_scene_tree;
struct wlr_output;
struct wlr_surface;
struct video_frost;
struct video_frost *video_frost_create(struct wlr_scene_tree *,struct wlr_output *);
void video_frost_raise(struct video_frost *);
void video_frost_hide(struct video_frost *);
enum video_frost_result { FROST_OFF, FROST_HIT, FROST_REBUILT, FROST_FALLBACK };
enum video_frost_result video_frost_update(struct video_frost *,struct wlr_surface *,int width,int height,int margin);
void video_frost_destroy(struct video_frost *);
#endif
