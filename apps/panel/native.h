#ifndef MOTHERSHIP_PANEL_H
#define MOTHERSHIP_PANEL_H

int panel_init(void);
void panel_run(void);
void panel_quit(void);
void panel_post(const char *view, const char *message, const char *domains,
	const char *windows, const char *node, const char *mode, int enabled, int overview);

#endif
