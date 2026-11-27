package missionlog

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"mothership-wm/services/desktopd/internal/app"
)

func utc(day string, h, m, s int) time.Time {
	t, _ := time.ParseInLocation(dayLayout, day, time.UTC)
	return t.Add(time.Duration(h)*time.Hour + time.Duration(m)*time.Minute + time.Duration(s)*time.Second)
}

func openUTC(t *testing.T, dir string, now time.Time) *Log {
	t.Helper()
	l, err := Open(dir, "", now, func(err error) { t.Log(err) })
	if err != nil {
		t.Fatal(err)
	}
	l.loc = time.UTC
	t.Cleanup(func() {
		select {
		case <-l.stop:
		default:
			close(l.stop)
			<-l.done
		}
	})
	return l
}

func TestSegmentsDropShortVisitsAndSplitAtMidnight(t *testing.T) {
	dir := t.TempDir()
	l := openUTC(t, dir, utc("2026-11-26", 9, 0, 0))
	l.Begin("Ops", false, utc("2026-11-26", 9, 0, 0))
	l.Begin("Ops", false, utc("2026-11-26", 9, 30, 0)) // layout change: same segment
	l.Begin("", true, utc("2026-11-26", 10, 0, 0))
	l.Begin("Dev", false, utc("2026-11-26", 10, 0, 3)) // 3 s Default visit is dropped
	l.Begin("Dev", false, utc("2026-11-26", 23, 0, 0))
	l.End(utc("2026-11-27", 1, 0, 0))

	day, err := l.Day("2026-11-26", utc("2026-11-27", 2, 0, 0))
	if err != nil {
		t.Fatal(err)
	}
	if len(day.Segments) != 2 || day.Open != nil {
		t.Fatalf("day 26: %+v", day)
	}
	ops, dev := day.Segments[0], day.Segments[1]
	if ops.Node != "Ops" || ops.End-ops.Start != 3600 || dev.Node != "Dev" ||
		dev.End != utc("2026-11-27", 0, 0, 0).Unix() {
		t.Fatalf("segments %+v", day.Segments)
	}
	next, _ := l.Day("2026-11-27", utc("2026-11-27", 2, 0, 0))
	if len(next.Segments) != 1 || next.Segments[0].Start != utc("2026-11-27", 0, 0, 0).Unix() ||
		next.Segments[0].End-next.Segments[0].Start != 3600 {
		t.Fatalf("day 27: %+v", next)
	}
	b, _ := os.ReadFile(filepath.Join(dir, "mothership-wm", "missions", "2026-11-26.jsonl"))
	if strings.Contains(string(b), "title") || strings.Count(string(b), "\n") != 2 {
		t.Fatalf("stored day:\n%s", b)
	}
}

func TestOpenSegmentShowsLiveTodayAndClosedOnEarlierDays(t *testing.T) {
	l := openUTC(t, t.TempDir(), utc("2026-11-26", 22, 0, 0))
	l.Begin("Ops", false, utc("2026-11-26", 22, 0, 0))
	now := utc("2026-11-27", 1, 0, 0)
	today, _ := l.Day("2026-11-27", now)
	if today.Open == nil || today.Open.Start != utc("2026-11-27", 0, 0, 0).Unix() || today.Open.End != 0 {
		t.Fatalf("today %+v", today)
	}
	yesterday, _ := l.Day("2026-11-26", now)
	if yesterday.Open != nil || len(yesterday.Segments) != 1 || yesterday.Segments[0].End != utc("2026-11-27", 0, 0, 0).Unix() {
		t.Fatalf("yesterday %+v", yesterday)
	}
	if _, err := l.Day("27-11-2026", now); err == nil {
		t.Fatal("bad day accepted")
	}
}

func TestCrashRecoveryUsesLastCheckpoint(t *testing.T) {
	dir := t.TempDir()
	l := openUTC(t, dir, utc("2026-11-27", 9, 0, 0))
	l.Begin("Ops", false, utc("2026-11-27", 9, 0, 0))
	l.mu.Lock()
	l.writeCheckpoint(utc("2026-11-27", 9, 20, 0))
	l.mu.Unlock()
	// No End: the process died. A new run closes at the checkpoint.
	again := openUTC(t, dir, utc("2026-11-27", 10, 0, 0))
	day, _ := again.Day("2026-11-27", utc("2026-11-27", 10, 0, 0))
	if len(day.Segments) != 1 || day.Segments[0].End-day.Segments[0].Start != 1200 {
		t.Fatalf("recovered %+v", day)
	}
	if _, err := os.Stat(again.checkpoint()); !os.IsNotExist(err) {
		t.Fatal("checkpoint survived recovery")
	}
}

func TestPruneAndClear(t *testing.T) {
	dir := t.TempDir()
	missions := filepath.Join(dir, "mothership-wm", "missions")
	if err := os.MkdirAll(missions, 0700); err != nil {
		t.Fatal(err)
	}
	for _, day := range []string{"2026-10-01", "2026-11-20"} {
		if err := os.WriteFile(filepath.Join(missions, day+".jsonl"), []byte(`{"node":"Ops","start":1,"end":9}`+"\n"), 0600); err != nil {
			t.Fatal(err)
		}
	}
	l := openUTC(t, dir, utc("2026-11-27", 9, 0, 0))
	if _, err := os.Stat(filepath.Join(missions, "2026-10-01.jsonl")); !os.IsNotExist(err) {
		t.Fatal("day older than 30 days kept")
	}
	if _, err := os.Stat(filepath.Join(missions, "2026-11-20.jsonl")); err != nil {
		t.Fatal("recent day pruned")
	}
	l.Begin("Dev", false, utc("2026-11-27", 9, 0, 0))
	if err := l.Clear(utc("2026-11-27", 9, 30, 0)); err != nil {
		t.Fatal(err)
	}
	if left, _ := filepath.Glob(filepath.Join(missions, "*.jsonl")); len(left) != 0 {
		t.Fatalf("files after Clear: %v", left)
	}
	l.End(utc("2026-11-27", 9, 40, 0))
	day, _ := l.Day("2026-11-27", utc("2026-11-27", 10, 0, 0))
	if len(day.Segments) != 1 || day.Segments[0].Start != utc("2026-11-27", 9, 30, 0).Unix() {
		t.Fatalf("open segment after Clear %+v", day)
	}
}

var _ app.MissionLog = (*Log)(nil)
