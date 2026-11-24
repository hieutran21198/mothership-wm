package main

import (
	"context"
	"fmt"
	"net"
	"os"
	"os/signal"
	"syscall"

	"mothership-wm/internal/domainipc"
	"mothership-wm/services/desktopd/internal/adapter/nodestate"
	"mothership-wm/services/desktopd/internal/adapter/unixapi"
	"mothership-wm/services/desktopd/internal/app"
)

func main() {
	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()
	if err := run(ctx, os.Args[1:]); err != nil {
		fmt.Fprintln(os.Stderr, "mothership-desktopd:", err)
		os.Exit(1)
	}
}

func run(ctx context.Context, args []string) error {
	if len(args) == 0 || args[0] == "help" || args[0] == "--help" {
		fmt.Println("usage: mothership-desktopd serve | list | create DOMAIN [DESCRIPTION] | assign SESSION WINDOW DOMAIN | focus DOMAIN | home | overview | map show | map hide")
		return nil
	}
	path, err := domainipc.Path(os.Getenv("XDG_RUNTIME_DIR"))
	if err != nil {
		return err
	}
	if len(args) == 1 && args[0] == "serve" {
		l, err := net.ListenUnix("unixpacket", &net.UnixAddr{Name: path, Net: "unixpacket"})
		if err != nil {
			return err
		}
		defer l.Close()
		if err := os.Chmod(path, 0600); err != nil {
			return err
		}
		store, nodes, err := nodestate.Open(os.Getenv("XDG_STATE_HOME"), os.Getenv("HOME"))
		if err != nil {
			return fmt.Errorf("load Nodes: %w", err)
		}
		service := app.New(nodes, store.Save, func(err error) { fmt.Fprintln(os.Stderr, "mothership-desktopd: save Last active:", err) })
		fmt.Println("DESKTOPD_READY")
		// Map visibility is independent of the acknowledged app projection.
		service.SetMapVisible(true)
		return unixapi.Serve(ctx, l, service)
	}
	reply, err := unixapi.Request(ctx, path, args)
	if err != nil {
		return err
	}
	fmt.Println(reply)
	return nil
}
