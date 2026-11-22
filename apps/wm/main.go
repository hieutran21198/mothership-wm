package main

import (
	"context"
	"errors"
	"flag"
	"fmt"
	"io"
	"os"
	"os/signal"
	"path/filepath"
	"syscall"
)

type nestedLauncher func(context.Context, string) error

func main() {
	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	code := run(ctx, os.Args[1:], os.Getenv, launchNested, os.Stderr)
	stop()
	os.Exit(code)
}

// run only accepts a nested Wayland parent. There is no generic backend selector.
func run(ctx context.Context, args []string, getenv func(string) string, launch nestedLauncher, stderr io.Writer) int {
	flags := flag.NewFlagSet("mothership-wm", flag.ContinueOnError)
	flags.SetOutput(stderr)
	parent := flags.String("parent", getenv("WAYLAND_DISPLAY"), "parent Wayland socket name or absolute path")
	if err := flags.Parse(args); err != nil {
		if errors.Is(err, flag.ErrHelp) {
			return 0
		}
		return 2
	}
	if flags.NArg() != 0 {
		fmt.Fprintln(stderr, "mothership-wm: unexpected positional arguments")
		return 2
	}
	path, err := parentSocket(getenv("XDG_RUNTIME_DIR"), *parent)
	if err != nil {
		fmt.Fprintf(stderr, "mothership-wm: %v\n", err)
		return 1
	}
	if err := launch(ctx, path); err != nil {
		if errors.Is(err, context.Canceled) && ctx.Err() != nil {
			return 0
		}
		fmt.Fprintf(stderr, "mothership-wm: %v\n", err)
		return 1
	}
	return 0
}

func parentSocket(runtimeDir, parent string) (string, error) {
	if parent == "" {
		return "", errors.New("no parent Wayland display; set WAYLAND_DISPLAY or pass --parent")
	}
	if !filepath.IsAbs(runtimeDir) {
		return "", errors.New("XDG_RUNTIME_DIR must be absolute for the local Wayland socket")
	}
	if filepath.IsAbs(parent) {
		return filepath.Clean(parent), nil
	}
	if filepath.Base(parent) != parent || parent == "." || parent == ".." {
		return "", errors.New("--parent must be a socket name or an absolute path")
	}
	return filepath.Join(runtimeDir, parent), nil
}
