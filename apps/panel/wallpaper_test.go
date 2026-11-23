package main

import (
	"bytes"
	"image/png"
	"os"
	"path/filepath"
	"testing"
)

func TestDefaultWallpaperAndOverride(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "wallpaper.json")
	if err := os.WriteFile(path, []byte(`{"path":"sample.png","mode":"contain"}`), 0600); err != nil {
		t.Fatal(err)
	}
	selected := loadWallpaperConfig(path, func(f string, a ...any) { t.Fatalf(f, a...) })
	if selected.Mode != "contain" || loadWallpaperConfig("", nil).Mode != "cover" {
		t.Fatalf("unexpected wallpaper mode %q", selected.Mode)
	}
	bundled, err := readWallpaper(path, "")
	if err != nil {
		t.Fatal(err)
	}
	size, err := png.DecodeConfig(bytes.NewReader(bundled))
	if err != nil || size.Width != 1672 || size.Height != 941 {
		t.Fatalf("embedded wallpaper: %dx%d / %v", size.Width, size.Height, err)
	}
	if _, err := readWallpaper(path, selected.Path); err == nil {
		t.Fatal("missing wallpaper should let the caller fall back")
	}
	if err := os.WriteFile(filepath.Join(dir, selected.Path), bundled, 0600); err != nil {
		t.Fatal(err)
	}
	got, err := readWallpaper(path, selected.Path)
	if err != nil || !bytes.Equal(got, bundled) {
		t.Fatalf("local override was not read unchanged: %v", err)
	}
	if _, err := readWallpaper(path, "https://example.test/x.png"); err == nil {
		t.Fatal("remote wallpaper not supported")
	}
}
