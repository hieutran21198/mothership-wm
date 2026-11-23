package main

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestMapThemeSeparatesBackgroundOpacity(t *testing.T) {
	dir := t.TempDir()
	file := filepath.Join(dir, "map.json")
	for _, opacity := range []float64{0, .4, 1} {
		raw := fmt.Sprintf(`{"background":"#2B485F","opacity":%g,"grid":"#36546B","text":"#EFF5FA","stroke":"#88A8B9","icons":{"Dev":"dev.svg"}}`, opacity)
		if err := os.WriteFile(file, []byte(raw), 0600); err != nil {
			t.Fatal(err)
		}
		got := loadMapTheme(file, func(f string, a ...any) { t.Fatalf(f, a...) })
		if got.Opacity != opacity || got.Background != "#2B485F" || got.Grid != "#36546B" || got.Text != "#EFF5FA" || got.Stroke != "#88A8B9" || got.Icons["Dev"] != "dev.svg" || got.Icons["dev"] != "" {
			t.Fatalf("theme %+v", got)
		}
	}
}

func TestMapThemeFallbackAndLocalAsset(t *testing.T) {
	var warnings []string
	warn := func(f string, a ...any) { warnings = append(warnings, fmt.Sprintf(f, a...)) }
	dir := t.TempDir()
	file := filepath.Join(dir, "map.json")
	if got := loadMapTheme(file, warn); got.Opacity != defaultMapTheme().Opacity || len(warnings) != 1 {
		t.Fatalf("missing theme: %+v / %v", got, warnings)
	}
	if err := os.WriteFile(file, []byte(`{"opacity":2,"background":"incorrect","grid":"#abcdef"}`), 0600); err != nil {
		t.Fatal(err)
	}
	got := loadMapTheme(file, warn)
	if got.Grid != "#abcdef" || got.Opacity != defaultMapTheme().Opacity || got.Background != defaultMapTheme().Background || len(warnings) != 3 {
		t.Fatalf("invalid fields: %+v / %v", got, warnings)
	}
	svg := `<svg xmlns="http://www.w3.org/2000/svg"><path d="M0 0H10V10Z"/></svg>`
	if err := os.WriteFile(filepath.Join(dir, "node.svg"), []byte(svg), 0600); err != nil {
		t.Fatal(err)
	}
	bytes, err := readMapIcon(file, "node.svg")
	if err != nil || string(bytes) != svg {
		t.Fatalf("local file: %q / %v", bytes, err)
	}
	for _, name := range []string{"https://example.test/node.svg", "map.json", "missing.svg"} {
		if _, err := readMapIcon(file, name); err == nil {
			t.Fatal("accepted", name)
		}
	}
	if err := os.WriteFile(filepath.Join(dir, "large.svg"), []byte(strings.Repeat("x", maxMapSVG+1)), 0600); err != nil {
		t.Fatal(err)
	}
	if _, err := readMapIcon(file, "large.svg"); err == nil {
		t.Fatal("oversized icon accepted")
	}
}

func TestMapSampleTheme(t *testing.T) {
	path := "themes/orbital.json"
	theme := loadMapTheme(path, func(f string, a ...any) { t.Fatalf(f, a...) })
	asset, err := readMapIcon(path, theme.Icons["Dev"])
	if theme.Opacity != 0 || defaultMapTheme().Opacity != 0 {
		t.Fatalf("sample and default tint should be fully transparent: %+v", theme)
	}
	if err != nil || !strings.Contains(string(asset), "<svg") {
		t.Fatalf("sample SVG: %v", err)
	}
}

func TestMapThemeFontAndFallback(t *testing.T) {
	file := filepath.Join(t.TempDir(), "theme.json")
	run := func(data string, warningCount int) mapTheme {
		t.Helper()
		if err := os.WriteFile(file, []byte(data), 0600); err != nil {
			t.Fatal(err)
		}
		var warnings int
		got := loadMapTheme(file, func(string, ...any) { warnings++ })
		if warnings != warningCount {
			t.Fatalf("warnings=%d want %d, theme=%+v", warnings, warningCount, got)
		}
		return got
	}
	if got := run(`{"font_family":" DejaVu Sans ","font_size":20.5}`, 0); got.FontFamily != "DejaVu Sans" || got.FontSize != 20.5 {
		t.Fatalf("valid font %+v", got)
	}
	if got := run(`{"font_family":"foo,bar","font_size":7}`, 2); got.FontFamily != "Sans" || got.FontSize != 13 {
		t.Fatalf("invalid font %+v", got)
	}
	if got := run(`{"font_family":"MonoSpace","font_size":29}`, 1); got.FontFamily != "MonoSpace" || got.FontSize != 13 {
		t.Fatalf("partial fallback %+v", got)
	}
}

func TestMapBorderColorIncludesIndependentAlpha(t *testing.T) {
	file := filepath.Join(t.TempDir(), "theme.json")
	for _, v := range []struct {
		input, expected string
		warns           int
	}{
		{"#6F91AC00", "#6F91AC00", 0},
		{"#112233", "#112233", 0},
		{"opaque", "#6F91AC9F", 1},
	} {
		if err := os.WriteFile(file, []byte(fmt.Sprintf(`{"border_color":%q,"opacity":0}`, v.input)), 0600); err != nil {
			t.Fatal(err)
		}
		warnings := 0
		got := loadMapTheme(file, func(string, ...any) { warnings++ })
		if got.BorderColor != v.expected || got.Opacity != 0 || warnings != v.warns {
			t.Fatalf("theme=%+v warnings=%d for %q", got, warnings, v.input)
		}
	}
}
