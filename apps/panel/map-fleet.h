#pragma once

#include <math.h>

struct fleet_point { double x,y; };

// Top-down contours from the approved fleet sheet. Coordinates use the
// same units, so the silhouettes scale together while keeping their proportions.
static const struct fleet_point fleet_mothership[] = {
	{0,-13},{4,-9},{5.6,-3.8},{11.3,-1.3},{16.5,3.6},{16.5,8.1},
	{10.6,7.3},{8,9.5},{5.6,7.8},{4.1,11.6},{2.6,13},{1.8,11.6},
	{-1.8,11.6},{-2.6,13},{-4.1,11.6},{-5.6,7.8},{-8,9.5},{-10.6,7.3},
	{-16.5,8.1},{-16.5,3.6},{-11.3,-1.3},{-5.6,-3.8},{-4,-9}
};
static const struct fleet_point fleet_node[] = {
	{0,-12.6},{4.3,-7.8},{5,-2.6},{10.6,3.4},{10.6,7.2},{6,6.3},{3.5,3.8},
	{2.6,8.9},{1.8,9.8},{-1.8,9.8},{-2.6,8.9},{-3.5,3.8},{-6,6.3},
	{-10.6,7.2},{-10.6,3.4},{-5,-2.6},{-4.3,-7.8}
};
#define FLEET_COUNT(points) (sizeof(points)/sizeof(*(points)))
#define FLEET_HULL_HALF_WIDTH 16.5
#define FLEET_HULL_HALF_HEIGHT 13.0
#define FLEET_NODE_HALF_HEIGHT 12.6
#define FLEET_FOCUS_SCALE 1.25
#define FLEET_SHIP_EDGE 1.30
#define FLEET_FOCUS_EDGE 1.15
#define FLEET_ORIGIN_X 94.0
#define FLEET_ORIGIN_Y 252.0

// Far exit along a unit ray from the anchor, including the exact normal
// clearance around the painted contour. A padded segment is a strip and
// two round caps. Test both parallel sides and every vertex cap so the
// return point also clears concave trailing corners and oblique approaches.
static inline double fleet_route_exit(const struct fleet_point *p,unsigned n,
	double scale,double ux,double uy,double pad) {
	double far=0;
	for(unsigned i=0;i<n;++i) {
		double ax=p[i].x*scale,ay=p[i].y*scale;
		double bx=p[(i+1)%n].x*scale,by=p[(i+1)%n].y*scale;
		double cross=ax*uy-ay*ux, v=pad*pad-cross*cross;
		if(v>=0) far=fmax(far,ax*ux+ay*uy+sqrt(v));
		double dx=bx-ax,dy=by-ay,len=hypot(dx,dy);
		if(len<=0) continue;
		double nx=-dy/len,ny=dx/len,den=nx*ux+ny*uy;
		if(fabs(den)<1e-9) continue;
		for(int sign=-1;sign<=1;sign+=2) {
			double t=(nx*ax+ny*ay+sign*pad)/den;
			double on=((t*ux-ax)*dx+(t*uy-ay)*dy)/len;
			if(on>=0 && on<=len) far=fmax(far,t);
		}
	}
	return far;
}
