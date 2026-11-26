#ifndef MOTHERSHIP_DISPLAY_SETTINGS_H
#define MOTHERSHIP_DISPLAY_SETTINGS_H
#include <gtk/gtk.h>

GtkWidget *display_settings_new(void (*close)(void));
void display_settings_open(void);
void display_settings_available(gboolean available);
void display_settings_bind_link(GtkWidget *);
void display_settings_current(const char *,const char *,int,int,int,int);
void display_settings_mode(int,int,int);
void display_settings_done(void);
void display_settings_result(unsigned,const char *);

#endif
