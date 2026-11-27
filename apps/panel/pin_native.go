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

// pinParts resolves the pinned window's notice source, Node label and app
// name from the current snapshot.
func pinParts(id uint64, appID string) (source, node, name string) {
	source, node = noticeHome, "Home"
	for _, w := range notificationSnapshot().Windows {
		if w.ID == id {
			source, node = noticeDefault, "Default Node"
			if w.Domain != "" {
				source, node = w.Domain, w.Domain
			}
		}
	}
	name = appID
	if entry := entryForAppID(appID); entry != "" {
		name = entryName(entry)
	}
	if name == "" {
		name = "App"
	}
	return source, node, name
}

// panelPinLabel names the comms pin: its Node, app and title. It runs on
// the GTK thread; the title never leaves the Panel.
//
//export panelPinLabel
func panelPinLabel(id C.uint64_t, title, app *C.char) *C.char {
	_, node, name := pinParts(uint64(id), C.GoString(app))
	label := node + " · " + name
	if t := strings.TrimSpace(C.GoString(title)); t != "" {
		label += " — " + t
	}
	return C.CString(label)
}

// panelPinNotice logs one System notice in the window's Node when a window
// is pinned. It runs on the GTK thread.
//
//export panelPinNotice
func panelPinNotice(id C.uint64_t, title, app *C.char) {
	source, _, name := pinParts(uint64(id), C.GoString(app))
	receiveNotification(notification{Source: source, App: "System", Title: "Window pinned",
		Body: pinNoticeBody(name, C.GoString(title))}, notificationSnapshot())
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
