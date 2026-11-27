package main

import (
	"reflect"
	"testing"

	"mothership-wm/internal/domainipc"
)

func TestLoadoutResolvesMembersAndSkipsUnknownApps(t *testing.T) {
	s := domainipc.Snapshot{Windows: []domainipc.Window{
		{ID: 9, Domain: "Dev", Mapped: true, AppID: "kitty"},
		{ID: 3, Domain: "Dev", Mapped: true, AppID: "com.mitchellh.ghostty"},
		{ID: 5, Domain: "Dev", Mapped: true, AppID: "org.gnome.Nautilus"},
		{ID: 6, Domain: "Dev", AppID: "com.mitchellh.ghostty"}, // unmapped
		{ID: 7, Domain: "Ops", Mapped: true, AppID: "com.mitchellh.ghostty"},
		{ID: 8, Domain: "Dev", Mapped: true},
	}}
	var launched launchedEntries
	launched.record("s1", 3, "com.mitchellh.ghostty.desktop")
	byAppID := func(app string) string {
		if app == "org.gnome.Nautilus" {
			return "org.gnome.Nautilus.desktop"
		}
		return ""
	}
	members := nodeMembers(s, "Dev", func(id uint64) string { return launched.lookup("s1", id) }, byAppID)
	entries, skipped := loadoutFromMembers(members)
	if want := []string{"com.mitchellh.ghostty.desktop", "org.gnome.Nautilus.desktop"}; !reflect.DeepEqual(entries, want) {
		t.Fatalf("entries %v, want %v", entries, want)
	}
	if want := []string{"Window 8", "kitty"}; !reflect.DeepEqual(skipped, want) {
		t.Fatalf("skipped %v, want %v", skipped, want)
	}
	if got := loadoutSaveMessage(entries, skipped); got != "Saved 2 apps. Not relaunchable: Window 8, kitty." {
		t.Fatal(got)
	}
	// A different session forgets launch records.
	if launched.lookup("s2", 3) != "" {
		t.Fatal("launch record crossed sessions")
	}
}

func TestMissingEntriesCountsRepeatsInOrder(t *testing.T) {
	loadout := []string{"term.desktop", "files.desktop", "term.desktop", "browser.desktop"}
	members := []loadoutMember{{entry: "term.desktop"}, {entry: ""}, {entry: "browser.desktop"}}
	if got := missingEntries(loadout, members); !reflect.DeepEqual(got, []string{"files.desktop", "term.desktop"}) {
		t.Fatalf("missing %v", got)
	}
	if got := missingEntries(loadout, nil); !reflect.DeepEqual(got, loadout) {
		t.Fatalf("empty Node %v", got)
	}
	if appCount(1) != "1 app" || appCount(3) != "3 apps" {
		t.Fatal("count wording")
	}
}
