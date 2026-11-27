//go:build linux && cgo && panel

package main

/*
#include <stdlib.h>
void systems_board_sound_post(const char *);
*/
import "C"

import (
	"bufio"
	"context"
	"encoding/json"
	"fmt"
	"os/exec"
	"strings"
	"time"
	"unsafe"
)

type soundOp struct {
	op    int
	name  string
	value int
}

var (
	systemsOpen = make(chan bool, 1)
	systemsOps  = make(chan soundOp, 32)
)

const soundUnavailable = "Sound unavailable: no PulseAudio-compatible server or pactl."

//export panelSystemsWatch
func panelSystemsWatch(on C.int) {
	for {
		select {
		case systemsOpen <- on != 0:
			return
		default:
			select {
			case <-systemsOpen:
			default:
			}
		}
	}
}

//export panelSystemsSound
func panelSystemsSound(op C.int, name *C.char, value C.int) {
	select {
	case systemsOps <- soundOp{int(op), C.GoString(name), int(value)}:
	default:
	}
}

func postSound(text string) {
	c := C.CString(text)
	C.systems_board_sound_post(c)
	C.free(unsafe.Pointer(c))
}

// runSystems keeps the Systems button's mute marks and the board's sound
// rows current. One `pactl subscribe` reacts to device and default changes
// only; stream events are ignored, so playback costs nothing here.
func runSystems(ctx context.Context) {
	if _, err := exec.LookPath("pactl"); err != nil {
		postSound(soundPost(nil, nil, soundUnavailable))
		return
	}
	refresh := make(chan struct{}, 1)
	signal := func() {
		select {
		case refresh <- struct{}{}:
		default:
		}
	}
	go superviseSoundEvents(ctx, signal)
	signal()
	for {
		select {
		case <-ctx.Done():
			return
		case on := <-systemsOpen:
			if on {
				signal()
			}
		case op := <-systemsOps:
			if err := runSoundOp(ctx, op); err != nil {
				postSound(fmt.Sprintf("fail\t%d\t%s\t%s\n", op.op, op.name, oneLine(err.Error())))
			}
			signal()
		case <-refresh:
			// Coalesce a burst of events (a slider drag) into one sample.
			select {
			case <-time.After(120 * time.Millisecond):
			case <-ctx.Done():
				return
			}
			select {
			case <-refresh:
			default:
			}
			postSound(sampleSound(ctx))
		}
	}
}

func runSoundOp(ctx context.Context, op soundOp) error {
	args, ok := soundArgs(op.op, op.name, op.value)
	if !ok {
		return fmt.Errorf("invalid request")
	}
	run, cancel := context.WithTimeout(ctx, 2*time.Second)
	defer cancel()
	out, err := exec.CommandContext(run, "pactl", args...).CombinedOutput()
	if err != nil {
		if msg := strings.TrimSpace(string(out)); msg != "" {
			return fmt.Errorf("%s", msg)
		}
		return err
	}
	return nil
}

func sampleSound(ctx context.Context) string {
	run, cancel := context.WithTimeout(ctx, 2500*time.Millisecond)
	defer cancel()
	pactl := func(args ...string) ([]byte, error) {
		return exec.CommandContext(run, "pactl", append([]string{"--format=json"}, args...)...).Output()
	}
	raw, err := pactl("info")
	if err != nil {
		return soundPost(nil, nil, soundUnavailable)
	}
	var info pactlInfo
	_ = json.Unmarshal(raw, &info)
	rawSinks, err := pactl("list", "sinks")
	if err != nil {
		return soundPost(nil, nil, soundUnavailable)
	}
	rawSources, err := pactl("list", "sources")
	if err != nil {
		return soundPost(nil, nil, soundUnavailable)
	}
	sinks, err1 := soundDevices(rawSinks, info.DefaultSink, false)
	sources, err2 := soundDevices(rawSources, info.DefaultSource, true)
	if err1 != nil || err2 != nil {
		return soundPost(nil, nil, "Sound unavailable: pactl returned an unreadable device list.")
	}
	return soundPost(sinks, sources, "")
}

// superviseSoundEvents restarts the subscription when the sound server
// restarts, backing off so a missing server does not spin.
func superviseSoundEvents(ctx context.Context, signal func()) {
	wait := time.Second
	for ctx.Err() == nil {
		started := time.Now()
		subscribeDevices(ctx, signal)
		signal()
		if time.Since(started) > time.Minute {
			wait = time.Second
		}
		select {
		case <-ctx.Done():
			return
		case <-time.After(wait):
		}
		wait = min(wait*2, time.Minute)
	}
}

func subscribeDevices(ctx context.Context, signal func()) {
	cmd := exec.CommandContext(ctx, "pactl", "subscribe")
	out, err := cmd.StdoutPipe()
	if err != nil || cmd.Start() != nil {
		return
	}
	scan := bufio.NewScanner(out)
	for scan.Scan() {
		if deviceEvent(scan.Text()) {
			signal()
		}
	}
	_ = cmd.Wait()
}
