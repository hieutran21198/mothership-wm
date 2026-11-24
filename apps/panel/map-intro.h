#ifndef MOTHERSHIP_MAP_INTRO_H
#define MOTHERSHIP_MAP_INTRO_H
#include <gtk/gtk.h>

enum { MAP_INTRO_FRAME_US=200000, MAP_INTRO_BASE_SCAN_US=450000,
	MAP_INTRO_MAX_SCAN_US=560000, MAP_INTRO_BASE_W=1600,
	MAP_INTRO_ICON_US=90000, MAP_INTRO_LEADER_US=70000, MAP_INTRO_LABEL_US=100000,
	MAP_INTRO_REVEAL_US=MAP_INTRO_ICON_US+MAP_INTRO_LEADER_US+MAP_INTRO_LABEL_US };

struct map_intro_beats { double icon,leader,label; };

static inline struct map_intro_beats map_intro_beats_at(gint64 since_scan) {
	return (struct map_intro_beats){
		CLAMP(since_scan/(double)MAP_INTRO_ICON_US,0,1),
		CLAMP((since_scan-MAP_INTRO_ICON_US)/(double)MAP_INTRO_LEADER_US,0,1),
		CLAMP((since_scan-MAP_INTRO_ICON_US-MAP_INTRO_LEADER_US)/(double)MAP_INTRO_LABEL_US,0,1),
	};
}

// The logical (not backing-buffer) width selects one duration at opening.
// No screen DPI, refresh rate, FPS, or number of callbacks belongs here.
static inline gint64 map_intro_scan_us(int logical_width) {
	gint64 extra=MAX(0,logical_width-MAP_INTRO_BASE_W)*100LL;
	return MIN(MAP_INTRO_BASE_SCAN_US+extra,MAP_INTRO_MAX_SCAN_US);
}

enum { MAP_RADAR_TIP=2, MAP_RADAR_STOPS=4 };
struct map_radar_stop { float at; GdkRGBA color; };
extern const struct map_radar_stop map_radar_stops[MAP_RADAR_STOPS];
struct map_radar_sample { double left,top,right,bottom,cut,tail,head,width; };
struct map_radar_sample map_radar_sample_at(graphene_rect_t,gint64 elapsed,gint64 duration);
GdkRGBA map_radar_lock_color(gint64 elapsed,const GdkRGBA *);

void map_frame_snapshot(GtkSnapshot *,graphene_rect_t,const GdkRGBA *);
void map_intro_snapshot(GtkSnapshot *,graphene_rect_t,gint64 elapsed,gint64 scan_us,const GdkRGBA *border);
#endif
