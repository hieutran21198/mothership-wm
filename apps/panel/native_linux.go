//go:build linux && cgo && panel

package main

/*
#cgo pkg-config: gtk4
#cgo LDFLAGS: -lgtk4-layer-shell
#include <stdlib.h>
#include "native.h"
*/
import "C"

import (
	"context"
	"flag"
	"fmt"
	"os"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"unsafe"

	"mothership-wm/internal/domainipc"
)

var active *controller

func main() {
	nested := flag.String("nested", "", "distinct child Wayland socket reported by Mothership")
	flag.Parse()
	path, err := domainipc.Path(os.Getenv("XDG_RUNTIME_DIR"))
	if err != nil {
		fail(err)
	}
	socket := *nested
	if socket == "" || socket == "." || socket == ".." || filepath.Base(socket) != socket ||
		socket == os.Getenv("WAYLAND_DISPLAY") || os.Getenv("WAYLAND_SOCKET") != "" {
		fail(fmt.Errorf("specify a distinct --nested socket without inherited WAYLAND_SOCKET"))
	}
	info, err := os.Stat(filepath.Join(filepath.Dir(path), socket))
	if err != nil || info.Mode()&os.ModeSocket == 0 {
		fail(fmt.Errorf("nested Wayland socket is unavailable"))
	}
	_ = os.Setenv("WAYLAND_DISPLAY", socket)
	runtime.LockOSThread()
	if C.panel_init() == 0 {
		fail(fmt.Errorf("could not open native Command Center in the nested WM"))
	}
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	active = &controller{ctx: ctx, path: path, show: showNative}
	go active.run()
	C.panel_run()
}

func fail(err error) {
	fmt.Fprintln(os.Stderr, "mothership-command-center:", err)
	os.Exit(1)
}

func showNative(v view) {
	var ws strings.Builder
	for _, w := range v.windows {
		fmt.Fprintf(&ws, "%s\t%s\n", w.key, w.label)
	}
	s := []string{v.title, v.message, strings.Join(v.domains, "\n"), ws.String()}
	ptr := make([]*C.char, len(s))
	for i, value := range s {
		ptr[i] = C.CString(value)
		defer C.free(unsafe.Pointer(ptr[i]))
	}
	var enabled C.int
	if v.enabled {
		enabled = 1
	}
	C.panel_post(ptr[0], ptr[1], ptr[2], ptr[3], enabled)
}

//export panelAction
func panelAction(op C.int, a, b *C.char) {
	name, key := C.GoString(a), C.GoString(b)
	switch op {
	case 1:
		active.submit(domainipc.CreateMethod, domainipc.DomainBody{Domain: strings.TrimSpace(name)})
	case 2:
		session, raw, _ := strings.Cut(key, ":")
		id, err := strconv.ParseUint(raw, 10, 64)
		if err == nil {
			active.submit(domainipc.AssignMethod, domainipc.AssignBody{Session: session, WindowID: id, Domain: name})
		}
	case 3:
		active.submit(domainipc.FocusMethod, domainipc.DomainBody{Domain: name})
	case 4:
		active.submit(domainipc.HomeMethod, domainipc.Empty{})
	}
}
