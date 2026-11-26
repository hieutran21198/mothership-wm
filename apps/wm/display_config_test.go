package main

import (
	"os"
	"path/filepath"
	"testing"

	"mothership-wm/internal/userconfig"
)

func TestDisplayPreferencesAreScopedToConnectorAndPreserveConfig(t *testing.T) {
	path := filepath.Join(t.TempDir(), "config.json")
	if err := os.WriteFile(path, []byte(`{"home_panel":{"width":850},"display":{"future":true}}`), 0600); err != nil {
		t.Fatal(err)
	}
	want := displayPreference{"DP-2", 2880, 1800, 90000, 150}
	if err := saveDisplay(path, want); err != nil {
		t.Fatal(err)
	}
	if got, err := readDisplay(path, "DP-2"); err != nil || got != want {
		t.Fatalf("got %+v %v", got, err)
	}
	if got, err := readDisplay(path, "DP-1"); err != nil || got != (displayPreference{}) {
		t.Fatalf("a different connector inherited settings: %+v %v", got, err)
	}
	doc, _, _ := userconfig.Read(path)
	d, _ := doc.Child("display")
	if string(d["future"]) != "true" || doc["home_panel"] == nil {
		t.Fatal("saving Display lost other settings")
	}
}
