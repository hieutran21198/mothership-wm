#include "../flow-order.h"
#include <assert.h>

static void strip_neighbors(void) {
	const struct flow_order_entry entries[NODE_WINDOWS] = {
		{true, 0}, {false, 0}, {true, 0}, {true, 7},
		{false, 9}, {true, 7}, {true, 9}, {true, 7},
	};
	assert(flow_order_neighbor(entries, NODE_WINDOWS, 0, 1) == 2);
	assert(flow_order_neighbor(entries, NODE_WINDOWS, 2, -1) == 0);
	assert(flow_order_neighbor(entries, NODE_WINDOWS, 3, 1) == 5);
	assert(flow_order_neighbor(entries, NODE_WINDOWS, 5, -1) == 3);
	assert(flow_order_neighbor(entries, NODE_WINDOWS, 2, 1) == -1);
	assert(flow_order_neighbor(entries, NODE_WINDOWS, 3, -1) == -1);
	assert(flow_order_neighbor(entries, NODE_WINDOWS, 5, 1) == -1);
	assert(flow_order_neighbor(entries, NODE_WINDOWS, 7, -1) == -1);
	assert(flow_order_neighbor(entries, NODE_WINDOWS, 0, -1) == -1);
	assert(flow_order_neighbor(entries, NODE_WINDOWS, 7, 1) == -1);
	assert(flow_order_neighbor(entries, NODE_WINDOWS, 1, 1) == -1);
	assert(flow_order_neighbor(entries, 1, 0, 1) == -1);
	assert(flow_order_neighbor(entries, 0, 0, 1) == -1);
}

static void group_neighbors(void) {
	struct rect area = {0, 0, 800, 600};
	struct rect tiles[FLOW_GROUP_MAX];
	flow_group_tiles(area, 2, 50, 50, 16, 100, 100, tiles);
	assert(flow_group_neighbor(tiles, 2, 0, false, 1) == 1);
	assert(flow_group_neighbor(tiles, 2, 1, false, -1) == 0);
	assert(flow_group_neighbor(tiles, 2, 0, true, 1) == -1);
	assert(flow_group_neighbor(tiles, 2, 1, false, 1) == -1);

	flow_group_tiles(area, 3, 50, 50, 16, 100, 100, tiles);
	assert(flow_group_neighbor(tiles, 3, 0, false, 1) == 1);
	assert(flow_group_neighbor(tiles, 3, 1, true, 1) == 2);
	assert(flow_group_neighbor(tiles, 3, 2, true, -1) == 1);
	assert(flow_group_neighbor(tiles, 3, 2, false, -1) == 0);
	assert(flow_group_neighbor(tiles, 3, 0, true, 1) == -1);
	flow_group_tiles(area, 3, 50, 35, 16, 100, 100, tiles);
	assert(flow_group_neighbor(tiles, 3, 0, false, 1) == 2);

	flow_group_tiles(area, 4, 60, 40, 16, 100, 100, tiles);
	assert(flow_group_neighbor(tiles, 4, 0, false, 1) == 1);
	assert(flow_group_neighbor(tiles, 4, 0, true, 1) == 2);
	assert(flow_group_neighbor(tiles, 4, 3, false, -1) == 2);
	assert(flow_group_neighbor(tiles, 4, 3, true, -1) == 1);
}

static void neighbor_distance(void) {
	const struct rect diagonal[] = {{0, 0, 40, 40}, {40, 40, 40, 40}};
	assert(flow_group_neighbor(diagonal, 2, 0, false, 1) == -1);
	assert(flow_group_neighbor(diagonal, 2, 0, true, 1) == -1);

	const struct rect staggered[] = {{10, 10, 40, 40}, {60, 30, 40, 40}, {100, 10, 40, 40}};
	assert(flow_group_neighbor(staggered, 3, 0, false, 1) == 1);
}

int main(void) {
	strip_neighbors();
	group_neighbors();
	neighbor_distance();
	return 0;
}
