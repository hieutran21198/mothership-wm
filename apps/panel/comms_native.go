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

func postComms(nodes map[string]bool) {
	names := make([]string, 0, len(nodes))
	for n := range nodes {
		names = append(names, n)
	}
	sort.Strings(names)
	var b strings.Builder
	for _, n := range names {
		fmt.Fprintf(&b, "%s\t%d\n", n, btoi(nodes[n]))
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
func runComms(ctx context.Context, c *controller) {
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
			postComms(commsNodes(routeAudio(all, audioSelection(c).Windows, procAudioParent)))
		}
	}
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
