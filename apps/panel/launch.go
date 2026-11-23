package main

import (
	"context"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"sync"
	"syscall"
	"time"
)

type appAction int

const (
	openTerminal appAction = 5
	openFiles    appAction = 6
)

type appLauncher struct {
	ctx     context.Context
	display string
	owned   sync.WaitGroup
	report  func(string)
}

func appCommand(ctx context.Context, action appAction, display string, env []string) (*exec.Cmd, string, error) {
	var name, binary string
	var args []string
	switch action {
	case openTerminal:
		name, binary, args = "Terminal", "ghostty", []string{"--gtk-single-instance=false"}
	case openFiles:
		name, binary, args = "File Manager", "cosmic-files", []string{"--no-daemon"}
	default:
		return nil, "", fmt.Errorf("unknown app action")
	}
	info, err := os.Stat(display)
	if !filepath.IsAbs(display) || err != nil || info.Mode()&os.ModeSocket == 0 {
		return nil, name, fmt.Errorf("%s needs a live nested Wayland socket", name)
	}
	cmd := exec.CommandContext(ctx, binary, args...)
	if cmd.Err != nil {
		return nil, name, fmt.Errorf("%s needs %s on PATH", name, binary)
	}
	// Do not hand a request or activation token to a host desktop instance.
	// An explicit unavailable bus also prevents D-Bus autolaunch. This is
	// display routing, not isolation from the user's filesystem.
	for _, item := range env {
		key, _, _ := strings.Cut(item, "=")
		switch key {
		case "WAYLAND_DISPLAY", "WAYLAND_SOCKET", "DISPLAY", "GDK_BACKEND",
			"XDG_ACTIVATION_TOKEN", "DESKTOP_STARTUP_ID", "DBUS_SESSION_BUS_ADDRESS":
			continue
		}
		cmd.Env = append(cmd.Env, item)
	}
	cmd.Env = append(cmd.Env, "WAYLAND_DISPLAY="+display, "GDK_BACKEND=wayland",
		"DBUS_SESSION_BUS_ADDRESS=unix:path=/dev/null")
	cmd.Cancel = func() error { return cmd.Process.Signal(syscall.SIGTERM) }
	cmd.WaitDelay = time.Second
	return cmd, name, nil
}

// Called only from the GTK thread, before it exits and waits for these children.
func (l *appLauncher) start(action appAction) {
	cmd, name, err := appCommand(l.ctx, action, l.display, os.Environ())
	if err != nil {
		l.report(err.Error())
		return
	}
	if err := cmd.Start(); err != nil {
		l.report(fmt.Sprintf("Could not start %s: %v", name, err))
		return
	}
	l.report(name + " started. Look in Home / Inbox.")
	l.owned.Add(1)
	go func() {
		defer l.owned.Done()
		if err := cmd.Wait(); err != nil && l.ctx.Err() == nil {
			l.report(fmt.Sprintf("%s stopped: %v", name, err))
		}
	}()
}
