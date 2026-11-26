//go:build linux && cgo && panel

package main

/*
#cgo pkg-config: gtk4 gio-unix-2.0 librsvg-2.0 epoxy wayland-client gmodule-2.0
#cgo LDFLAGS: -lgtk4-layer-shell
#include <stdlib.h>
#include "native.h"
#include "app-grid.h"
#include "slash-ui.h"
#include "shortcut-settings.h"
#include "system-tray.h"
*/
import "C"

import (
	"context"
	"flag"
	"fmt"
	"os"
	"os/signal"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"sync/atomic"
	"syscall"
	"time"
	"unsafe"

	"mothership-wm/internal/appbus"
	"mothership-wm/internal/domainipc"
	"mothership-wm/internal/userconfig"
)

var active *controller
var apps *appLauncher
var preferencePaths panelConfigPaths
var preferences panelPreferences

func main() {
	nested := flag.String("nested", "", "distinct child Wayland socket reported by Mothership")
	flag.Parse()
	path, err := domainipc.Path(os.Getenv("XDG_RUNTIME_DIR"))
	if err != nil {
		fail(err)
	}
	socket := *nested
	if socket == "" || socket == "." || socket == ".." || filepath.Base(socket) != socket ||
		socket == os.Getenv("WAYLAND_DISPLAY") || os.Getenv("WAYLAND_SOCKET") != "" {
		fail(fmt.Errorf("specify a distinct --nested socket without inherited WAYLAND_SOCKET"))
	}
	info, err := os.Stat(filepath.Join(filepath.Dir(path), socket))
	if err != nil || info.Mode()&os.ModeSocket == 0 {
		fail(fmt.Errorf("nested Wayland socket is unavailable"))
	}
	_ = os.Setenv("WAYLAND_DISPLAY", socket)
	runtime.LockOSThread()
	if os.Getenv("GSK_RENDERER") == "" {
		// Keep the GTK compositor on OpenGL rather than selecting Vulkan.
		_ = os.Setenv("GSK_RENDERER", "gl")
	}
	cleanupTypography := installShellTypography(filepath.Dir(path))
	defer cleanupTypography()
	if C.panel_init() == 0 {
		cleanupTypography()
		fail(fmt.Errorf("could not open native Command Center in the nested WM"))
	}
	home, _ := os.UserHomeDir()
	preferencePaths = panelConfigPaths{user: userconfig.Path(home), theme: os.Getenv("MOTHERSHIP_MAP_THEME"), wallpaper: os.Getenv("MOTHERSHIP_WALLPAPER_CONFIG")}
	if dir, err := os.UserConfigDir(); err == nil {
		preferencePaths.legacyMap = filepath.Join(dir, "mothership-wm", "center-map.json")
	}
	preferences = loadPanelPreferences(preferencePaths, configWarning)
	configureWallpaper(preferences.wallpaperSource, preferences.wallpaper)
	icons, _ := prepareMapIcons(preferences, configWarning, false)
	configureMap(preferences, icons)
	C.panel_set_home_width(C.int(preferences.homeWidth))
	C.panel_set_saved_cursor_size(C.int(preferences.cursorSize))
	C.panel_set_home_guide(cBool(preferences.homeGuide))
	C.panel_set_node_preferences(cBool(preferences.nodeGuide), cBool(preferences.nodeTelemetry), C.int(preferences.nodePreviewZoom), C.int(preferences.nodeHoverZoom))
	C.panel_set_toast_mode(C.int(preferences.toastMode))
	panelNotices = openNotificationLog()
	renderNotificationLog()
	C.panel_set_video_enabled(cBool(preferences.wallpaper.allowsVideo()))
	applyDefaultNodePreference(preferences.defaultNode)
	applyAppPins(preferences.appPins)
	ctx, cancel := signal.NotifyContext(context.Background(), syscall.SIGTERM, os.Interrupt)
	active = &controller{ctx: ctx, path: path, show: showNative}
	apps = &appLauncher{ctx: ctx, display: filepath.Join(filepath.Dir(path), socket), report: active.report}
	address, shared := os.LookupEnv(appbus.AddressEnv)
	if address == "" {
		if private, stopBus, err := appbus.Start(ctx, filepath.Dir(path)); err == nil {
			address = private
			defer stopBus()
		} else {
			configWarning("notifications: %v", err)
		}
	}
	if address != "" {
		apps.bus = address
		if err := registerNotificationBus(address); err != nil {
			configWarning("notifications: %v", err)
		} else {
			defer closeNotificationBus()
		}
		if shared {
			caddr := C.CString(address)
			C.system_tray_connect(caddr)
			C.free(unsafe.Pointer(caddr))
			defer C.system_tray_disconnect()
		}
	}
	go active.run()
	panelShortcutRequest(-1, 0, 0, 0, 0)
	volumeDone := make(chan struct{})
	go func() { defer close(volumeDone); runNodeVolume(ctx, active) }()
	go sampleHome(ctx)
	go func() {
		<-ctx.Done()
		C.panel_quit()
	}()
	C.panel_run()
	cancel()
	apps.owned.Wait()
	<-volumeDone
}

