//go:build linux && cgo && panel

#include "map-intro.h"
#include "shell-paint.h"
#include <math.h>

static void solid(GtkSnapshot *snap,const GdkRGBA *color,double x,double y,double w,double h) {
	if (w<=0 || h<=0) return;
	graphene_rect_t box=GRAPHENE_RECT_INIT(x,y,w,h);
	gtk_snapshot_append_color(snap,color,&box);
}

static void diagonal(GtkSnapshot *snap,const GdkRGBA *color,double x,double y,double size,int rise,double weight) {
	if (size<=0) return;
	graphene_point_t origin=GRAPHENE_POINT_INIT(x,y);
	gtk_snapshot_save(snap);
	gtk_snapshot_translate(snap,&origin);
	gtk_snapshot_rotate(snap,45*rise);
	solid(snap,color,0,-weight*.5,size*G_SQRT2,weight);
	gtk_snapshot_restore(snap);
}

static void frame_open(GtkSnapshot *snap,double x,double y,double w,double h,double progress,const GdkRGBA *border) {
	double t=CLAMP(progress,0,1);
	t=t*t*(3-2*t);
	double width=w*(.30+.70*t),height=h*t;
	const double weight=MAP_PERIMETER_WIDTH, edge=.5+MAP_PERIMETER_INSET;
	double l=x+(w-width)*.5+edge,r=l+width-2*edge;
	double top=y+(h-height)*.5+edge,bottom=top+height-2*edge;
	// Begin cyan, then resolve to the chosen border before the corner lock.
	const GdkRGBA tint={
		.45+(border->red-.45)*t,.83+(border->green-.83)*t,.91+(border->blue-.91)*t,
		MIN(1,border->alpha*(1.15-.15*t)),
	};
	if (height<2) {
		solid(snap,&tint,l,y+h*.5-weight*.5,r-l,weight);
		return;
	}
	double c=MIN(map_frame_cut_at(MAP_PERIMETER_INSET),(bottom-top)*.5);
	// Eight thin primitives, not a stroke with full-map mask bounds.
	solid(snap,&tint,l+c,top-weight*.5,r-l-2*c,weight);
	solid(snap,&tint,l+c,bottom-weight*.5,r-l-2*c,weight);
	solid(snap,&tint,l-weight*.5,top+c,weight,bottom-top-2*c);
	solid(snap,&tint,r-weight*.5,top+c,weight,bottom-top-2*c);
	diagonal(snap,&tint,l,top+c,c,-1,weight);
	diagonal(snap,&tint,r-c,top,c,1,weight);
	diagonal(snap,&tint,l,bottom-c,c,1,weight);
	diagonal(snap,&tint,r-c,bottom,c,-1,weight);
}

static void steady_frame_piece(GtkSnapshot *s,graphene_rect_t frame,graphene_rect_t bounds,
		const GdkRGBA *border) {
	if (bounds.size.width<=0 || bounds.size.height<=0) return;
	cairo_t *cr=gtk_snapshot_append_cairo(s,&bounds);
	map_frame_outline(cr,frame.origin.x,frame.origin.y,frame.size.width,frame.size.height,MAP_PERIMETER_INSET);
	cairo_set_source_rgba(cr,border->red,border->green,border->blue,border->alpha);
	cairo_set_line_width(cr,MAP_PERIMETER_WIDTH);
	cairo_stroke(cr);
	cairo_destroy(cr);
}
void map_frame_snapshot(GtkSnapshot *snap,graphene_rect_t frame,const GdkRGBA *border) {
	double l=frame.origin.x,r=l+frame.size.width;
	double t=frame.origin.y,b=t+frame.size.height;
	// Disjoint integer-aligned boxes; no full-view Cairo raster target.
	double xl=l+16,xr=r-16,yt=t+16,yb=b-16;
	steady_frame_piece(snap,frame,GRAPHENE_RECT_INIT(xl,t-2,xr-xl,4),border);
	steady_frame_piece(snap,frame,GRAPHENE_RECT_INIT(xl,b-2,xr-xl,4),border);
	steady_frame_piece(snap,frame,GRAPHENE_RECT_INIT(l-2,yt,4,yb-yt),border);
	steady_frame_piece(snap,frame,GRAPHENE_RECT_INIT(r-2,yt,4,yb-yt),border);
	steady_frame_piece(snap,frame,GRAPHENE_RECT_INIT(l-2,t-2,18,18),border);
	steady_frame_piece(snap,frame,GRAPHENE_RECT_INIT(xr,t-2,18,18),border);
	steady_frame_piece(snap,frame,GRAPHENE_RECT_INIT(l-2,yb,18,18),border);
	steady_frame_piece(snap,frame,GRAPHENE_RECT_INIT(xr,yb,18,18),border);
}

