package main

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
	"unicode/utf8"

	"mothership-wm/internal/domainipc"
)

func TestNotificationSourceUsesOnlyUnambiguousLiveNode(t *testing.T) {
	state := domainipc.Snapshot{WMReady: true, Selection: domainipc.Selection{Domain: "Review"}, Windows: []domainipc.Window{
		{ID: 1, PID: 42, Domain: "Dev", Mapped: true},
		{ID: 2, PID: 42, Domain: "Dev", Mapped: true},
		{ID: 3, PID: 42, Domain: "Review", Mapped: false},
		{ID: 4, PID: 72, Mapped: true},
	}}
	if got := notificationSource(state, 42); got != "Dev" {
		t.Fatal(got)
	}
	if got := notificationSource(state, 72); got != noticeDefault {
		t.Fatal(got)
	}
	for _, p := range []int{0, 99} {
		if got := notificationSource(state, p); got != noticeVoid {
			t.Fatal(got)
		}
	}
	state.Windows[2].Mapped = true
	if got := notificationSource(state, 42); got != noticeVoid {
		t.Fatal(got)
	}
	state.WMReady = false
	if got := notificationSource(state, 72); got != noticeVoid {
		t.Fatal(got)
	}
}

func TestToastRouting(t *testing.T) {
	for _, c := range []struct {
		mode          int
		source, shown string
		want          bool
	}{
		{toastCurrent, "Dev", "Dev", true},
		{toastCurrent, "Dev", "Review", false},
		{toastCurrent, "Dev", noticeHome, false},
		{toastCurrent, noticeHome, noticeHome, true},
		{toastCurrent, noticeVoid, noticeHome, false},
		{toastAll, "Dev", "Review", true},
		{toastAll, noticeVoid, "Dev", true},
		{toastOff, "Dev", "Dev", false},
		{toastOff, noticeHome, noticeHome, false},
	} {
		if got := showToast(c.mode, c.source, c.shown); got != c.want {
			t.Fatalf("%+v got %v", c, got)
		}
	}
}

func TestNotificationHistoryPersistsSourceAndReadState(t *testing.T) {
	t.Setenv("XDG_STATE_HOME", t.TempDir())
	l := openNotificationLog()
	l.prepend(notification{Source: noticeHome, App: "System", Title: "Reloaded", Received: time.Now()})
	l.prepend(notification{Source: "Dev", App: "Terminal", Title: "Running", BusID: 2, Received: time.Now()})
	l.prepend(notification{Source: "Review", App: "Terminal", Title: "Complete", BusID: 2, Received: time.Now()})
	l.markRead("")
	l = openNotificationLog()
	if len(l.entries) != 2 || l.entries[0].Source != "Dev" || l.entries[0].Title != "Complete" ||
		l.entries[0].BusID != 0 || !l.entries[0].Read || !l.entries[1].Read {
		t.Fatalf("%+v", l.entries)
	}
	st, err := os.Stat(l.path)
	if err != nil || st.Mode().Perm() != 0600 {
		t.Fatalf("private log: %v %v", st, err)
	}
	if s := noticeText(strings.Repeat("ầ", 300), 200); len(s) > 200 || !utf8.ValidString(s) {
		t.Fatalf("invalid bounded text: %d", len(s))
	}
}

func TestToastModeSavesWithHomeOptions(t *testing.T) {
	path := filepath.Join(t.TempDir(), "config.json")
	if err := os.WriteFile(path, []byte(`{"home_panel":{"other":true}}`), 0600); err != nil {
		t.Fatal(err)
	}
	p := loadPanelPreferences(panelConfigPaths{user: path}, strictPreferenceWarnings(t))
	if p.toastMode != toastCurrent {
		t.Fatal(p.toastMode)
	}
	mode := toastOff
	if err := p.saveHomeAndToastSettings(path, 840, nil, nil, &mode); err != nil {
		t.Fatal(err)
	}
	if got := loadPanelPreferences(panelConfigPaths{user: path}, strictPreferenceWarnings(t)).toastMode; got != toastOff {
		t.Fatal(got)
	}
	raw, _ := os.ReadFile(path)
	if !strings.Contains(string(raw), `"other": true`) {
		t.Fatal(string(raw))
	}
}

func TestClearNotificationLogScopeAndPersistence(t *testing.T) {
	t.Setenv("XDG_STATE_HOME", t.TempDir())
	l := openNotificationLog()
	for _, source := range []string{"Dev", noticeHome, noticeVoid, "Dev"} {
		l.prepend(notification{Source: source, Title: source})
	}
	if err := l.clear("Dev"); err != nil {
		t.Fatal(err)
	}
	l = openNotificationLog()
	if len(l.entries) != 2 || l.entries[0].Source != noticeVoid || l.entries[1].Source != noticeHome {
		t.Fatalf("scoped clear retained wrong records: %+v", l.entries)
	}
	if err := l.clear(""); err != nil {
		t.Fatal(err)
	}
	if got := openNotificationLog(); len(got.entries) != 0 {
		t.Fatalf("cleared entries returned: %+v", got.entries)
	}
}

func TestClearNotificationLogKeepsRecordsOnWriteFailure(t *testing.T) {
	l := notificationLog{path: t.TempDir(), entries: []notification{{Source: noticeHome, Title: "Keep"}}}
	if err := l.clear(""); err == nil || len(l.entries) != 1 {
		t.Fatalf("clear must report failure and keep history: %v, %+v", err, l.entries)
	}
}

func TestNotificationRecordActionsSurviveReplacementAndReload(t *testing.T) {
	t.Setenv("XDG_STATE_HOME", t.TempDir())
	l := openNotificationLog()
	l.prepend(notification{Source: "Dev", Title: "Same title", BusID: 1})
	id := l.entries[0].id
	l.prepend(notification{Source: "Review", Title: "Same title"})
	l.prepend(notification{Source: "Review", Title: "Same title", BusID: 1})
	if l.entries[0].id != id || l.entries[0].Source != "Dev" || !l.markEntryRead(id) {
		t.Fatalf("replacement lost its identity or could not be marked read: %+v", l.entries)
	}
	if !l.entries[0].Read || l.entries[1].Read || l.markEntryRead(id) {
		t.Fatalf("read changed the wrong records: %+v", l.entries)
	}
	l = openNotificationLog()
	if len(l.entries) != 2 || l.entries[0].id == 0 || l.entries[0].id == l.entries[1].id || !l.entries[0].Read {
		t.Fatalf("reload lost distinct records or their read state: %+v", l.entries)
	}
	if err := l.deleteEntry(l.entries[0].id); err != nil {
		t.Fatal(err)
	}
	l = openNotificationLog()
	if len(l.entries) != 1 || l.entries[0].Source != "Review" || l.entries[0].Read {
		t.Fatalf("delete changed the wrong records: %+v", l.entries)
	}
	l.path = t.TempDir()
	if err := l.deleteEntry(l.entries[0].id); err == nil || len(l.entries) != 1 {
		t.Fatalf("failed delete changed history: %v %+v", err, l.entries)
	}
}
