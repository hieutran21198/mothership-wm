package main

import (
	"errors"
	"testing"

	"mothership-wm/internal/domainipc"
)

func TestParseWindowListSkipsMalformedLines(t *testing.T) {
	rows := parseWindowList("3\tStandup\tbrave\n0\tzero\tx\nbad\tline\n7\t\t\n9\tonly two")
	if len(rows) != 2 || rows[0] != (windowListRow{3, "Standup", "brave"}) || rows[1] != (windowListRow{7, "", ""}) {
		t.Fatalf("rows %+v", rows)
	}
}

func TestWindowSearchJoinsMembershipAndOrdersByRecency(t *testing.T) {
	s := domainipc.Snapshot{
		WMReady: true,
		Domains: []string{"Ops", "Dev", "Archive"},
		NodeProfiles: []domainipc.NodeProfile{
			{Domain: "Ops", Shortcut: 2}, {Domain: "Dev", Shortcut: 3}, {Domain: "Archive"},
		},
		Windows: []domainipc.Window{
			{ID: 1, Domain: "Ops", Mapped: true},
			{ID: 2, Domain: "Dev", Mapped: true},
			{ID: 3, Mapped: true},
			{ID: 4, Domain: "Archive", Mapped: true},
			{ID: 5, Domain: "Dev", Mapped: true},
			{ID: 6, Domain: "Dev"}, // Unmapped in desktopd: not reachable.
		},
	}
	rows := []windowListRow{
		{1, "build log", "ghostty"}, {2, "zeta", "brave"}, {3, "Inbox note", "gedit"},
		{4, "old", "files"}, {5, "Alpha", "brave"}, {6, "hidden", "x"}, {8, "unknown to desktopd", "x"},
	}
	got := windowSearchRows(s, []string{"Dev", "Ops"}, rows, true)
	var ids []uint64
	for _, r := range got {
		ids = append(ids, r.id)
	}
	want := []uint64{5, 2, 1, 4, 3}
	if len(ids) != len(want) {
		t.Fatalf("ids %v, want %v", ids, want)
	}
	for i := range want {
		if ids[i] != want[i] {
			t.Fatalf("ids %v, want %v", ids, want)
		}
	}
	if got[0].node != "Dev" || got[0].key != 3 || got[3].key != 0 || !got[4].inbox || got[4].key != 1 {
		t.Fatalf("labels %+v", got)
	}
	if enc := encodeWindowSearch(got[4:]); enc != "3\tDefault\t1\tInbox note\tgedit\n" {
		t.Fatalf("encoded %q", enc)
	}
	if without := windowSearchRows(s, nil, rows, false); len(without) != 4 {
		t.Fatalf("Inbox listed with Default Node disabled: %+v", without)
	}
}

func TestWindowCommandErrorNamesClosedWindow(t *testing.T) {
	if err := windowCommandError(errors.New("unknown Window ID")); err == nil || err.Error() != "Window closed." {
		t.Fatal(err)
	}
	if windowCommandError(nil) != nil {
		t.Fatal("nil error changed")
	}
}