func fail(err error) {
	fmt.Fprintln(os.Stderr, "mothership-command-center:", err)
	os.Exit(1)
}

func showNative(v view) {
	var ws strings.Builder
	for _, w := range v.windows {
		fmt.Fprintf(&ws, "%s\t%s\n", w.key, w.label)
	}
	var profiles strings.Builder
	for _, p := range v.profiles {
		fmt.Fprintf(&profiles, "%s\t%d\t%d\t%t\t%s\t%s\t%d\n", p.Domain, p.CreatedAt, p.LastActive, p.empty, p.Description, strings.Join(p.apps, ","), p.Shortcut)
	}
	s := []string{v.title, v.message, strings.Join(v.domains, "\n"), ws.String(), v.node, v.mode, profiles.String()}
	ptr := make([]*C.char, len(s))
	for i, value := range s {
		ptr[i] = C.CString(value)
		defer C.free(unsafe.Pointer(ptr[i]))
	}
	var enabled C.int
	if v.enabled {
		enabled = 1
	}
	var overview C.int
	if v.overview {
		overview = 1
	}
	var mapVisible C.int
	if v.mapVisible {
		mapVisible = 1
	}
	var blankHome C.int
	if v.blankHome {
		blankHome = 1
	}
	var ready C.int
	if v.ready {
		ready = 1
	}
	var defaultNode C.int
	if v.defaultNode {
		defaultNode = 1
	}
	C.panel_post(ptr[0], ptr[1], ptr[2], ptr[3], ptr[4], ptr[5], ptr[6], enabled, overview, mapVisible, blankHome, defaultNode, ready, C.uint64_t(v.connectFailed), C.uint64_t(v.preparedRelay))
}

func cBool(v bool) C.int {
	if v {
		return 1
	}
	return 0
}

func prepareWallpaper(path string, cfg wallpaperConfig) (unsafe.Pointer, bool, error) {
	cfg = cfg.playable()
	video, isVideo, err := videoWallpaperPath(path, cfg.Path)
	if err != nil {
		return nil, isVideo, err
	}
	if isVideo {
		cpath := C.CString(video)
		defer C.free(unsafe.Pointer(cpath))
		prepared := C.wallpaper_prepare_mp4(cpath)
		if prepared == nil {
			return nil, true, fmt.Errorf("cannot decode local H.264 MP4 (requires GStreamer video plugins)")
		}
		return prepared, true, nil
	}
	data, err := readWallpaper(path, cfg.Path)
	if err != nil {
		return nil, false, err
	}
	prepared := C.wallpaper_prepare((*C.uchar)(unsafe.Pointer(&data[0])), C.size_t(len(data)))
	if prepared == nil {
		return nil, false, fmt.Errorf("not a renderable PNG")
	}
	return prepared, false, nil
}

