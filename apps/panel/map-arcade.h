#pragma once
#include <gtk/gtk.h>


typedef void (*map_arcade_ship_painter)(cairo_t *,double,double,double,gboolean,gboolean,gboolean,gboolean);

GtkWidget *map_arcade_new(map_arcade_ship_painter paint);
void map_arcade_layout(GtkWidget *,double,double,double,double);
void map_arcade_start(GtkWidget *);
void map_arcade_stop(GtkWidget *);
void map_arcade_weapon(GtkWidget *,gboolean plasma,gboolean pressed);
void map_arcade_aim(GtkWidget *,gboolean,double,double);
void map_arcade_focus(GtkWidget *,gboolean);
unsigned map_arcade_key_bit(guint);
gboolean map_arcade_key(GtkWidget *,guint,gboolean);
