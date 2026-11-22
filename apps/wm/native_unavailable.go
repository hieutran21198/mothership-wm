//go:build !linux || !cgo || !wlroots

package main

import (
	"context"
	"errors"
)

func launchNested(context.Context, string) error {
	return errors.New("native backend not built; use the wm-native devenv profile and build with -tags wlroots")
}
