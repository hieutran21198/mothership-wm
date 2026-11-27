//go:build linux && cgo && panel

package main

/*
#include <stdint.h>
#include <stdlib.h>
#include "node-switcher.h"
int panel_default_node_enabled(void);
*/
import "C"

import (
	"math"
	"unsafe"
)

// The tractor beam lives on the GTK thread: begin, move and drop arrive as
// Wayland events from the WM.
var beamState struct {
	window  uint64
	targets []beamTarget
	hovered int
}

//export panelBeamBegin
func panelBeamBegin(id C.uint64_t, x, y C.int) {
	targets, current := beamTargets(notificationSnapshot(), uint64(id), C.panel_default_node_enabled() != 0)
	beamState.window, beamState.targets, beamState.hovered = uint64(id), targets, -1
	if len(targets) == 0 {
		return
	}
	names := make([]*C.char, len(targets))
	descriptions := make([]*C.char, len(targets))
	counts := make([]C.int, len(targets))
	for i, t := range targets {
		names[i], descriptions[i], counts[i] = C.CString(t.label), C.CString(t.description), C.int(t.windows)
	}
	cur := C.size_t(math.MaxUint)
	if current >= 0 {
		cur = C.size_t(current)
	}
	C.node_switcher_beam(&names[0], &descriptions[0], &counts[0], C.size_t(len(targets)), cur, C.double(x), C.double(y))
	for i := range names {
		C.free(unsafe.Pointer(names[i]))
		C.free(unsafe.Pointer(descriptions[i]))
	}
}

//export panelBeamMove
func panelBeamMove(x, y C.int) {
	if beamState.window == 0 {
		return
	}
	hit := C.node_switcher_beam_move(C.double(x), C.double(y))
	beamState.hovered = -1
	if uint64(hit) < uint64(len(beamState.targets)) {
		beamState.hovered = int(hit)
	}
}

// panelBeamEnd hides the strip; on a drop over a target it moves the
// window there. It never changes the current view.
//
//export panelBeamEnd
func panelBeamEnd(drop C.int, x, y C.int) {
	if beamState.window == 0 {
		return
	}
	if drop != 0 {
		panelBeamMove(x, y)
	}
	window, hovered, targets := beamState.window, beamState.hovered, beamState.targets
	beamState.window, beamState.targets, beamState.hovered = 0, nil, -1
	C.node_switcher_hide()
	if drop == 0 || hovered < 0 || hovered >= len(targets) {
		return
	}
	name := C.CString(targets[hovered].node)
	defer C.free(unsafe.Pointer(name))
	panelMoveWindow(name, C.uint64_t(window))
}
