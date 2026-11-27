//go:build linux && cgo && panel

package main

/*
#include <stdlib.h>
#include "notification-ui.h"
void panel_silent_modes(const char *,int,int);
*/
import "C"

import (
	"errors"
	"os"
	"os/exec"
	"unsafe"
)

// All Silent running state lives on the GTK thread, like the notification log.
var (
	silent       silentHold
	silentView   string
	silentLast   = map[string]int{} // Last enabled mode per view, for the wing toggle.
	silentMarker string             // Present while an audio hold may have muted apps.
)

// pushSilentState sends the Map cloak list and the current view's mode to C.
func pushSilentState() {
	names := C.CString(silentNodes(preferences.silent))
	defer C.free(unsafe.Pointer(names))
	_, err := exec.LookPath("pactl")
	C.panel_silent_modes(names, C.int(preferences.silent[silentView]), C.int(btoi(err == nil)))
}

// applySilentView moves the hold to view. Ending a hold restores audio and
// shows one digest under the destination's toast setting.
func applySilentView(view string) {
	silentView = view
	held, ended := silent.apply(view, preferences.silent[view])
	setAudioHold(silent.audioTarget())
	if ended && held > 0 && showToast(preferences.toastMode, view, view) {
		displayedBusNotice = 0
		cNoticeStrings([]string{view, sourceLabel(view), "System", "Silent running ended", silentDigest(held)}, func(s []*C.char) {
			C.notice_show(s[0], s[1], s[2], s[3], s[4])
		})
	}
}

// refreshSilentRunning reapplies the current view after a config change.
func refreshSilentRunning() {
	applySilentView(silentView)
	pushSilentState()
}

//export panelSilentView
func panelSilentView(view *C.char) {
	if v := C.GoString(view); v != silentView {
		applySilentView(v)
		pushSilentState()
	}
}

//export panelSaveSilentRunning
func panelSaveSilentRunning(mode C.int) *C.char {
	if err := preferences.saveSilentRunning(preferencePaths.user, silentView, int(mode)); err != nil {
		return C.CString("Could not save Silent running: " + err.Error())
	}
	if mode != silentOff {
		silentLast[silentView] = int(mode)
	}
	refreshSilentRunning()
	return nil
}

// panelToggleSilentRunning switches between Off and the last enabled mode.
//
//export panelToggleSilentRunning
func panelToggleSilentRunning() *C.char {
	mode := silentOff
	if preferences.silent[silentView] == silentOff {
		if mode = silentLast[silentView]; mode == silentOff {
			mode = silentNotices
		}
	}
	return panelSaveSilentRunning(C.int(mode))
}

// panelToggleSilentNode switches one named Node between Off and its last
// enabled mode from the Map's Node Detail. It does not change the view.
//
//export panelToggleSilentNode
func panelToggleSilentNode(name *C.char) *C.char {
	node := C.GoString(name)
	mode := silentOff
	if preferences.silent[node] == silentOff {
		if mode = silentLast[node]; mode == silentOff {
			mode = silentNotices
		}
	}
	if err := preferences.saveSilentRunning(preferencePaths.user, node, mode); err != nil {
		return C.CString("Could not save Silent running: " + err.Error())
	}
	if mode != silentOff {
		silentLast[node] = mode
	}
	refreshSilentRunning()
	return nil
}

// checkSilentMarker reports an audio hold the previous Panel did not end.
// It cannot know which streams that Panel muted, so it does not guess.
func checkSilentMarker() {
	if silentMarker == "" {
		return
	}
	if _, err := os.Stat(silentMarker); errors.Is(err, os.ErrNotExist) {
		return
	}
	_ = os.Remove(silentMarker)
	systemNotice("Check app audio", "Mothership restarted during Silent running. Apps in other Nodes may still be muted.")
}
