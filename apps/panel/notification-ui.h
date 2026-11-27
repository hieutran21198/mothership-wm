//go:build linux && cgo && panel

#ifndef MOTHERSHIP_NOTIFICATION_UI_H
#define MOTHERSHIP_NOTIFICATION_UI_H
#include <gtk/gtk.h>
void notice_init(GtkOverlay *,void (*region)(void));
GtkWidget *notice_log_widget(void);
int notice_log_visible(void);
void notice_unread_count(int);
GtkWidget *notice_close_target(void);
void notice_log_toggle(void);
void notice_log_toggle_for(const char *,const char *);
int notice_source_unread(const char *);
void notice_set_source_count(const char *,int);
gboolean notice_log_close(void);
// Instant skips the exit fade when another shell view takes over.
gboolean notice_log_dismiss(gboolean instant);
void notice_layout(int width,int height,int bell_center,int dock_gap);
void notice_log_begin(void);
void notice_log_group(const char *source,const char *label,int unread);
void notice_log_row(guint id,const char *app,const char *title,const char *body,const char *time,int unread);
void notice_log_end(void);
void notice_context(const char *node,int mode);
void notice_show(const char *node,const char *source,const char *app,const char *title,const char *body);
void notice_toast_close(void);
#endif
