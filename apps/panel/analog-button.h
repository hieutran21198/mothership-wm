#ifndef MOTHERSHIP_ANALOG_BUTTON_H
#define MOTHERSHIP_ANALOG_BUTTON_H
#include <gtk/gtk.h>

// Rest, hover, focus, press, on and disabled are independent from layout.
void analog_button_paint(cairo_t *,double x,double y,double w,double h,GtkStateFlags);
void analog_button_style(GtkWidget *button,int pad_x,int pad_y);
void analog_button_on(GtkWidget *button,gboolean on);
void analog_button_label(GtkWidget *button,const char *label);
void analog_button_icon(GtkWidget *button,const char *name);
#endif
