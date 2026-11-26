//go:build linux && cgo && panel

package main

/*
#include <stdlib.h>
#include "native.h"
*/
import "C"

import (
	"fmt"
	"os"
	"unsafe"

	"mothership-wm/internal/shellfonts"
)

func installShellTypography(runtimeDir string) func() {
	fonts, cleanup, err := shellfonts.Extract(runtimeDir)
	if err != nil {
		fmt.Fprintf(os.Stderr, "mothership-command-center: shell font: %v; using Sans\n", err)
		return func() {}
	}
	for _, font := range fonts {
		path := C.CString(font)
		C.panel_add_shell_font(path)
		C.free(unsafe.Pointer(path))
	}
	family := C.CString(defaultShellFamily)
	C.panel_use_shell_family(family)
	C.free(unsafe.Pointer(family))
	return cleanup
}
