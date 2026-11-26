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

func TestVideoWallpaperPathUsesRegularLocalFiles(t *testing.T) {
	d := t.TempDir()
	config := filepath.Join(d, "config.json")
	p := filepath.Join(d, "sky.MP4")
	if err := os.WriteFile(p, []byte("fixture"), 0600); err != nil {
		t.Fatal(err)
	}
	path, video, err := videoWallpaperPath(config, "sky.MP4")
	if path != p || !video || err != nil {
		t.Fatalf("path=%q video=%v err=%v", path, video, err)
	}
	for _, asset := range []string{"https://example.test/sky.mp4", "missing.mp4", "directory.mp4"} {
		if asset == "directory.mp4" {
			if err := os.Mkdir(filepath.Join(d, asset), 0700); err != nil {
				t.Fatal(err)
			}
		}
		if _, video, err := videoWallpaperPath(config, asset); !video || err == nil {
			t.Fatalf("accepted %q: video=%v err=%v", asset, video, err)
		}
	}
	if _, video, err := videoWallpaperPath(config, "sky.png"); video || err != nil {
		t.Fatalf("intercepted PNG: video=%v err=%v", video, err)
	}
}

func TestWallpaperVideoPreferenceKeepsSelectedPath(t *testing.T) {
	cfg := filepath.Join(t.TempDir(), "config.json")
	const seed = `{"wallpaper":{"path":"selected.MP4","mode":"contain","custom":"keep"},"unrelated":{"keep":true}}`
	if err := os.WriteFile(cfg, []byte(seed), 0600); err != nil {
		t.Fatal(err)
	}
	warn := func(f string, a ...any) { t.Fatalf(f, a...) }
	p := loadPanelPreferences(panelConfigPaths{user: cfg}, warn)
	if !p.wallpaper.allowsVideo() {
		t.Fatal("existing local MP4 must stay enabled")
	}
	off := false
	if err := p.saveHomeSettings(cfg, 1440, &off, nil); err != nil {
		t.Fatal(err)
	}
	p = loadPanelPreferences(panelConfigPaths{user: cfg}, warn)
	if p.wallpaper.allowsVideo() || p.wallpaper.Path != "selected.MP4" ||
		p.wallpaper.playable().Path != "" || p.wallpaper.Mode != "contain" || p.homeWidth != 1440 {
		t.Fatalf("did not preserve or disable clip: %+v", p.wallpaper)
	}
	data, err := os.ReadFile(cfg)
	if err != nil || !bytes.Contains(data, []byte(`"custom": "keep"`)) || !bytes.Contains(data, []byte(`"keep": true`)) {
		t.Fatalf("unrelated settings changed: %s, %v", data, err)
	}
	on := true
	if err := p.saveHomeSettings(cfg, 1440, &on, nil); err != nil {
		t.Fatal(err)
	}
	p = loadPanelPreferences(panelConfigPaths{user: cfg}, warn)
	if p.wallpaper.playable().Path != "selected.MP4" || !p.wallpaper.allowsVideo() {
		t.Fatal("re-enabling must use the original path")
	}
}
