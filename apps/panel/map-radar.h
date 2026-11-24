#ifndef MOTHERSHIP_MAP_RADAR_H
#define MOTHERSHIP_MAP_RADAR_H

#include <gtk/gtk.h>

void map_radar_init(GtkWindow *hud);
gboolean map_radar_begin(GtkWindow *hud);
gboolean map_radar_step(GtkWindow *hud,graphene_rect_t frame,gint64 elapsed,gint64 duration,
	const GdkRGBA *border,const graphene_rect_t *exclude);
void map_radar_end(void);

#endif
