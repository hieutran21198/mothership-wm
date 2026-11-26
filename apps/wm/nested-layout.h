#ifndef MOTHERSHIP_NESTED_LAYOUT_H
#define MOTHERSHIP_NESTED_LAYOUT_H

#include <stdbool.h>

#define NODE_WINDOWS 8
#define HOME_WINDOWS 2
#define WM_PACKET_MAX 512
#define FLOAT_BAR 20
#define FLOAT_GRIP 16

enum node_layout { TILING, STACK, FLOAT, FLOW, FLOW_HORIZONTAL, FLOW_VERTICAL, STAGE };
enum { FLOW_GAP=18 };

static inline bool flow_layout(enum node_layout layout) {
	return layout==FLOW || layout==FLOW_HORIZONTAL || layout==FLOW_VERTICAL;
}

static inline bool flow_vertical(enum node_layout layout,int width,int height) {
	return layout==FLOW_VERTICAL || (layout==FLOW && height>width);
}
struct rect { int x, y, width, height; };

enum { STAGE_EDGE=12 };

static inline struct rect stage_rect(int width,int height) {
	int margin=width>=180?STAGE_EDGE:0;
	int content_h=height>4*STAGE_EDGE ? height-STAGE_EDGE : height;
	return (struct rect){margin,0,width-2*margin,content_h};
}

static inline int clamp_int(int v, int lo, int hi) {
	return v < lo ? lo : v > hi ? hi : v;
}

static inline struct rect tile_rect(int i, int n, int width, int height) {
	int cols = n == 1 ? 1 : n <= 4 ? 2 : 3;
	int rows = (n + cols - 1) / cols;
	int row = i / cols, col = i % cols;
	int remaining = n - row * cols;
	int row_cols = remaining < cols ? remaining : cols;
	int x = col * width / row_cols, y = row * height / rows;
	return (struct rect){x, y, (col + 1) * width / row_cols - x,
		(row + 1) * height / rows - y};
}

static inline struct rect float_rect(struct rect r, int width, int height) {
	int min_w = width < 180 ? width : 180;
	int min_h = height < 100 ? height : 100;
	r.width = clamp_int(r.width, min_w, width);
	r.height = clamp_int(r.height, min_h, height);
	r.x = clamp_int(r.x, 0, width - r.width);
	r.y = clamp_int(r.y, 0, height - r.height);
	return r;
}

static inline struct rect float_start(int slot, int width, int height) {
	int w = width * 2 / 3, h = height * 2 / 3;
	int x = (width - w) * (slot + 1) / (NODE_WINDOWS + 1);
	int y = (height - h) * (slot + 1) / (NODE_WINDOWS + 1);
	return float_rect((struct rect){x, y, w, h}, width, height);
}

#endif
