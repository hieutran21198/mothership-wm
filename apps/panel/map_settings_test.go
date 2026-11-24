package main

import (
	"bytes"
	"fmt"
	"os"
	"path/filepath"
	"testing"

	"mothership-wm/internal/userconfig"
)

func TestMapSettingsOverlayKeepsSelectedTheme(t *testing.T) {
	root := t.TempDir()
	themeText := []byte(`{"opacity":0,"border_color":"#ABCDEF6F","font_family":"Monospace","font_size":19,"icons":{"Dev":"dev.svg"}}`)
	target := filepath.Join(root, "managed.theme.json")
	themePath := filepath.Join(root, "selected.theme.json")
	settingsPath := filepath.Join(root, "xdg", "mothership-wm", "center-map.json")
	if err := os.WriteFile(target, themeText, 0400); err != nil {
		t.Fatal(err)
	}
	if err := os.Symlink(target, themePath); err != nil {
		t.Fatal(err)
	}
	warn := func(f string, a ...any) { t.Fatalf(f, a...) }
	theme := loadMapTheme(themePath, warn)
	baseline := defaultMapSettings(theme.BorderColor)
	if got, err := loadMapSettings(settingsPath, baseline); err != nil || got != baseline {
		t.Fatalf("%+v %v", got, err)
	}
	want := mapSettings{BorderColor: "#123456BB", OuterMargin: 32, InnerPadding: 18}
	if err := saveMapSettings(settingsPath, want); err != nil {
		t.Fatal(err)
	}
	if got, err := loadMapSettings(settingsPath, baseline); err != nil || got != want {
		t.Fatalf("%+v %v", got, err)
	}
	b, err := os.ReadFile(target)
	if err != nil || !bytes.Equal(b, themeText) {
		t.Fatalf("theme was modified: %s %v", b, err)
	}
	info, err := os.Lstat(themePath)
	if err != nil || info.Mode()&os.ModeSymlink == 0 {
		t.Fatalf("theme link replaced: %v", err)
	}
	again := loadMapTheme(themePath, warn)
	if again.Opacity != 0 || again.FontFamily != "Monospace" || again.FontSize != 19 || again.Icons["Dev"] != "dev.svg" {
		t.Fatalf("%+v", again)
	}
}

func TestMapSettingsBadDataAndWriteFailure(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "center-map.json")
	base := defaultMapSettings("#6F91AC9F")
	if err := os.WriteFile(path, []byte(`{"border_color":"bad","outer_margin":200,"inner_padding":0}`), 0600); err != nil {
		t.Fatal(err)
	}
	got, err := loadMapSettings(path, base)
	if err != nil || got.BorderColor != base.BorderColor || got.OuterMargin != 24 || got.InnerPadding != 0 {
		t.Fatalf("%+v %v", got, err)
	}
	link := filepath.Join(dir, "symlink.json")
	if err := os.Symlink(path, link); err != nil {
		t.Fatal(err)
	}
	if err := saveMapSettings(link, base); err == nil {
		t.Fatal("replaced symlink")
	}
	if err := saveMapSettings(filepath.Join(path, "not-a-directory"), base); err == nil {
		t.Fatal("ignored write error")
	}
	if err := saveMapSettings(path, mapSettings{BorderColor: base.BorderColor, OuterMargin: -1}); err == nil {
		t.Fatal("accepted invalid margin")
	}
}

func TestFrostedIsExplicitAndPersists(t *testing.T) {
	path := filepath.Join(t.TempDir(), "center-map.json")
	base := defaultMapSettings("#6F91AC9F")
	if base.Frosted {
		t.Fatal("Clear must be the default")
	}
	if err := os.WriteFile(path, []byte(`{"outer_margin":40,"inner_padding":8}`), 0600); err != nil {
		t.Fatal(err)
	}
	old, err := loadMapSettings(path, base)
	if err != nil || old.Frosted || old.OuterMargin != 40 || old.InnerPadding != 8 {
		t.Fatalf("existing three-field settings: %+v / %v", old, err)
	}
	old.Frosted = true
	if err := saveMapSettings(path, old); err != nil {
		t.Fatal(err)
	}
	if got, err := loadMapSettings(path, base); err != nil || got != old {
		t.Fatalf("frosted round-trip: %+v / %v", got, err)
	}
	old.Frosted = false
	if err := saveMapSettings(path, old); err != nil {
		t.Fatal(err)
	}
	if got, err := loadMapSettings(path, base); err != nil || got != old {
		t.Fatalf("clear round-trip: %+v / %v", got, err)
	}
}

