package main

import (
	"bytes"
	"context"
	"crypto/rand"
	"encoding/hex"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"slices"
	"strconv"
	"strings"
	"sync"
	"syscall"
	"time"

	"mothership-wm/internal/domainipc"
)

type appAction int

const (
	openTerminal       appAction = 5
	openFiles          appAction = 6
	openCosmicTerminal appAction = 16
	openArk            appAction = 17
	openBrave          appAction = 18
	openBtop           appAction = 19
)

type appLauncher struct {
	ctx     context.Context
	display string
	bus     string
	owned   sync.WaitGroup
	report  func(string)
}

func appCommand(ctx context.Context, action appAction, display string, env []string) (*exec.Cmd, string, error) {
	var name, binary string
	var args []string
	var waylandQt bool
	switch action {
	case openTerminal:
		name, binary, args = "Terminal", "ghostty", []string{"--gtk-single-instance=false"}
	case openFiles:
		name, binary, args = "File Manager", "cosmic-files", []string{"--no-daemon"}
	case openCosmicTerminal:
		name, binary = "COSMIC Terminal", "cosmic-term"
	case openArk:
		name, binary, waylandQt = "Ark", "ark", true
	case openBrave:
		name, binary = "Brave", "brave"
	case openBtop:
		name, binary = "btop", "ghostty"
	default:
		return nil, "", fmt.Errorf("unknown app action")
	}
	info, err := os.Stat(display)
	if !filepath.IsAbs(display) || err != nil || info.Mode()&os.ModeSocket == 0 {
		return nil, name, fmt.Errorf("%s needs a live nested Wayland socket", name)
	}
	switch action {
	case openBrave:
		args = []string{"--ozone-platform=wayland"}
		if profile := braveProfile(display, nativeSession(env)); profile != "" {
			if err := os.Mkdir(profile, 0700); err != nil && !os.IsExist(err) {
				return nil, name, fmt.Errorf("could not make a private Brave profile: %w", err)
			}
			st, err := os.Lstat(profile)
			if err != nil || !st.IsDir() || st.Mode().Perm() != 0700 {
				return nil, name, fmt.Errorf("Brave profile is not a private directory")
			}
			args = append(args, "--user-data-dir="+profile)
		}
		args = append(args, "--no-first-run", "--no-default-browser-check", "--new-window", "about:blank")
	case openBtop:
		tool, err := exec.LookPath("btop")
		if err != nil {
			return nil, name, fmt.Errorf("btop needs btop on PATH")
		}
		args = []string{"--gtk-single-instance=false", "-e", tool}
	}
	cmd := exec.CommandContext(ctx, binary, args...)
	if cmd.Err != nil {
		return nil, name, fmt.Errorf("%s needs %s on PATH", name, binary)
	}
	// Do not hand a request or activation token to a host desktop instance.
	// An explicit unavailable bus also prevents D-Bus autolaunch. This is
	// display routing, not isolation from the user's filesystem.
	for _, item := range env {
		key, _, _ := strings.Cut(item, "=")
		switch key {
		case "WAYLAND_DISPLAY", "WAYLAND_SOCKET", "DISPLAY", "GDK_BACKEND", launchTokenKey,
			"XDG_ACTIVATION_TOKEN", "DESKTOP_STARTUP_ID", "DBUS_SESSION_BUS_ADDRESS":
			continue
		}
		if action == openBrave && !nativeSession(env) {
			switch key {
			case "CHROME_USER_DATA_DIR", "CHROME_CONFIG_HOME", "CHROMIUM_USER_FLAGS", "CHROMIUM_FLAGS",
				"BRAVE_USER_FLAGS", "BRAVE_FLAGS":
				continue
			}
		}
		if waylandQt && key == "QT_QPA_PLATFORM" {
			continue
		}
		cmd.Env = append(cmd.Env, item)
	}
	cmd.Env = append(cmd.Env, "WAYLAND_DISPLAY="+display, "GDK_BACKEND=wayland",
		"DBUS_SESSION_BUS_ADDRESS=unix:path=/dev/null")
	if waylandQt {
		cmd.Env = append(cmd.Env, "QT_QPA_PLATFORM=wayland")
	}
	cmd.Cancel = func() error { return cmd.Process.Signal(syscall.SIGTERM) }
	cmd.WaitDelay = time.Second
	return cmd, name, nil
}

// Called only from the GTK thread, before it exits and waits for these children.
func (l *appLauncher) start(action appAction) error {
	_, err := l.startTracked(action, false, "")
	return err
}

