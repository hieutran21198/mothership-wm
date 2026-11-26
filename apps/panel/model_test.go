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

func TestProfileEmptyRequiresAuthoritativeWindowMembership(t *testing.T) {
	var got view
	c := controller{online: true, show: func(v view) { got = v }}
	c.state.Domains = []string{"Dev", "Empty", "Unmapped"}
	c.state.Windows = []domainipc.Window{
		{ID: 1, Domain: "Dev", Mapped: true},
		{ID: 2, Domain: "Unmapped"},
	}
	c.render()
	if len(got.emptyDomains) != 0 {
		t.Fatalf("unknown state should not claim empty Nodes: %v", got.emptyDomains)
	}
	c.state.WMReady = true
	c.render()
	if len(got.emptyDomains) != 1 || got.emptyDomains[0] != "Empty" {
		t.Fatalf("empty Nodes = %v, want [Empty]", got.emptyDomains)
	}
}

func TestNodeDetailReflectsAppLifecycleAndActivity(t *testing.T) {
	s := domainipc.Snapshot{WMReady: true, Domains: []string{"Dev", "Review"},
		NodeProfiles: []domainipc.NodeProfile{{Domain: "Dev", Description: "Backend", CreatedAt: 100}},
		Windows: []domainipc.Window{
			{ID: 1, Domain: "Dev", Mapped: true, AppID: "org.example.Editor"},
			{ID: 2, Domain: "Dev", Mapped: true, AppID: "org.example.Editor.desktop"},
			{ID: 3, Domain: "Dev", AppID: "org.example.Hidden"},
		}}
	update := func(kind, body string) {
		t.Helper()
		if err := reduce(&s, domainipc.Event{Revision: s.Revision + 1, Event: kind, Body: []byte(body)}); err != nil {
			t.Fatal(err)
		}
	}
	rows := profileRows(s)
	if len(rows[0].apps) != 1 || rows[0].apps[0] != "org.example.Editor" || rows[0].empty || !rows[1].empty || rows[0].LastActive != 0 || rows[0].Description != "Backend" {
		t.Fatalf("wrong starting detail: %+v", rows)
	}
	update("window.updated", `{"id":2,"mapped":true,"domain":"Review","app_id":"org.example.Terminal"}`)
	update("window.closed", `{"window_id":1}`)
	update("selection.changed", `{"state":"acknowledged","domain":"Dev","last_active":220}`)
	rows = profileRows(s)
	if len(rows[0].apps) != 0 || rows[0].empty || rows[0].LastActive != 220 || rows[0].CreatedAt != 100 ||
		len(rows[1].apps) != 1 || rows[1].apps[0] != "org.example.Terminal" || rows[1].empty {
		t.Fatalf("wrong updated detail: %+v", rows)
	}
	update("wm.changed", `{"wm_ready":false,"selection":{"state":"unknown"}}`)
	rows = profileRows(s)
	if rows[0].empty || rows[1].empty || len(rows[1].apps) != 0 || rows[0].LastActive != 220 {
		t.Fatalf("wrong disconnected detail: %+v", rows)
	}
}

func TestRelayRejectionKeepsConfirmedMapAndIdentifiesRequest(t *testing.T) {
	var v view
	c := controller{
		online: true, pending: true,
		state: domainipc.Snapshot{WMReady: true, MapVisible: true, Selection: domainipc.Selection{State: "acknowledged", View: "overview"}},
		show:  func(got view) { v = got },
	}
	c.connect("Dev", 18)
	if v.connectFailed != 18 || !v.ready || !v.mapVisible || !v.overview || v.node != "" {
		t.Fatalf("a stale action changed confirmed Map state or lacked its error token: %+v", v)
	}
}

func TestDeleteEventClearsCatalogAndProfile(t *testing.T) {
	s := domainipc.Snapshot{
		Domains:      []string{"A", "B"},
		NodeProfiles: []domainipc.NodeProfile{{Domain: "A", Shortcut: 1}, {Domain: "B", Shortcut: 2}},
		Windows:      []domainipc.Window{{ID: 7}},
	}
	err := reduce(&s, domainipc.Event{Revision: 1, Event: domainipc.DomainDeletedEvent, Body: []byte(`{"domain":"A"}`)})
	if err != nil || s.Revision != 1 || len(s.Domains) != 1 || s.Domains[0] != "B" ||
		len(s.NodeProfiles) != 1 || s.NodeProfiles[0].Domain != "B" || s.NodeProfiles[0].Shortcut != 2 ||
		len(s.Windows) != 1 || s.Windows[0].ID != 7 {
		t.Fatalf("state after deletion: %+v, error: %v", s, err)
	}
}
