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

	"mothership-wm/internal/domainipc"
)

type nestedLauncher func(context.Context, string, string) error

func main() {
	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	code := run(ctx, os.Args[1:], os.Getenv, launchNested, os.Stderr)
	stop()
	os.Exit(code)
}

// Native mode is explicit; nested mode continues to require a Wayland parent.
func run(ctx context.Context, args []string, getenv func(string) string, launch nestedLauncher, stderr io.Writer) int {
	flags := flag.NewFlagSet("mothership-wm", flag.ContinueOnError)
	flags.SetOutput(stderr)
	parent := flags.String("parent", getenv("WAYLAND_DISPLAY"), "parent Wayland socket name or absolute path")
	native := flags.Bool("native", false, "use the local DRM and input seat")
	domain := flags.Bool("desktopd", false, "connect to the private desktopd in XDG_RUNTIME_DIR")
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
	var path string
	var err error
	if *native {
		explicitParent := false
		flags.Visit(func(f *flag.Flag) { explicitParent = explicitParent || f.Name == "parent" })
		if explicitParent {
			fmt.Fprintln(stderr, "mothership-wm: --native cannot be combined with --parent")
			return 2
		}
		if getenv("MOTHERSHIP_NATIVE_TEST_HEADLESS") != "1" &&
			(getenv("WAYLAND_DISPLAY") != "" || getenv("WAYLAND_SOCKET") != "" || getenv("DISPLAY") != "") {
			fmt.Fprintln(stderr, "mothership-wm: native mode requires a local login or TTY")
			return 1
		}
		if !filepath.IsAbs(getenv("XDG_RUNTIME_DIR")) {
			fmt.Fprintln(stderr, "mothership-wm: XDG_RUNTIME_DIR must be absolute")
			return 1
		}
	} else {
		path, err = parentSocket(getenv("XDG_RUNTIME_DIR"), *parent)
		if err != nil {
			fmt.Fprintf(stderr, "mothership-wm: %v\n", err)
			return 1
		}
	}
	var domainPath string
	if *domain {
		domainPath, err = domainipc.Path(getenv("XDG_RUNTIME_DIR"))
		if err != nil {
			fmt.Fprintf(stderr, "mothership-wm: %v\n", err)
			return 1
		}
	}
	if err := launch(ctx, path, domainPath); err != nil {
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
