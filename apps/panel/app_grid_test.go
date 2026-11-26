//go:build linux

package main

import (
	"os"
	"path/filepath"
	"reflect"
	"testing"

	"mothership-wm/internal/userconfig"
)

func TestAppGridPinsPersistExactDesktopIDsWithoutReplacingOtherPreferences(t *testing.T) {
	root := t.TempDir()
	path := userconfig.Path(root)
	const id = "com.system76.CosmicFiles.desktop"
	const second = "com.mitchellh.ghostty.desktop"
	putPreferenceFixture(t, path, `{"home_panel":{"width":960},"center_map":{"outer_margin":41},"app_grid":{"pinned":["com.mitchellh.ghostty.desktop"],"extra":true}}`)
	prefs := loadPanelPreferences(panelConfigPaths{user: path}, strictPreferenceWarnings(t))
	if err := prefs.saveAppPin(path, id, true); err != nil {
		t.Fatal(err)
	}
	next := loadPanelPreferences(panelConfigPaths{user: path}, strictPreferenceWarnings(t))
	if next.homeWidth != 960 || next.settings.OuterMargin != 41 ||
		!reflect.DeepEqual(next.appPins, []string{second, id}) {
		t.Fatalf("unexpected reloaded preferences: %+v", next)
	}
	doc, _, err := userconfig.Read(path)
	if err != nil {
		t.Fatal(err)
	}
	grid, _ := doc.Child("app_grid")
	if string(grid["extra"]) != "true" {
		t.Fatalf("lost module settings: %v", grid)
	}
	if err := prefs.saveAppPin(path, id, false); err != nil {
		t.Fatal(err)
	}
	if !reflect.DeepEqual(prefs.appPins, []string{second}) {
		t.Fatal(prefs.appPins)
	}
	for _, invalid := range []string{"../bad.desktop", "/desktopdir/a.desktop", "just an executable", "test\n.desktop"} {
		if err := prefs.saveAppPin(path, invalid, true); err == nil {
			t.Fatal("accepted", invalid)
		}
	}
}

func TestFirstGridPinMigratesMapWithoutWritingLegacy(t *testing.T) {
	root := t.TempDir()
	path := userconfig.Path(root)
	legacy := filepath.Join(root, "legacy.json")
	putPreferenceFixture(t, legacy, `{"frosted":true,"animation":0}`)
	paths := panelConfigPaths{user: path, legacyMap: legacy}
	p := loadPanelPreferences(paths, strictPreferenceWarnings(t))
	if err := p.saveAppPin(path, "work.desktop", true); err != nil {
		t.Fatal(err)
	}
	n := loadPanelPreferences(paths, strictPreferenceWarnings(t))
	if !n.settings.Frosted || n.settings.Animation != 0 || len(n.appPins) != 1 || n.appPins[0] != "work.desktop" {
		t.Fatalf("lost preferences: %+v", n)
	}
	a, _ := os.ReadFile(legacy)
	if string(a) != `{"frosted":true,"animation":0}` {
		t.Fatal("legacy overwritten")
	}
}

func TestGridNeverUsesDesktopActivationOrUnverifiedCommands(t *testing.T) {
	for _, c := range []struct {
		id, exec      string
		dbus, allowed bool
	}{
		{"com.system76.CosmicFiles.desktop", "cosmic-files", false, true},
		{"com.mitchellh.ghostty.desktop", "ghostty", false, true},
		{"com.mitchellh.ghostty.desktop", "ghostty", true, true},
		{"com.mitchellh.ghostty.desktop", "sh", true, false},
		{"com.system76.CosmicTerm.desktop", "cosmic-term", false, true},
		{"com.system76.CosmicTerm.desktop", "cosmic-term", true, false},
		{"org.kde.ark.desktop", "ark", false, true},
		{"org.kde.ark.desktop", "ark", true, false},
		{"brave-browser.desktop", "brave", false, true},
		{"brave-browser.desktop", "brave", true, false},
		{"com.brave.Browser.desktop", "brave", false, true},
		{"btop.desktop", "btop", false, true},
		{"btop.desktop", "sh", false, false},
		{"com.system76.CosmicFiles.desktop", "cosmic-files", true, false},
		{"com.system76.CosmicFiles.desktop", "sh", false, false},
		{"other.desktop", "cosmic-files", false, false},
		{"firefox.desktop", "firefox", false, false},
	} {
		if _, ok := gridLaunchAction(c.id, c.exec, c.dbus); ok != c.allowed {
			t.Fatalf("%+v got %t", c, ok)
		}
	}
}
