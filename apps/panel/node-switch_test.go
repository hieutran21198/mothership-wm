package main

import (
	"slices"
	"testing"

	"mothership-wm/internal/domainipc"
)

func switchSnapshot(node string) domainipc.Snapshot {
	return domainipc.Snapshot{
		Session:      "session",
		WMReady:      true,
		Domains:      []string{"Ops", "Dev", "Review"},
		Selection:    domainipc.Selection{State: "acknowledged", Domain: node},
		NodeProfiles: []domainipc.NodeProfile{{Domain: "Review", Description: "Code review"}},
		Windows: []domainipc.Window{
			{ID: 1, Domain: "Review", Mapped: true},
			{ID: 2, Domain: "Review"},
		},
	}
}

func TestNodeSwitchKeepsAcknowledgedOrderDuringGesture(t *testing.T) {
	s := switchSnapshot("Review")
	var n nodeSwitch
	n.reconcile(s, true, false)
	s.Selection.Domain = "Dev"
	n.reconcile(s, true, true)
	view := n.step(s, 1, true, true)
	if view == nil || view.current != 0 || view.selected != 1 ||
		view.rows[1] != (switchNodeRow{name: "Review", description: "Code review", windows: 1}) ||
		!slices.Equal(n.order, []string{"Dev", "Review", "", "Ops"}) {
		t.Fatalf("first step: %+v, order %q", view, n.order)
	}
	s.Domains = append(s.Domains, "Archive")
	n.reconcile(s, true, false)
	view = n.step(s, -1, true, true)
	if view == nil || view.selected != view.current || len(view.rows) != 4 {
		t.Fatalf("reverse step: %+v", view)
	}
	if _, ok := n.finish(true, true); ok {
		t.Fatal("returning to the current Node must not activate")
	}
	n.step(s, 1, true, true)
	if name, ok := n.finish(true, true); !ok || name != "Review" {
		t.Fatalf("quick switch = %q, %v", name, ok)
	}
	if _, ok := n.finish(true, true); ok {
		t.Fatal("duplicate release committed")
	}
	if view := n.step(s, 1, true, true); view == nil || len(view.rows) != 5 {
		t.Fatalf("next gesture did not see new catalog: %+v", view)
	}
}

func TestNodeSwitchCancelsAndDoesNotResumeHeldGesture(t *testing.T) {
	for _, change := range []struct {
		name   string
		update func(*domainipc.Snapshot)
	}{
		{"deleted candidate", func(s *domainipc.Snapshot) { s.Domains = []string{"Dev", "Ops"} }},
		{"new session", func(s *domainipc.Snapshot) { s.Session = "new" }},
		{"external selection", func(s *domainipc.Snapshot) { s.Selection.Domain = "Ops" }},
		{"map transition", func(s *domainipc.Snapshot) { s.MapVisible = true }},
	} {
		t.Run(change.name, func(t *testing.T) {
			s := switchSnapshot("Dev")
			var n nodeSwitch
			n.reconcile(s, true, false)
			if n.step(s, 1, true, true) == nil {
				t.Fatal("gesture did not begin")
			}
			change.update(&s)
			if !n.reconcile(s, true, false) || n.step(s, 1, true, true) != nil {
				t.Fatal("stale gesture could be resumed")
			}
			if _, ok := n.finish(true, true); ok {
				t.Fatal("stale gesture committed")
			}
			if n.step(s, 1, true, true) == nil {
				t.Fatal("new gesture did not begin")
			}
		})
	}
}

func TestNodeSwitchFromHomeAndDefaultPreference(t *testing.T) {
	s := switchSnapshot("")
	s.Selection.View = "home"
	var n nodeSwitch
	n.reconcile(s, true, false)
	view := n.step(s, 1, true, true)
	if view == nil || view.current != -1 || view.rows[view.selected].name != "Default Node" {
		t.Fatalf("first step from Home: %+v", view)
	}
	if _, ok := n.finish(true, false); ok {
		t.Fatal("committed after preference changed")
	}
	view = n.step(s, -1, true, false)
	if view == nil || view.rows[view.selected].name != "Review" {
		t.Fatalf("reverse from Home: %+v", view)
	}
	s.Selection.View = "home_node"
	n.reconcile(s, true, false)
	n.finish(false, true)
	n.step(s, 1, true, false)
	if name, ok := n.finish(true, false); !ok || name != "Dev" {
		t.Fatalf("disabled Default Node was used: %q, %v", name, ok)
	}
}
