package main

import (
	"encoding/json"
	"fmt"

	"mothership-wm/internal/userconfig"
)

const (
	minPreviewZoom     = 100
	defaultPreviewZoom = 150
	maxPreviewZoom     = 300
	minHoverZoom       = 100
	defaultHoverZoom   = 100
	maxHoverZoom       = 300
)

type panelConfigPaths struct {
	user, legacyMap, theme, wallpaper string
}

type panelPreferences struct {
	theme           mapTheme
	settings        mapSettings
	wallpaper       wallpaperConfig
	iconSource      string
	wallpaperSource string
	settingsBase    mapSettings
	usedLegacy      bool
	homeWidth       int
	cursorSize      int
	homeGuide       bool
	nodeGuide       bool
	nodeTelemetry   bool
	nodePreviewZoom int
	nodeHoverZoom   int
	toastMode       int
	appPins         []string
	defaultNode     bool
	silent          map[string]int // Silent running by Node name or noticeDefault.
}

func loadPanelPreferences(paths panelConfigPaths, warn func(string, ...any)) panelPreferences {
	// Explicit launch inputs still work for existing setups and dev fixtures,
	// but are never written to the user's preferences.
	theme := loadMapTheme(paths.theme, warn)
	wall := loadWallpaperConfig(paths.wallpaper, warn)
	base := defaultMapSettings(theme.BorderColor)
	p := panelPreferences{
		theme: theme, settings: base, wallpaper: wall, iconSource: paths.theme,
		wallpaperSource: paths.wallpaper, settingsBase: base, homeWidth: defaultHomeWidth, cursorSize: userconfig.CursorSizeDefault, homeGuide: true, nodeGuide: true, nodePreviewZoom: defaultPreviewZoom, nodeHoverZoom: defaultHoverZoom, defaultNode: true,
	}
	doc, exists, err := userconfig.Read(paths.user)
	if err != nil {
		warn("user config %q: %v; using defaults and explicit launch inputs", paths.user, err)
		return p
	}
	if !exists {
		// Legacy user overrides apply only before the canonical file exists.
		// Deleting a key from a canonical file must not resurrect a legacy value.
		if paths.legacyMap != "" {
			p.settings, err = loadMapSettings(paths.legacyMap, base)
			if err != nil {
				warn("legacy Center Map settings %q: %v", paths.legacyMap, err)
			} else {
				p.usedLegacy = true
			}
		}
		return p
	}

	p.homeWidth = loadHomeWidth(doc, warn)
	p.cursorSize = userconfig.CursorSize(doc)
	p.homeGuide = loadHomeGuide(doc, warn)
	if node, err := doc.Child("node_panel"); err != nil {
		warn("node_panel: %v", err)
	} else {
		for key, dest := range map[string]*bool{"show_keyboard_guide": &p.nodeGuide, "show_telemetry": &p.nodeTelemetry} {
			raw, ok := node[key]
			if !ok {
				continue
			}
			var value *bool
			if err := json.Unmarshal(raw, &value); err != nil || value == nil {
				warn("node_panel.%s must be true or false", key)
			} else {
				*dest = *value
			}
		}
		if raw, ok := node["preview_zoom_percent"]; ok {
			var zoom int
			if err := json.Unmarshal(raw, &zoom); err != nil || zoom < minPreviewZoom || zoom > maxPreviewZoom {
				warn("node_panel.preview_zoom_percent must be an integer between %d and %d", minPreviewZoom, maxPreviewZoom)
			} else {
				p.nodePreviewZoom = zoom
			}
		}
		if raw, ok := node["hover_zoom_percent"]; ok {
			var zoom int
			if err := json.Unmarshal(raw, &zoom); err != nil || zoom < minHoverZoom || zoom > maxHoverZoom {
				warn("node_panel.hover_zoom_percent must be an integer between %d and %d", minHoverZoom, maxHoverZoom)
			} else {
				p.nodeHoverZoom = zoom
			}
		}
	}
	p.toastMode = loadToastMode(doc, warn)
	p.silent = loadSilentRunning(doc, warn)
	p.appPins = loadAppPins(doc, warn)
	if home, err := doc.Child("home"); err != nil {
		warn("home: %v", err)
	} else if raw, ok := home["default_node"]; ok {
		var enabled *bool
		if err := json.Unmarshal(raw, &enabled); err != nil || enabled == nil {
			warn("home.default_node must be true or false")
		} else {
			p.defaultNode = *enabled
		}
	}
	if raw, ok := doc["center_map"]; ok {
		var style rawTheme
		if _, err := doc.Child("center_map"); err != nil {
			warn("center_map: %v", err)
		} else if err := json.Unmarshal(raw, &style); err != nil {
			warn("center_map theme: %v", err)
		} else {
			// The user's camera speed is an override, not the theme speed
			// that the existing Reset button returns to.
			style.PanSpeed = nil
			p.theme = applyMapTheme(theme, style, warn)
			if style.Icons != nil {
				p.iconSource = paths.user
			}
		}
		var settings rawMapSettings
		if err := json.Unmarshal(raw, &settings); err != nil {
			warn("center_map settings: %v", err)
		} else {
			warnInvalidMapSettings(settings, warn)
			p.settings = applyMapSettings(base, settings)
		}
	}
	if raw, ok := doc["wallpaper"]; ok {
		var patch struct {
			Path         *string `json:"path"`
			Mode         *string `json:"mode"`
			VideoEnabled *bool   `json:"video_enabled"`
		}
		if _, err := doc.Child("wallpaper"); err != nil {
			warn("wallpaper: %v", err)
		} else if err := json.Unmarshal(raw, &patch); err != nil {
			warn("wallpaper: %v", err)
		} else {
			if patch.Path != nil {
				p.wallpaper.Path, p.wallpaperSource = *patch.Path, paths.user
			}
			if patch.VideoEnabled != nil {
				p.wallpaper.VideoEnabled = patch.VideoEnabled
			}
			if patch.Mode != nil {
				switch *patch.Mode {
				case "cover", "contain":
					p.wallpaper.Mode = *patch.Mode
				default:
					warn("wallpaper mode %q is not cover or contain", *patch.Mode)
				}
			}
		}
	}
	return p
}

