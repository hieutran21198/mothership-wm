package main

import "testing"

func TestPinNoticeBody(t *testing.T) {
	for _, tc := range []struct{ app, title, want string }{
		{"Firefox", "Standup call", "Firefox — Standup call stays in sight in other Nodes. Meta + P or × unpins."},
		{"Ghostty", "  ", "Ghostty stays in sight in other Nodes. Meta + P or × unpins."},
	} {
		if got := pinNoticeBody(tc.app, tc.title); got != tc.want {
			t.Errorf("pinNoticeBody(%q, %q) = %q, want %q", tc.app, tc.title, got, tc.want)
		}
	}
}