func (l *appLauncher) startTracked(action appAction, node bool, token string) (int, error) {
	cmd, name, err := appCommand(l.ctx, action, l.display, os.Environ())
	if err != nil {
		l.report(err.Error())
		return 0, err
	}
	if token != "" {
		cmd.Env = append(cmd.Env, launchTokenKey+"="+token)
	}
	if l.bus != "" {
		for i, item := range cmd.Env {
			if strings.HasPrefix(item, "DBUS_SESSION_BUS_ADDRESS=") {
				cmd.Env[i] = "DBUS_SESSION_BUS_ADDRESS=" + l.bus
			}
		}
	}
	if err := cmd.Start(); err != nil {
		err = fmt.Errorf("could not start %s: %w", name, err)
		l.report(err.Error())
		return 0, err
	}
	if !node {
		l.report(name + " launch requested. Look in Home / Inbox.")
	}
	l.owned.Add(1)
	go func() {
		defer l.owned.Done()
		if err := cmd.Wait(); err != nil && l.ctx.Err() == nil {
			l.report(fmt.Sprintf("%s stopped: %v", name, err))
		}
	}()
	return cmd.Process.Pid, nil
}

const launchTokenKey = "MOTHERSHIP_GRID_LAUNCH"

func newGridLaunchToken() string {
	var token [16]byte
	// crypto/rand.Read fills the buffer and does not return an error.
	rand.Read(token[:])
	return hex.EncodeToString(token[:])
}

// Match a specific request, not a similarly named app. An ordinary exec,
// fork, or detached worker can inherit the token even when its PID differs
// from the launcher. Never forward or print the process environment.
func ownsGridLaunch(procRoot string, pid int, token string) bool {
	if pid <= 0 || token == "" {
		return false
	}
	f, err := os.Open(filepath.Join(procRoot, strconv.Itoa(pid), "environ"))
	if err != nil {
		return false
	}
	defer f.Close()
	const limit = 2 << 20
	data, err := io.ReadAll(io.LimitReader(f, limit+1))
	if err != nil || len(data) > limit {
		return false
	}
	want := []byte(launchTokenKey + "=" + token)
	for _, entry := range bytes.Split(data, []byte{0}) {
		if bytes.Equal(entry, want) {
			return true
		}
	}
	return false
}

func gridLaunchWindow(windows []domainipc.Window, after uint64, matches func(int) bool) uint64 {
	var id uint64
	for _, w := range windows {
		if w.ID <= after || w.Domain != "" || !w.Mapped || w.PID <= 0 || !matches(w.PID) {
			continue
		}
		if id != 0 {
			return 0
		}
		id = w.ID
	}
	return id
}

func nativeSession(env []string) bool {
	return slices.Contains(env, "MOTHERSHIP_NATIVE_SESSION=1")
}

// braveProfile is the --user-data-dir for a Mothership Brave launch, or ""
// for Brave's normal profile. The native session is the user's desktop, so
// it keeps the normal profile across sessions. A nested session keeps a
// private profile in its runtime so Chromium's singleton cannot forward the
// window to a host browser; it is removed with that runtime.
func braveProfile(display string, native bool) string {
	if native {
		return ""
	}
	return filepath.Join(filepath.Dir(display), "brave-profile")
}

// The Nix wrapper may fork Brave, and a warm launch may be forwarded to
// the existing browser. Neither case keeps the command's PID on Wayland.
// An empty profile matches a Brave browser on its normal profile, which
// has no --user-data-dir flag.
func ownsBraveProfile(procRoot string, pid int, profile string) bool {
	if pid <= 0 {
		return false
	}
	args, err := os.ReadFile(filepath.Join(procRoot, strconv.Itoa(pid), "cmdline"))
	if err != nil {
		return false
	}
	argv := bytes.Split(bytes.TrimRight(args, "\x00"), []byte{0})
	isBrave := func(exe string) bool {
		switch filepath.Base(exe) {
		case "brave", "brave-browser", "brave-bin":
			return true
		}
		return false
	}
	flag := []byte("--user-data-dir=" + profile)
	if len(argv) == 1 {
		// Chromium on this build flattens its process title into a single
		// argument. Check the executable, then match the complete flag.
		exe, err := os.Readlink(filepath.Join(procRoot, strconv.Itoa(pid), "exe"))
		if err != nil || !isBrave(exe) {
			return false
		}
		if profile == "" {
			return !bytes.Contains(argv[0], []byte(" --user-data-dir"))
		}
		marker := append(append([]byte{' '}, flag...), ' ')
		return bytes.Contains(append(argv[0], ' '), marker)
	}
	if !isBrave(string(argv[0])) {
		return false
	}
	for _, a := range argv[1:] {
		if profile == "" && bytes.HasPrefix(a, []byte("--user-data-dir")) {
			return false
		}
		if profile != "" && bytes.Equal(a, flag) {
			return true
		}
	}
	return profile == ""
}

func braveLaunchWindow(windows []domainipc.Window, after uint64, ownsProfile func(int) bool) uint64 {
	var id uint64
	for _, w := range windows {
		if w.ID <= after || !w.Mapped || w.Domain != "" || w.PID <= 0 {
			continue
		}
		switch w.AppID {
		case "brave-browser", "brave", "com.brave.Browser":
		default:
			continue
		}
		if !ownsProfile(w.PID) {
			continue
		}
		if id != 0 {
			return 0
		}
		id = w.ID
	}
	return id
}
