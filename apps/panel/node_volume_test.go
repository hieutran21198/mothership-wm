package main

import (
	"reflect"
	"testing"

	"mothership-wm/internal/domainipc"
)

func testStream(i uint32, serial, pid string, mute bool) audioStream {
	return audioStream{Index: i, Mute: mute, Properties: map[string]string{"object.serial": serial, "application.process.id": pid, "application.name": "Browser"}}
}

func TestNodeAudioScopeRejectsSharedAndUnattributedStreams(t *testing.T) {
	windows := []domainipc.Window{
		{PID: 10, Domain: "Dev", Mapped: true},
		{PID: 11, Domain: "Dev", Mapped: true},
		{PID: 11, Domain: "Review", Mapped: true},
		{PID: 12, Mapped: true},
	}
	streams := []audioStream{
		testStream(1, "101", "99", false), // browser helper belongs to Dev
		testStream(2, "102", "11", false), // shared
		testStream(3, "103", "12", false), // Default
		testStream(4, "104", "13", false), // unknown
		testStream(5, "", "10", false),    // no reliable stream identity
	}
	routed := routeAudio(streams, windows, func(pid int) int {
		if pid == 99 {
			return 10
		}
		return 0
	})
	dev := audioGroups(routed, "Dev")
	if len(dev) != 1 || dev[0].Streams[0].key.Index != 1 {
		t.Fatalf("Dev groups: %+v", dev)
	}
	def := audioGroups(routed, defaultAudioNode)
	if len(def) != 1 || def[0].Streams[0].key.Index != 3 {
		t.Fatalf("Default groups: %+v", def)
	}
}
func TestNodeMuteRestoresOnlyStreamsItMuted(t *testing.T) {
	windows := []domainipc.Window{{PID: 10, Domain: "Dev", Mapped: true}, {PID: 11, Domain: "Review", Mapped: true}}
	streams := []audioStream{testStream(1, "101", "10", false), testStream(2, "102", "10", true), testStream(3, "103", "11", false)}
	route := func() []nodeAudioStream { return routeAudio(streams, windows, func(int) int { return 0 }) }
	state := audioMuteState{nodes: map[string]bool{"Dev": true}, restore: map[audioKey]string{}}
	type change struct {
		k    audioKey
		mute bool
	}
	var changes []change
	set := func(k audioKey, mute bool) error { changes = append(changes, change{k, mute}); return nil }
	if err := state.reconcile(streams, route(), set); err != nil {
		t.Fatal(err)
	}
	streams[0].Mute = true
	streams = append(streams, testStream(4, "104", "10", false))
	if err := state.reconcile(streams, route(), set); err != nil {
		t.Fatal(err)
	}
	streams[3].Mute = true
	// Index one is reused by a distinct stream; it must not be unmuted.
	streams[0] = testStream(1, "105", "11", true)
	delete(state.nodes, "Dev")
	if err := state.reconcile(streams, route(), set); err != nil {
		t.Fatal(err)
	}
	want := []change{{audioKey{1, 101}, true}, {audioKey{4, 104}, true}, {audioKey{4, 104}, false}}
	if !reflect.DeepEqual(changes, want) || len(state.restore) != 0 {
		t.Fatalf("changes %+v, want %+v", changes, want)
	}
}

func TestNodeMuteMovesBetweenMutedNodesWithoutUnmuting(t *testing.T) {
	key := audioKey{1, 101}
	state := audioMuteState{nodes: map[string]bool{"Dev": true, "Review": true}, restore: map[audioKey]string{key: "Dev"}}
	streams := []audioStream{testStream(1, "101", "10", true)}
	windows := []domainipc.Window{{PID: 10, Domain: "Review", Mapped: true}}
	routed := routeAudio(streams, windows, func(int) int { return 0 })
	if err := state.reconcile(streams, routed, func(audioKey, bool) error {
		t.Fatal("stream must stay muted during transfer")
		return nil
	}); err != nil || state.restore[key] != "Review" {
		t.Fatalf("restore ownership = %+v, err = %v", state.restore, err)
	}
}

func TestCommsNodesShowPlayingAndMutedNodes(t *testing.T) {
	windows := []domainipc.Window{
		{PID: 10, Domain: "Ops", Mapped: true},
		{PID: 11, Domain: "Dev", Mapped: true},
		{PID: 12, Mapped: true},
		{PID: 13, Domain: "Quiet", Mapped: true},
	}
	paused := testStream(4, "104", "13", false)
	paused.Corked = true
	streams := []audioStream{
		testStream(1, "101", "10", false), // Ops playing
		testStream(2, "102", "11", true),  // Dev playing but muted
		testStream(3, "103", "12", false), // Default Node: not on the Map
		paused,                            // Quiet: paused
		testStream(5, "105", "10", true),  // Ops has one unmuted stream left
	}
	got := commsNodes(routeAudio(streams, windows, func(int) int { return 0 }))
	if want := map[string]bool{"Ops": false, "Dev": true}; !reflect.DeepEqual(got, want) {
		t.Fatalf("comms %v, want %v", got, want)
	}
}
