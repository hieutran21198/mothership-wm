#ifndef MOTHERSHIP_SLASH_UI_H
#define MOTHERSHIP_SLASH_UI_H
#include <gtk/gtk.h>

enum slash_view { SLASH_MAP, SLASH_NOTIFICATIONS, SLASH_SETTINGS, SLASH_DISPLAY, SLASH_SHORTCUTS, SLASH_SCREENSHOT, SLASH_MISSIONS,
	SLASH_SYSTEMS, SLASH_SYSTEMS_SOUND, SLASH_SYSTEMS_BLUETOOTH, SLASH_SYSTEMS_SCAN, SLASH_SYSTEMS_MUTE };

GtkWidget *slash_ui_new(void (*close)(void),void (*enter_node)(const char *,gboolean,guint64),
	void (*open_view)(enum slash_view),void (*layout_changed)(void),void (*delete)(const char *,int));
void slash_ui_delete_result(const char *);
void slash_ui_node_error(guint64,const char *);
void slash_ui_window_done(guint64);
void slash_ui_window_row(guint32 serial,guint64 id,const char *title,const char *app_id);
void slash_ui_window_end(guint32 serial);
void slash_ui_show(gboolean in_node);
void slash_ui_hide(void);
void slash_ui_nodes(const char *);
void slash_ui_fit(int,int);
gboolean slash_ui_composing(void);

#endif
