//go:build linux && cgo && panel

package main

/*
#cgo pkg-config: gtk4 gio-unix-2.0 librsvg-2.0 epoxy wayland-client
#cgo LDFLAGS: -lgtk4-layer-shell
#include <stdlib.h>
#include "native.h"
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
	"syscall"
	"unsafe"

	"mothership-wm/internal/domainipc"
)

var active *controller
var apps *appLauncher
var mapSettingsPath string

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
	configureWallpaper()
	configureMap()
	ctx, cancel := signal.NotifyContext(context.Background(), syscall.SIGTERM, os.Interrupt)
	active = &controller{ctx: ctx, path: path, show: showNative}
	apps = &appLauncher{ctx: ctx, display: filepath.Join(filepath.Dir(path), socket), report: active.report}
	go active.run()
	go func() {
		<-ctx.Done()
		C.panel_quit()
	}()
	C.panel_run()
	cancel()
	apps.owned.Wait()
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
	C.panel_post(ptr[0], ptr[1], ptr[2], ptr[3], ptr[4], ptr[5], ptr[6], enabled, overview, mapVisible, blankHome, ready, C.uint64_t(v.connectFailed), C.uint64_t(v.preparedRelay))
}

func configureWallpaper() {
	path := os.Getenv("MOTHERSHIP_WALLPAPER_CONFIG")
	warn := func(f string, a ...any) { fmt.Fprintf(os.Stderr, "mothership-wallpaper: "+f+"\n", a...) }
	cfg := loadWallpaperConfig(path, warn)
	data, err := readWallpaper(path, cfg.Path)
	var contain C.int
	if cfg.Mode == "contain" {
		contain = 1
	}
	fallback := func() {
		data := defaultMapWallpaper
		if C.wallpaper_set((*C.uchar)(unsafe.Pointer(&data[0])), C.size_t(len(data)), contain) == 0 {
			warn("bundled wallpaper unavailable; using procedural backdrop")
		}
	}
	switch {
	case err != nil:
		warn("image %q: %v; using bundled wallpaper", cfg.Path, err)
		fallback()
	default:
		if C.wallpaper_set((*C.uchar)(unsafe.Pointer(&data[0])), C.size_t(len(data)), contain) == 0 {
			warn("image is not a renderable PNG; using bundled wallpaper")
			fallback()
		}
	}
	C.wallpaper_show()
}

func configureMap() {
	path := os.Getenv("MOTHERSHIP_MAP_THEME")
	warn := func(f string, a ...any) { fmt.Fprintf(os.Stderr, "mothership-center-map: "+f+"\n", a...) }
	theme := loadMapTheme(path, warn)
	colors := []string{theme.Background, theme.Grid, theme.Text, theme.Stroke, theme.BorderColor}
	cs := make([]*C.char, len(colors))
	for i, color := range colors {
		cs[i] = C.CString(color)
		defer C.free(unsafe.Pointer(cs[i]))
	}
	C.map_hud_configure(cs[0], C.double(theme.Opacity), cs[1], cs[2], cs[3], cs[4])
	settings := defaultMapSettings(theme.BorderColor)
	if dir, err := os.UserConfigDir(); err == nil {
		mapSettingsPath = filepath.Join(dir, "mothership-wm", "center-map.json")
		settings, err = loadMapSettings(mapSettingsPath, settings)
		if err != nil {
			warn("settings: %v; using theme defaults", err)
		}
	}
	border := C.CString(settings.BorderColor)
	var frosted, showFPS C.int
	if settings.Frosted {
		frosted = 1
	}
	if settings.ShowFPS {
		showFPS = 1
	}
	C.panel_set_map_settings(border, C.int(settings.OuterMargin), C.int(settings.InnerPadding), frosted, C.int(settings.Animation), showFPS, C.int(theme.PanSpeed), C.int(settings.PanSpeedOverride), maxMapMargin, maxMapPadding)
	C.free(unsafe.Pointer(border))
	family := C.CString(theme.FontFamily)
	if C.map_hud_set_font(family, C.double(theme.FontSize)) == 0 {
		warn("font family %q is not available; using Mothership shell font", theme.FontFamily)
	}
	C.free(unsafe.Pointer(family))
	for node, asset := range theme.Icons {
		data, err := readMapIcon(path, asset)
		if err != nil {
			warn("node %q icon %q: %v; using diamond", node, asset, err)
			continue
		}
		name, svg := C.CString(node), C.CBytes(data)
		ok := C.map_hud_icon(name, (*C.uchar)(svg), C.size_t(len(data)))
		C.free(unsafe.Pointer(name))
		C.free(svg)
		if ok == 0 {
			warn("node %q icon %q is not a renderable SVG; using diamond", node, asset)
		}
	}
}

//export panelSaveMapSettings
func panelSaveMapSettings(border *C.char, outer, inner, frosted, animation, showFPS, panOverride C.int) *C.char {
	settings := mapSettings{BorderColor: C.GoString(border), OuterMargin: int(outer), InnerPadding: int(inner), Frosted: frosted != 0, Animation: int(animation), ShowFPS: showFPS != 0, PanSpeedOverride: int(panOverride)}
	if err := saveMapSettings(mapSettingsPath, settings); err != nil {
		return C.CString("Could not save settings: " + err.Error())
	}
	return nil
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
	case C.int(openTerminal), C.int(openFiles):
		apps.start(appAction(op))
	case 8, 9, 10:
		modes := []string{domainipc.Stack, domainipc.Float, domainipc.Flow}
		active.submit(domainipc.LayoutMethod, domainipc.LayoutBody{Domain: name, Mode: modes[int(op)-8]})
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
