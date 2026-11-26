#ifndef MOTHERSHIP_WALLPAPER_VIDEO_H
#define MOTHERSHIP_WALLPAPER_VIDEO_H
#include <gtk/gtk.h>

struct wallpaper_video;
struct wallpaper_video *wallpaper_video_prepare(const char *path);
void wallpaper_video_attach(struct wallpaper_video *,GtkPicture *,void (*failed)(void));
void wallpaper_video_free(struct wallpaper_video *);
#endif
