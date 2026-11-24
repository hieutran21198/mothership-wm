//go:build linux && cgo && panel

#include "map-relay.h"
#include <math.h>

void map_relay_draw(GtkSnapshot *snap,double hx,double hy,double tx,double ty,double first,double last,
		const struct map_relay_motion *m) {
	if(m->phase!=RELAY_OUT && m->phase!=RELAY_WAIT && m->phase!=RELAY_IN && m->phase!=RELAY_DONE) return;
	double dx=tx-hx,dy=ty-hy,length=hypot(dx,dy);
	if(length<=0 || last-first<25) return;
	const gboolean monitor=m->phase==RELAY_IN || m->phase==RELAY_DONE;
	// Leave both endpoint symbols unobscured; repaint only the returning
	// line segment and the icon's small local bounds, not the static map.
	double along=first+11+(last-first-22)*(monitor?1-m->progress:m->progress);
	double x=hx+dx*along/length,y=hy+dy*along/length;
	if(monitor && m->progress>0) {
		graphene_point_t start=GRAPHENE_POINT_INIT(hx,hy);
		double lead=m->progress>=1?first:along;
		graphene_rect_t solid=GRAPHENE_RECT_INIT(lead,-.65,last-lead,1.3);
		const GdkRGBA ink={.40,.84,.89,.94};
		gtk_snapshot_save(snap);gtk_snapshot_translate(snap,&start);
		gtk_snapshot_rotate(snap,atan2(dy,dx)*180/G_PI);
		gtk_snapshot_append_color(snap,&ink,&solid);
		gtk_snapshot_restore(snap);
	}
	graphene_rect_t tile=GRAPHENE_RECT_INIT(x-13,y-13,26,26);
	cairo_t *cr=gtk_snapshot_append_cairo(snap,&tile);
	cairo_translate(cr,x,y);
	cairo_arc(cr,0,0,11,0,2*G_PI);
	cairo_set_source_rgba(cr,.04,.11,.16,.97);cairo_fill(cr);
	cairo_set_source_rgba(cr,.56,.96,.92,1);
	cairo_set_line_width(cr,1.4);
	cairo_set_line_join(cr,CAIRO_LINE_JOIN_ROUND);
	cairo_set_line_cap(cr,CAIRO_LINE_CAP_ROUND);
	if(monitor) {
		cairo_rectangle(cr,-6,-5,12,8);
		cairo_move_to(cr,0,3);cairo_line_to(cr,0,6);
		cairo_move_to(cr,-3,6);cairo_line_to(cr,3,6);
		cairo_stroke(cr);
	} else {
		for(int r=4;r<=8;r+=4) {
			cairo_new_sub_path(cr);cairo_arc(cr,0,4,r,-.82*G_PI,-.18*G_PI);
		}
		cairo_stroke(cr);
		cairo_arc(cr,0,4,1.45,0,2*G_PI);cairo_fill(cr);
	}
	cairo_destroy(cr);
}