func replaceWallpaper(prepared unsafe.Pointer, video bool, mode string) {
	var contain C.int
	if mode == "contain" {
		contain = 1
	}
	if video {
		C.wallpaper_replace_mp4(prepared, contain)
	} else {
		C.wallpaper_replace(prepared, contain)
	}
}

func configureWallpaper(path string, cfg wallpaperConfig) {
	pix, video, err := prepareWallpaper(path, cfg)
	// A playing video always has a static backdrop to restore on an error.
	if err != nil || video {
		if err != nil {
			fmt.Fprintf(os.Stderr, "mothership-wallpaper: %q: %v; using bundled wallpaper\n", cfg.Path, err)
		}
		fallback, _, fallbackErr := prepareWallpaper(path, wallpaperConfig{})
		if fallbackErr == nil {
			replaceWallpaper(fallback, false, cfg.Mode)
		}
	}
	if err == nil {
		replaceWallpaper(pix, video, cfg.Mode)
	}
	C.wallpaper_show()
}

func configureMap(p panelPreferences, icons unsafe.Pointer) {
	warn := func(f string, a ...any) { fmt.Fprintf(os.Stderr, "mothership-center-map: "+f+"\n", a...) }
	theme := p.theme
	colors := []string{theme.Background, theme.Grid, theme.Text, theme.Stroke, theme.BorderColor}
	cs := make([]*C.char, len(colors))
	for i, color := range colors {
		cs[i] = C.CString(color)
		defer C.free(unsafe.Pointer(cs[i]))
	}
	C.map_hud_configure(cs[0], C.double(theme.Opacity), cs[1], cs[2], cs[3], cs[4])
	settings := p.settings
	border := C.CString(settings.BorderColor)
	var frosted, showFPS C.int
	if settings.Frosted {
		frosted = 1
	}
	if settings.ShowFPS {
		showFPS = 1
	}
	C.panel_set_map_settings(border, C.int(settings.OuterMargin), C.int(settings.InnerPadding), frosted, C.int(settings.Animation), showFPS, C.int(theme.PanSpeed), C.int(settings.PanSpeedOverride), maxMapMargin, maxMapPadding, cs[4], defaultMapMargin, defaultMapPadding)
	C.free(unsafe.Pointer(border))
	family := C.CString(theme.FontFamily)
	if C.map_hud_set_font(family, C.double(theme.FontSize)) == 0 {
		warn("font family %q is not available; using Mothership shell font", theme.FontFamily)
	}
	C.free(unsafe.Pointer(family))
	C.map_hud_icons_replace(icons)
}

func configWarning(f string, a ...any) {
	fmt.Fprintf(os.Stderr, "mothership-config: "+f+"\n", a...)
}

func prepareMapIcons(p panelPreferences, warn func(string, ...any), strict bool) (unsafe.Pointer, error) {
	set := C.map_hud_icons_new()
	for node, asset := range p.theme.Icons {
		data, err := readMapIcon(p.iconSource, asset)
		if err == nil && len(data) != 0 {
			name := C.CString(node)
			ok := C.map_hud_icon_add(set, name, (*C.uchar)(unsafe.Pointer(&data[0])), C.size_t(len(data)))
			C.free(unsafe.Pointer(name))
			if ok == 0 {
				err = fmt.Errorf("not a renderable SVG")
			}
		} else if err == nil {
			err = fmt.Errorf("empty SVG")
		}
		if err != nil {
			if strict {
				C.map_hud_icons_free(set)
				return nil, fmt.Errorf("node %q icon %q: %w", node, asset, err)
			}
			warn("node %q icon %q: %v; using diamond", node, asset, err)
		}
	}
	return set, nil
}

