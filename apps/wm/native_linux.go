//go:build linux && cgo && wlroots

package main

/*
#cgo pkg-config: wlroots-0.20 wayland-client wayland-server wayland-protocols pixman-1 xkbcommon cairo pangocairo libdrm
#cgo CFLAGS: -DWLR_USE_UNSTABLE -I${SRCDIR}
#include <stdint.h>
#include <stdlib.h>
#include "group-chrome.h"
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
	"runtime"
	"syscall"
	"time"
	"unsafe"

	"mothership-wm/internal/domainipc"
	"mothership-wm/internal/shellfonts"
	"mothership-wm/internal/userconfig"
)

type nativeControl struct {
	handle *C.struct_mothership_control
}

func newNativeControl(parent, stopWrite *os.File) nativeControl {
	fd := C.int(-1)
	if parent != nil {
		fd = C.int(parent.Fd())
	}
	return nativeControl{handle: C.mothership_control_create(fd, C.int(stopWrite.Fd()))}
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
	// Keep font setup and Group rendering on the compositor thread.
	runtime.LockOSThread()
	defer runtime.UnlockOSThread()
	if fonts, cleanup, err := shellfonts.Extract(os.Getenv("XDG_RUNTIME_DIR")); err == nil {
		defer cleanup()
		defer C.group_chrome_clear_fonts()
		for _, font := range fonts {
			path := C.CString(font)
			if !C.group_chrome_add_font(path) {
				fmt.Fprintf(os.Stderr, "mothership-wm: could not register shell font %q\n", font)
			}
			C.free(unsafe.Pointer(path))
		}
	} else {
		fmt.Fprintf(os.Stderr, "mothership-wm: shell fonts: %v\n", err)
	}

	// Pass an explicit connection fd. libwayland's normal connect API can
	// otherwise prefer the inherited WAYLAND_SOCKET over the selected path.
	var parent *os.File
	if parentPath != "" {
		conn, err := (&net.Dialer{}).DialContext(ctx, "unix", parentPath)
		if err != nil {
			return fmt.Errorf("connect to nested Wayland parent %q: %w", parentPath, err)
		}
		parent, err = conn.(*net.UnixConn).File()
		conn.Close()
		if err != nil {
			return fmt.Errorf("retain Wayland parent connection: %w", err)
		}
		defer parent.Close()
	}

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
		kind := "nested"
		if parentPath == "" {
			kind = "native"
		}
		return fmt.Errorf("%s Wayland backend: %s", kind, C.GoString(reason))
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

//export wmLoadCursorSize
func wmLoadCursorSize() C.int {
	home, _ := os.UserHomeDir()
	doc, _, err := userconfig.Read(userconfig.Path(home))
	if err != nil {
		return C.int(userconfig.CursorSizeDefault)
	}
	return C.int(userconfig.CursorSize(doc))
}

//export wmLoadDisplay
func wmLoadDisplay(connector *C.char, w, h, hz, scale *C.int) {
	home, _ := os.UserHomeDir()
	p, err := readDisplay(userconfig.Path(home), C.GoString(connector))
	if err != nil {
		fmt.Fprintf(os.Stderr, "mothership-wm display settings: %v\n", err)
		return
	}
	*w, *h, *hz, *scale = C.int(p.Width), C.int(p.Height), C.int(p.Refresh), C.int(p.Scale)
}

//export wmSaveDisplay
func wmSaveDisplay(connector *C.char, w, h, hz, scale C.int) *C.char {
	home, _ := os.UserHomeDir()
	p := displayPreference{C.GoString(connector), int(w), int(h), int(hz), int(scale)}
	if err := saveDisplay(userconfig.Path(home), p); err != nil {
		return C.CString(err.Error())
	}
	return nil
}
