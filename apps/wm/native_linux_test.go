//go:build linux && cgo && wlroots

package main

import (
	"context"
	"errors"
	"io"
	"net"
	"os"
	"path/filepath"
	"syscall"
	"testing"
	"time"
)

func localSocketPair(t *testing.T) (*os.File, net.Conn) {
	t.Helper()
	fds, err := syscall.Socketpair(syscall.AF_UNIX, syscall.SOCK_STREAM|syscall.SOCK_CLOEXEC, 0)
	if err != nil {
		t.Fatal(err)
	}
	local := os.NewFile(uintptr(fds[0]), "test-local-wayland")
	peerFile := os.NewFile(uintptr(fds[1]), "test-peer-wayland")
	t.Cleanup(func() { local.Close() })
	defer peerFile.Close()
	peer, err := net.FileConn(peerFile)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { peer.Close() })
	if err := peer.SetReadDeadline(time.Now().Add(2 * time.Second)); err != nil {
		t.Fatal(err)
	}
	return local, peer
}

func controlForTest(t *testing.T, local *os.File) (nativeControl, *os.File) {
	t.Helper()
	readStop, writeStop, err := os.Pipe()
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { readStop.Close(); writeStop.Close() })
	if err := readStop.SetReadDeadline(time.Now().Add(2 * time.Second)); err != nil {
		t.Fatal(err)
	}
	control := newNativeControl(local, writeStop)
	if control.handle == nil {
		t.Fatal("create shutdown control")
	}
	t.Cleanup(control.close)
	return control, readStop
}

func assertSocketUsable(t *testing.T, local *os.File, peer net.Conn) {
	t.Helper()
	if _, err := local.Write([]byte{7}); err != nil {
		t.Fatal(err)
	}
	var data [1]byte
	if _, err := io.ReadFull(peer, data[:]); err != nil || data[0] != 7 {
		t.Fatalf("peer read %v, %v", data, err)
	}
}

func TestNativeControlCancelsOnlyItsStartupSocket(t *testing.T) {
	local, peer := localSocketPair(t)
	another, otherPeer := localSocketPair(t)
	control, _ := controlForTest(t, local)
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	join := control.watch(ctx)
	defer join()
	cancel()

	var data [1]byte
	if _, err := peer.Read(data[:]); !errors.Is(err, io.EOF) {
		t.Fatalf("startup socket did not close on cancellation: %v", err)
	}
	if control.serve() {
		t.Fatal("canceled startup could still publish readiness")
	}
	assertSocketUsable(t, another, otherPeer)
}

func TestNativeControlKeepsServingSocketForGracefulShutdown(t *testing.T) {
	local, peer := localSocketPair(t)
	control, readStop := controlForTest(t, local)
	if !control.serve() {
		t.Fatal("could not enter serving phase")
	}
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	join := control.watch(ctx)
	defer join()
	cancel()

	var signal [1]byte
	if _, err := io.ReadFull(readStop, signal[:]); err != nil || signal[0] != 1 {
		t.Fatalf("serving event loop did not receive stop byte: %v, %v", signal, err)
	}
	assertSocketUsable(t, local, peer)
}

func TestNativeStartupCancelUnblocksRoundtrip(t *testing.T) {
	// The peer is an inert test socket, not a Wayland compositor or GUI.
	dir, err := os.MkdirTemp("", "mship-wm-")
	if err != nil {
		t.Fatal(err)
	}
	defer os.RemoveAll(dir)
	path := filepath.Join(dir, "parent")
	listener, err := net.ListenUnix("unix", &net.UnixAddr{Name: path, Net: "unix"})
	if err != nil {
		t.Fatal(err)
	}
	defer listener.Close()
	if err := listener.SetDeadline(time.Now().Add(2 * time.Second)); err != nil {
		t.Fatal(err)
	}
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	done := make(chan error, 1)
	go func() { done <- launchNested(ctx, path, "") }()

	peer, err := listener.AcceptUnix()
	if err != nil {
		t.Fatal(err)
	}
	defer peer.Close()
	if err := peer.SetReadDeadline(time.Now().Add(2 * time.Second)); err != nil {
		t.Fatal(err)
	}
	// Wait until libwayland starts talking to the peer; never reply to its roundtrip.
	var header [8]byte
	if _, err := io.ReadFull(peer, header[:]); err != nil {
		t.Fatal(err)
	}
	cancel()
	select {
	case err := <-done:
		if err != nil {
			t.Fatalf("intentional startup cancellation returned an error: %v", err)
		}
	case <-time.After(2 * time.Second):
		t.Fatal("cancellation did not unwind the blocked parent roundtrip")
	}
}
