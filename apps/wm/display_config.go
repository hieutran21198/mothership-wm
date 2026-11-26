package main

import (
	"encoding/json"
	"fmt"

	"mothership-wm/internal/userconfig"
)

type displayPreference struct {
	Connector string `json:"connector"`
	Width     int    `json:"width"`
	Height    int    `json:"height"`
	Refresh   int    `json:"refresh_mhz"`
	Scale     int    `json:"scale_percent"`
}

func readDisplay(path, connector string) (displayPreference, error) {
	doc, _, err := userconfig.Read(path)
	if err != nil || doc["display"] == nil {
		return displayPreference{}, err
	}
	var p displayPreference
	if err := json.Unmarshal(doc["display"], &p); err != nil {
		return displayPreference{}, err
	}
	if p.Connector != connector {
		return displayPreference{}, nil
	}
	return p, nil
}

func saveDisplay(path string, p displayPreference) error {
	doc, _, err := userconfig.Read(path)
	if err != nil {
		return err
	}
	display, err := doc.Child("display")
	if err != nil {
		return err
	}
	for key, value := range map[string]any{
		"connector": p.Connector, "width": p.Width, "height": p.Height,
		"refresh_mhz": p.Refresh, "scale_percent": p.Scale,
	} {
		if err := display.Set(key, value); err != nil {
			return err
		}
	}
	if err := doc.Set("display", display); err != nil {
		return fmt.Errorf("update display: %w", err)
	}
	return userconfig.Write(path, doc)
}
