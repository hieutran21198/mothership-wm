//go:build linux && cgo && panel

package main

/*
#include <stdint.h>
#include <stdlib.h>
void panel_home_notice(const char *);
*/
import "C"

import (
	"strings"
	"unsafe"

	"mothership-wm/internal/domainipc"
)

// panelPinLabel names the comms pin: its Node, app and title. It runs on
// the GTK thread; the title never leaves the Panel.
//
//export panelPinLabel
func panelPinLabel(id C.uint64_t, title, app *C.char) *C.char {
	node := "Home"
	for _, w := range notificationSnapshot().Windows {
		if w.ID == uint64(id) {
			node = "Default Node"
			if w.Domain != "" {
				node = w.Domain
			}
		}
	}
	appID := C.GoString(app)
	name := appID
	if entry := entryForAppID(appID); entry != "" {
		name = entryName(entry)
	}
	if name == "" {
		name = "App"
	}
	label := node + " · " + name
	if t := strings.TrimSpace(C.GoString(title)); t != "" {
		label += " — " + t
	}
	return C.CString(label)
}

// panelPinActivate connects to the pinned window's current Node and focuses it.
//
//export panelPinActivate
func panelPinActivate(id C.uint64_t) {
	active.mu.Lock()
	session := active.state.Session
	active.mu.Unlock()
	active.submitCommand(domainipc.FocusWindowMethod, domainipc.WindowRef{Session: session, WindowID: uint64(id)}, false, 0, "", func(err error) {
		if err = windowCommandError(err); err != nil {
			c := C.CString("Could not open the pinned window: " + err.Error())
			C.panel_home_notice(c)
			C.free(unsafe.Pointer(c))
		}
	})
}
