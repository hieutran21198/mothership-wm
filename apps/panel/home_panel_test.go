//go:build linux

package main

import (
	"context"
	"os"
	"path/filepath"
	"reflect"
	"strings"
	"testing"
	"time"

	"mothership-wm/internal/userconfig"
)

func TestHomePanelWidthUsesCanonicalOverridesAndMigratesLegacy(t *testing.T) {
	root := t.TempDir()
	path := userconfig.Path(root)
	legacy := filepath.Join(root, "old", "center-map.json")
	putPreferenceFixture(t, legacy, `{"animation":0,"outer_margin":74}`)
	paths := panelConfigPaths{user: path, legacyMap: legacy}
	p := loadPanelPreferences(paths, strictPreferenceWarnings(t))
	if p.homeWidth != 840 || !p.usedLegacy {
		t.Fatalf("initial width and legacy: %+v", p)
	}
	if err := p.saveHomeWidth(path, 960); err != nil {
		t.Fatal(err)
	}
	next := loadPanelPreferences(paths, strictPreferenceWarnings(t))
	if next.homeWidth != 960 || next.settings.Animation != 0 || next.settings.OuterMargin != 74 || next.usedLegacy {
		t.Fatalf("first Save lost module overrides: %+v", next)
	}
	legacyAfter, _ := os.ReadFile(legacy)
	if string(legacyAfter) != `{"animation":0,"outer_margin":74}` {
		t.Fatal("legacy file was changed")
	}
	putPreferenceFixture(t, path, `{"home_panel":{"width":960,"future":true},"wallpaper":{"mode":"contain"},"center_map":{"show_fps":true}}`)
	next = loadPanelPreferences(paths, strictPreferenceWarnings(t))
	if err := next.saveHomeWidth(path, 1000); err != nil {
		t.Fatal(err)
	}
	doc, _, err := userconfig.Read(path)
	if err != nil {
		t.Fatal(err)
	}
	dock, _ := doc.Child("home_panel")
	wall, _ := doc.Child("wallpaper")
	center, _ := doc.Child("center_map")
	if string(dock["width"]) != "1000" || string(dock["future"]) != "true" || string(wall["mode"]) != `"contain"` || string(center["show_fps"]) != "true" {
		t.Fatal(string(doc["home_panel"]), string(doc["wallpaper"]), string(doc["center_map"]))
	}
	putPreferenceFixture(t, path, `{"home_panel":{"width":0}}`)
	n := 0
	if got := loadPanelPreferences(paths, func(string, ...any) { n++ }); got.homeWidth != 840 || n != 1 {
		t.Fatalf("invalid width must warn and fall back: %d, %d warnings", got.homeWidth, n)
	}
}

func TestHomeKeyboardGuideDefaultAndSave(t *testing.T) {
	path := userconfig.Path(t.TempDir())
	paths := panelConfigPaths{user: path}
	warn := strictPreferenceWarnings(t)
	putPreferenceFixture(t, path, `{"home_panel":{"width":960,"future":true},"center_map":{"show_fps":true}}`)
	p := loadPanelPreferences(paths, warn)
	if !p.homeGuide {
		t.Fatal("missing guide preference must default to on")
	}
	off := false
	if err := p.saveHomeSettings(path, p.homeWidth, nil, &off); err != nil {
		t.Fatal(err)
	}
	p = loadPanelPreferences(paths, warn)
	if p.homeGuide || p.homeWidth != 960 || !p.settings.ShowFPS {
		t.Fatalf("guide save overwrote other preferences: %+v", p)
	}
	doc, _, err := userconfig.Read(path)
	if err != nil {
		t.Fatal(err)
	}
	home, _ := doc.Child("home_panel")
	if string(home["future"]) != "true" {
		t.Fatal("guide save overwrote an unknown key")
	}
	on := true
	if err := p.saveHomeSettings(path, p.homeWidth, nil, &on); err != nil {
		t.Fatal(err)
	}
	if !loadPanelPreferences(paths, warn).homeGuide {
		t.Fatal("guide did not stay on after restart")
	}
}