func patchMapSettings(dst userconfig.Object, before, after mapSettings) error {
	for _, field := range []struct {
		key           string
		before, after any
	}{
		{"border_color", before.BorderColor, after.BorderColor},
		{"outer_margin", before.OuterMargin, after.OuterMargin},
		{"inner_padding", before.InnerPadding, after.InnerPadding},
		{"frosted", before.Frosted, after.Frosted},
		{"animation", before.Animation, after.Animation},
		{"show_fps", before.ShowFPS, after.ShowFPS},
		{"pan_speed_percent", before.PanSpeedOverride, after.PanSpeedOverride},
	} {
		if field.before == field.after {
			continue
		}
		if field.key == "pan_speed_percent" && after.PanSpeedOverride == 0 {
			delete(dst, field.key)
		} else if err := dst.Set(field.key, field.after); err != nil {
			return err
		}
	}
	return nil
}

func (p *panelPreferences) saveMap(path string, wanted mapSettings) error {
	if err := validateMapSettings(wanted); err != nil {
		return err
	}
	doc, exists, err := userconfig.Read(path)
	if err != nil {
		return fmt.Errorf("read existing preferences: %w", err)
	}
	center, err := doc.Child("center_map")
	if err != nil {
		return fmt.Errorf("read center_map: %w", err)
	}
	if !exists && p.usedLegacy {
		// A first, explicit Save carries accepted legacy overrides forward.
		// Do not copy defaults or anything from development theme files.
		if err := patchMapSettings(center, p.settingsBase, p.settings); err != nil {
			return err
		}
	}
	if err := patchMapSettings(center, p.settings, wanted); err != nil {
		return err
	}
	if err := doc.Set("center_map", center); err != nil {
		return err
	}
	if err := userconfig.Write(path, doc); err != nil {
		return err
	}
	p.usedLegacy = false
	p.settings = wanted
	return nil
}

func (p *panelPreferences) saveNodePanel(path string, show, telemetry bool, zoom, hover int) error {
	if zoom < minPreviewZoom || zoom > maxPreviewZoom {
		return fmt.Errorf("Preview zoom must be between %d%% and %d%%", minPreviewZoom, maxPreviewZoom)
	}
	if hover < minHoverZoom || hover > maxHoverZoom {
		return fmt.Errorf("Hover zoom must be between %d%% and %d%%", minHoverZoom, maxHoverZoom)
	}
	doc, _, err := userconfig.Read(path)
	if err != nil {
		return err
	}
	module, err := doc.Child("node_panel")
	if err != nil {
		return err
	}
	if err := module.Set("show_keyboard_guide", show); err != nil {
		return err
	}
	if err := module.Set("show_telemetry", telemetry); err != nil {
		return err
	}
	if err := module.Set("preview_zoom_percent", zoom); err != nil {
		return err
	}
	if err := module.Set("hover_zoom_percent", hover); err != nil {
		return err
	}
	if err := doc.Set("node_panel", module); err != nil {
		return err
	}
	if err := userconfig.Write(path, doc); err != nil {
		return err
	}
	p.nodeGuide, p.nodeTelemetry, p.nodePreviewZoom, p.nodeHoverZoom = show, telemetry, zoom, hover
	return nil
}
