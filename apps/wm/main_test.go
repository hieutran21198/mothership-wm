package main

import (
	"bytes"
	"context"
	"errors"
	"strings"
	"testing"
)

func TestMissingParentFailsEvenWithOtherBackendsAvailable(t *testing.T) {
	env := map[string]string{
		"XDG_RUNTIME_DIR": "/run/user/1000",
		"DISPLAY":         ":1",
		"WAYLAND_SOCKET":  "9",
		"WLR_BACKENDS":    "drm,headless",
	}
	var stderr bytes.Buffer
	code := run(context.Background(), nil, func(k string) string { return env[k] },
		func(context.Context, string) error {
			t.Fatal("launcher was called without a selected parent")
			return nil
		}, &stderr)
	if code != 1 || !strings.Contains(stderr.String(), "no parent Wayland display") {
		t.Fatalf("got code=%d stderr=%q", code, stderr.String())
	}
}

func TestNestedParentSelection(t *testing.T) {
	tests := []struct {
		name string
		args []string
		want string
	}{
		{"environment", nil, "/run/user/1000/wayland-1"},
		{"explicit name", []string{"--parent", "wayland-2"}, "/run/user/1000/wayland-2"},
		{"absolute override", []string{"--parent", "/tmp/test/wayland-test"}, "/tmp/test/wayland-test"},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			env := map[string]string{
				"XDG_RUNTIME_DIR": "/run/user/1000", "WAYLAND_DISPLAY": "wayland-1",
			}
			var stderr bytes.Buffer
			var got string
			code := run(context.Background(), tt.args, func(k string) string { return env[k] },
				func(_ context.Context, path string) error {
					got = path
					return nil
				}, &stderr)
			if code != 0 || got != tt.want || stderr.Len() != 0 {
				t.Fatalf("got code=%d path=%q stderr=%q; want path=%q", code, got, stderr.String(), tt.want)
			}
		})
	}
}

func TestRejectInvalidOrUnexpectedBackend(t *testing.T) {
	for _, args := range [][]string{
		{"--parent", "../wayland-1"}, {"--parent", "."}, {"--backend", "drm"},
		{"--backend", "headless"}, {"--backend", "x11"}, {"/tmp/socket"},
	} {
		env := map[string]string{"XDG_RUNTIME_DIR": "/run/user/1000", "WAYLAND_DISPLAY": "wayland-1"}
		var stderr bytes.Buffer
		code := run(context.Background(), args, func(k string) string { return env[k] },
			func(context.Context, string) error {
				t.Fatalf("invalid flags %q reached launcher", args)
				return nil
			}, &stderr)
		if code == 0 || stderr.Len() == 0 {
			t.Fatalf("invalid flags %q got code=%d stderr=%q", args, code, stderr.String())
		}
	}
}

func TestRuntimeDirIsRequiredEvenWithAbsoluteParent(t *testing.T) {
	for _, runtimeDir := range []string{"", "relative"} {
		if _, err := parentSocket(runtimeDir, "/tmp/wayland"); err == nil {
			t.Fatalf("accepted XDG_RUNTIME_DIR=%q", runtimeDir)
		}
	}
}

func TestNestedFailureDoesNotRetry(t *testing.T) {
	env := map[string]string{"XDG_RUNTIME_DIR": "/run/user/1000", "WAYLAND_DISPLAY": "wayland-1"}
	var stderr bytes.Buffer
	calls := 0
	code := run(context.Background(), nil, func(k string) string { return env[k] },
		func(context.Context, string) error {
			calls++
			return errors.New("output failed")
		}, &stderr)
	if code != 1 || calls != 1 || !strings.Contains(stderr.String(), "output failed") {
		t.Fatalf("got code=%d calls=%d stderr=%q", code, calls, stderr.String())
	}
}

func TestCancellationIsAQuietExit(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	cancel()
	env := map[string]string{"XDG_RUNTIME_DIR": "/run/user/1000", "WAYLAND_DISPLAY": "wayland-1"}
	var stderr bytes.Buffer
	code := run(ctx, nil, func(k string) string { return env[k] },
		func(ctx context.Context, _ string) error { return ctx.Err() }, &stderr)
	if code != 0 || stderr.Len() != 0 {
		t.Fatalf("got code=%d stderr=%q", code, stderr.String())
	}
}
