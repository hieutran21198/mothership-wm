package main

import (
	"encoding/json"
	"fmt"
	"io"
	"math"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"unicode"

	"mothership-wm/internal/domainipc"
)

const defaultShellFamily = "Chakra Petch"
const defaultMapLabelFamily = "Black Ops One"

const maxMapTheme = 64 << 10
const maxMapSVG = 256 << 10
const minMapFontSize, maxMapFontSize = 8, 28

type mapTheme struct {
	Background  string            `json:"background"`
	Opacity     float64           `json:"opacity"`
	Grid        string            `json:"grid"`
	Text        string            `json:"text"`
	BorderColor string            `json:"border_color"`
	Stroke      string            `json:"stroke"`
	FontFamily  string            `json:"font_family"`
	FontSize    float64           `json:"font_size"`
	PanSpeed    int               `json:"pan_speed_percent"`
	Icons       map[string]string `json:"icons"`
}

func defaultMapTheme() mapTheme {
	return mapTheme{Background: "#172C42", Opacity: 0, Grid: "#324C62", Text: "#EDF5FB", Stroke: "#759BB0", BorderColor: "#7895A5", FontFamily: defaultMapLabelFamily, FontSize: 13, PanSpeed: defaultMapPanSpeed}
}

var mapColor = regexp.MustCompile(`^#[0-9a-fA-F]{6}$`)
var mapBorderColor = regexp.MustCompile(`^#[0-9a-fA-F]{6}([0-9a-fA-F]{2})?$`)

func readLocalAsset(path string, limit int64) ([]byte, error) {
	s, err := os.Stat(path)
	if err != nil {
		return nil, err
	}
	if !s.Mode().IsRegular() || s.Size() > limit {
		return nil, fmt.Errorf("expected regular file of at most %d bytes", limit)
	}
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()
	b, err := io.ReadAll(io.LimitReader(f, limit+1))
	if err == nil && int64(len(b)) > limit {
		return nil, fmt.Errorf("file exceeds %d bytes", limit)
	}
	return b, err
}

func loadMapTheme(path string, warn func(string, ...any)) mapTheme {
	defaults := defaultMapTheme()
	if path == "" {
		return defaults
	}
	data, err := readLocalAsset(path, maxMapTheme)
	if err != nil {
		warn("theme %q: %v; using defaults", path, err)
		return defaults
	}
	raw, err := domainipc.Decode[rawTheme](data)
	if err != nil {
		warn("theme %q: %v; using defaults", path, err)
		return defaults
	}
	return applyMapTheme(defaults, raw, warn)
}

type rawTheme struct {
	Background  *string           `json:"background"`
	Opacity     *float64          `json:"opacity"`
	Grid        *string           `json:"grid"`
	Text        *string           `json:"text"`
	BorderColor *string           `json:"border_color"`
	Stroke      *string           `json:"stroke"`
	FontFamily  *string           `json:"font_family"`
	FontSize    *float64          `json:"font_size"`
	PanSpeed    json.RawMessage   `json:"pan_speed_percent"`
	Icons       map[string]string `json:"icons"`
}

func applyMapTheme(t mapTheme, raw rawTheme, warn func(string, ...any)) mapTheme {
	for _, k := range []struct {
		name string
		in   *string
		out  *string
	}{
		{"background", raw.Background, &t.Background}, {"grid", raw.Grid, &t.Grid}, {"text", raw.Text, &t.Text}, {"stroke", raw.Stroke, &t.Stroke},
	} {
		if k.in == nil {
			continue
		}
		if !mapColor.MatchString(*k.in) {
			warn("theme %s must be #RRGGBB; using default", k.name)
			continue
		}
		*k.out = *k.in
	}
	if raw.BorderColor != nil {
		if mapBorderColor.MatchString(*raw.BorderColor) {
			t.BorderColor = *raw.BorderColor
		} else {
			warn("theme border_color must be #RRGGBB or #RRGGBBAA; using default")
		}
	}
	if raw.Opacity != nil {
		v := *raw.Opacity
		if math.IsNaN(v) || v < 0 || v > 1 {
			warn("theme opacity must be 0 through 1; using default")
		} else {
			t.Opacity = v
		}
	}
	if raw.FontFamily != nil {
		name := strings.TrimSpace(*raw.FontFamily)
		if name == "" || len(name) > 128 || strings.Contains(name, ",") || strings.IndexFunc(name, unicode.IsControl) >= 0 {
			warn("theme font_family must be a single font family; using default")
		} else {
			t.FontFamily = name
		}
	}
	if raw.FontSize != nil {
		size := *raw.FontSize
		if math.IsNaN(size) || size < minMapFontSize || size > maxMapFontSize {
			warn("theme font_size must be %d through %d logical px; using default", minMapFontSize, maxMapFontSize)
		} else {
			t.FontSize = size
		}
	}
	if v, ok := mapPanSpeed(raw.PanSpeed); ok {
		t.PanSpeed = v
	} else if len(raw.PanSpeed) != 0 && string(raw.PanSpeed) != "null" {
		warn("theme pan_speed_percent must be an integer %d through %d; using default", minMapPanSpeed, maxMapPanSpeed)
	}
	if raw.Icons != nil {
		t.Icons = raw.Icons
	}
	return t
}

func readMapIcon(themePath, asset string) ([]byte, error) {
	if asset == "" || strings.Contains(asset, "://") || !strings.EqualFold(filepath.Ext(asset), ".svg") {
		return nil, fmt.Errorf("expected a local .svg file")
	}
	if !filepath.IsAbs(asset) {
		asset = filepath.Join(filepath.Dir(themePath), asset)
	}
	return readLocalAsset(asset, maxMapSVG)
}
