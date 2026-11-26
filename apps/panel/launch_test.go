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

	"mothership-wm/internal/domainipc"
)

func TestAppCommandsStayOnNestedWayland(t *testing.T) {
	dir := t.TempDir()
	t.Setenv("PATH", dir)
	for _, name := range []string{"ghostty", "cosmic-files", "cosmic-term", "ark", "brave", "btop"} {
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
		qt     bool
	}{
		{openTerminal, []string{"ghostty", "--gtk-single-instance=false"}, false},
		{openFiles, []string{"cosmic-files", "--no-daemon"}, false},
		{openCosmicTerminal, []string{"cosmic-term"}, false},
		{openArk, []string{"ark"}, true},
		{openBrave, []string{"brave", "--ozone-platform=wayland", "--user-data-dir=" + filepath.Join(dir, "brave-profile"),
			"--no-first-run", "--no-default-browser-check", "--new-window", "about:blank"}, false},
		{openBtop, []string{"ghostty", "--gtk-single-instance=false", "-e", filepath.Join(dir, "btop")}, false},
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
		if tc.qt {
			want = append(want, "QT_QPA_PLATFORM=wayland")
		}
		if !slices.Equal(cmd.Env, want) {
			t.Fatalf("env: %v", cmd.Env)
		}
	}
	profile := filepath.Join(dir, "brave-profile")
	if err := os.Remove(profile); err != nil {
		t.Fatal(err)
	}
	if err := os.Symlink(t.TempDir(), profile); err != nil {
		t.Fatal(err)
	}
	if _, _, err := appCommand(context.Background(), openBrave, child, nil); err == nil {
		t.Fatal("Brave profile must not follow a symlink")
	}
	t.Setenv("PATH", t.TempDir())
	if _, _, err := appCommand(context.Background(), openFiles, child, nil); err == nil || !strings.Contains(err.Error(), "cosmic-files on PATH") {
		t.Fatalf("missing binary: %v", err)
	}
	if _, _, err := appCommand(context.Background(), openFiles, "host-name", nil); err == nil || !strings.Contains(err.Error(), "nested Wayland socket") {
		t.Fatalf("invalid socket: %v", err)
	}
}

func TestBraveRouteRequiresExactNestedProfileAndNewWindow(t *testing.T) {
	root := t.TempDir()
	const profile = "/nested/brave-profile"
	for pid, cmdline := range map[string]string{
		"24": "/nix/opt/brave --user-data-dir=" + profile + " --new-window about:blank\x00",
		"25": "/nix/opt/brave --user-data-dir=" + profile + "-other --new-window about:blank\x00",
		"21": "/nix/opt/brave\x00--user-data-dir=" + profile + "\x00--ozone-platform=wayland\x00",
		"22": "/nix/opt/brave\x00--user-data-dir=" + profile + "-other\x00",
		"23": "other\x00--user-data-dir=" + profile + "\x00",
	} {
		dir := filepath.Join(root, pid)
		if err := os.Mkdir(dir, 0700); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(filepath.Join(dir, "cmdline"), []byte(cmdline), 0600); err != nil {
			t.Fatal(err)
		}
	}
	for _, pid := range []string{"24", "25"} {
		if err := os.Symlink("/nix/opt/brave", filepath.Join(root, pid, "exe")); err != nil {
			t.Fatal(err)
		}
	}
	owned := func(pid int) bool { return ownsNestedBraveProfile(root, pid, profile) }
	if !owned(24) || owned(25) {
		t.Fatal("flattened process title did not require an exact profile")
	}
	windows := []domainipc.Window{
		{ID: 1, Mapped: true, AppID: "brave-browser", PID: 21},
		{ID: 2, Mapped: true, AppID: "brave-browser", PID: 22},
		{ID: 3, Mapped: true, AppID: "brave-browser", PID: 23},
		{ID: 4, Mapped: true, AppID: "brave-browser", PID: 21},
	}
	if got := braveLaunchWindow(windows, 1, owned); got != 4 {
		t.Fatalf("route %d, want 4", got)
	}
	windows = append(windows, domainipc.Window{ID: 5, Mapped: true, AppID: "brave-browser", PID: 21})
	if got := braveLaunchWindow(windows, 1, owned); got != 0 {
		t.Fatalf("ambiguous candidates: %d", got)
	}
}

func TestGridLaunchRoutesOnlyNewWindowsWithExactMarker(t *testing.T) {
	root := t.TempDir()
	for pid, env := range map[string]string{
		"11": launchTokenKey + "=other\x00",
		"12": "OTHER=private\x00" + launchTokenKey + "=wanted\x00",
		"13": launchTokenKey + "=wanted-suffix\x00",
	} {
		dir := filepath.Join(root, pid)
		if err := os.Mkdir(dir, 0700); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(filepath.Join(dir, "environ"), []byte(env), 0600); err != nil {
			t.Fatal(err)
		}
	}
	match := func(pid int) bool { return ownsGridLaunch(root, pid, "wanted") }
	windows := []domainipc.Window{
		{ID: 1, PID: 12, Mapped: true},
		{ID: 2, PID: 11, Mapped: true},
		{ID: 3, PID: 13, Mapped: true},
		{ID: 4, PID: 12, Mapped: true},
		{ID: 5, PID: 12}, // not mapped yet
		{ID: 6, PID: 12, Mapped: true, Domain: "Other"},
	}
	if got := gridLaunchWindow(windows, 1, match); got != 4 {
		t.Fatalf("got window %d, want detached window 4", got)
	}
	windows[4].Mapped = true
	if got := gridLaunchWindow(windows, 1, match); got != 0 {
		t.Fatalf("ambiguous launch matched window %d", got)
	}
}

func TestGridLaunchTokensAreUnique(t *testing.T) {
	a, b := newGridLaunchToken(), newGridLaunchToken()
	if len(a) != 32 || len(b) != 32 || a == b {
		t.Fatal("launch tokens must be distinct 128-bit values")
	}
}