func TestHomeTelemetryIsDerivedFromRealCounterDeltas(t *testing.T) {
	old := cpuCounters(strings.NewReader("cpu 100 0 50 100 20 0 0 0 19 0\ncpu10 20 0 10 100 0 0 0 0\ncpu2 10 0 20 100 0 0 0 0\n"))
	new := cpuCounters(strings.NewReader("cpu 125 0 75 148 22 0 0 0 19 0\ncpu10 40 0 20 130 0 0 0 0\ncpu2 20 0 30 180 0 0 0 0\n"))
	if cpuPercent(old, new, "cpu") != 50 || cpuPercent(old, new, "cpu2") != 20 || cpuPercent(old, new, "cpu32") != -1 ||
		!reflect.DeepEqual(orderedCPUs(old), []string{"cpu2", "cpu10"}) {
		t.Fatalf("wrong CPU delta: old=%v current=%v", old, new)
	}
	if physical := physicalCPUUsage(old, new, map[string]string{"cpu2": "0/0", "cpu10": "0/0"}); !reflect.DeepEqual(physical, []int{31}) {
		t.Fatalf("siblings must combine deltas in one core bar: %v", physical)
	}
	if used, total, ok := ramUsage(strings.NewReader("MemTotal: 2048 kB\nMemAvailable: 768 kB\n")); !ok || used != 1280 || total != 2048 {
		t.Fatalf("wrong RAM: %d/%d, %v", used, total, ok)
	}
	routes := "vpn 00000000 12345678 0003 0 0 5 00000000\neth1 00000000 12345678 0003 0 0 20 00000000\nwlan0 00000000 12345678 0003 0 0 10 00000000\n"
	if best := bestInterface(strings.NewReader(routes), func(n string) bool { return n != "vpn" }); best != "wlan0" {
		t.Fatalf("route=%q", best)
	}
	t0 := time.Now()
	a := linkCounter{name: "wlan0", at: t0, rx: 800, tx: 200}
	b := linkCounter{name: "wlan0", at: t0.Add(2 * time.Second), rx: 2500800, tx: 200}
	if rx, tx, ok := linkRates(a, b); !ok || rx != 10 || tx != 0 {
		t.Fatalf("wrong rates %.3f/%.3f %v", rx, tx, ok)
	}
	for _, sample := range []linkCounter{
		{name: "other", at: b.at, rx: b.rx, tx: b.tx}, {name: b.name, at: b.at, rx: 0, tx: b.tx},
	} {
		if _, _, ok := linkRates(a, sample); ok {
			t.Fatal("changed/reset interface should require warm-up")
		}
	}
}

func TestHostActionsNeverClaimLockBeforeConfirmation(t *testing.T) {
	for _, c := range []struct {
		name, identity, after, want string
		callLock                    bool
	}{
		{"valid", "1000", "yes", "Host session locked.", true},
		{"foreign", "998", "yes", "Lock unavailable", false},
		{"still open", "1000", "no", "confirmation unavailable", true},
	} {
		t.Run(c.name, func(t *testing.T) {
			ctx, cancel := context.WithTimeout(context.Background(), 20*time.Millisecond)
			defer cancel()
			requested := false
			mock := func(_ context.Context, bin string, args ...string) (string, error) {
				if bin != "loginctl" || args[1] != "self" {
					t.Fatal("unexpected command", bin, args)
				}
				if args[0] == "lock-session" {
					requested = true
					return "", nil
				}
				state := "no"
				if requested {
					state = c.after
				}
				return "User=" + c.identity + "\nType=wayland\nRemote=no\nActive=yes\nLockedHint=" + state, nil
			}
			if got := lockHost(ctx, "", 1000, mock); !strings.Contains(got, c.want) || requested != c.callLock {
				t.Fatalf("%q requested=%v", got, requested)
			}
		})
	}
	if msg := audioStatus(context.Background(), func(_ context.Context, bin string, args ...string) (string, error) {
		if bin != "wpctl" || !reflect.DeepEqual(args, []string{"get-volume", "@DEFAULT_AUDIO_SINK@"}) {
			t.Fatal("audio attempted a non-read path", bin, args)
		}
		return "Volume: 0.49 [MUTED]", nil
	}); !strings.Contains(msg, "49% · Muted") {
		t.Fatal(msg)
	}
}

func TestHomeCursorSizeSave(t *testing.T) {
	path := filepath.Join(t.TempDir(), "config.json")
	if err := os.WriteFile(path, []byte("{\"home_panel\":{\"width\":960},\"other\":{\"keep\":true}}"), 0600); err != nil {
		t.Fatal(err)
	}
	p := loadPanelPreferences(panelConfigPaths{user: path}, func(string, ...any) {})
	if p.cursorSize != 24 {
		t.Fatalf("default cursor size %d", p.cursorSize)
	}
	invalid, selected := 65, 48
	if err := p.saveHomePreferences(path, 960, nil, nil, nil, &invalid); err == nil {
		t.Fatal("invalid cursor size accepted")
	}
	if err := p.saveHomePreferences(path, 960, nil, nil, nil, &selected); err != nil {
		t.Fatal(err)
	}
	next := loadPanelPreferences(panelConfigPaths{user: path}, func(string, ...any) {})
	if next.cursorSize != selected || next.homeWidth != 960 {
		t.Fatalf("reloaded cursor %d, panel width %d", next.cursorSize, next.homeWidth)
	}
	b, err := os.ReadFile(path)
	if err != nil || !strings.Contains(string(b), "\"keep\": true") {
		t.Fatalf("unrelated config changed: %s, %v", b, err)
	}
}
