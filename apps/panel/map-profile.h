#ifndef MOTHERSHIP_MAP_PROFILE_H
#define MOTHERSHIP_MAP_PROFILE_H
#include <gtk/gtk.h>

struct profile_rect { double x,y,w,h; };
struct profile_layout { struct profile_rect card,fly; };

enum { PROFILE_CORNER=10, PROFILE_FLY_CORNER=8, PROFILE_RIM_GAP=6, PROFILE_PADDING=16, PROFILE_ACTION=40 };

struct profile_data {
	char *description;
	gint64 created_at,last_active;
	gboolean empty;
	int shortcut;
	char **app_ids;
	GPtrArray *icons;
	GskRenderNode *paint;
	double paint_width, paint_height;
	int paint_scale;
	gint64 age_min, activity_min;
};

void profile_data_free(gpointer);
GHashTable *profile_parse(const char *);
void profile_draw_chrome(cairo_t *, struct profile_rect, gboolean flat);
double profile_height(const struct profile_data *);
void profile_snapshot(GtkSnapshot *, GtkWidget *, struct profile_layout, const char *, struct profile_data *);

static inline gboolean profile_contains(struct profile_rect r,double cut,double x,double y) {
	return x>=r.x && x<=r.x+r.w && y>=r.y && y<=r.y+r.h &&
		x-r.x+y-r.y>=cut && r.x+r.w-x+y-r.y>=cut &&
		x-r.x+r.y+r.h-y>=cut && r.x+r.w-x+r.y+r.h-y>=cut;
}

// Use screen coordinates for a fixed-size readable card. Leave extra room at
// all edges so the card cannot tip into the Map's chamfer while clamping.
enum profile_side { PROFILE_RIGHT, PROFILE_LEFT, PROFILE_BELOW, PROFILE_ABOVE, PROFILE_SIDE_COUNT };

static inline gboolean profile_place_on(struct profile_layout *out,struct profile_rect safe,
		double x,double y,double marker_radius,double height,enum profile_side side) {
	const double space=marker_radius+13;
	double width=MIN(374,safe.w);
	if (width<154 || safe.h<height) return FALSE;
	double left,top;
	if (side==PROFILE_RIGHT || side==PROFILE_LEFT) {
		left=side==PROFILE_RIGHT ? x+space : x-space-width;
		top=CLAMP(y-height*.5,safe.y,safe.y+safe.h-height);
		if (left<safe.x || left+width>safe.x+safe.w) return FALSE;
	} else {
		left=CLAMP(x-width*.5,safe.x,safe.x+safe.w-width);
		top=side==PROFILE_BELOW ? y+space : y-space-height;
		if (top<safe.y || top+height>safe.y+safe.h) return FALSE;
	}
	out->card=(struct profile_rect){left,top,width,height};
	out->fly=(struct profile_rect){left+width-PROFILE_PADDING-PROFILE_ACTION,
		top+height-PROFILE_PADDING-PROFILE_ACTION,PROFILE_ACTION,PROFILE_ACTION};
	return TRUE;
}

#endif
