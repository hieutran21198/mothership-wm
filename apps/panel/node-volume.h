#ifndef MOTHERSHIP_NODE_VOLUME_H
#define MOTHERSHIP_NODE_VOLUME_H
#include <gtk/gtk.h>
void node_volume_init(GtkOverlay *,void (*region)(void));
GtkWidget *node_volume_widget(void);
gboolean node_volume_close(void);
void node_volume_open(const char *node,const char *label);
void node_volume_context(const char *node);
void node_volume_layout(int width,int height,int anchor,int bottom);
void node_volume_post(const char *node,const char *rows,int mute,const char *message);
#endif
