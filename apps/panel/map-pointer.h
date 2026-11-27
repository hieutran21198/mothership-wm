#ifndef MOTHERSHIP_PANEL_MAP_POINTER_H
#define MOTHERSHIP_PANEL_MAP_POINTER_H
#include <gtk/gtk.h>
void map_pointer_init(GtkWindow *window);
void map_pointer_zoom_enabled(gboolean enabled,int margin);
void map_pointer_aim_enabled(gboolean);
void map_pointer_video_frost(gboolean,int margin);
enum { SCENE_GLASS_GRID, SCENE_GLASS_LOG, SCENE_GLASS_COUNT };
void map_pointer_scene_glass(unsigned slot,int x,int y,int w,int h,unsigned alpha);
void panel_home_tap(void);
void panel_node_apps(void);
void panel_node_slash(void);
void panel_node_notifications(void);
void panel_open_node_settings(void);
void panel_map_weapon(gboolean plasma,gboolean pressed);
void panel_map_aim(gboolean,double,double);
void panel_map_click(double x,double y);
void panel_map_contact(uint32_t phase,double x,double y);
void panel_map_scroll(double x,double y,double delta);
void panel_node_strip(int x,int y,int w,int h,gboolean motion);
void panel_node_strip_pointer(double x,gboolean focus);
void panel_node_group_query(double x,double y);
void panel_node_group_action(uint64_t id,unsigned action,int sx,int sy,int gap);
void panel_node_group_settings_anchor(int x,int y);
void panel_node_group_state(uint64_t id,unsigned count,gboolean hidden,int sx,int sy,int gap);

void panel_request_window_picker(void);
void panel_pick_window(uint32_t serial,uint64_t id);
void panel_set_picker_scale(int percent);
void panel_set_strip_hover_scale(int percent);
void map_pointer_set_cursor_size(int);
void panel_place_window_preview(uint32_t serial,uint64_t id,int x,int y,int width,int height);
void panel_node_number(uint32_t slot);
void panel_node_move(uint32_t slot,uint64_t id);
void panel_pin_state(uint64_t id,int x,int y,int w,int h,const char *title,const char *app_id);
void panel_pin_activate(uint64_t id);
gboolean panel_request_window_list(uint32_t serial);
void panel_node_switch_step(int32_t direction);
void panel_node_switch_finish(uint32_t commit);
void panel_window_count(uint32_t count);
void panel_window_overflow(uint32_t count);
void panel_picker_begin(uint32_t serial,gboolean keyboard);
void panel_picker_row(uint64_t id,const char *title,const char *app_id);
void panel_picker_highlight(uint32_t serial,uint64_t id);
void panel_picker_end(uint32_t serial);
void panel_display_query(void);
gboolean panel_display_apply(int w,int h,int hz,int scale);
void map_pointer_screenshot_begin(void);
void map_pointer_screenshot_cancel(void);
void map_pointer_screenshot_window(uint32_t hi,uint32_t lo);
#endif
