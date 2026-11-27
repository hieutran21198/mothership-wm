package nodestate

import (
	"os"
	"path/filepath"
	"reflect"
	"testing"

	"mothership-wm/services/desktopd/internal/domain"
)

func TestNodeRestartAndValidation(t *testing.T) {
	dir := t.TempDir()
	s, nodes, err := Open(dir, "")
	if err != nil || len(nodes) != 0 {
		t.Fatalf("empty state: %v %v", nodes, err)
	}
	initial := domain.Node{Name: "Dev", Description: "Backend Platform", CreatedAt: 10}
	if err = s.Save(initial); err != nil {
		t.Fatal(err)
	}
	active := initial
	active.LastActive = 20
	if err = s.Save(active); err != nil {
		t.Fatal(err)
	}
	_, got, err := Open(dir, "")
	if err != nil || !reflect.DeepEqual(got, []domain.Node{active}) {
		t.Fatalf("restart metadata: %v %v", got, err)
	}
	f := filepath.Join(s.dir, "Dev.json")
	info, err := os.Stat(f)
	if err != nil || info.Mode().Perm() != 0600 {
		t.Fatalf("state permissions: %v %v", info, err)
	}
	if err = s.Save(domain.Node{Name: "../other", CreatedAt: 11}); err == nil {
		t.Fatal("unsafe path accepted")
	}
	if err = os.WriteFile(f, []byte("{"), 0600); err != nil {
		t.Fatal(err)
	}
	if _, _, err = Open(dir, ""); err == nil {
		t.Fatal("malformed record was silently discarded")
	}
}

func TestShortcutMigrationKeepsAssignmentsAndAnEmptySlot(t *testing.T) {
	dir := t.TempDir()
	s, _, err := Open(dir, "")
	if err != nil {
		t.Fatal(err)
	}
	// Existing Dev keeps 9, even with an earlier named/created legacy Node.
	if err = s.Save(domain.Node{Name: "Dev", CreatedAt: 20, Shortcut: 9}); err != nil {
		t.Fatal(err)
	}
	if err = os.WriteFile(filepath.Join(s.dir, "Alpha.json"), []byte(`{"name":"Alpha","created_at":10}`), 0600); err != nil {
		t.Fatal(err)
	}
	_, nodes, err := Open(dir, "")
	if err != nil || len(nodes) != 2 || nodes[0].Shortcut != 1 || nodes[1].Shortcut != 9 {
		t.Fatalf("migration %v %v", nodes, err)
	}
	if err = os.Remove(filepath.Join(s.dir, "Alpha.json")); err != nil {
		t.Fatal(err)
	}
	_, nodes, err = Open(dir, "")
	if err != nil || len(nodes) != 1 || nodes[0].Shortcut != 9 {
		t.Fatalf("gap shifted a saved shortcut: %v %v", nodes, err)
	}
}

func TestDeleteStaysDeletedAfterOpen(t *testing.T) {
	state := t.TempDir()
	s, _, err := Open(state, "")
	if err != nil {
		t.Fatal(err)
	}
	n := domain.Node{Name: "Dev", CreatedAt: 12, Shortcut: 1}
	if err := s.Save(n); err != nil {
		t.Fatal(err)
	}
	if err := s.Delete(n); err != nil {
		t.Fatal(err)
	}
	_, nodes, err := Open(state, "")
	if err != nil || len(nodes) != 0 {
		t.Fatalf("deleted Node returned: %v, %v", nodes, err)
	}
}

func TestLoadoutFilesSitBesideNodesAndLeaveWithThem(t *testing.T) {
	dir := t.TempDir()
	s, _, err := Open(dir, "")
	if err != nil {
		t.Fatal(err)
	}
	dev := domain.Node{Name: "Dev", CreatedAt: 10}
	if err := s.Save(dev); err != nil {
		t.Fatal(err)
	}
	entries := []string{"com.mitchellh.ghostty.desktop", "com.system76.CosmicFiles.desktop", "com.mitchellh.ghostty.desktop"}
	if err := s.SaveLoadout("Dev", entries); err != nil {
		t.Fatal(err)
	}
	// The Node loader must not see or reject loadout files.
	s, nodes, err := Open(dir, "")
	if err != nil || len(nodes) != 1 {
		t.Fatalf("restart with loadout: %v %v", nodes, err)
	}
	var warnings []error
	warn := func(err error) { warnings = append(warnings, err) }
	if got := s.LoadLoadouts(nodes, warn); !reflect.DeepEqual(got["Dev"], entries) || len(warnings) != 0 {
		t.Fatalf("loaded %v warnings %v", got, warnings)
	}
	for _, bad := range [][]string{{"../evil.desktop"}, {"no-suffix"}, make([]string, 9)} {
		if err := s.SaveLoadout("Dev", bad); err == nil {
			t.Fatalf("accepted %q", bad)
		}
	}
	if err := os.WriteFile(s.loadoutPath("Dev"), []byte(`{"entries":["bad"]}`), 0600); err != nil {
		t.Fatal(err)
	}
	if got := s.LoadLoadouts(nodes, warn); len(got) != 0 || len(warnings) != 1 {
		t.Fatalf("malformed loadout: %v %v", got, warnings)
	}
	if err := s.SaveLoadout("Dev", entries); err != nil {
		t.Fatal(err)
	}
	if err := s.Delete(dev); err != nil {
		t.Fatal(err)
	}
	if _, err := os.Stat(s.loadoutPath("Dev")); !os.IsNotExist(err) {
		t.Fatalf("loadout outlived its Node: %v", err)
	}
	// Clearing a loadout that is already gone is not an error.
	if err := s.SaveLoadout("Dev", nil); err != nil {
		t.Fatal(err)
	}
}
