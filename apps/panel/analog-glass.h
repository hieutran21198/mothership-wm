#ifndef MOTHERSHIP_ANALOG_GLASS_H
#define MOTHERSHIP_ANALOG_GLASS_H

#include <gtk/gtk.h>

GtkWidget *analog_glass_new(void);
void analog_glass_set_scene_backdrop(GtkWidget *,int slot);
// Item chrome and pointer hover only. The caller owns content and selection.
GtkWidget *analog_item_new(GtkWidget *content);
void analog_item_select(GtkWidget *item,gboolean selected);
void analog_item_set_inherit_backdrop(GtkWidget *);
void analog_glass_wallpaper_changed(void);
void wallpaper_analog_backdrop(GtkSnapshot *,int,int,double,double,int,int,GskPath *);

#endif
