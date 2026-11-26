#ifndef MOTHERSHIP_SLASH_UI_H
#define MOTHERSHIP_SLASH_UI_H
#include <gtk/gtk.h>

enum slash_view { SLASH_MAP, SLASH_NOTIFICATIONS, SLASH_SETTINGS, SLASH_DISPLAY, SLASH_SHORTCUTS };

GtkWidget *slash_ui_new(void (*close)(void),void (*enter_node)(const char *,gboolean,guint64),
	void (*open_view)(enum slash_view),void (*layout_changed)(void),void (*delete)(const char *,int));
void slash_ui_delete_result(const char *);
void slash_ui_node_error(guint64,const char *);
void slash_ui_show(gboolean in_node);
void slash_ui_hide(void);
void slash_ui_nodes(const char *);
void slash_ui_fit(int,int);
gboolean slash_ui_composing(void);

#endif
