//go:build linux && cgo && panel

package main

/*
#include <stdlib.h>
#include "notification-ui.h"
#include "map_hud.h"
int notice_connect_bus(const char *);
void notice_disconnect_bus(void);
*/
import "C"

import (
	"fmt"
	"time"
	"unsafe"

	"mothership-wm/internal/domainipc"
)

// Both launcher-owned notifications and the GTK log mutate this on the GTK thread.
var panelNotices notificationLog
var displayedBusNotice uint32
var previousNoticeCounts = map[string]int{}

func sourceLabel(s string) string {
	switch s {
	case noticeHome:
		return "Home"
	case noticeDefault:
		return "Default Node"
	case noticeVoid:
		return "From the Void"
	default:
		return s
	}
}

func cNoticeStrings(args []string, f func([]*C.char)) {
	ptr := make([]*C.char, len(args))
	for i, v := range args {
		ptr[i] = C.CString(v)
		defer C.free(unsafe.Pointer(ptr[i]))
	}
	f(ptr)
}

func mapNoticeSource(s string) bool {
	return s != noticeHome && s != noticeDefault && s != noticeVoid
}

func setSourceNoticeCount(source string, count int) {
	cNoticeStrings([]string{source}, func(v []*C.char) {
		C.notice_set_source_count(v[0], C.int(count))
		if mapNoticeSource(source) {
			C.map_hud_notice_count(v[0], C.uint(count))
		}
	})
}

func renderNotificationLog() {
	var order []string
	groups := make(map[string][]notification)
	for _, n := range panelNotices.entries {
		if _, seen := groups[n.Source]; !seen {
			order = append(order, n.Source)
		}
		groups[n.Source] = append(groups[n.Source], n)
	}
	counts := map[string]int{}
	total := 0
	visible := C.notice_log_visible() != 0
	if visible {
		C.notice_log_begin()
	}
	for _, source := range order {
		entries := groups[source]
		unread := 0
		for _, n := range entries {
			if !n.Read {
				unread++
			}
		}
		total += unread
		counts[source] = unread
		if !visible {
			continue
		}
		cNoticeStrings([]string{source, sourceLabel(source)}, func(s []*C.char) {
			C.notice_log_group(s[0], s[1], C.int(unread))
		})
		for _, n := range entries {
			cNoticeStrings([]string{n.App, n.Title, n.Body, n.Received.Local().Format("02 Jan 15:04")}, func(s []*C.char) {
				C.notice_log_row(C.guint(n.id), s[0], s[1], s[2], s[3], cBool(!n.Read))
			})
		}
	}
	if visible {
		C.notice_log_end()
	}
	C.notice_unread_count(C.int(total))
	for source, count := range counts {
		if count != previousNoticeCounts[source] {
			setSourceNoticeCount(source, count)
		}
	}
	for source := range previousNoticeCounts {
		if _, ok := counts[source]; !ok {
			setSourceNoticeCount(source, 0)
		}
	}
	previousNoticeCounts = counts
}

func notificationSnapshot() domainipc.Snapshot {
	if active == nil {
		return domainipc.Snapshot{}
	}
	active.mu.Lock()
	defer active.mu.Unlock()
	state := active.state
	state.Windows = append([]domainipc.Window(nil), state.Windows...)
	return state
}

func notificationSelectionSource(state domainipc.Snapshot) string {
	if state.Selection.State == "acknowledged" {
		if state.Selection.Domain != "" {
			return state.Selection.Domain
		}
		if state.Selection.View == "home_node" {
			return noticeDefault
		}
	}
	return noticeHome
}

func receiveNotification(n notification, state domainipc.Snapshot) {
	n.App = noticeText(n.App, 96)
	n.Title = noticeText(n.Title, 200)
	n.Body = noticeText(n.Body, 640)
	if n.App == "" {
		n.App = "App"
	}
	if n.Title == "" {
		n.Title = "Notification"
	}
	n.Received = time.Now()
	panelNotices.prepend(n)
	n = panelNotices.entries[0]
	renderNotificationLog()
	if mapNoticeSource(n.Source) {
		cNoticeStrings([]string{n.Source}, func(s []*C.char) { C.map_hud_notice_received(s[0]) })
	}
	// Silent running keeps every entry and Map pulse but holds the toast.
	if !silent.hold() && showToast(preferences.toastMode, n.Source, notificationSelectionSource(state)) {
		displayedBusNotice = n.BusID
		cNoticeStrings([]string{n.Source, sourceLabel(n.Source), n.App, n.Title, n.Body}, func(s []*C.char) {
			C.notice_show(s[0], s[1], s[2], s[3], s[4])
		})
	}
}

//export panelRefreshNotificationLog
func panelRefreshNotificationLog() { renderNotificationLog() }

//export panelMarkNotificationsRead
func panelMarkNotificationsRead(source *C.char) {
	panelNotices.markRead(C.GoString(source))
	renderNotificationLog()
}

//export panelClearNotificationLog
func panelClearNotificationLog(source *C.char) C.int {
	if err := panelNotices.clear(C.GoString(source)); err != nil {
		return 0
	}
	renderNotificationLog()
	return 1
}

//export panelMarkNotificationRead
func panelMarkNotificationRead(id C.guint) {
	if panelNotices.markEntryRead(uint32(id)) {
		renderNotificationLog()
	}
}

//export panelDeleteNotification
func panelDeleteNotification(id C.guint) C.int {
	if err := panelNotices.deleteEntry(uint32(id)); err != nil {
		return 0
	}
	renderNotificationLog()
	return 1
}

//export panelShellNotification
func panelShellNotification(title *C.char) {
	receiveNotification(notification{Source: noticeHome, App: "System", Title: C.GoString(title)}, notificationSnapshot())
}

//export panelAppNotification
func panelAppNotification(id C.uint, pid C.int, name, title, body *C.char) {
	s := notificationSnapshot()
	receiveNotification(notification{BusID: uint32(id), Source: notificationSource(s, int(pid)), App: C.GoString(name),
		Title: C.GoString(title), Body: C.GoString(body)}, s)
}

//export panelAppNotificationEnded
func panelAppNotificationEnded(id C.uint) {
	if displayedBusNotice == uint32(id) {
		displayedBusNotice = 0
		C.notice_toast_close()
	}
}

func closeNotificationBus() { C.notice_disconnect_bus() }

func registerNotificationBus(address string) error {
	c := C.CString(address)
	defer C.free(unsafe.Pointer(c))
	if C.notice_connect_bus(c) == 0 {
		return fmt.Errorf("could not register private notification service")
	}
	return nil
}

// systemNotice logs one System notification for the current view.
func systemNotice(title, body string) {
	state := notificationSnapshot()
	receiveNotification(notification{Source: notificationSelectionSource(state), App: "System",
		Title: title, Body: body}, state)
}

//export panelNodeNotFound
func panelNodeNotFound(slot C.uint) {
	state := notificationSnapshot()
	receiveNotification(notification{Source: notificationSelectionSource(state), App: "System",
		Title: "Node not found", Body: fmt.Sprintf("No Node is assigned to Win+%d.", uint(slot))}, state)
}
