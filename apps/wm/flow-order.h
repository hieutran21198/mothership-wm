#ifndef MOTHERSHIP_FLOW_ORDER_H
#define MOTHERSHIP_FLOW_ORDER_H

#include <stdint.h>
#include "flow-group.h"

struct flow_order_entry {
	bool eligible;
	uint64_t group;
};

// The first eligible window sets the boundary for a strip swap.
static inline int flow_order_neighbor(const struct flow_order_entry *entries,
		int count, int focused, int direction) {
	if (count > NODE_WINDOWS || focused < 0 || focused >= count ||
			!entries[focused].eligible || (direction != -1 && direction != 1)) {
		return -1;
	}
	for (int slot = focused + direction; slot >= 0 && slot < count; slot += direction) {
		if (!entries[slot].eligible) continue;
		return entries[slot].group == entries[focused].group ? slot : -1;
	}
	return -1;
}

static inline int flow_group_neighbor(const struct rect *tiles, int count,
		int focused, bool vertical, int direction) {
	if (count > FLOW_GROUP_MAX || focused < 0 || focused >= count ||
			(direction != -1 && direction != 1)) {
		return -1;
	}

	struct rect source = tiles[focused];
	int64_t axis_center = vertical ? 2LL * source.y + source.height : 2LL * source.x + source.width;
	int64_t band_start = vertical ? source.x : source.y;
	int band_size = vertical ? source.width : source.height;
	int target = -1;
	int64_t nearest_axis = INT64_MAX, nearest_band = INT64_MAX;
	for (int slot = 0; slot < count; slot++) {
		struct rect tile = tiles[slot];
		int64_t start = vertical ? tile.x : tile.y;
		int size = vertical ? tile.width : tile.height;
		if (start >= band_start + band_size || band_start >= start + size) continue;
		int64_t center = vertical ? 2LL * tile.y + tile.height : 2LL * tile.x + tile.width;
		int64_t axis_distance = (center - axis_center) * direction;
		if (axis_distance <= 0) continue;
		int64_t band_distance = (2 * start + size) - (2 * band_start + band_size);
		if (band_distance < 0) band_distance = -band_distance;
		if (axis_distance < nearest_axis ||
				(axis_distance == nearest_axis && band_distance < nearest_band)) {
			target = slot;
			nearest_axis = axis_distance;
			nearest_band = band_distance;
		}
	}
	return target;
}

#endif
