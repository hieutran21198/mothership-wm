//go:build linux && cgo && wlroots

package main

/*
#cgo pkg-config: wlroots-0.20 wayland-client wayland-server wayland-protocols pixman-1 xkbcommon cairo pangocairo libdrm
#cgo CFLAGS: -DWLR_USE_UNSTABLE -I${SRCDIR}
#include <stdint.h>
struct mothership_control;
struct mothership_control *mothership_control_create(int parent_fd, int stop_write_fd);
void mothership_control_destroy(struct mothership_control *control);
int mothership_control_serve(struct mothership_control *control);
void mothership_control_interrupt(struct mothership_control *control);
int mothership_wm_run(struct mothership_control *control, int stop_read_fd, int domain_fd, const char **reason);
*/
import "C"

import (
	"context"
	"crypto/rand"
	"encoding/hex"
	"fmt"
	"net"
	"os"
	"syscall"
	"time"

	"mothership-wm/internal/domainipc"
)

type nativeControl struct {
	handle *C.struct_mothership_control
}

func newNativeControl(parent, stopWrite *os.File) nativeControl {
	return nativeControl{handle: C.mothership_control_create(C.int(parent.Fd()), C.int(stopWrite.Fd()))}
}

func (c nativeControl) close() {
	C.mothership_control_destroy(c.handle)
}

func (c nativeControl) serve() bool {
	return C.mothership_control_serve(c.handle) != 0
}

func (c nativeControl) interrupt() {
	C.mothership_control_interrupt(c.handle)
}

func (c nativeControl) watch(ctx context.Context) func() {
	finished := make(chan struct{})
	cancelWatch := context.AfterFunc(ctx, func() {
		defer close(finished)
		c.interrupt()
	})
	return func() {
		// Keep the control and its original fds alive until a running callback ends.
		if !cancelWatch() {
			<-finished
		}
	}
}

func launchNested(ctx context.Context, parentPath, domainPath string) error {
	// Pass an explicit connection fd. libwayland's normal connect API can
	// otherwise prefer the inherited WAYLAND_SOCKET over the selected path.
	conn, err := (&net.Dialer{}).DialContext(ctx, "unix", parentPath)
	if err != nil {
		return fmt.Errorf("connect to nested Wayland parent %q: %w", parentPath, err)
	}
	parent, err := conn.(*net.UnixConn).File()
	conn.Close()
	if err != nil {
		return fmt.Errorf("retain Wayland parent connection: %w", err)
	}
	defer parent.Close()

	readStop, writeStop, err := os.Pipe()
	if err != nil {
		return fmt.Errorf("create shutdown pipe: %w", err)
	}
	defer readStop.Close()
	defer writeStop.Close()
	control := newNativeControl(parent, writeStop)
	if control.handle == nil {
		return fmt.Errorf("allocate native shutdown control")
	}
	defer control.close()
	defer control.watch(ctx)()

	var domainFD C.int = -1
	if domainPath != "" {
		peer, err := connectDesktop(ctx, domainPath)
		if err != nil {
			return fmt.Errorf("connect to desktopd: %w", err)
		}
		defer peer.Close()
		domainFD = C.int(peer.Fd())
	}

	var reason *C.char
	if C.mothership_wm_run(control.handle, C.int(readStop.Fd()), domainFD, &reason) != 0 {
		return fmt.Errorf("nested Wayland backend: %s", C.GoString(reason))
	}
	return nil
}

func connectDesktop(ctx context.Context, path string) (*os.File, error) {
	c, err := domainipc.Dial(ctx, path)
	if err != nil {
		return nil, err
	}
	defer c.Close()
	stop := context.AfterFunc(ctx, func() { c.Close() })
	defer stop()
	var nonce [16]byte
	rand.Read(nonce[:])
	if err := domainipc.Write(c, "wm "+hex.EncodeToString(nonce[:])); err != nil {
		return nil, err
	}
	_ = c.SetReadDeadline(time.Now().Add(domainipc.Timeout))
	reply, err := domainipc.Read(c)
	if err != nil {
		return nil, err
	}
	if reply != "ok" {
		return nil, fmt.Errorf("registration refused: %s", reply)
	}
	f, err := c.File()
	if err != nil {
		return nil, err
	}
	if err := syscall.SetNonblock(int(f.Fd()), true); err != nil {
		f.Close()
		return nil, err
	}
	return f, nil
}
