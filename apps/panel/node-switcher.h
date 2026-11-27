#ifndef MOTHERSHIP_NODE_SWITCHER_H
#define MOTHERSHIP_NODE_SWITCHER_H

#include <stddef.h>
#include <stdint.h>

// GTK main thread only. The strip copies its input. Pass SIZE_MAX for current
// when the source view is outside a Node.
void node_switcher_show(const char *const *names, const char *const *descriptions,
	const int *counts, size_t length, size_t current, size_t selected);
void node_switcher_hide(void);

// Beam mode: the same strip as a drop target while a preview card is
// dragged. Nothing is selected until the pointer is over an eligible tile.
// The current Node and full Nodes (eight windows) cannot be chosen.
void node_switcher_beam(const char *const *names, const char *const *descriptions,
	const int *counts, size_t length, size_t current, double from_x, double from_y);
// Moves the beam's pointer end; returns the highlighted index or SIZE_MAX.
size_t node_switcher_beam_move(double x, double y);

#endif
