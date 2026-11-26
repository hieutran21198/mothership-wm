#ifndef MOTHERSHIP_SCREENSHOT_H
#define MOTHERSHIP_SCREENSHOT_H

#include <gtk/gtk.h>
#include <stdint.h>

void screenshot_init(GtkWindow *, GtkOverlay *, GtkWidget *, GtkWidget *,
    void (*prepare)(gboolean invoking_menu), void (*region_changed)(void), void (*notice)(const char *));
gboolean screenshot_visible(void);
gboolean panel_screenshot_enabled(void);
gboolean screenshot_key(guint, GdkModifierType);
void screenshot_open_from_menu(void);
void panel_screenshot_available(gboolean);
void panel_screenshot_open(void);
void panel_screenshot_target(uint32_t, uint32_t, int, int, int, int, const char *);
void panel_screenshot_frame(int, int, int, int, int, int);
void panel_screenshot_image(int, int, int, int);
void panel_screenshot_problem(const char *, gboolean);

#endif
