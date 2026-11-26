#ifndef MOTHERSHIP_FLOW_GROUP_H
#define MOTHERSHIP_FLOW_GROUP_H
#include "nested-layout.h"

enum { FLOW_GROUP_MAX=4, FLOW_GROUP_GAP=16, FLOW_GROUP_MIN_WIDTH=1024,
	FLOW_GROUP_MIN_HEIGHT=560, FLOW_GROUP_GAP_MAX=72 };

static inline void flow_group_tiles(struct rect r,int n,int sx,int sy,int gap,
		int min_w,int min_h,struct rect out[FLOW_GROUP_MAX]) {
	out[0]=r;
	if(n<2 || n>FLOW_GROUP_MAX) return;
	int gx=clamp_int(gap,0,r.width>2*min_w?r.width-2*min_w:0);
	int area_w=r.width-gx, limit_w=area_w/2<min_w?area_w/2:min_w;
	int left=clamp_int(area_w*sx/100,limit_w,area_w-limit_w);
	int right=area_w-left, rx=r.x+left+gx;
	int gy=clamp_int(gap,0,r.height>2*min_h?r.height-2*min_h:0);
	int area_h=r.height-gy, limit_h=area_h/2<min_h?area_h/2:min_h;
	int top=clamp_int(area_h*sy/100,limit_h,area_h-limit_h);
	int bot=area_h-top, by=r.y+top+gy;
	out[0]=(struct rect){r.x,r.y,left,n==4?top:r.height};
	out[1]=(struct rect){rx,r.y,right,n==2?r.height:top};
	if(n==3) out[2]=(struct rect){rx,by,right,bot};
	if(n==4) {
		out[2]=(struct rect){r.x,by,left,bot};
		out[3]=(struct rect){rx,by,right,bot};
	}
}
#endif
