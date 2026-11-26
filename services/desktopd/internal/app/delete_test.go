package app

import (
	"errors"
	"strings"
	"testing"

	"mothership-wm/services/desktopd/internal/domain"
)

func TestDeleteGuardsAndShortcutReuse(t *testing.T) {
	s := New([]domain.Node{{Name: "A", Shortcut: 1}, {Name: "B", Shortcut: 2}}, nil, nil)
	wm := s.Connect("s")
	s.Ready(wm, true)
	s.AcknowledgeEntry(wm, Selection{Kind: SelectionAcknowledged, Domain: "A"})
	if _, err := s.BeginDelete("A", true); !errors.Is(err, ErrActiveDelete) {
		t.Fatalf("active deletion: %v", err)
	}
	s.AcknowledgeView(wm, Selection{Kind: SelectionAcknowledged, Home: true})
	if err := s.SetNodeManager("A", "Pilot"); err != nil {
		t.Fatal(err)
	}
	for _, force := range []bool{false, true} {
		if _, err := s.BeginDelete("A", force); err == nil || !strings.Contains(err.Error(), "Pilot") {
			t.Fatalf("manager didn't block force=%v: %v", force, err)
		}
	}
	if err := s.SetNodeManager("A", ""); err != nil {
		t.Fatal(err)
	}
	plan, err := s.BeginDelete("A", false)
	if err != nil {
		t.Fatal(err)
	}
	if err := s.SetNodeManager("A", "Pilot"); err == nil {
		t.Fatal("manager attached during deletion")
	}
	if err := s.FinishDelete(plan, func(domain.Node) error { return errors.New("read only") }); err == nil || !s.names.Exists("A") {
		t.Fatalf("lost Node after persistence error: %v", err)
	}
	plan, err = s.BeginDelete("A", false)
	if err != nil {
		t.Fatal(err)
	}
	if err := s.FinishDelete(plan, nil); err != nil {
		t.Fatal(err)
	}
	s.Create("C")
	if s.names.Exists("A") || s.names.Node("B").Shortcut != 2 || s.names.Node("C").Shortcut != 1 {
		t.Fatal(s.names.Nodes())
	}
}
