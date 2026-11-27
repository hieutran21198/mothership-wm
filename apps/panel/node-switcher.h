#ifndef MOTHERSHIP_NODE_SWITCHER_H
#define MOTHERSHIP_NODE_SWITCHER_H

#include <stddef.h>
#include <stdint.h>

// GTK main thread only. The strip copies its input. Pass SIZE_MAX for current
// when the source view is outside a Node.
void node_switcher_show(const char *const *names, const char *const *descriptions,
	const int *counts, size_t length, size_t current, size_t selected);
void node_switcher_hide(void);

#endif
