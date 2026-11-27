package main

import (
	"os"
	"reflect"
	"strings"
	"testing"

	"mothership-wm/internal/domainipc"
	"mothership-wm/internal/userconfig"
)

func TestSilentHoldFollowsAcknowledgedView(t *testing.T) {
	var h silentHold
	if held, ended := h.apply("Research", silentNotices); ended || held != 0 || h.node != "Research" {
		t.Fatalf("enter: %+v ended=%v", h, ended)
	}
	for range 3 {
		if !h.hold() {
			t.Fatal("notice not held in a silent Node")
		}
	}
	// Switching to audio inside the same Node keeps the count.
	if _, ended := h.apply("Research", silentAudio); ended || h.held != 3 || h.audioTarget() != "Research" {
		t.Fatalf("mode change: %+v", h)
	}
	if held, ended := h.apply(noticeHome, silentOff); !ended || held != 3 || h.node != "" || h.hold() {
		t.Fatalf("leave: held=%d ended=%v state=%+v", held, ended, h)
	}
	// Moving directly between two silent Nodes ends one hold and starts another.
	h.apply("Research", silentNotices)
	h.hold()
	if held, ended := h.apply(noticeDefault, silentAudio); !ended || held != 1 || h.node != noticeDefault || h.held != 0 {
		t.Fatalf("switch: held=%d ended=%v state=%+v", held, ended, h)
	}
	// Turning the mode off in place ends the hold with its digest.
	h.hold()
	if held, ended := h.apply(noticeDefault, silentOff); !ended || held != 1 {
		t.Fatalf("off in place: held=%d ended=%v", held, ended)
	}
	if _, ended := h.apply(noticeHome, silentNotices); ended || h.node != "" {
		t.Fatalf("Home cannot run silent: %+v", h)
	}
	if silentDigest(1) != "1 notice held" || silentDigest(5) != "5 notices held" {
		t.Fatal("digest wording")
	}
}

func TestSilentRunningConfigIsSparsePerNode(t *testing.T) {
	path := userconfig.Path(t.TempDir())
	if err := os.MkdirAll(path[:strings.LastIndex(path, "/")], 0o700); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(path, []byte(`{"home_panel":{"width":960},"nodes":{"Ops":{"silent_running":"bogus"}}}`), 0o600); err != nil {
		t.Fatal(err)
	}
	var warnings []string
	warn := func(f string, a ...any) { warnings = append(warnings, f) }
	doc, _, err := userconfig.Read(path)
	if err != nil {
		t.Fatal(err)
	}
	if modes := loadSilentRunning(doc, warn); len(modes) != 0 || len(warnings) != 1 {
		t.Fatalf("invalid value: %v %v", modes, warnings)
	}
	var p panelPreferences
	for _, step := range []struct {
		view string
		mode int
	}{{"Research", silentAudio}, {"default", silentNotices}, {noticeDefault, silentNotices}, {"Ops", silentOff}} {
		if err := p.saveSilentRunning(path, step.view, step.mode); err != nil {
			t.Fatal(err)
		}
	}
	data, _ := os.ReadFile(path)
	text := string(data)
	for _, want := range []string{`"width": 960`, `"Research"`, `"notices_audio"`, `"default_node"`} {
		if !strings.Contains(text, want) {
			t.Fatalf("saved config lacks %s:\n%s", want, text)
		}
	}
	if strings.Contains(text, `"Ops"`) {
		t.Fatalf("Off left an empty Node object:\n%s", text)
	}
	doc, _, _ = userconfig.Read(path)
	want := map[string]int{"Research": silentAudio, "default": silentNotices, noticeDefault: silentNotices}
	if got := loadSilentRunning(doc, warn); !reflect.DeepEqual(got, want) || !reflect.DeepEqual(p.silent, want) {
		t.Fatalf("round trip %v / %v, want %v", got, p.silent, want)
	}
	if got := silentNodes(want); got != "Research\ndefault" {
		t.Fatalf("Map list %q", got)
	}
	for _, view := range []string{"Research", "default", noticeDefault} {
		if err := p.saveSilentRunning(path, view, silentOff); err != nil {
			t.Fatal(err)
		}
	}
	data, _ = os.ReadFile(path)
	if strings.Contains(string(data), "nodes") || strings.Contains(string(data), "default_node") {
		t.Fatalf("all Off should leave no Silent running keys:\n%s", data)
	}
	if err := p.saveSilentRunning(path, noticeHome, silentNotices); err == nil {
		t.Fatal("Home accepted Silent running")
	}
}

func TestSilentAudioHoldMutesOtherNodesAndRestoresOnlyItsOwn(t *testing.T) {
	windows := []domainipc.Window{
		{PID: 10, Domain: "Research", Mapped: true},
		{PID: 11, Domain: "Ops", Mapped: true},
		{PID: 12, Mapped: true},
	}
	streams := []audioStream{
		testStream(1, "101", "10", false), // Research keeps playing
		testStream(2, "102", "11", false), // Ops is muted by the hold
		testStream(3, "103", "11", true),  // pre-muted Ops app stays muted
		testStream(4, "104", "12", false), // Inbox is muted by the hold
		testStream(5, "105", "99", false), // unattributed: left alone
	}
	route := func() []nodeAudioStream { return routeAudio(streams, windows, func(int) int { return 0 }) }
	state := audioMuteState{nodes: map[string]bool{}, restore: map[audioKey]string{}, hold: "Research"}
	var changes []string
	set := func(k audioKey, mute bool) error {
		for i := range streams {
			if streams[i].Index == k.Index {
				streams[i].Mute = mute
			}
		}
		changes = append(changes, map[bool]string{true: "mute ", false: "unmute "}[mute]+string(rune('0'+k.Index)))
		return nil
	}
	if err := state.reconcile(streams, route(), set); err != nil {
		t.Fatal(err)
	}
	// Node Volume's own mute on Ops survives the end of the hold.
	state.nodes["Ops"] = true
	state.hold = ""
	if err := state.reconcile(streams, route(), set); err != nil {
		t.Fatal(err)
	}
	want := []string{"mute 2", "mute 4", "unmute 4"}
	if !reflect.DeepEqual(changes, want) {
		t.Fatalf("changes %v, want %v", changes, want)
	}
	if !streams[1].Mute || !streams[2].Mute || streams[0].Mute || streams[4].Mute {
		t.Fatalf("final mute state %+v", streams)
	}
}