//export panelReloadConfig
func panelReloadConfig() C.int {
	// Invoked on the locked GTK thread only from blank Home. Validate and
	// prepare every replacement before touching the running configuration.
	var bad error
	next := loadPanelPreferences(preferencePaths, func(f string, a ...any) {
		if bad == nil {
			bad = fmt.Errorf(f, a...)
		}
	})
	if bad != nil {
		configWarning("reload kept current settings: %v", bad)
		return 0
	}
	icons, err := prepareMapIcons(next, configWarning, true)
	if err != nil {
		configWarning("reload kept current settings: %v", err)
		return 0
	}
	pix, video, err := prepareWallpaper(next.wallpaperSource, next.wallpaper)
	if err != nil {
		C.map_hud_icons_free(icons)
		configWarning("reload kept current settings: wallpaper %q: %v", next.wallpaper.Path, err)
		return 0
	}
	replaceWallpaper(pix, video, next.wallpaper.Mode)
	configureMap(next, icons)
	preferences = next
	C.panel_set_home_width(C.int(next.homeWidth))
	C.panel_set_saved_cursor_size(C.int(next.cursorSize))
	C.panel_set_home_guide(cBool(next.homeGuide))
	C.panel_set_node_preferences(cBool(next.nodeGuide), cBool(next.nodeTelemetry), C.int(next.nodePreviewZoom), C.int(next.nodeHoverZoom))
	C.panel_set_toast_mode(C.int(next.toastMode))
	C.panel_set_video_enabled(cBool(next.wallpaper.allowsVideo()))
	applyDefaultNodePreference(next.defaultNode)
	applyAppPins(next.appPins)
	configWarning("reloaded user configuration")
	return 1
}

//export panelSaveMapSettings
func panelSaveMapSettings(border *C.char, outer, inner, frosted, animation, showFPS, panOverride C.int) *C.char {
	settings := mapSettings{BorderColor: C.GoString(border), OuterMargin: int(outer), InnerPadding: int(inner), Frosted: frosted != 0, Animation: int(animation), ShowFPS: showFPS != 0, PanSpeedOverride: int(panOverride)}
	if err := preferences.saveMap(preferencePaths.user, settings); err != nil {
		return C.CString("Could not save settings: " + err.Error())
	}
	return nil
}

func applyDefaultNodePreference(enabled bool) {
	var on C.int
	if enabled {
		on = 1
	}
	C.panel_set_default_node_enabled(on)
}

//export panelSaveNodePreferences
func panelSaveNodePreferences(show, telemetry, zoom, hover C.int) *C.char {
	if err := preferences.saveNodePanel(preferencePaths.user, show != 0, telemetry != 0, int(zoom), int(hover)); err != nil {
		return C.CString("Could not save: " + err.Error())
	}
	return nil
}

//export panelSlashEnterNode
func panelSlashEnterNode(name *C.char, create C.int, request C.uint64_t) {
	node := C.GoString(name)
	finish := func(err error) {
		if err == nil {
			// The acknowledged desktopd selection event closes Slash.
			return
		}
		text := C.CString(err.Error())
		C.slash_ui_node_error(C.guint64(request), text)
		C.free(unsafe.Pointer(text))
	}
	if create != 0 {
		active.createAndEnter(node, finish)
		return
	}
	active.submitCommand(domainipc.FocusMethod, domainipc.DomainBody{Domain: node}, false, 0, "", finish)
}

//export panelDeleteNode
func panelDeleteNode(name *C.char, force C.int) {
	node := C.GoString(name)
	active.submitCommand(domainipc.DeleteMethod, domainipc.DeleteBody{Domain: node, Force: force != 0}, false, 0, "", func(err error) {
		message := "Deleted " + node + "."
		if err != nil {
			message = err.Error()
		}
		c := C.CString(message)
		C.slash_ui_delete_result(c)
		if err == nil {
			C.panel_home_notice(c)
		}
		C.free(unsafe.Pointer(c))
	})
}