static void lock_corners(GtkSnapshot *snap,double l,double t,double r,double b,GdkRGBA tint) {
	const double c=map_frame_cut_at(MAP_PERIMETER_INSET), leg=8, weight=MAP_PERIMETER_WIDTH;
	for(int i=0;i<4;i++) {
		double x=i%2?r:l,y=i/2?b:t;
		solid(snap,&tint,i%2?x-c-leg:x+c,y-weight*.5,leg,weight);
		solid(snap,&tint,x-weight*.5,i/2?y-c-leg:y+c,weight,leg);
		diagonal(snap,&tint,i%2?x-c:x,i%2?y:y+(i/2?-c:c),c,(i%2) != (i/2)?1:-1,weight);
	}
}

const struct map_radar_stop map_radar_stops[MAP_RADAR_STOPS]={
	{0,{.24,.68,.82,0}},{.82,{.24,.68,.82,.067}},
	{.98,{.47,.86,.95,.23}},{1,{.47,.86,.95,0}},
};

struct map_radar_sample map_radar_sample_at(graphene_rect_t frame,gint64 elapsed,gint64 duration) {
	double x=frame.origin.x,y=frame.origin.y,w=frame.size.width,h=frame.size.height;
	double width=MIN(112,w*.16),head=x+(w+width)*CLAMP(elapsed/(double)duration,0,1);
	return (struct map_radar_sample){x+.5,y+.5,x+w-.5,y+h-.5,MAP_FRAME_CUT,head-width,head+MAP_RADAR_TIP,width+MAP_RADAR_TIP};
}

GdkRGBA map_radar_lock_color(gint64 elapsed,const GdkRGBA *border) {
	return (GdkRGBA){.47,.86,.95,.32*MIN(1,border->alpha/.62)*MAX(0,1-elapsed/100000.)};
}

static void band(GtkSnapshot *snap,double x,double y,double r,double b,double tail,double head) {
	x=MAX(x,tail);r=MIN(r,head);
	if (r<=x || b<=y) return;
	graphene_rect_t beam=GRAPHENE_RECT_INIT(x,y,r-x,b-y);
	graphene_point_t from=GRAPHENE_POINT_INIT(tail,y),to=GRAPHENE_POINT_INIT(head,y);
	GskColorStop stops[MAP_RADAR_STOPS];
	for (int i=0;i<MAP_RADAR_STOPS;i++)
		stops[i]=(GskColorStop){map_radar_stops[i].at,map_radar_stops[i].color};
	gtk_snapshot_append_linear_gradient(snap,&beam,&from,&to,stops,G_N_ELEMENTS(stops));
}

static void corner(GtkSnapshot *snap,double x,double y,int i,double c,double tail,double head) {
	if (head<=x || tail>=x+c) return;
	GskPathBuilder *builder=gsk_path_builder_new();
	gsk_path_builder_move_to(builder,x+(i%2?0:c),y+(i/2?0:c));
	gsk_path_builder_line_to(builder,x+(i%2?c:0),y+(i/2?0:c));
	gsk_path_builder_line_to(builder,x+(i%2?0:c),y+(i/2?c:0));
	gsk_path_builder_close(builder);
	GskPath *path=gsk_path_builder_free_to_path(builder);
	gtk_snapshot_push_fill(snap,path,GSK_FILL_RULE_WINDING);
	band(snap,x,y,x+c,y+c,tail,head);
	gtk_snapshot_pop(snap);
	gsk_path_unref(path);
}

void map_intro_snapshot(GtkSnapshot *snap,graphene_rect_t frame,gint64 elapsed,gint64 scan_us,const GdkRGBA *border) {
	double x=frame.origin.x,y=frame.origin.y,w=frame.size.width,h=frame.size.height;
	if (elapsed<MAP_INTRO_FRAME_US) {
		frame_open(snap,x,y,w,h,(double)elapsed/MAP_INTRO_FRAME_US,border);
		return;
	}
	gint64 passed=elapsed-MAP_INTRO_FRAME_US;
	if (passed>=scan_us) return;
	struct map_radar_sample s=map_radar_sample_at(frame,passed,scan_us);
	double l=s.left,r=s.right,top=s.top,b=s.bottom,c=s.cut;
	band(snap,l,top+c,r,b-c,s.tail,s.head);
	band(snap,l+c,top,r-c,top+c,s.tail,s.head);
	band(snap,l+c,b-c,r-c,b,s.tail,s.head);
	for(int i=0;i<4;i++)
		corner(snap,i%2?r-c:l,i/2?b-c:top,i,c,s.tail,s.head);
	GdkRGBA lock=map_radar_lock_color(passed,border);
	if (lock.alpha>0) {
		double shift=-MAP_PERIMETER_INSET;
		lock_corners(snap,l-shift,top-shift,r+shift,b+shift,lock);
	}
}
