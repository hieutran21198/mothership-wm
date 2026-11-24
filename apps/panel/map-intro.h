#ifndef MOTHERSHIP_MAP_INTRO_H
#define MOTHERSHIP_MAP_INTRO_H
#include <gtk/gtk.h>

enum { MAP_INTRO_FRAME_US=200000, MAP_INTRO_BASE_SCAN_US=450000,
	MAP_INTRO_MAX_SCAN_US=560000, MAP_INTRO_BASE_W=1600,
	MAP_INTRO_ICON_US=90000, MAP_INTRO_LABEL_US=100000,
	MAP_INTRO_REVEAL_US=MAP_INTRO_ICON_US+MAP_INTRO_LABEL_US };

struct map_intro_beats { double icon,label; };

static inline struct map_intro_beats map_intro_beats_at(gint64 since_scan) {
	return (struct map_intro_beats){
		CLAMP(since_scan/(double)MAP_INTRO_ICON_US,0,1),
		CLAMP((since_scan-MAP_INTRO_ICON_US)/(double)MAP_INTRO_LABEL_US,0,1),
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
// Subtract a reserved rectangle from a strip; the remainder has ≤4 pieces.
static inline int map_radar_visible_rects(graphene_rect_t area,const graphene_rect_t *exclude,
		graphene_rect_t pieces[4]) {
	graphene_rect_t overlap;
	if(!exclude || !graphene_rect_intersection(&area,exclude,&overlap) ||
			overlap.size.width<=0 || overlap.size.height<=0) {
		pieces[0]=area;return 1;
	}
	double x=area.origin.x,y=area.origin.y,w=area.size.width,h=area.size.height;
	double l=overlap.origin.x,t=overlap.origin.y,r=l+overlap.size.width,b=t+overlap.size.height;
	int n=0;
	if(t>y) pieces[n++]=GRAPHENE_RECT_INIT(x,y,w,t-y);
	if(l>x) pieces[n++]=GRAPHENE_RECT_INIT(x,t,l-x,b-t);
	if(r<x+w) pieces[n++]=GRAPHENE_RECT_INIT(r,t,x+w-r,b-t);
	if(b<y+h) pieces[n++]=GRAPHENE_RECT_INIT(x,b,w,y+h-b);
	return n;
}

GdkRGBA map_radar_lock_color(gint64 elapsed,const GdkRGBA *);

void map_frame_snapshot(GtkSnapshot *,graphene_rect_t,const GdkRGBA *);
void map_intro_snapshot(GtkSnapshot *,graphene_rect_t,gint64 elapsed,gint64 scan_us,const GdkRGBA *border,const graphene_rect_t *exclude);
#endif
