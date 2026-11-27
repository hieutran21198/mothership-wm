package unixapi

import (
	"context"
	"encoding/json"
	"net"
	"path/filepath"
	"strings"
	"sync"
	"testing"
	"time"

	"mothership-wm/internal/domainipc"
	"mothership-wm/services/desktopd/internal/app"
)

type fakeMissions struct {
	mu      sync.Mutex
	begun   []string
	ended   int
	endedAt []time.Time
	cleared bool
	day     app.MissionDay
}

func (f *fakeMissions) Begin(node string, def bool, _ time.Time) {
	f.mu.Lock()
	defer f.mu.Unlock()
	if def {
		node = "@default"
	}
	f.begun = append(f.begun, node)
}
func (f *fakeMissions) End(at time.Time) {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.ended++
	f.endedAt = append(f.endedAt, at)
}

// waitFor polls the fake until check holds; WM events arrive on their own
// connection, so no request on another socket orders after them.
func (f *fakeMissions) waitFor(t *testing.T, check func() bool) {
	t.Helper()
	for deadline := time.Now().Add(2 * time.Second); time.Now().Before(deadline); time.Sleep(5 * time.Millisecond) {
		f.mu.Lock()
		ok := check()
		f.mu.Unlock()
		if ok {
			return
		}
	}
	t.Fatalf("missions never reached the expected state: begun %v ended %d", f.begun, f.ended)
}
func (f *fakeMissions) Day(day string, _ time.Time) (app.MissionDay, error) {
	d := f.day
	d.Day = day
	return d, nil
}
func (f *fakeMissions) Clear(time.Time) error {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.cleared = true
	return nil
}

func TestMissionsFollowAcknowledgedViewsAndServeDays(t *testing.T) {
	path := filepath.Join(t.TempDir(), "desktopd.sock")
	l, err := net.ListenUnix("unixpacket", &net.UnixAddr{Name: path, Net: "unixpacket"})
	if err != nil {
		t.Fatal(err)
	}
	ctx, cancel := context.WithCancel(context.Background())
	service := &app.Service{}
	log := &fakeMissions{day: app.MissionDay{
		Segments: []app.MissionSegment{{Node: "Ops", Start: 100, End: 200}, {DefaultNode: true, Start: 300, End: 400}},
		Open:     &app.MissionSegment{Node: "Ops", Start: 500},
	}}
	service.UseMissionLog(log)
	done := make(chan error, 1)
	go func() { done <- ServeWithConfig(ctx, l, service, nil, "") }()
	t.Cleanup(func() { cancel(); <-done })
	s := testServer{t: t, ctx: ctx, path: path}
	wm := s.register()
	send(t, wm, "ready_window_focus")
	success(t, s.request(1, "create", `{"domain":"Ops"}`), 1)
	apply := func(id uint64, method, body string) {
		c := s.request(id, method, body)
		f := read(t, wm)
		send(t, wm, "applied "+strings.Fields(f)[1])
		success(t, c, id)
	}
	apply(2, domainipc.FocusMethod, `{"domain":"Ops"}`)
	apply(3, domainipc.LayoutMethod, `{"domain":"Ops","mode":"stage"}`) // same view
	apply(4, domainipc.DefaultNodeMethod, "{}")
	apply(5, domainipc.HomeMethod, "{}")
	log.waitFor(t, func() bool {
		got := log.begun
		return len(got) == 3 && got[0] == "Ops" && got[1] == "Ops" && got[2] == "@default" && log.ended == 1
	})

	// Idle ends at the last input and returning resumes the current view.
	apply(8, domainipc.FocusMethod, `{"domain":"Ops"}`)
	send(t, wm, "idle 300")
	send(t, wm, "idle 330") // repeated report: no second End
	send(t, wm, "active 1")
	log.waitFor(t, func() bool { return len(log.begun) == 5 && log.begun[4] == "Ops" && log.ended == 2 })
	log.mu.Lock()
	idleEnd := log.endedAt[1]
	log.mu.Unlock()
	if quiet := time.Since(idleEnd); quiet < 299*time.Second || quiet > 310*time.Second {
		t.Fatalf("idle ended %v ago, want about 300s", quiet)
	}
	day := decode[domainipc.MissionDay](t, success(t, s.request(6, domainipc.MissionsGetMethod, `{"day":"2026-11-27"}`), 6))
	if day.Day != "2026-11-27" || len(day.Lanes) != 2 || !day.Lanes[1].DefaultNode ||
		len(day.Segments) != 2 || day.Segments[1] != [3]int64{1, 300, 400} || day.Open == nil || *day.Open != [3]int64{0, 500, 0} {
		t.Fatalf("day %+v", day)
	}
	success(t, s.request(7, domainipc.MissionsClearMethod, "{}"), 7)
	log.waitFor(t, func() bool { return log.cleared })
}

func TestMissionWireMergesGapsOnlyWhenTooLarge(t *testing.T) {
	small := missionWire(app.MissionDay{Day: "d", Segments: []app.MissionSegment{
		{Node: "Ops", Start: 0, End: 10}, {Node: "Ops", Start: 20, End: 30},
	}})
	if small.Merged || len(small.Segments) != 2 {
		t.Fatalf("small day changed: %+v", small)
	}
	var big app.MissionDay
	for i := int64(0); i < 2000; i++ {
		node := []string{"Ops", "Dev"}[i%2]
		big.Segments = append(big.Segments, app.MissionSegment{Node: node, Start: i * 40, End: i*40 + 30})
	}
	wire := missionWire(big)
	b, _ := json.Marshal(domainipc.Response{ID: 1, Body: mustJSON(wire)})
	if !wire.Merged || len(b) > domainipc.MaxPacket || len(wire.Segments) >= 2000 {
		t.Fatalf("merged=%v size=%d segments=%d", wire.Merged, len(b), len(wire.Segments))
	}
	var total int64
	for _, s := range wire.Segments {
		total += s[2] - s[1]
	}
	if total < 2000*30 {
		t.Fatalf("merging lost time: %d", total)
	}
}
