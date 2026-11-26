#ifndef MOTHERSHIP_SYSTEM_TRAY_H
#define MOTHERSHIP_SYSTEM_TRAY_H
#include <gtk/gtk.h>

void system_tray_init(GtkOverlay *root,void (*updated)(void));
void system_tray_connect(const char *address);
void system_tray_disconnect(void);
gboolean system_tray_has_items(void);
GtkWidget *system_tray_widget(void);
GtkWidget *system_tray_trigger(void);
void system_tray_place(int w,int h,int anchor,int bottom,gboolean live);
gboolean system_tray_open(GtkWidget *from);
gboolean system_tray_close(void);

#endif
