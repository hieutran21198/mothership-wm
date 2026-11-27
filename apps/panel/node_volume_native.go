//go:build linux && cgo && panel

package main

/*
#include <stdlib.h>
#include "node-volume.h"
*/
import "C"

import (
	"context"
	"fmt"
	"os"
	"os/exec"
	"strconv"
	"strings"
	"time"
	"unsafe"

	"mothership-wm/internal/domainipc"
)

type volumeRequest struct {
	kind       int
	node       string
	pid, value int
}

var nodeVolumeRequests = make(chan volumeRequest, 64)

// audioHoldRequests carries the latest Silent running audio hold; a newer
// value replaces one that has not been read yet, so a change is never lost.
var audioHoldRequests = make(chan string, 1)

func setAudioHold(node string) {
	for {
		select {
		case audioHoldRequests <- node:
			return
		default:
			select {
			case <-audioHoldRequests:
			default:
			}
		}
	}
}

// markAudioHold keeps a marker while a hold is active, so a restarted Panel
// can warn that it cannot know which streams a crashed Panel muted.
func markAudioHold(active bool) {
	if silentMarker == "" {
		return
	}
	if active {
		_ = os.WriteFile(silentMarker, nil, 0o600)
	} else {
		_ = os.Remove(silentMarker)
	}
}

const (
	volumeWatch = iota
	volumeHide
	volumeSet
	volumeMute
	volumeToggleNode
)

func captureAudio(ctx context.Context) ([]audioStream, error) {
	ctx, cancel := context.WithTimeout(ctx, 1200*time.Millisecond)
	defer cancel()
	out, err := exec.CommandContext(ctx, "pactl", "--format=json", "list", "sink-inputs").Output()
	if err != nil {
		return nil, fmt.Errorf("Audio service unavailable.")
	}
	return parseAudioStreams(out)
}

func audioSet(ctx context.Context, key audioKey, method, value string) error {
	ctx, cancel := context.WithTimeout(ctx, 700*time.Millisecond)
	defer cancel()
	if err := exec.CommandContext(ctx, "pactl", method, strconv.FormatUint(uint64(key.Index), 10), value).Run(); err != nil {
		return fmt.Errorf("An app ended or could not be updated.")
	}
	return nil
}

func muteValue(muted bool) string {
	if muted {
		return "1"
	}
	return "0"
}

func renderNodeAudio(node string, groups []audioGroup, muted bool, message string) {
	clean := strings.NewReplacer("\t", " ", "\n", " ", "\r", " ", "\x00", "")
	var rows strings.Builder
	for _, g := range groups {
		fmt.Fprintf(&rows, "%d\t%d\t%d\t%d\t%s\t%s\n", g.PID, g.Volume, btoi(g.Mute), btoi(g.Writable), clean.Replace(g.Name), clean.Replace(g.Icon))
	}
	name, data, notice := C.CString(node), C.CString(rows.String()), C.CString(message)
	C.node_volume_post(name, data, C.int(btoi(muted)), notice)
	C.free(unsafe.Pointer(name))
	C.free(unsafe.Pointer(data))
	C.free(unsafe.Pointer(notice))
}

//export panelNodeVolumeRequest
func panelNodeVolumeRequest(kind C.int, node *C.char, pid, value C.int) {
	r := volumeRequest{int(kind), C.GoString(node), int(pid), int(value)}
	select {
	case nodeVolumeRequests <- r:
	default:
		renderNodeAudio(r.node, nil, false, "Volume controls are busy. Try again.")
	}
}

func audioSelection(c *controller) domainipc.Snapshot {
	c.mu.Lock()
	defer c.mu.Unlock()
	if !c.online {
		return domainipc.Snapshot{}
	}
	s := c.state
	s.Windows = append([]domainipc.Window(nil), s.Windows...)
	return s
}

func runNodeVolume(ctx context.Context, c *controller) {
	ticker := time.NewTicker(time.Second)
	defer ticker.Stop()
	m := audioMuteState{nodes: map[string]bool{}, restore: map[audioKey]string{}}
	defer func() {
		if len(m.restore) == 0 {
			return
		}
		cleanup, cancel := context.WithTimeout(context.Background(), 2*time.Second)
		defer cancel()
		m.nodes = map[string]bool{}
		m.hold = ""
		if all, err := captureAudio(cleanup); err == nil {
			_ = m.reconcile(all, nil, func(key audioKey, muted bool) error {
				return audioSet(cleanup, key, "set-sink-input-mute", muteValue(muted))
			})
		}
	}()

	defer markAudioHold(false)
	var watching, session string
	for {
		var req volumeRequest
		select {
		case <-ctx.Done():
			return
		case hold := <-audioHoldRequests:
			if hold == m.hold {
				continue
			}
			m.hold = hold
			markAudioHold(hold != "")
		case <-ticker.C:
			if watching == "" && len(m.nodes) == 0 && len(m.restore) == 0 && m.hold == "" {
				continue
			}
		case req = <-nodeVolumeRequests:
			if req.kind == volumeHide {
				watching = ""
				continue
			}
			if req.kind == volumeWatch {
				watching = req.node
			}
		}
		s := audioSelection(c)
		node := audioNodeSelected(s)
		if !s.WMReady {
			continue
		}
		if req.node != "" && req.node != node {
			watching = ""
			continue
		}
		if s.Session != session && s.Session != "" {
			m.nodes = map[string]bool{}
			session = s.Session
		}
		all, err := captureAudio(ctx)
		if err != nil {
			if watching != "" && watching == node {
				renderNodeAudio(watching, nil, m.nodes[watching], err.Error())
			}
			continue
		}
		routed := routeAudio(all, s.Windows, procAudioParent)
		groups := audioGroups(routed, node)
		switch req.kind {
		case volumeToggleNode:
			if len(groups) != 0 || m.nodes[node] {
				if m.nodes[node] {
					delete(m.nodes, node)
				} else {
					m.nodes[node] = true
				}
			}
		case volumeMute, volumeSet:
			for _, g := range groups {
				if g.PID != req.pid {
					continue
				}
				for _, item := range g.Streams {
					method, value := "set-sink-input-mute", muteValue(!g.Mute)
					if req.kind == volumeSet {
						if !g.Writable {
							break
						}
						method, value = "set-sink-input-volume", strconv.Itoa(max(0, min(100, req.value)))+"%"
					} else if m.nodes[node] {
						break
					}
					if e := audioSet(ctx, item.key, method, value); e != nil {
						err = e
					}
				}
				break
			}
		}
		if e := m.reconcile(all, routed, func(key audioKey, muted bool) error {
			return audioSet(ctx, key, "set-sink-input-mute", muteValue(muted))
		}); e != nil {
			err = e
		}
		if req.kind >= volumeSet && watching == node {
			if observed, e := captureAudio(ctx); e == nil {
				groups = audioGroups(routeAudio(observed, audioSelection(c).Windows, procAudioParent), node)
			}
		}
		if watching != "" && watching == audioNodeSelected(audioSelection(c)) {
			msg := ""
			if err != nil {
				msg = err.Error()
			}
			renderNodeAudio(watching, groups, m.nodes[watching], msg)
		}
	}
}
