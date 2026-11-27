package appbus

import (
	"context"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"time"
)

const (
	fileChooserConfig = "[preferred]\ndefault=none\norg.freedesktop.impl.portal.FileChooser=gtk\n"
	gtkFileChooser    = "[portal]\nDBusName=org.freedesktop.impl.portal.desktop.gtk\nInterfaces=org.freedesktop.impl.portal.FileChooser;\n"
)

// Use only these installed entries. The private bus does not load service
// directories and never asks the parent desktop to activate a service.
func installedPortal(env map[string]string, name string) string {
	dirs := env["XDG_DATA_DIRS"]
	if dirs == "" {
		dirs = "/usr/local/share:/usr/share"
	}
	for _, dir := range filepath.SplitList(dirs) {
		if !filepath.IsAbs(dir) {
			continue
		}
		data, err := os.ReadFile(filepath.Join(dir, "dbus-1", "services", name+".service"))
		if err != nil {
			continue
		}
		for _, line := range strings.Split(string(data), "\n") {
			path, ok := strings.CutPrefix(line, "Exec=")
			if !ok || !filepath.IsAbs(path) {
				continue
			}
			if info, err := os.Stat(path); err == nil &&
				info.Mode().IsRegular() && info.Mode().Perm()&0111 != 0 {
				return path
			}
		}
	}
	return ""
}

// StartFileChooser runs the portal and its GTK backend on the private bus
// and the child display. The launcher owns their lifetime.
func StartFileChooser(ctx context.Context, runtime, display, address string, env map[string]string, stderr io.Writer) (func(), error) {
	const frontendName = "org.freedesktop.portal.Desktop"
	const backendName = "org.freedesktop.impl.portal.desktop.gtk"
	portal := installedPortal(env, frontendName)
	backend := installedPortal(env, backendName)
	if portal == "" || backend == "" {
		return nil, fmt.Errorf("xdg-desktop-portal and xdg-desktop-portal-gtk are required")
	}
	configDir := filepath.Join(runtime, "portal-config")
	if err := os.MkdirAll(configDir, 0700); err != nil {
		return nil, err
	}
	if err := os.WriteFile(filepath.Join(configDir, "mothership-portals.conf"), []byte(fileChooserConfig), 0600); err != nil {
		return nil, err
	}
	if err := os.WriteFile(filepath.Join(configDir, "gtk.portal"), []byte(gtkFileChooser), 0600); err != nil {
		return nil, err
	}
	setEnv := func(cmd *exec.Cmd) {
		for key, value := range env {
			if key != "DISPLAY" && key != "WAYLAND_SOCKET" {
				cmd.Env = append(cmd.Env, key+"="+value)
			}
		}
		cmd.Env = append(cmd.Env, "WAYLAND_DISPLAY="+display,
			"XDG_RUNTIME_DIR="+runtime, "XDG_CURRENT_DESKTOP=Mothership",
			"XDG_SESSION_TYPE=wayland", "GDK_BACKEND=wayland",
			"DBUS_SESSION_BUS_ADDRESS="+address)
	}
	start := func(cmd *exec.Cmd) (func(), error) {
		cmd.Stderr = stderr
		if err := cmd.Start(); err != nil {
			return nil, err
		}
		return func() {
			_ = cmd.Process.Kill()
			_ = cmd.Wait()
		}, nil
	}
	waitCtx, cancel := context.WithTimeout(ctx, 4*time.Second)
	defer cancel()
	waitFor := func(name string) error {
		cmd := exec.CommandContext(waitCtx, "gdbus", "wait", "--address", address, name)
		if out, err := cmd.CombinedOutput(); err != nil {
			return fmt.Errorf("waiting for %s: %w %s", name, err, out)
		}
		return nil
	}
	gtk := exec.CommandContext(ctx, backend)
	setEnv(gtk)
	stopGTK, err := start(gtk)
	if err != nil {
		return nil, err
	}
	if err = waitFor(backendName); err != nil {
		stopGTK()
		return nil, err
	}
	desktop := exec.CommandContext(ctx, portal)
	setEnv(desktop)
	desktop.Env = append(desktop.Env,
		"XDG_DESKTOP_PORTAL_DIR="+configDir, "NIX_XDG_DESKTOP_PORTAL_DIR="+configDir)
	stopDesktop, err := start(desktop)
	if err != nil {
		stopGTK()
		return nil, err
	}
	stop := func() { stopDesktop(); stopGTK() }
	if err = waitFor(frontendName); err != nil {
		stop()
		return nil, err
	}
	return stop, nil
}
