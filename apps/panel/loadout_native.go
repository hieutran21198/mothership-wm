//go:build linux && cgo && panel

package main

/*
#include <stdlib.h>
#include <glib-object.h>
#include "app-grid.h"
void panel_gtk_call(guint64);
void panel_loadout_status(const char *);
void panel_loadout_offer(int missing,int total,int running);
*/
import "C"

import (
	"fmt"
	"strings"
	"sync"
	"time"
	"unsafe"

	"mothership-wm/internal/domainipc"
)

var launched launchedEntries

// Nodes whose launch sequence is running; a second activation is ignored.
var loadoutRunning sync.Map

// GTK-thread dispatcher: goroutines hand work to the GTK main loop.
var gtkCalls = struct {
	sync.Mutex
	next uint64
	m    map[uint64]func()
}{m: map[uint64]func(){}}

func onGTK(f func()) {
	gtkCalls.Lock()
	gtkCalls.next++
	id := gtkCalls.next
	gtkCalls.m[id] = f
	gtkCalls.Unlock()
	C.panel_gtk_call(C.guint64(id))
}

//export panelGtkCall
func panelGtkCall(id C.guint64) {
	gtkCalls.Lock()
	f := gtkCalls.m[uint64(id)]
	delete(gtkCalls.m, uint64(id))
	gtkCalls.Unlock()
	if f != nil {
		f()
	}
}

// App ID resolution scans installed desktop files, so cache it per app ID.
var appIDEntries = struct {
	sync.Mutex
	m map[string]string
}{m: map[string]string{}}

func entryForAppID(app string) string {
	appIDEntries.Lock()
	defer appIDEntries.Unlock()
	if entry, ok := appIDEntries.m[app]; ok {
		return entry
	}
	c := C.CString(app)
	defer C.free(unsafe.Pointer(c))
	entry := ""
	if found := C.app_catalog_desktop_id_for(c); found != nil {
		entry = C.GoString(found)
		C.g_free(C.gpointer(found))
	}
	appIDEntries.m[app] = entry
	return entry
}

func entryName(entry string) string {
	c := C.CString(entry)
	defer C.free(unsafe.Pointer(c))
	name := C.app_catalog_name(c)
	defer C.g_free(C.gpointer(name))
	return C.GoString(name)
}

func currentMembers(s domainipc.Snapshot, node string) []loadoutMember {
	return nodeMembers(s, node, func(id uint64) string { return launched.lookup(s.Session, id) }, entryForAppID)
}

// loadoutOffer counts the current Node's missing entries for the lane.
// It runs in the controller goroutine with the state lock held.
func loadoutOfferFor(s domainipc.Snapshot, node string) (missing, total int, running bool) {
	loadout := profileLoadout(s, node)
	if node == "" || len(loadout) == 0 {
		return 0, 0, false
	}
	_, running = loadoutRunning.Load(node)
	return len(missingEntries(loadout, currentMembers(s, node))), len(loadout), running
}

func postLoadoutOffer(missing, total int, running bool) {
	C.panel_loadout_offer(C.int(missing), C.int(total), C.int(btoi(running)))
}

func loadoutStatus(msg string) {
	onGTK(func() {
		c := C.CString(msg)
		C.panel_loadout_status(c)
		C.free(unsafe.Pointer(c))
	})
}

//export panelSaveLoadout
func panelSaveLoadout(name *C.char) {
	node := C.GoString(name)
	entries, skipped := loadoutFromMembers(currentMembers(notificationSnapshot(), node))
	if len(entries) == 0 {
		loadoutStatus(loadoutSaveMessage(entries, skipped))
		return
	}
	active.submitCommand(domainipc.LoadoutSetMethod, domainipc.LoadoutBody{Domain: node, Entries: entries}, false, 0, "", func(err error) {
		if err != nil {
			loadoutStatus("Could not save the loadout: " + err.Error())
			return
		}
		loadoutStatus(loadoutSaveMessage(entries, skipped))
	})
}

//export panelClearLoadout
func panelClearLoadout(name *C.char) {
	node := C.GoString(name)
	active.submitCommand(domainipc.LoadoutSetMethod, domainipc.LoadoutBody{Domain: node, Entries: []string{}}, false, 0, "", func(err error) {
		if err != nil {
			loadoutStatus("Could not clear the loadout: " + err.Error())
			return
		}
		loadoutStatus("Loadout cleared.")
	})
}

// panelLaunchLoadout starts the missing entries of a Node's loadout. It
// never changes the view; windows join that Node through the launch marker.
//
//export panelLaunchLoadout
func panelLaunchLoadout(name *C.char) *C.char {
	node := C.GoString(name)
	s := notificationSnapshot()
	loadout := profileLoadout(s, node)
	if len(loadout) == 0 {
		return C.CString(node + " has no loadout. Save one in Node Panel Settings.")
	}
	missing := missingEntries(loadout, currentMembers(s, node))
	if len(missing) == 0 {
		return C.CString("Every app in the " + node + " loadout is already open.")
	}
	if _, busy := loadoutRunning.LoadOrStore(node, true); busy {
		return nil
	}
	active.rerender()
	go runLoadout(node, s.Session, missing)
	return nil
}

const loadoutPace = 3 * time.Second

// runLoadout launches entries in order. Each launch waits up to
// loadoutPace for its window, so windows usually join in loadout order.
func runLoadout(node, session string, entries []string) {
	defer func() {
		loadoutRunning.Delete(node)
		active.rerender()
	}()
	var mu sync.Mutex
	var failed []string
	fail := func(entry, why string) {
		mu.Lock()
		failed = append(failed, fmt.Sprintf("%s (%s)", entryName(entry), why))
		mu.Unlock()
	}
	var wg sync.WaitGroup
	for i, entry := range entries {
		s := notificationSnapshot()
		if s.Session != session {
			for _, rest := range entries[i:] {
				fail(rest, "session ended")
			}
			break
		}
		members := 0
		for _, w := range s.Windows {
			if w.Domain == node {
				members++
			}
		}
		if members >= 8 {
			for _, rest := range entries[i:] {
				fail(rest, "Node is full")
			}
			break
		}
		settled := make(chan struct{})
		started := make(chan string, 1)
		wg.Add(1)
		onGTK(func() {
			started <- launchLoadoutEntry(entry, node, session, func(ok bool) {
				if !ok {
					fail(entry, "no window joined")
				}
				close(settled)
				wg.Done()
			})
		})
		if err := <-started; err != "" {
			fail(entry, err)
			wg.Done()
			continue
		}
		select {
		case <-settled:
		case <-time.After(loadoutPace):
		case <-active.ctx.Done():
			return
		}
	}
	wg.Wait()
	if len(failed) == 0 {
		return
	}
	onGTK(func() {
		receiveNotification(notification{Source: node, App: "System", Title: "Loadout incomplete",
			Body: strings.Join(failed, "; ") + "."}, notificationSnapshot())
	})
}

// launchLoadoutEntry runs on the GTK thread. done is always called exactly
// once unless an error string is returned.
func launchLoadoutEntry(entry, node, session string, done func(bool)) string {
	id := C.CString(entry)
	defer C.free(unsafe.Pointer(id))
	var exe *C.char
	var dbus C.int
	info := C.app_catalog_find(id, &exe, &dbus)
	if info == nil {
		return "not installed"
	}
	defer C.g_object_unref(C.gpointer(info))
	return launchGridEntry(info, entry, C.GoString(exe), dbus != 0, node, session, done)
}
