package main

import (
	"strconv"
	"strings"
	"testing"
	"time"

	"mothership-wm/internal/domainipc"
)

func TestMissionTextOrdersLanesAndMarksLiveAndDeleted(t *testing.T) {
	loc := time.UTC
	midnight := time.Date(2026, 11, 27, 0, 0, 0, 0, loc)
	now := midnight.Add(10 * time.Hour)
	day := domainipc.MissionDay{
		Day:   "2026-11-27",
		Lanes: []domainipc.MissionLane{{Node: "Ops"}, {DefaultNode: true}, {Node: "Gone"}},
		Segments: [][3]int64{
			{0, midnight.Add(-time.Hour).Unix(), midnight.Add(time.Hour).Unix()}, // clipped at midnight
			{1, midnight.Add(2 * time.Hour).Unix(), midnight.Add(2*time.Hour + 30*time.Minute).Unix()},
			{2, midnight.Add(3 * time.Hour).Unix(), midnight.Add(3*time.Hour + 10*time.Minute).Unix()},
		},
		Open: &[3]int64{0, midnight.Add(9 * time.Hour).Unix(), 0},
	}
	text := missionText(day, []string{"Ops"}, now, true)
	lines := strings.Split(strings.TrimSpace(text), "\n")
	want := []string{
		"day\tToday · Fri 27 Nov\t1\t36000\t" + itoa(midnight.Unix()) + "\t86400",
		"lane\tOps\t0\t0\t7200",
		"lane\tDefault Node\t0\t1\t1800",
		"lane\tGone\t1\t0\t600",
		"seg\t0\t0\t3600\t0",
		"seg\t1\t7200\t9000\t0",
		"seg\t2\t10800\t11400\t0",
		"seg\t0\t32400\t36000\t1",
	}
	if strings.Join(lines, "\n") != strings.Join(want, "\n") {
		t.Fatalf("got\n%s\nwant\n%s", text, strings.Join(want, "\n"))
	}
	// An earlier day ignores Open: desktopd already closed that part.
	if past := missionText(day, []string{"Ops"}, now, false); strings.Contains(past, "\t1\n") && strings.Contains(past, "seg\t0\t32400") {
		t.Fatalf("past day showed a live segment:\n%s", past)
	}
	if missionDayName(now, -1) != "2026-11-26" || missionDayName(now, 0) != "2026-11-27" {
		t.Fatal("day names")
	}
	today := todayFrom(day, now)
	if today.closed["Ops"] != 3600 || today.openNode != "Ops" || today.closed["Gone"] != 600 {
		t.Fatalf("today %+v", today)
	}
}

func itoa(v int64) string { return strconv.FormatInt(v, 10) }
