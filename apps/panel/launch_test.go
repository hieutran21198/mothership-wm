package main

import (
	"context"
	"net"
	"os"
	"path/filepath"
	"reflect"
	"slices"
	"strings"
	"testing"
)

func TestAppCommandsStayOnNestedWayland(t *testing.T) {
	dir := t.TempDir()
	t.Setenv("PATH", dir)
	for _, name := range []string{"ghostty", "cosmic-files"} {
		if err := os.WriteFile(filepath.Join(dir, name), []byte("#!/bin/sh\nexit 0\n"), 0700); err != nil {
			t.Fatal(err)
		}
	}
	child := filepath.Join(dir, "child")
	listen, err := net.Listen("unix", child)
	if err != nil {
		t.Fatal(err)
	}
	defer listen.Close()
	for _, tc := range []struct {
		action appAction
		args   []string
	}{
		{openTerminal, []string{"ghostty", "--gtk-single-instance=false"}},
		{openFiles, []string{"cosmic-files", "--no-daemon"}},
	} {
		cmd, _, err := appCommand(context.Background(), tc.action, child, []string{
			"WAYLAND_DISPLAY=/host", "WAYLAND_SOCKET=9", "DISPLAY=:0", "GDK_BACKEND=x11",
			"DBUS_SESSION_BUS_ADDRESS=unix:path=/host-bus", "XDG_ACTIVATION_TOKEN=host-token",
			"DESKTOP_STARTUP_ID=host-start", "HOME=/same-home",
		})
		if err != nil {
			t.Fatal(err)
		}
		if !reflect.DeepEqual(cmd.Args, tc.args) {
			t.Fatalf("args: %v", cmd.Args)
		}
		want := []string{"HOME=/same-home", "WAYLAND_DISPLAY=" + child, "GDK_BACKEND=wayland", "DBUS_SESSION_BUS_ADDRESS=unix:path=/dev/null"}
		if !slices.Equal(cmd.Env, want) {
			t.Fatalf("env: %v", cmd.Env)
		}
	}
	t.Setenv("PATH", t.TempDir())
	if _, _, err := appCommand(context.Background(), openFiles, child, nil); err == nil || !strings.Contains(err.Error(), "cosmic-files on PATH") {
		t.Fatalf("missing binary: %v", err)
	}
	if _, _, err := appCommand(context.Background(), openFiles, "host-name", nil); err == nil || !strings.Contains(err.Error(), "nested Wayland socket") {
		t.Fatalf("invalid socket: %v", err)
	}
}
