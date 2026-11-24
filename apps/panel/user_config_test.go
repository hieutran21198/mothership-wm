package main

import (
	"encoding/json"
	"os"
	"path/filepath"
	"testing"

	"mothership-wm/internal/userconfig"
)

func putPreferenceFixture(t *testing.T, path, content string) {
	t.Helper()
	if err := os.MkdirAll(filepath.Dir(path), 0700); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(path, []byte(content), 0600); err != nil {
		t.Fatal(err)
	}
}

func strictPreferenceWarnings(t *testing.T) func(string, ...any) {
	t.Helper()
	return func(fmt string, args ...any) { t.Errorf(fmt, args...) }
}

func TestUserConfigPathSparseAndExplicitValues(t *testing.T) {
	home := t.TempDir()
	path := userconfig.Path(home)
	if want := filepath.Join(home, "config", ".mothership", "config.json"); path != want {
		t.Fatalf("path = %s, want %s", path, want)
	}
	theme := filepath.Join(home, "dev", "theme.json")
	legacy := filepath.Join(home, "xdg", "mothership-wm", "center-map.json")
	wall := filepath.Join(home, "dev", "wall.json")
	putPreferenceFixture(t, theme, `{"opacity":0.7,"font_family":"Monospace","icons":{"Dev":"dev.svg"},"pan_speed_percent":135}`)
	putPreferenceFixture(t, legacy, `{"outer_margin":91,"animation":2,"frosted":true}`)
	putPreferenceFixture(t, wall, `{"path":"dev-wallpaper.png","mode":"cover"}`)
	putPreferenceFixture(t, path, `{
		"center_map":{"opacity":0,"frosted":false,"show_fps":false,"animation":0,"inner_padding":0,"pan_speed_percent":150},
		"wallpaper":{"mode":"contain"}
	}`)
	p := loadPanelPreferences(panelConfigPaths{path, legacy, theme, wall}, strictPreferenceWarnings(t))
	if p.usedLegacy || p.settings.Animation != 0 || p.settings.InnerPadding != 0 || p.settings.Frosted || p.settings.ShowFPS ||
		p.settings.OuterMargin != defaultMapMargin || p.settings.PanSpeedOverride != 150 ||
		p.theme.Opacity != 0 || p.theme.FontFamily != "Monospace" || p.theme.PanSpeed != 135 ||
		p.theme.Icons["Dev"] != "dev.svg" || p.iconSource != theme ||
		p.wallpaper.Mode != "contain" || p.wallpaper.Path != "dev-wallpaper.png" || p.wallpaperSource != wall {
		t.Fatalf("unexpected effective configuration: %+v", p)
	}
}

func TestUserConfigFirstSaveRetainsExistingPreferences(t *testing.T) {
	home := t.TempDir()
	paths := panelConfigPaths{
		user: userconfig.Path(home), legacyMap: filepath.Join(home, "old", "center-map.json"),
		theme: filepath.Join(home, "dev", "theme.json"), wallpaper: filepath.Join(home, "dev", "wall.json"),
	}
	legacy := `{"outer_margin":42,"animation":0,"show_fps":true,"pan_speed_percent":160}`
	putPreferenceFixture(t, paths.legacyMap, legacy)
	putPreferenceFixture(t, paths.theme, `{"font_family":"Monospace","border_color":"#123456","icons":{"Dev":"dev.svg"}}`)
	putPreferenceFixture(t, paths.wallpaper, `{"mode":"contain","path":"local.png"}`)
	p := loadPanelPreferences(paths, strictPreferenceWarnings(t))
	if _, err := os.Stat(paths.user); !os.IsNotExist(err) {
		t.Fatalf("startup modified canonical config: %v", err)
	}
	wanted := p.settings
	wanted.InnerPadding = 0
	if err := p.saveMap(paths.user, wanted); err != nil {
		t.Fatal(err)
	}
	again := loadPanelPreferences(paths, strictPreferenceWarnings(t))
	if again.usedLegacy || again.settings != wanted || again.settings.OuterMargin != 42 ||
		again.settings.Animation != 0 || again.settings.PanSpeedOverride != 160 || !again.settings.ShowFPS ||
		again.theme.Icons["Dev"] != "dev.svg" || again.iconSource != paths.theme ||
		again.wallpaper.Mode != "contain" || again.wallpaper.Path != "local.png" || again.wallpaperSource != paths.wallpaper {
		t.Fatalf("transition lost state: %+v", again)
	}
	old, _ := os.ReadFile(paths.legacyMap)
	if string(old) != legacy {
		t.Fatalf("legacy state was changed: %q", old)
	}
	doc, _, err := userconfig.Read(paths.user)
	if err != nil {
		t.Fatal(err)
	}
	center, err := doc.Child("center_map")
	if err != nil || len(center) != 5 || center["border_color"] != nil || doc["wallpaper"] != nil {
		t.Fatalf("Save copied launch defaults or omitted overrides: %v, %v", doc, err)
	}
	again.settings.PanSpeedOverride = 160
	wanted.PanSpeedOverride = 0
	if err := again.saveMap(paths.user, wanted); err != nil {
		t.Fatal(err)
	}
	reset := loadPanelPreferences(paths, strictPreferenceWarnings(t))
	if reset.settings.PanSpeedOverride != 0 || reset.theme.PanSpeed != defaultMapPanSpeed {
		t.Fatalf("reset resurrected legacy speed: %+v", reset)
	}
}

