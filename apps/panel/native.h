#ifndef MOTHERSHIP_PANEL_H
#define MOTHERSHIP_PANEL_H
#include <stddef.h>

int panel_init(void);
void panel_run(void);
void panel_quit(void);
void panel_set_map_settings(const char *, int outer, int inner, int frosted, int animation, int show_fps, int theme_pan, int pan_override, int max_outer, int max_inner);
void panel_post(const char *view, const char *message, const char *domains,
	const char *windows, const char *node, const char *mode, const char *profiles, int enabled, int overview, int map_visible, int blank_home);
void map_hud_configure(const char *, double, const char *, const char *, const char *, const char *);
int wallpaper_set(const unsigned char *, size_t, int contain);
void wallpaper_show(void);
int map_hud_set_font(const char *, double);
int map_hud_icon(const char *, const unsigned char *, size_t);

#endif
