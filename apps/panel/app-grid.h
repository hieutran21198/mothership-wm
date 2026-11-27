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
// Loadouts: find an installed entry, and the unique entry for an app ID.
GDesktopAppInfo *app_catalog_find(const char *id,const char **executable,int *dbus);
char *app_catalog_desktop_id_for(const char *app_id);
char *app_catalog_name(const char *id);
gboolean app_catalog_launch_native(GDesktopAppInfo *,const char *display,
	const char *bus,const char *token,int *pid,GError **error);
#endif
