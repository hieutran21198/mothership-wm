#ifndef MOTHERSHIP_MAP_FRAME_H
#define MOTHERSHIP_MAP_FRAME_H
#include <stdbool.h>

enum { MAP_FRAME_CUT=12 };

static inline int map_frame_effective_margin(int w,int h,int requested) {
	int limit=((w<h?w:h)-32)/2;
	if (limit<0) return 0;
	return requested<limit?requested:limit;
}

static inline bool map_frame_contains(int w,int h,int requested,double x,double y) {
	if (w<120 || h<100) return false;
	double m=map_frame_effective_margin(w,h,requested)+.5;
	double l=x-m,t=y-m,r=w-m-x,b=h-m-y;
	return l>=0 && t>=0 && r>=0 && b>=0 &&
		l+t>=MAP_FRAME_CUT && r+t>=MAP_FRAME_CUT &&
		l+b>=MAP_FRAME_CUT && r+b>=MAP_FRAME_CUT;
}
#endif