func TestMapSavePatchesOnlyItsChangedKeys(t *testing.T) {
	path := userconfig.Path(t.TempDir())
	putPreferenceFixture(t, path, `{"center_map":{"icons":{"Dev":"icon.svg"},"font_size":19,"frosted":true,"show_fps":true},"wallpaper":{"path":"scene.png"},"future_module":{"meaning":7}}`)
	p := loadPanelPreferences(panelConfigPaths{user: path}, strictPreferenceWarnings(t))
	// A separate editor changes the document after this running Panel read it.
	putPreferenceFixture(t, path, `{"center_map":{"icons":{"Dev":"icon.svg"},"font_size":22,"frosted":true,"show_fps":true},"wallpaper":{"path":"scene.png"},"future_module":{"meaning":7}}`)
	wanted := p.settings
	wanted.Frosted, wanted.ShowFPS = false, false
	if err := p.saveMap(path, wanted); err != nil {
		t.Fatal(err)
	}
	doc, _, err := userconfig.Read(path)
	if err != nil {
		t.Fatal(err)
	}
	center, _ := doc.Child("center_map")
	extra, _ := doc.Child("future_module")
	for _, name := range []string{"frosted", "show_fps"} {
		if string(center[name]) != "false" {
			t.Fatalf("explicit false did not persist: %s=%s", name, center[name])
		}
	}
	next := loadPanelPreferences(panelConfigPaths{user: path}, strictPreferenceWarnings(t))
	if next.theme.FontSize != 22 || next.theme.Icons["Dev"] != "icon.svg" || next.iconSource != path ||
		next.wallpaper.Path != "scene.png" || next.wallpaperSource != path || string(extra["meaning"]) != "7" || len(center) != 4 {
		t.Fatalf("Save overwrote unrelated keys: %+v / %v", next, doc)
	}
}

func TestUserConfigInvalidIsNotOverwritten(t *testing.T) {
	for _, broken := range []string{`{"center_map":`, `{"center_map":7}`, `{"center_map":null}`} {
		path := userconfig.Path(t.TempDir())
		putPreferenceFixture(t, path, broken)
		p := loadPanelPreferences(panelConfigPaths{user: path}, func(string, ...any) {})
		if err := p.saveMap(path, p.settings); err == nil {
			t.Fatalf("saved over invalid config %s", broken)
		}
		raw, _ := os.ReadFile(path)
		if string(raw) != broken {
			t.Fatalf("invalid file changed: %q", raw)
		}
	}
	d := t.TempDir()
	path := userconfig.Path(d)
	target := filepath.Join(d, "managed.conf")
	putPreferenceFixture(t, target, "{}")
	if err := os.MkdirAll(filepath.Dir(path), 0700); err != nil {
		t.Fatal(err)
	}
	if err := os.Symlink(target, path); err != nil {
		t.Fatal(err)
	}
	p := loadPanelPreferences(panelConfigPaths{user: path}, strictPreferenceWarnings(t))
	if err := p.saveMap(path, p.settings); err == nil {
		t.Fatal("Save replaced a managed symlink")
	}
	var after map[string]json.RawMessage
	raw, _ := os.ReadFile(target)
	if len(raw) != 2 || json.Unmarshal(raw, &after) != nil {
		t.Fatalf("managed config modified: %q", raw)
	}
}
