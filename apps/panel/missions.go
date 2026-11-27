package main

import (
	"fmt"
	"slices"
	"strings"
	"time"

	"mothership-wm/internal/domainipc"
)

const missionDays = 30 // matches desktopd retention

func missionDayName(now time.Time, offset int) string {
	y, m, d := now.Date()
	return time.Date(y, m, d+offset, 0, 0, 0, 0, now.Location()).Format("2006-01-02")
}

func missionDayStart(day string, loc *time.Location) (time.Time, bool) {
	t, err := time.ParseInLocation("2006-01-02", day, loc)
	return t, err == nil
}

// missionLaneLabel names a lane. A Node that no longer exists keeps its
// history and is marked deleted.
func missionLaneLabel(l domainipc.MissionLane, domains []string) (string, bool) {
	if l.DefaultNode {
		return "Default Node", false
	}
	return l.Node, !slices.Contains(domains, l.Node)
}

// missionText formats a day for the overlay:
//
//	day <label> <today> <now> <day start>
//	lane <label> <deleted> <default> <total seconds>
//	seg <lane> <start> <end> <live>   (seconds from the day's midnight)
//
// Lanes are ordered by total time, longest first.
func missionText(day domainipc.MissionDay, domains []string, now time.Time, today bool) string {
	start, ok := missionDayStart(day.Day, now.Location())
	if !ok {
		return ""
	}
	base := start.Unix()
	limit := int64(24 * 3600)
	if next := start.AddDate(0, 0, 1).Unix() - base; next != limit {
		limit = next // DST days are 23 or 25 hours
	}
	type seg struct {
		lane       int
		start, end int64
		live       bool
	}
	var segs []seg
	totals := make([]int64, len(day.Lanes))
	add := func(s [3]int64, live bool) {
		lane := int(s[0])
		if lane < 0 || lane >= len(day.Lanes) {
			return
		}
		end := s[2]
		if live {
			end = now.Unix()
		}
		a, b := max(s[1]-base, 0), min(end-base, limit)
		if b <= a {
			return
		}
		totals[lane] += b - a
		segs = append(segs, seg{lane, a, b, live})
	}
	for _, s := range day.Segments {
		add(s, false)
	}
	if day.Open != nil && today {
		add(*day.Open, true)
	}
	order := make([]int, 0, len(day.Lanes))
	for i := range day.Lanes {
		if totals[i] > 0 {
			order = append(order, i)
		}
	}
	slices.SortStableFunc(order, func(a, b int) int { return int(totals[b] - totals[a]) })
	position := make(map[int]int, len(order))
	label := start.Format("Mon 2 Jan")
	if today {
		label = "Today · " + label
	}
	var b strings.Builder
	fmt.Fprintf(&b, "day\t%s\t%d\t%d\t%d\t%d\n", label, btoi(today), now.Unix()-base, base, limit)
	clean := strings.NewReplacer("\t", " ", "\n", " ")
	for i, lane := range order {
		position[lane] = i
		name, deleted := missionLaneLabel(day.Lanes[lane], domains)
		fmt.Fprintf(&b, "lane\t%s\t%d\t%d\t%d\n", clean.Replace(name), btoi(deleted), btoi(day.Lanes[lane].DefaultNode), totals[lane])
	}
	for _, s := range segs {
		fmt.Fprintf(&b, "seg\t%d\t%d\t%d\t%d\n", position[s.lane], s.start, s.end, btoi(s.live))
	}
	return b.String()
}

// missionToday keeps today's closed seconds per Node and the open segment,
// for the Map Node Detail Today row.
type missionToday struct {
	day       string
	closed    map[string]int64
	openNode  string
	openStart int64
}

func todayFrom(day domainipc.MissionDay, now time.Time) missionToday {
	t := missionToday{day: day.Day, closed: map[string]int64{}}
	start, ok := missionDayStart(day.Day, now.Location())
	if !ok {
		return t
	}
	for _, s := range day.Segments {
		if l := int(s[0]); l >= 0 && l < len(day.Lanes) && !day.Lanes[l].DefaultNode {
			t.closed[day.Lanes[l].Node] += s[2] - max(s[1], start.Unix())
		}
	}
	if day.Open != nil {
		if l := int(day.Open[0]); l >= 0 && l < len(day.Lanes) && !day.Lanes[l].DefaultNode {
			t.openNode, t.openStart = day.Lanes[l].Node, max(day.Open[1], start.Unix())
		}
	}
	return t
}

func btoi(v bool) int {
	if v {
		return 1
	}
	return 0
}