//export panelAction
func panelAction(op C.int, a, b *C.char) {
	name, key := C.GoString(a), C.GoString(b)
	switch op {
	case 1:
		active.submit(domainipc.CreateMethod, domainipc.DomainBody{Domain: strings.TrimSpace(name)})
	case 2:
		session, raw, _ := strings.Cut(key, ":")
		id, err := strconv.ParseUint(raw, 10, 64)
		if err == nil {
			active.submit(domainipc.AssignMethod, domainipc.AssignBody{Session: session, WindowID: id, Domain: name})
		}
	case 3:
		active.submit(domainipc.FocusMethod, domainipc.DomainBody{Domain: name})
	case 4:
		active.submit(domainipc.HomeMethod, domainipc.Empty{})
	case 16:
		if preferences.defaultNode {
			active.submit(domainipc.DefaultNodeMethod, domainipc.Empty{})
		}
	case C.int(openTerminal), C.int(openFiles):
		if apps.start(appAction(op)) == nil {
			bringHomeLaunchForward()
		}
	case 8, 9, 10:
		modes := []string{domainipc.Stack, domainipc.Float, domainipc.Flow}
		active.submit(domainipc.LayoutMethod, domainipc.LayoutBody{Domain: name, Mode: modes[int(op)-8]})
	case 17:
		mode := domainipc.Flow
		if key == domainipc.Stage {
			mode = domainipc.Stage
		}
		active.submit(domainipc.LayoutMethod, domainipc.LayoutBody{Domain: name, Mode: mode, RestoreFlow: mode == domainipc.Flow})
	case 11:
		active.submit(domainipc.NextMethod, domainipc.DomainBody{Domain: name})
	case 12:
		relay, _ := strconv.ParseUint(key, 10, 64)
		active.connect(name, relay)
	case 13:
		active.openMap(name == "overview")
	case 14:
		active.blankHome(name == "overview")
	case 15:
		relay, _ := strconv.ParseUint(key, 10, 64)
		active.reveal(relay)
	}
}

func sampleHome(ctx context.Context) {
	tick := time.NewTicker(time.Second)
	defer tick.Stop()
	var sampler homeSampler
	for {
		out := sampler.sample(time.Now())
		buf := make([]C.int, len(out.cores))
		for i, v := range out.cores {
			buf[i] = C.int(v)
		}
		var cores *C.int
		if len(buf) > 0 {
			cores = &buf[0]
		}
		clock, date := C.CString(out.now.Format("15:04")), C.CString(out.now.Format("02/01"))
		var known C.int
		if out.networkOK {
			known = 1
		}
		C.panel_home_sample(C.int(out.cpuPct), cores, C.int(len(buf)), C.double(out.used), C.double(out.total),
			C.double(out.rx), C.double(out.tx), known, C.int(out.health), C.int(out.healthCPU), clock, date)
		C.free(unsafe.Pointer(clock))
		C.free(unsafe.Pointer(date))
		select {
		case <-ctx.Done():
			return
		case <-tick.C:
		}
	}
}

//export panelSaveHomeSettings
func panelSaveHomeSettings(width, video, guide, toast, cursor C.int) *C.char {
	enabled, showGuide := video != 0, guide != 0
	next := preferences.wallpaper
	next.VideoEnabled = &enabled
	changed := next.playable().Path != preferences.wallpaper.playable().Path
	var prepared unsafe.Pointer
	var mp4 bool
	if changed {
		var err error
		prepared, mp4, err = prepareWallpaper(preferences.wallpaperSource, next)
		if err != nil {
			return C.CString("Could not switch wallpaper: " + err.Error())
		}
	}
	mode, size := int(toast), int(cursor)
	if err := preferences.saveHomePreferences(preferencePaths.user, int(width), &enabled, &showGuide, &mode, &size); err != nil {
		if prepared != nil {
			C.wallpaper_discard_prepared(prepared, cBool(mp4))
		}
		return C.CString("Could not save Home Panel: " + err.Error())
	}
	if changed {
		replaceWallpaper(prepared, mp4, next.Mode)
	}
	C.panel_set_video_enabled(video)
	C.panel_set_home_guide(guide)
	C.panel_set_toast_mode(toast)
	C.panel_set_saved_cursor_size(cursor)
	return nil
}

var systemBusy [2]atomic.Bool

