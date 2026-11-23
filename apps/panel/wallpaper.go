package main

import (
	"bytes"
	_ "embed"
	"fmt"
	"image/png"
	"path/filepath"
	"strings"

	"mothership-wm/internal/domainipc"
)

//go:embed assets/mothership-frost-ring.png
var defaultMapWallpaper []byte

const (
	maxWallpaperPixels = 4096 * 4096
	maxWallpaperBytes  = 128 << 20
)

type wallpaperConfig struct {
	Path string `json:"path"`
	Mode string `json:"mode"`
}

func loadWallpaperConfig(path string, warn func(string, ...any)) wallpaperConfig {
	defaults := wallpaperConfig{Mode: "cover"}
	if path == "" {
		return defaults
	}
	data, err := readLocalAsset(path, 8<<10)
	if err != nil {
		warn("config %q: %v; using bundled wallpaper in cover mode", path, err)
		return defaults
	}
	cfg, err := domainipc.Decode[wallpaperConfig](data)
	if err != nil {
		warn("config %q: %v; using bundled wallpaper in cover mode", path, err)
		return defaults
	}
	if cfg.Mode != "contain" && cfg.Mode != "cover" {
		if cfg.Mode != "" {
			warn("mode %q is not cover or contain; using cover", cfg.Mode)
		}
		cfg.Mode = defaults.Mode
	}
	return cfg
}

func readWallpaper(configPath, asset string) ([]byte, error) {
	if asset == "" {
		return defaultMapWallpaper, nil
	}
	if strings.Contains(asset, "://") || !strings.EqualFold(filepath.Ext(asset), ".png") {
		return nil, fmt.Errorf("expected a local .png file")
	}
	if !filepath.IsAbs(asset) {
		asset = filepath.Join(filepath.Dir(configPath), asset)
	}
	data, err := readLocalAsset(asset, maxWallpaperBytes)
	if err != nil {
		return nil, err
	}
	size, err := png.DecodeConfig(bytes.NewReader(data))
	if err != nil {
		return nil, err
	}
	if size.Width > 4096 || size.Height > 4096 || uint64(size.Width)*uint64(size.Height) > maxWallpaperPixels {
		return nil, fmt.Errorf("wallpaper exceeds the supported pixel size")
	}
	return data, nil
}
