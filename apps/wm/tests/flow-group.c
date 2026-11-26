#include "../flow-group.h"
#include <assert.h>

int main(void) {
	struct rect r={12,12,1576,795};
	struct rect tiles[FLOW_GROUP_MAX];
	flow_group_tiles(r,2,60,50,16,220,140,tiles);
	assert(tiles[0].width==936 && tiles[0].height==795);
	assert(tiles[1].x-tiles[0].x-tiles[0].width==16);
	assert(tiles[1].x+tiles[1].width==r.x+r.width);
	flow_group_tiles(r,3,60,55,16,220,140,tiles);
	assert(tiles[0].height==r.height);
	assert(tiles[1].x==tiles[2].x && tiles[1].width==tiles[2].width);
	assert(tiles[2].y-tiles[1].y-tiles[1].height==16);
	flow_group_tiles(r,4,50,50,0,220,140,tiles);
	assert(tiles[2].x==tiles[0].x && tiles[3].x==tiles[1].x);
	assert(tiles[2].y==tiles[0].y+tiles[0].height);
	assert(tiles[3].y+tiles[3].height==r.y+r.height);
	flow_group_tiles((struct rect){0,0,460,300},4,75,25,72,220,140,tiles);
	assert(tiles[0].width>=220 && tiles[1].width>=220);
	assert(tiles[0].height>=140 && tiles[3].height>=140);
	flow_group_tiles(r,5,50,50,0,220,140,tiles);
	assert(tiles[0].width==r.width && tiles[0].height==r.height);
	return 0;
}
