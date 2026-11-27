package main

import (
	"context"
	"fmt"
	"net"
	"os"
	"os/signal"
	"syscall"
	"time"

	"mothership-wm/internal/appbus"
	"mothership-wm/internal/domainipc"
	"mothership-wm/internal/userconfig"
	"mothership-wm/services/desktopd/internal/adapter/missionlog"
	"mothership-wm/services/desktopd/internal/adapter/nodestate"
	"mothership-wm/services/desktopd/internal/adapter/tray"
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
		fmt.Println("usage: mothership-desktopd serve | list | create DOMAIN [DESCRIPTION] | assign SESSION WINDOW DOMAIN | focus DOMAIN | home | overview | layout DOMAIN MODE | next DOMAIN | map show | map hide")
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
		loadouts := store.LoadLoadouts(nodes, func(err error) { fmt.Fprintln(os.Stderr, "mothership-desktopd:", err) })
		for i := range nodes {
			nodes[i].Loadout = loadouts[nodes[i].Name]
		}
		service := app.New(nodes, store.Save, func(err error) { fmt.Fprintln(os.Stderr, "mothership-desktopd: save Last active:", err) })
		service.UseLoadoutStore(store.SaveLoadout)
		missions, err := missionlog.Open(os.Getenv("XDG_STATE_HOME"), os.Getenv("HOME"), time.Now(),
			func(err error) { fmt.Fprintln(os.Stderr, "mothership-desktopd:", err) })
		if err != nil {
			fmt.Fprintln(os.Stderr, "mothership-desktopd: Mission Log unavailable:", err)
		} else {
			service.UseMissionLog(missions)
			// Close at shutdown time, not when the defer is registered.
			defer func() { missions.Close(time.Now()) }()
		}
		if address := os.Getenv(appbus.AddressEnv); address != "" {
			stopTray, err := tray.Start(address)
			if err != nil {
				fmt.Fprintln(os.Stderr, "desktopd: system tray unavailable:", err)
			} else {
				defer stopTray()
			}
		}
		fmt.Println("DESKTOPD_READY")
		// Map visibility is independent of the acknowledged app projection.
		service.SetMapVisible(true)
		return unixapi.ServeWithConfig(ctx, l, service, store.Delete, userconfig.Path(os.Getenv("HOME")))
	}
	reply, err := unixapi.Request(ctx, path, args)
	if err != nil {
		return err
	}
	fmt.Println(reply)
	return nil
}
