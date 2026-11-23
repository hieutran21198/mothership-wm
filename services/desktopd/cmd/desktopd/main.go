package main

import (
	"context"
	"fmt"
	"net"
	"os"
	"os/signal"
	"syscall"

	"mothership-wm/internal/domainipc"
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
		fmt.Println("usage: mothership-desktopd serve | list | create DOMAIN | assign SESSION WINDOW DOMAIN | focus DOMAIN | home")
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
		return unixapi.Serve(ctx, l, &app.Service{})
	}
	reply, err := unixapi.Request(ctx, path, args)
	if err != nil {
		return err
	}
	fmt.Println(reply)
	return nil
}
