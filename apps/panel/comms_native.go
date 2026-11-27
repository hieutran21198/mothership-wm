//go:build linux && cgo && panel

package main

/*
#include <stdlib.h>
#include "map_hud.h"
*/
import "C"

import (
	"bufio"
	"context"
	"fmt"
	"os/exec"
	"sort"
	"strings"
	"time"
	"unsafe"
)

// commsWatch carries the latest Map visibility; only the newest matters.
var commsWatch = make(chan bool, 1)

//export panelCommsWatch
func panelCommsWatch(on C.int) {
	for {
		select {
		case commsWatch <- on != 0:
			return
		default:
			select {
			case <-commsWatch:
			default:
			}
		}
	}
}

// postComms sends "name\tcomms\tmusic" rows; each kind is 0 none,
// 1 playing or 2 muted.
func postComms(nodes map[string]shipAudio) {
	names := make([]string, 0, len(nodes))
	for n := range nodes {
		names = append(names, n)
	}
	sort.Strings(names)
	var b strings.Builder
	for _, n := range names {
		a := nodes[n]
		fmt.Fprintf(&b, "%s\t%d\t%d\n", n, shipAudioLevel(a.Comms, a.CommsMuted), shipAudioLevel(a.Music, a.MusicMuted))
	}
	text := b.String()
	onGTK(func() {
		c := C.CString(text)
		C.map_hud_set_comms(c)
		C.free(unsafe.Pointer(c))
	})
}

// runComms subscribes to playback changes only while the Map is open, so
// comms status costs nothing when it is closed.
func runComms(ctx context.Context, c *controller, appBus string) {
	var stop context.CancelFunc
	events := make(chan struct{}, 1)
	signal := func() {
		select {
		case events <- struct{}{}:
		default:
		}
	}
	defer func() {
		if stop != nil {
			stop()
		}
	}()
	for {
		select {
		case <-ctx.Done():
			return
		case on := <-commsWatch:
			if on && stop == nil {
				watch, cancel := context.WithCancel(ctx)
				stop = cancel
				go subscribeAudio(watch, signal)
				signal()
			} else if !on && stop != nil {
				stop()
				stop = nil
				postComms(nil)
			}
		case <-events:
			if stop == nil {
				continue
			}
			// Coalesce a burst of stream events into one sample.
			select {
			case <-time.After(150 * time.Millisecond):
			case <-ctx.Done():
				return
			}
			all, err := captureAudio(ctx)
			if err != nil {
				postComms(nil)
				continue
			}
			routed := routeAudio(all, audioSelection(c).Windows, procAudioParent)
			var players map[int]bool
			if needsPlayers(routed) {
				players = mediaPlayers(ctx, appBus)
			}
			postComms(shipAudioNodes(routed, func(s audioStream) bool { return musicStream(s, players, procAudioParent) }))
		}
	}
}

// mediaPlayers returns the processes that own an MPRIS player reporting
// Playing. Apps launched by Mothership publish players on the app bus, not
// the bus the Panel inherited, so ask that one when it exists. It is best
// effort: without busctl or a bus, only media.role=music marks music.
func mediaPlayers(ctx context.Context, appBus string) map[int]bool {
	ctx, cancel := context.WithTimeout(ctx, 800*time.Millisecond)
	defer cancel()
	target := "--user"
	if appBus != "" {
		target = "--address=" + appBus
	}
	bus := func(args ...string) []byte {
		out, err := exec.CommandContext(ctx, "busctl", append([]string{target, "--json=short"}, args...)...).Output()
		if err != nil {
			return nil
		}
		return out
	}
	const dbus = "org.freedesktop.DBus"
	players := map[int]bool{}
	for _, name := range parseBusNames(bus("call", dbus, "/org/freedesktop/DBus", dbus, "ListNames")) {
		if !parseBusPlaying(bus("get-property", name, "/org/mpris/MediaPlayer2", "org.mpris.MediaPlayer2.Player", "PlaybackStatus")) {
			continue
		}
		if pid := parseBusPID(bus("call", dbus, "/org/freedesktop/DBus", dbus, "GetConnectionUnixProcessID", "s", name)); pid > 0 {
			players[pid] = true
		}
	}
	return players
}

func subscribeAudio(ctx context.Context, signal func()) {
	cmd := exec.CommandContext(ctx, "pactl", "subscribe")
	out, err := cmd.StdoutPipe()
	if err != nil || cmd.Start() != nil {
		return
	}
	scan := bufio.NewScanner(out)
	for scan.Scan() {
		if strings.Contains(scan.Text(), "sink-input") {
			signal()
		}
	}
	_ = cmd.Wait()
}
