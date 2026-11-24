#ifndef MOTHERSHIP_RETAINED_PAINT_H
#define MOTHERSHIP_RETAINED_PAINT_H
#include <gtk/gtk.h>
GtkWidget *retained_wallpaper_new(cairo_surface_t *,gboolean contain,GtkWidget *fallback);
void retained_wallpaper_source(GtkWidget *,cairo_surface_t *,gboolean contain);
void retained_wallpaper_update(GtkWidget *,gboolean frost,int margin);
GtkWidget *retained_grid_new(gboolean *ready,GtkWidget *fallback);
void retained_grid_update(GtkWidget *,GdkRGBA grid,GdkRGBA tint,int margin,int padding);
GdkTexture *map_gl_frost_texture(GdkGLContext *, cairo_surface_t *);
GdkGLContext *map_gpu_context(GtkWidget *,const char *part);
#endif
