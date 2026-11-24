package main

import (
	"encoding/json"
	"errors"
	"io/fs"

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
	raw, err := domainipc.Decode[rawMapSettings](data)
	if err != nil {
		return base, err
	}
	return applyMapSettings(base, raw), nil
}

type rawMapSettings struct {
	BorderColor  *string         `json:"border_color"`
	OuterMargin  *int            `json:"outer_margin"`
	InnerPadding *int            `json:"inner_padding"`
	Frosted      *bool           `json:"frosted"`
	Animation    json.RawMessage `json:"animation"`
	ShowFPS      json.RawMessage `json:"show_fps"`
	PanSpeed     json.RawMessage `json:"pan_speed_percent"`
}

func applyMapSettings(base mapSettings, raw rawMapSettings) mapSettings {
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
	return base
}

func warnInvalidMapSettings(raw rawMapSettings, warn func(string, ...any)) {
	if raw.BorderColor != nil && !mapBorderColor.MatchString(*raw.BorderColor) {
		warn("center_map.border_color must be #RRGGBB or #RRGGBBAA")
	}
	if raw.OuterMargin != nil && (*raw.OuterMargin < 0 || *raw.OuterMargin > maxMapMargin) {
		warn("center_map.outer_margin must be 0 through %d", maxMapMargin)
	}
	if raw.InnerPadding != nil && (*raw.InnerPadding < 0 || *raw.InnerPadding > maxMapPadding) {
		warn("center_map.inner_padding must be 0 through %d", maxMapPadding)
	}
	if len(raw.Animation) != 0 {
		var animation *int
		if json.Unmarshal(raw.Animation, &animation) != nil || (animation != nil && !validMapAnimation(*animation)) {
			warn("center_map.animation must be 0, 1 or 2")
		}
	}
	if len(raw.ShowFPS) != 0 {
		var enabled *bool
		if json.Unmarshal(raw.ShowFPS, &enabled) != nil {
			warn("center_map.show_fps must be a boolean")
		}
	}
	if _, ok := mapPanSpeed(raw.PanSpeed); !ok && len(raw.PanSpeed) != 0 && string(raw.PanSpeed) != "null" {
		warn("center_map.pan_speed_percent must be an integer %d through %d", minMapPanSpeed, maxMapPanSpeed)
	}
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

func validateMapSettings(settings mapSettings) error {
	if !mapBorderColor.MatchString(settings.BorderColor) ||
		settings.OuterMargin < 0 || settings.OuterMargin > maxMapMargin ||
		settings.InnerPadding < 0 || settings.InnerPadding > maxMapPadding ||
		!validMapAnimation(settings.Animation) ||
		(settings.PanSpeedOverride != 0 && !validMapPanSpeed(settings.PanSpeedOverride)) {
		return errors.New("invalid Center Map settings")
	}
	return nil
}
