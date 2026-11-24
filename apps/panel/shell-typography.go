//go:build linux && cgo && panel

package main

/*
#include <stdlib.h>
#include "native.h"
*/
import "C"

import (
	"embed"
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
	"unsafe"
)

//go:embed assets/fonts/chakra-petch/*.ttf assets/fonts/chakra-petch/OFL.txt
var shellFonts embed.FS

// Register only in this process's Pango font map. Keep the private files
// available while the local renderer can read them.
func installShellTypography(runtimeDir string) func() {
	dir, err := os.MkdirTemp(runtimeDir, "shell-fonts-")
	if err != nil {
		fmt.Fprintf(os.Stderr, "mothership-command-center: shell font: %v; using Sans\n", err)
		return func() {}
	}
	cleanup := func() { _ = os.RemoveAll(dir) }
	fonts, _ := fs.Glob(shellFonts, "assets/fonts/chakra-petch/*.ttf")
	for _, font := range fonts {
		data, err := shellFonts.ReadFile(font)
		if err == nil {
			err = os.WriteFile(filepath.Join(dir, filepath.Base(font)), data, 0600)
		}
		if err != nil {
			fmt.Fprintf(os.Stderr, "mothership-command-center: shell font: %v; using Sans\n", err)
			cleanup()
			return func() {}
		}
	}
	for _, font := range fonts {
		path := C.CString(filepath.Join(dir, filepath.Base(font)))
		ok := C.panel_add_shell_font(path)
		C.free(unsafe.Pointer(path))
		if ok == 0 {
			return cleanup
		}
	}
	family := C.CString(defaultShellFamily)
	C.panel_use_shell_family(family)
	C.free(unsafe.Pointer(family))
	return cleanup
}