//export panelSystemAction
func panelSystemAction(op C.int) {
	i := int(op)
	if i < 0 || i >= len(systemBusy) || active == nil || (i == 1 && os.Getenv("MOTHERSHIP_NATIVE_SESSION") == "1") || !systemBusy[i].CompareAndSwap(false, true) {
		return
	}
	go func() {
		defer systemBusy[i].Store(false)
		ctx, cancel := context.WithTimeout(active.ctx, 2500*time.Millisecond)
		defer cancel()
		var message string
		if i == 0 {
			message = audioStatus(ctx, runHomeCommand)
		} else {
			message = lockHost(ctx, os.Getenv("XDG_SESSION_ID"), os.Getuid(), runHomeCommand)
		}
		c := C.CString(message)
		C.panel_home_notice(c)
		C.free(unsafe.Pointer(c))
	}()
}

func applyAppPins(pins []string) {
	text := C.CString(strings.Join(pins, "\n"))
	C.app_grid_set_pins(text)
	C.free(unsafe.Pointer(text))
}

//export panelSaveAppPin
func panelSaveAppPin(id *C.char, shouldPin C.int) *C.char {
	if err := preferences.saveAppPin(preferencePaths.user, C.GoString(id), shouldPin != 0); err != nil {
		return C.CString("Could not save pin: " + err.Error())
	}
	return nil
}

//export panelGridAppSupported
func panelGridAppSupported(id, executable *C.char, dbus C.int) C.int {
	_, ok := gridLaunchAction(C.GoString(id), filepath.Base(C.GoString(executable)), dbus != 0)
	if ok {
		return 1
	}
	return 0
}

func bringHomeLaunchForward() {
	if !preferences.defaultNode || active == nil {
		return
	}
	c := active
	c.mu.Lock()
	s := c.state
	inHome := s.Selection.State == "acknowledged" &&
		(s.Selection.View == "home" || (s.Selection.View == "overview" && !s.MapVisible))
	c.mu.Unlock()
	if inHome {
		c.submit(domainipc.DefaultNodeMethod, domainipc.Empty{})
	}
}

var braveLaunchPending atomic.Bool

//export panelLaunchGridApp
func panelLaunchGridApp(info *C.GDesktopAppInfo, id, executable *C.char, dbus C.int) *C.char {
	key := C.GoString(id)
	binary := filepath.Base(C.GoString(executable))
	action, ok := gridLaunchAction(key, binary, dbus != 0)
	if !ok && os.Getenv("MOTHERSHIP_NATIVE_SESSION") != "1" {
		fmt.Fprintf(os.Stderr, "mothership-app-grid: standalone launch blocked for %q\n", key)
		return C.CString("no safe standalone Wayland launch.")
	}
	active.mu.Lock()
	state := active.state
	node, session := state.Selection.Domain, state.Session
	var after uint64
	for _, w := range state.Windows {
		after = max(after, w.ID)
	}
	active.mu.Unlock()
	// One in-flight request per shared nested profile. A second request
	// cannot claim the first request's new browser window.
	brave := action == openBrave
	if brave && !braveLaunchPending.CompareAndSwap(false, true) {
		return C.CString("Brave is still opening a window. Try again in a moment.")
	}
	var pid int
	var err error
	var token string
	if node != "" && !brave {
		token = newGridLaunchToken()
	}
	if ok {
		pid, err = apps.startTracked(action, node != "", token)
	} else {
		pid, err = launchNativeDesktopApp(info, token)
	}
	if err != nil {
		if brave {
			braveLaunchPending.Store(false)
		}
		return C.CString(err.Error())
	}
	if brave {
		profile := nestedBraveProfile(apps.display)
		go func() {
			defer braveLaunchPending.Store(false)
			attachGridWindow(active, session, node, 12*time.Second, func(w []domainipc.Window) uint64 {
				return braveLaunchWindow(w, after, func(pid int) bool { return ownsNestedBraveProfile("/proc", pid, profile) })
			})
		}()
	} else if node != "" {
		go attachGridWindow(active, session, node, 12*time.Second, func(windows []domainipc.Window) uint64 {
			return gridLaunchWindow(windows, after, func(candidate int) bool {
				return (pid > 0 && candidate == pid) || ownsGridLaunch("/proc", candidate, token)
			})
		})
	}
	if node == "" {
		bringHomeLaunchForward()
	}
	return nil
}

