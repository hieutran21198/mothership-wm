//go:build linux && cgo && tray

// Package tray adapts the session's StatusNotifier and DBusMenu protocols.
package tray

/*
#cgo pkg-config: gio-2.0
#include <stdlib.h>
int tray_start(const char *address);
void tray_stop(void);
*/
import "C"

import (
	"errors"
	"unsafe"
)

func Start(address string) (func(), error) {
	value := C.CString(address)
	defer C.free(unsafe.Pointer(value))
	if C.tray_start(value) == 0 {
		return nil, errors.New("could not register the private D-Bus host")
	}
	return func() { C.tray_stop() }, nil
}
