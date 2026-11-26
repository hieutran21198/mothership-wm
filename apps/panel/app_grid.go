//go:build linux

package main

import (
	"encoding/json"
	"fmt"
	"slices"
	"strings"

	"mothership-wm/internal/userconfig"
)

func validDesktopID(id string) bool {
	return len(id) > len(".desktop") && len(id) <= 240 && strings.HasSuffix(id, ".desktop") &&
		!strings.ContainsAny(id, "/\\\n\r\t\x00")
}

func loadAppPins(doc userconfig.Object, warn func(string, ...any)) []string {
	obj, err := doc.Child("app_grid")
	var pins []string
	if err == nil && obj["pinned"] != nil {
		err = json.Unmarshal(obj["pinned"], &pins)
	}
	if err == nil {
		if len(pins) > 256 {
			err = fmt.Errorf("too many pinned apps")
		}
		for _, id := range pins {
			if !validDesktopID(id) {
				err = fmt.Errorf("expected desktop-file IDs")
				break
			}
		}
	}
	if err != nil {
		warn("app_grid.pinned: %v", err)
		return nil
	}
	slices.Sort(pins)
	return slices.Compact(pins)
}

func (p *panelPreferences) saveAppPin(path, id string, active bool) error {
	if !validDesktopID(id) {
		return fmt.Errorf("invalid desktop-file ID")
	}
	doc, exists, err := userconfig.Read(path)
	if err != nil {
		return err
	}
	grid, err := doc.Child("app_grid")
	if err != nil {
		return err
	}
	pins := loadAppPins(doc, func(f string, a ...any) { err = fmt.Errorf(f, a...) })
	if err != nil {
		return err
	}
	if i := slices.Index(pins, id); i >= 0 {
		if !active {
			pins = slices.Delete(pins, i, i+1)
		}
	} else if active {
		if len(pins) == 256 {
			return fmt.Errorf("too many pinned apps")
		}
		pins = append(pins, id)
		slices.Sort(pins)
	}
	if pins == nil {
		pins = []string{}
	}
	if err := grid.Set("pinned", pins); err != nil {
		return err
	}
	if err := doc.Set("app_grid", grid); err != nil {
		return err
	}
	if !exists && p.usedLegacy {
		center, _ := doc.Child("center_map")
		if err := patchMapSettings(center, p.settingsBase, p.settings); err != nil {
			return err
		}
		if len(center) > 0 {
			if err := doc.Set("center_map", center); err != nil {
				return err
			}
		}
	}
	if err := userconfig.Write(path, doc); err != nil {
		return err
	}
	p.appPins, p.usedLegacy = pins, false
	return nil
}

func gridLaunchAction(id, binary string, dbus bool) (appAction, bool) {
	// Ghostty's installed entry opts into activation and single-instance.
	// This one exact adapter deliberately does neither: it invokes our
	// separate already-proven GTK command with single-instance off.
	if id == "com.mitchellh.ghostty.desktop" && binary == "ghostty" {
		return openTerminal, true
	}
	if dbus {
		return 0, false
	}
	switch {
	case id == "com.system76.CosmicFiles.desktop" && binary == "cosmic-files":
		return openFiles, true
	case id == "com.system76.CosmicTerm.desktop" && binary == "cosmic-term":
		return openCosmicTerminal, true
	case id == "org.kde.ark.desktop" && binary == "ark":
		return openArk, true
	case (id == "brave-browser.desktop" || id == "com.brave.Browser.desktop") && binary == "brave":
		return openBrave, true
	case id == "btop.desktop" && binary == "btop":
		return openBtop, true
	default:
		return 0, false
	}
}