func TestMapAnimationDefaultsAndRoundTrip(t *testing.T) {
	path := filepath.Join(t.TempDir(), "center-map.json")
	base := defaultMapSettings("#6F91AC9F")
	if base.Animation != 1 {
		t.Fatal("Linear must be the default")
	}
	for _, raw := range []string{"", ",\"animation\":null", ",\"animation\":-1", ",\"animation\":3", ",\"animation\":1.5", ",\"animation\":\"2\""} {
		data := fmt.Sprintf(`{"outer_margin":57,"frosted":true%s}`, raw)
		if err := os.WriteFile(path, []byte(data), 0600); err != nil {
			t.Fatal(err)
		}
		got, err := loadMapSettings(path, base)
		if err != nil || got.Animation != 1 || got.OuterMargin != 57 || !got.Frosted {
			t.Fatalf("animation override %q: %+v / %v", raw, got, err)
		}
	}
	for level := 0; level <= 2; level++ {
		want := base
		want.Animation = level
		if err := saveMapSettings(path, want); err != nil {
			t.Fatal(err)
		}
		if got, err := loadMapSettings(path, base); err != nil || got != want {
			t.Fatalf("animation %d: %+v / %v", level, got, err)
		}
	}
	base.Animation = 3
	if err := saveMapSettings(path, base); err == nil {
		t.Fatal("saved unrecognized animation level")
	}
}

func TestShowFPSPreference(t *testing.T) {
	path := filepath.Join(t.TempDir(), "center-map.json")
	base := defaultMapSettings("#6F91AC9F")
	for _, raw := range []string{`{}`, `{"show_fps":"yes","animation":2}`} {
		if err := os.WriteFile(path, []byte(raw), 0600); err != nil {
			t.Fatal(err)
		}
		got, err := loadMapSettings(path, base)
		if err != nil || got.ShowFPS || (len(raw) > 2 && got.Animation != 2) {
			t.Fatalf("default Show FPS: %+v / %v", got, err)
		}
	}
	for _, enabled := range []bool{true, false} {
		want := base
		want.ShowFPS = enabled
		if err := saveMapSettings(path, want); err != nil {
			t.Fatal(err)
		}
		if got, err := loadMapSettings(path, base); err != nil || got != want {
			t.Fatalf("Show FPS %v: %+v / %v", enabled, got, err)
		}
	}
}

func TestPanSpeedThemeAndUserOverride(t *testing.T) {
	d := t.TempDir()
	themePath, prefPath := filepath.Join(d, "theme.json"), filepath.Join(d, "prefs.json")
	write := func(path, data string) {
		t.Helper()
		if err := os.WriteFile(path, []byte(data), 0600); err != nil {
			t.Fatal(err)
		}
	}
	warn := func(string, ...any) {}
	write(themePath, `{"pan_speed_percent":130}`)
	if speed := loadMapTheme(themePath, warn).PanSpeed; speed != 130 {
		t.Fatalf("theme speed %d", speed)
	}
	dft := defaultMapSettings("#6F91AC")
	apply := func() int {
		t.Helper()
		prefs, err := loadMapSettings(prefPath, dft)
		if err != nil {
			t.Fatal(err)
		}
		if prefs.PanSpeedOverride != 0 {
			return prefs.PanSpeedOverride
		}
		return loadMapTheme(themePath, warn).PanSpeed
	}
	if apply() != 130 {
		t.Fatal("missing override did not use theme")
	}
	dftSave := dft
	dftSave.PanSpeedOverride = 160
	if err := saveMapSettings(prefPath, dftSave); err != nil {
		t.Fatal(err)
	}
	write(themePath, `{"pan_speed_percent":115}`)
	if apply() != 160 {
		t.Fatal("override did not persist across theme change")
	}
	if err := saveMapSettings(prefPath, dft); err != nil {
		t.Fatal(err)
	}
	if apply() != 115 {
		t.Fatal("reset did not follow changed theme")
	}
	for _, v := range []string{`"fast"`, "49", "201", "2.3", "null"} {
		write(prefPath, `{"pan_speed_percent":`+v+`}`)
		if apply() != 115 {
			t.Fatalf("invalid preference %s changed speed", v)
		}
	}
	write(themePath, `{"pan_speed_percent":0,"font_size":19}`)
	bad := loadMapTheme(themePath, warn)
	if bad.PanSpeed != 100 || bad.FontSize != 19 {
		t.Fatalf("bad speed corrupted defaults or font: %+v", bad)
	}
}

func saveMapSettings(path string, settings mapSettings) error {
	if err := validateMapSettings(settings); err != nil {
		return err
	}
	return userconfig.Write(path, settings)
}
