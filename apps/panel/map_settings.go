package main

import (
	"encoding/json"
	"errors"
	"fmt"
	"io/fs"
	"os"
	"path/filepath"

	"mothership-wm/internal/domainipc"
)

const (
	defaultMapMargin    = 24
	defaultMapPadding   = 12
	maxMapMargin        = 96
	maxMapPadding       = 64
	defaultMapAnimation = 1
	minMapPanSpeed      = 50
	defaultMapPanSpeed  = 100
	maxMapPanSpeed      = 200
)

type mapSettings struct {
	BorderColor      string `json:"border_color"`
	OuterMargin      int    `json:"outer_margin"`
	InnerPadding     int    `json:"inner_padding"`
	Frosted          bool   `json:"frosted,omitempty"`
	Animation        int    `json:"animation"`
	ShowFPS          bool   `json:"show_fps,omitempty"`
	PanSpeedOverride int    `json:"pan_speed_percent,omitempty"`
}

func defaultMapSettings(border string) mapSettings {
	return mapSettings{BorderColor: border, OuterMargin: defaultMapMargin, InnerPadding: defaultMapPadding, Animation: defaultMapAnimation}
}

func loadMapSettings(path string, base mapSettings) (mapSettings, error) {
	data, err := readLocalAsset(path, 8<<10)
	if errors.Is(err, fs.ErrNotExist) {
		return base, nil
	}
	if err != nil {
		return base, err
	}
	raw, err := domainipc.Decode[struct {
		BorderColor  *string         `json:"border_color"`
		OuterMargin  *int            `json:"outer_margin"`
		InnerPadding *int            `json:"inner_padding"`
		Frosted      *bool           `json:"frosted"`
		Animation    json.RawMessage `json:"animation"`
		ShowFPS      json.RawMessage `json:"show_fps"`
		PanSpeed     json.RawMessage `json:"pan_speed_percent"`
	}](data)
	if err != nil {
		return base, err
	}
	if raw.BorderColor != nil && mapBorderColor.MatchString(*raw.BorderColor) {
		base.BorderColor = *raw.BorderColor
	}
	if raw.OuterMargin != nil && *raw.OuterMargin >= 0 && *raw.OuterMargin <= maxMapMargin {
		base.OuterMargin = *raw.OuterMargin
	}
	if raw.InnerPadding != nil && *raw.InnerPadding >= 0 && *raw.InnerPadding <= maxMapPadding {
		base.InnerPadding = *raw.InnerPadding
	}
	if raw.Frosted != nil {
		base.Frosted = *raw.Frosted
	}
	var animation *int
	if json.Unmarshal(raw.Animation, &animation) == nil && animation != nil && validMapAnimation(*animation) {
		base.Animation = *animation
	}
	var showFPS *bool
	if json.Unmarshal(raw.ShowFPS, &showFPS) == nil && showFPS != nil {
		base.ShowFPS = *showFPS
	}
	if v, ok := mapPanSpeed(raw.PanSpeed); ok {
		base.PanSpeedOverride = v
	}
	return base, nil
}

func validMapPanSpeed(v int) bool { return v >= minMapPanSpeed && v <= maxMapPanSpeed }

func mapPanSpeed(raw json.RawMessage) (int, bool) {
	var v *int
	if json.Unmarshal(raw, &v) != nil || v == nil || !validMapPanSpeed(*v) {
		return 0, false
	}
	return *v, true
}

func validMapAnimation(value int) bool { return value >= 0 && value <= 2 }

func saveMapSettings(path string, settings mapSettings) error {
	if path == "" {
		return errors.New("user configuration directory unavailable")
	}
	if !mapBorderColor.MatchString(settings.BorderColor) ||
		settings.OuterMargin < 0 || settings.OuterMargin > maxMapMargin ||
		settings.InnerPadding < 0 || settings.InnerPadding > maxMapPadding ||
		!validMapAnimation(settings.Animation) ||
		(settings.PanSpeedOverride != 0 && !validMapPanSpeed(settings.PanSpeedOverride)) {
		return errors.New("invalid Center Map settings")
	}
	if info, err := os.Lstat(path); err == nil {
		if !info.Mode().IsRegular() {
			return errors.New("settings path is not a regular file")
		}
	} else if !errors.Is(err, fs.ErrNotExist) {
		return err
	}
	dir := filepath.Dir(path)
	if err := os.MkdirAll(dir, 0700); err != nil {
		return fmt.Errorf("create settings directory: %w", err)
	}
	file, err := os.CreateTemp(dir, ".center-map-*")
	if err != nil {
		return err
	}
	defer os.Remove(file.Name())
	encodeErr := json.NewEncoder(file).Encode(settings)
	closeErr := file.Close()
	if encodeErr != nil {
		return encodeErr
	}
	if closeErr != nil {
		return closeErr
	}
	return os.Rename(file.Name(), path)
}
