#ifndef MOTHERSHIP_WLR_LAYER_SHELL_PROTOCOL_TYPES_H
#define MOTHERSHIP_WLR_LAYER_SHELL_PROTOCOL_TYPES_H

/* wlroots 0.20.2's installed public header references these protocol enums
 * but its package does not install the scanner header. These are the only
 * layer-shell protocol declarations needed by that public header.
 * Values: https://github.com/swaywm/wlr-protocols/blob/master/unstable/wlr-layer-shell-unstable-v1.xml
 */
enum zwlr_layer_shell_v1_layer {
	ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND = 0,
	ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM = 1,
	ZWLR_LAYER_SHELL_V1_LAYER_TOP = 2,
	ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY = 3,
};

enum zwlr_layer_surface_v1_keyboard_interactivity {
	ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE = 0,
	ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE = 1,
	ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND = 2,
};

#endif
