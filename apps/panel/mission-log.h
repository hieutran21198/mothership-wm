//go:build linux && cgo && panel

#ifndef MOTHERSHIP_MISSION_LOG_H
#define MOTHERSHIP_MISSION_LOG_H
#include <gtk/gtk.h>

// The Mission Log overlay on blank Home: one day of time per Node.
void mission_log_init(GtkOverlay *,void (*region)(void));
GtkWidget *mission_log_widget(void);
gboolean mission_log_visible(void);
void mission_log_toggle(void);
gboolean mission_log_dismiss(void);
void mission_log_layout(int width,int height,int center_x,int dock_gap);
// Thread-safe: called from Go with a formatted day or a status line.
void mission_log_post(const char *);
void mission_log_status(const char *);
#endif
