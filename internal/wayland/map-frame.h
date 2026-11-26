#ifndef MOTHERSHIP_MAP_FRAME_H
#define MOTHERSHIP_MAP_FRAME_H
#include <stdbool.h>

enum {
	MAP_FRAME_CUT=12,
	MAP_PANEL_GAP=18,
	MAP_ARCADE_FRAME_INSET=6,
	SHELL_DOCK_HEIGHT=62,
	SHELL_DOCK_BOTTOM=22,
	SHELL_NODE_GUIDE_HEIGHT=23,
	SHELL_NODE_GUIDE_GAP=16,
	SHELL_PREVIEW_PITCH=78,
	SHELL_PREVIEW_REST_INSET=9,
	SHELL_PREVIEW_BOTTOM=3,
	MAP_DOCK_CLEARANCE=16,
};

struct map_frame_box { double x,y,width,height; };

// The raised center of the Node rail begins at local y=0.
static inline int shell_node_dock_top(int height) {
	int top=height-SHELL_DOCK_HEIGHT-SHELL_DOCK_BOTTOM;
	return top>0?top:0;
}

static inline bool map_frame_available(int w,int h) {
	return w>=120 && h>=SHELL_DOCK_HEIGHT+SHELL_DOCK_BOTTOM+MAP_DOCK_CLEARANCE+34;
}

// One shell layout rule for the settled frame, retained clips and WM input.
// Only the lower edge is reserved for the dock. At fractional scale the
// compositor rounds after the logical geometry is settled, not before.
static inline struct map_frame_box map_frame_layout(int w,int h,int requested) {
	int usable_h=h-SHELL_DOCK_HEIGHT-SHELL_DOCK_BOTTOM-MAP_DOCK_CLEARANCE;
	int limit=(w-32)/2;
	if(limit>usable_h-33) limit=usable_h-33;
	int margin=requested<limit?requested:limit;
	if(margin<0) margin=0;
	// map_frame_strokes paints half a logical pixel beyond its nominal
	// box. Align the painted outer edge, not the path or border center.
	double bottom=usable_h-.5;
	return (struct map_frame_box){margin,margin,w-2*margin,bottom-margin};
}

static inline int map_frame_content_padding(struct map_frame_box frame,int requested) {
	double extent=frame.width<frame.height?frame.width:frame.height;
	int limit=extent>16?(int)((extent-16)/2):0;
	return requested<limit?requested:limit;
}

static inline bool map_frame_contains(int w,int h,int requested,double x,double y) {
	if (!map_frame_available(w,h)) return false;
	struct map_frame_box f=map_frame_layout(w,h,requested);
	double l=x-f.x-.5,t=y-f.y-.5,r=f.x+f.width-.5-x,b=f.y+f.height-.5-y;
	return l>=0 && t>=0 && r>=0 && b>=0 &&
		l+t>=MAP_FRAME_CUT && r+t>=MAP_FRAME_CUT &&
		l+b>=MAP_FRAME_CUT && r+b>=MAP_FRAME_CUT;
}
static inline bool map_arcade_contains(int w,int h,int margin,double x,double y) {
	if(!map_frame_available(w,h)) return false;
	struct map_frame_box f=map_frame_layout(w,h,margin);
	double inset=MAP_ARCADE_FRAME_INSET,cut=MAP_FRAME_CUT-inset*(2-1.414213562373095);
	double l=x-f.x-inset,t=y-f.y-inset,r=f.x+f.width-inset-x,b=f.y+f.height-inset-y;
	return l>=0 && t>=0 && r>=0 && b>=0 && l+t>=cut && r+t>=cut && l+b>=cut && r+b>=cut;
}

#endif
