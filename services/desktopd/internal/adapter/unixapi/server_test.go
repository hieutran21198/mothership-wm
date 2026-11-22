package unixapi

import (
	"context"
	"fmt"
	"net"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"mothership-wm/internal/domainipc"
	"mothership-wm/services/desktopd/internal/app"
)

func TestPrivateProtocolPublishesAndWaitsForApply(t *testing.T) {
	path := filepath.Join(t.TempDir(), "desktopd.sock")
	l, err := net.ListenUnix("unixpacket", &net.UnixAddr{Name: path, Net: "unixpacket"})
	if err != nil {
		t.Fatal(err)
	}
	ctx, cancel := context.WithCancel(context.Background())
	done := make(chan error, 1)
	go func() { done <- Serve(ctx, l, &app.Service{}) }()
	t.Cleanup(func() {
		cancel()
		if err := <-done; err != nil {
			t.Error(err)
		}
	})
	dial := func() *net.UnixConn {
		t.Helper()
		c, err := domainipc.Dial(ctx, path)
		if err != nil {
			t.Fatal(err)
		}
		_ = c.SetReadDeadline(time.Now().Add(domainipc.Timeout))
		t.Cleanup(func() { c.Close() })
		return c
	}
	send := func(c *net.UnixConn, s string) {
		t.Helper()
		if err := domainipc.Write(c, s); err != nil {
			t.Fatal(err)
		}
	}
	receive := func(c *net.UnixConn) string {
		t.Helper()
		s, err := domainipc.Read(c)
		if err != nil {
			t.Fatal(err)
		}
		return s
	}
	cmd := func(s string) string {
		t.Helper()
		c := dial()
		defer c.Close()
		send(c, s)
		return receive(c)
	}
	wm := dial()
	const session = "0123456789abcdef0123456789abcdef"
	send(wm, "wm "+session)
	if got := receive(wm); got != "ok" {
		t.Fatal(got)
	}
	if got := cmd("create A"); got != "ok" {
		t.Fatal(got)
	}
	if got := cmd("focus A"); got != "err WM is not ready" {
		t.Fatal(got)
	}
	send(wm, "ready")
	send(wm, "new 7")
	send(wm, "map 7")
	awaitList := func(want string) {
		t.Helper()
		until := time.Now().Add(time.Second)
		for {
			if got := cmd("list"); strings.Contains(got, want) {
				return
			} else if time.Now().After(until) {
				t.Fatalf("list did not show %q: %q", want, got)
			}
			time.Sleep(time.Millisecond)
		}
	}
	awaitList("window 7 mapped")
	if got := cmd("assign " + session + " 7 A"); got != "ok" {
		t.Fatal(got)
	}
	send(wm, "unmap 7")
	awaitList("window 7 unmapped A")
	focus := dial()
	send(focus, "focus A")
	f := strings.Fields(receive(wm))
	if len(f) != 4 || f[0] != "focus" || f[2] != "7" || f[3] != "1" {
		t.Fatalf("expected unmapped member in projection: %v", f)
	}
	_ = focus.SetReadDeadline(time.Now().Add(25 * time.Millisecond))
	if got, err := domainipc.Read(focus); err == nil {
		t.Fatalf("reply before WM applied: %q", got)
	}
	_ = focus.SetReadDeadline(time.Now().Add(domainipc.Timeout))
	send(wm, fmt.Sprintf("applied %s", f[1]))
	if got := receive(focus); got != "ok" {
		t.Fatal(got)
	}
	send(wm, "close 7")
	// Once a new lifecycle event is processed, the old membership is gone.
	send(wm, "new 8")
	awaitList("window 8 unmapped")
	if got := cmd("assign " + session + " 7 A"); got != "err unknown Window ID" {
		t.Fatal(got)
	}
}
