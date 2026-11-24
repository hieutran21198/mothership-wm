package main

import (
	"encoding/json"
	"fmt"

	"mothership-wm/internal/userconfig"
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
}

func loadPanelPreferences(paths panelConfigPaths, warn func(string, ...any)) panelPreferences {
	// Explicit launch inputs still work for existing setups and dev fixtures,
	// but are never written to the user's preferences.
	theme := loadMapTheme(paths.theme, warn)
	wall := loadWallpaperConfig(paths.wallpaper, warn)
	base := defaultMapSettings(theme.BorderColor)
	p := panelPreferences{
		theme: theme, settings: base, wallpaper: wall, iconSource: paths.theme,
		wallpaperSource: paths.wallpaper, settingsBase: base,
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
			Path *string `json:"path"`
			Mode *string `json:"mode"`
		}
		if _, err := doc.Child("wallpaper"); err != nil {
			warn("wallpaper: %v", err)
		} else if err := json.Unmarshal(raw, &patch); err != nil {
			warn("wallpaper: %v", err)
		} else {
			if patch.Path != nil {
				p.wallpaper.Path, p.wallpaperSource = *patch.Path, paths.user
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
