package main

import (
	"bytes"
	"context"
	"fmt"
	"io"
	"net"
	"os"
	"os/signal"
	"path/filepath"
	"strings"
	"syscall"
	"testing"
	"time"
)

func TestMain(m *testing.M) {
	if os.Getenv("MOTHERSHIP_LAUNCH_TEST_CHILD") == "1" {
		ctx, stop := signal.NotifyContext(context.Background(), syscall.SIGTERM)
		defer stop()
		switch filepath.Base(os.Args[0]) {
		case "mothership-desktopd":
			fmt.Println("DESKTOPD_READY")
		case "mothership-wm":
			fmt.Println("WAYLAND_DISPLAY=wayland-0")
		case "mothership-command-center":
			os.Exit(23)
		}
		<-ctx.Done()
		os.Exit(0)
	}
	os.Exit(m.Run())
}

func TestFailedShellCleansOwnedSessionOnly(t *testing.T) {
	host, bin := t.TempDir(), t.TempDir()
	if err := os.Chmod(host, 0700); err != nil {
		t.Fatal(err)
	}
	socket := filepath.Join(host, "host-display")
	parent, err := net.ListenUnix("unix", &net.UnixAddr{Name: socket, Net: "unix"})
	if err != nil {
		t.Fatal(err)
	}
	defer parent.Close()
	sentinel := filepath.Join(host, "unrelated")
	if err := os.WriteFile(sentinel, []byte("preserve"), 0600); err != nil {
		t.Fatal(err)
	}
	exe, err := os.Executable()
	if err != nil {
		t.Fatal(err)
	}
	for _, name := range []string{"mothership-desktopd", "mothership-wm", "mothership-command-center"} {
		if err := os.Symlink(exe, filepath.Join(bin, name)); err != nil {
			t.Fatal(err)
		}
	}
	t.Setenv("MOTHERSHIP_LAUNCH_TEST_CHILD", "")
	env := []string{"XDG_RUNTIME_DIR=" + host, "WAYLAND_DISPLAY=host-display",
		"WAYLAND_SOCKET=ignore", "MOTHERSHIP_LAUNCH_TEST_CHILD=1"}
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	var output bytes.Buffer
	err = run(ctx, bin, env, &output, io.Discard)
	if err == nil || !strings.Contains(err.Error(), "Shell exited before readiness: exit status 23") ||
		strings.Contains(output.String(), "ready in Home") {
		t.Fatalf("unexpected startup outcome: %v, output %q", err, output.String())
	}
	if paths, err := filepath.Glob(filepath.Join(host, "mothership-*")); err != nil || len(paths) != 0 {
		t.Fatalf("session leaked dirs: %v, %v", paths, err)
	}
	if data, err := os.ReadFile(sentinel); err != nil || string(data) != "preserve" {
		t.Fatalf("touched sibling: %q, %v", data, err)
	}
	if info, err := os.Stat(socket); err != nil || info.Mode()&os.ModeSocket == 0 {
		t.Fatalf("touched parent socket: %v", err)
	}
}
