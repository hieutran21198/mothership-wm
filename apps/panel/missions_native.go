//go:build linux && cgo && panel

package main

/*
#include <stdlib.h>
void mission_log_post(const char *);
void mission_log_status(const char *);
*/
import "C"

import (
	"context"
	"sync"
	"sync/atomic"
	"time"
	"unsafe"

	"mothership-wm/internal/domainipc"
)

var missionOffset atomic.Int64 // 0 is today, -1 yesterday, ...

func fetchMissionDay(day string) (domainipc.MissionDay, error) {
	ctx, cancel := context.WithTimeout(active.ctx, 2*time.Second)
	defer cancel()
	_, reply, done, err := connection(ctx, active.path, domainipc.MissionsGetMethod, 1, domainipc.MissionDayBody{Day: day})
	done()
	if err != nil {
		return domainipc.MissionDay{}, err
	}
	return domainipc.Decode[domainipc.MissionDay](reply.Body)
}

func postMission(text string) {
	c := C.CString(text)
	C.mission_log_post(c)
	C.free(unsafe.Pointer(c))
}

func missionStatus(text string) {
	c := C.CString(text)
	C.mission_log_status(c)
	C.free(unsafe.Pointer(c))
}

func loadMissionDay(offset int) {
	now := time.Now()
	day, err := fetchMissionDay(missionDayName(now, offset))
	if err != nil {
		postMission("error\t" + err.Error() + "\n")
		return
	}
	postMission(missionText(day, notificationSnapshot().Domains, now, offset == 0))
}

// panelMissionLoad shows a day relative to today, within retention.
//
//export panelMissionLoad
func panelMissionLoad(offset C.int) {
	o := min(0, max(-(missionDays-1), int(offset)))
	missionOffset.Store(int64(o))
	go loadMissionDay(o)
}

//export panelMissionClear
func panelMissionClear() {
	go func() {
		ctx, cancel := context.WithTimeout(active.ctx, 2*time.Second)
		defer cancel()
		_, _, done, err := connection(ctx, active.path, domainipc.MissionsClearMethod, 1, domainipc.Empty{})
		done()
		if err != nil {
			missionStatus("Could not clear history: " + err.Error())
			return
		}
		missionStatus("History cleared.")
		loadMissionDay(int(missionOffset.Load()))
		refreshMissionToday()
	}()
}

// Today's totals feed the Map Node Detail row. They refresh when the
// acknowledged view changes, never on a timer.
var missionTodayCache = struct {
	sync.Mutex
	today    missionToday
	selected domainipc.Selection
}{}

func refreshMissionToday() {
	now := time.Now()
	day, err := fetchMissionDay(missionDayName(now, 0))
	if err != nil {
		return
	}
	missionTodayCache.Lock()
	missionTodayCache.today = todayFrom(day, now)
	missionTodayCache.Unlock()
	active.rerender()
}

// missionSelection runs in render with the controller lock held.
func missionSelection(s domainipc.Selection) {
	missionTodayCache.Lock()
	changed := missionTodayCache.selected != s
	missionTodayCache.selected = s
	missionTodayCache.Unlock()
	if changed {
		go refreshMissionToday()
	}
}

// missionTodayFor returns closed seconds today and the open segment's start
// (0 when the Node is not open) for one Node.
func missionTodayFor(node string) (int64, int64) {
	missionTodayCache.Lock()
	defer missionTodayCache.Unlock()
	t := missionTodayCache.today
	if t.day != missionDayName(time.Now(), 0) {
		return 0, 0
	}
	open := int64(0)
	if t.openNode == node {
		open = t.openStart
	}
	return t.closed[node], open
}
