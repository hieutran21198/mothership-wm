//go:build !linux || !cgo || !tray

package tray

import "errors"

func Start(string) (func(), error) {
	return nil, errors.New("desktopd built without the native tray adapter")
}
