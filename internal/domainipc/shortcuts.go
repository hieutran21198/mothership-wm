package domainipc

import (
	"fmt"
	"strings"
)

const (
	ShortcutsGetMethod = "shortcuts.get"
	ShortcutsSetMethod = "shortcuts.set"
)

type Shortcuts struct {
	CloseWindow  string `json:"close_window"`
	AppGrid      string `json:"app_grid"`
	NodeSettings string `json:"node_settings"`
	NodeKeys     string `json:"node_keys"`
}

func DefaultShortcuts() Shortcuts {
	return Shortcuts{CloseWindow: "Q", AppGrid: "A", NodeSettings: ",", NodeKeys: "numbers"}
}

// The V1 editor uses Super and physical keys from these three letter rows.
func shortcutKey(s string) (int, bool) {
	if s == "," {
		return 51, true
	}
	if len(s) != 1 {
		return 0, false
	}
	for i, row := range []string{"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"} {
		if n := strings.Index(row, s); n >= 0 {
			return [...]int{16, 30, 44}[i] + n, true
		}
	}
	return 0, false
}

func (s Shortcuts) Keycodes() ([4]int, error) {
	var keys [4]int
	for i, label := range []string{s.CloseWindow, s.AppGrid, s.NodeSettings} {
		key, ok := shortcutKey(label)
		if !ok {
			return keys, fmt.Errorf("Choose a letter or comma for each shortcut.")
		}
		for j := 0; j < i; j++ {
			if keys[j] == key {
				return keys, fmt.Errorf("Win + %s is already in use.", label)
			}
		}
		keys[i] = key
	}
	switch s.NodeKeys {
	case "numbers":
		keys[3] = 2
	case "function":
		keys[3] = 59
	default:
		return keys, fmt.Errorf("Choose 1–9 or F1–F9 for switching Nodes.")
	}
	return keys, nil
}
