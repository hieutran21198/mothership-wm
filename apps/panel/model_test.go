package main

import (
	"encoding/json"
	"strings"
	"testing"

	"mothership-wm/internal/domainipc"
)

func TestReducePreservesIdentityAndSelection(t *testing.T) {
	s := domainipc.Snapshot{
		Revision: 4, Session: "one", WMReady: true,
		Selection: domainipc.Selection{State: "acknowledged", Domain: "Dev"},
		Windows:   []domainipc.Window{{ID: 1, Mapped: true, Domain: "Dev"}, {ID: 2, Mapped: true, Domain: "Dev"}},
	}
	e := func(rev uint64, kind, body string) domainipc.Event {
		return domainipc.Event{Revision: rev, Event: kind, Body: json.RawMessage(body)}
	}
	if err := reduce(&s, e(5, "window.updated", `{"id":2,"mapped":true,"domain":"Review"}`)); err != nil {
		t.Fatal(err)
	}
	if s.Selection.Domain != "Dev" || s.Windows[1].ID != 2 || s.Windows[1].Domain != "Review" {
		t.Fatalf("membership created a view switch: %+v", s)
	}
	if err := reduce(&s, e(7, "selection.changed", `{"state":"acknowledged","view":"home"}`)); err == nil {
		t.Fatal("accepted revision gap")
	}
	if s.Selection.Domain != "Dev" || s.Revision != 5 {
		t.Fatalf("gap mutated confirmed state: %+v", s)
	}
	if err := reduce(&s, e(6, "selection.changed", `{"state":"acknowledged","view":"home"}`)); err != nil {
		t.Fatal(err)
	}
	var rendered view
	c := controller{state: s, online: true, pending: true, show: func(v view) { rendered = v }}
	c.render()
	if rendered.title != "Home" || rendered.enabled || rendered.windows[1].key != "one:2" || !strings.Contains(rendered.windows[1].label, "Review") {
		t.Fatalf("bad view: %+v", rendered)
	}
	if err := reduce(&s, e(7, "wm.changed", `{"wm_ready":true,"session":"two","selection":{"state":"acknowledged","view":"home"}}`)); err != nil {
		t.Fatal(err)
	}
	if len(s.Windows) != 0 || s.Session != "two" {
		t.Fatalf("new session kept stale refs: %+v", s)
	}
}

func TestMapVisibilityEventLeavesSelectionAlone(t *testing.T) {
	original := domainipc.Selection{State: "acknowledged", Domain: "Dev", Mode: "tiling"}
	s := domainipc.Snapshot{Revision: 7, Selection: original, Domains: []string{"Dev", "Review"}}
	if err := reduce(&s, domainipc.Event{Revision: 8, Event: domainipc.MapChangedEvent, Body: []byte(`{"visible":true}`)}); err != nil {
		t.Fatal(err)
	}
	if !s.MapVisible || s.Selection != original || s.Revision != 8 || s.Domains[0] != "Dev" {
		t.Fatalf("wrong view after event: %+v", s)
	}
	if err := reduce(&s, domainipc.Event{Revision: 9, Event: domainipc.MapChangedEvent, Body: []byte(`{"visible":false}`)}); err != nil {
		t.Fatal(err)
	}
	if s.MapVisible || s.Selection != original || s.Revision != 9 {
		t.Fatalf("wrong view after hide: %+v", s)
	}
}