func launchNativeDesktopApp(info *C.GDesktopAppInfo, token string) (int, error) {
	display, bus, launch := C.CString(apps.display), C.CString(apps.bus), C.CString(token)
	defer C.free(unsafe.Pointer(display))
	defer C.free(unsafe.Pointer(bus))
	defer C.free(unsafe.Pointer(launch))
	var pid C.int
	var launchErr *C.GError
	if C.app_catalog_launch_native(info, display, bus, launch, &pid, &launchErr) == 0 {
		if launchErr != nil {
			defer C.g_error_free(launchErr)
			return 0, fmt.Errorf("%s", C.GoString(launchErr.message))
		}
		return 0, fmt.Errorf("could not start the desktop entry")
	}
	return int(pid), nil
}

// Launch against the originally selected Node, never against the later seat
// focus. A process that cannot be tied to the request stays in Inbox.
func attachGridWindow(c *controller, session, node string, timeout time.Duration, match func([]domainipc.Window) uint64) {
	ctx, cancel := context.WithTimeout(c.ctx, timeout)
	defer cancel()
	tick := time.NewTicker(70 * time.Millisecond)
	defer tick.Stop()
	report := func(msg string) {
		v := C.CString(msg)
		C.panel_home_notice(v)
		C.free(unsafe.Pointer(v))
	}
	for {
		select {
		case <-ctx.Done():
			if c.ctx.Err() == nil && node != "" {
				report("No matching Node window. Check Home / Inbox.")
			}
			return
		case <-tick.C:
			c.mu.Lock()
			s := c.state
			sameSession := c.online && s.Session == session
			windows := append([]domainipc.Window(nil), s.Windows...)
			c.mu.Unlock()
			if !sameSession {
				continue
			}
			id := match(windows)
			if id == 0 {
				continue
			}
			if node == "" {
				return
			}
			_, _, done, err := connection(ctx, c.path, domainipc.AssignMethod, 1, domainipc.AssignBody{
				Session: session, WindowID: id, Domain: node,
			})
			done()
			if err != nil {
				report(err.Error() + " Check Home / Inbox.")
			}
			return
		}
	}
}

//export panelShortcutRequest
func panelShortcutRequest(save, closeKey, gridKey, settingsKey, functionRow C.int) {
	c := active
	if c == nil {
		return
	}
	go func() {
		var next domainipc.Shortcuts
		var err error
		if save > 0 {
			next = domainipc.Shortcuts{CloseWindow: string(rune(closeKey)), AppGrid: string(rune(gridKey)), NodeSettings: string(rune(settingsKey)), NodeKeys: "numbers"}
			if functionRow != 0 {
				next.NodeKeys = "function"
			}
			_, _, cleanup, e := connection(c.ctx, c.path, domainipc.ShortcutsSetMethod, 1, next)
			cleanup()
			err = e
		} else {
			_, reply, cleanup, e := connection(c.ctx, c.path, domainipc.ShortcutsGetMethod, 1, domainipc.Empty{})
			cleanup()
			err = e
			if err == nil {
				next, err = domainipc.Decode[domainipc.Shortcuts](reply.Body)
			}
		}
		if err == nil {
			_, err = next.Keycodes()
		}
		var close, grid, settings, fn C.int
		message := ""
		if err != nil {
			message = err.Error()
		} else {
			close, grid, settings = C.int(next.CloseWindow[0]), C.int(next.AppGrid[0]), C.int(next.NodeSettings[0])
			if next.NodeKeys == "function" {
				fn = 1
			}
		}
		v := C.CString(message)
		C.shortcut_settings_result(save, close, grid, settings, fn, v)
		C.free(unsafe.Pointer(v))
	}()
}
