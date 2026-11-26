#ifndef MOTHERSHIP_PANEL_H
#define MOTHERSHIP_PANEL_H
#include <stddef.h>
#include <stdint.h>

int panel_add_shell_font(const char *path);
void panel_use_shell_family(const char *family);
int panel_init(void);
void panel_run(void);
void panel_quit(void);
void panel_set_home_width(int);
void panel_set_saved_cursor_size(int);
void panel_set_home_guide(int);
void panel_set_node_preferences(int,int,int);
void panel_set_video_enabled(int);
void panel_set_default_node_enabled(int);
void app_grid_set_pins(const char *);
void panel_home_notice(const char *);
void panel_set_toast_mode(int);
#include "notification-ui.h"
void panel_home_sample(int,const int *,int,double,double,double,double,int,int,int,const char *,const char *);
void panel_set_map_settings(const char *, int outer, int inner, int frosted, int animation, int show_fps, int theme_pan, int pan_override, int max_outer, int max_inner, const char *default_border, int default_outer, int default_inner);
void panel_post(const char *view, const char *message, const char *domains,
	const char *windows, const char *node, const char *mode, const char *profiles, int enabled, int overview, int map_visible, int blank_home,int default_node,int ready,uint64_t connect_failed,uint64_t prepared_relay);
void map_hud_configure(const char *, double, const char *, const char *, const char *, const char *);
int wallpaper_set(const unsigned char *, size_t, int contain);
void wallpaper_show(void);
void *wallpaper_prepare(const unsigned char *,size_t);
void wallpaper_replace(void *,int contain);
void *wallpaper_prepare_mp4(const char *);
void wallpaper_replace_mp4(void *,int contain);
void wallpaper_discard_prepared(void *,int mp4);
int map_hud_set_font(const char *, double);
void *map_hud_icons_new(void);
void map_hud_icons_free(void *);
int map_hud_icon_add(void *,const char *,const unsigned char *,size_t);
void map_hud_icons_replace(void *);

#endif
