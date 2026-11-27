package unixapi

import (
	"context"
	"encoding/json"
	"net"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"mothership-wm/internal/domainipc"
	"mothership-wm/services/desktopd/internal/app"
)

type fakeMissions struct {
	begun   []string
	ended   int
	cleared bool
	day     app.MissionDay
}

func (f *fakeMissions) Begin(node string, def bool, _ time.Time) {
	if def {
		node = "@default"
	}
	f.begun = append(f.begun, node)
}
func (f *fakeMissions) End(time.Time) { f.ended++ }
func (f *fakeMissions) Day(day string, _ time.Time) (app.MissionDay, error) {
	d := f.day
	d.Day = day
	return d, nil
}
func (f *fakeMissions) Clear(time.Time) error { f.cleared = true; return nil }

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
	if got := log.begun; len(got) != 3 || got[0] != "Ops" || got[1] != "Ops" || got[2] != "@default" || log.ended != 1 {
		t.Fatalf("begun %v ended %d", got, log.ended)
	}

	day := decode[domainipc.MissionDay](t, success(t, s.request(6, domainipc.MissionsGetMethod, `{"day":"2026-11-27"}`), 6))
	if day.Day != "2026-11-27" || len(day.Lanes) != 2 || !day.Lanes[1].DefaultNode ||
		len(day.Segments) != 2 || day.Segments[1] != [3]int64{1, 300, 400} || day.Open == nil || *day.Open != [3]int64{0, 500, 0} {
		t.Fatalf("day %+v", day)
	}
	success(t, s.request(7, domainipc.MissionsClearMethod, "{}"), 7)
	if !log.cleared {
		t.Fatal("clear not forwarded")
	}
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
