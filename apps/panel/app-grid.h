#ifndef MOTHERSHIP_APP_GRID_H
#define MOTHERSHIP_APP_GRID_H
#include <gtk/gtk.h>
#include <gio/gdesktopappinfo.h>

GtkWidget *app_grid_new(void (*dismissed)(void));
void app_grid_resize(int width,int height);
void app_grid_show(gboolean in_node);
void app_grid_hide(gboolean instant);
void app_grid_set_pins(const char *);
GPtrArray *app_catalog_snapshot(void);
gboolean app_catalog_launch(GDesktopAppInfo *);
gboolean app_catalog_launch_native(GDesktopAppInfo *,const char *display,
	const char *bus,const char *token,int *pid,GError **error);
#endif
