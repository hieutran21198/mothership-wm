package unixapi

import (
	"encoding/json"

	"mothership-wm/internal/domainipc"
	"mothership-wm/services/desktopd/internal/app"
)

// missionWire encodes a day compactly. If it still exceeds one packet,
// gaps inside a lane are joined with a growing threshold; a 24-hour bar
// cannot show such gaps anyway.
func missionWire(day app.MissionDay) domainipc.MissionDay {
	wire := domainipc.MissionDay{Day: day.Day, Lanes: []domainipc.MissionLane{}, Segments: [][3]int64{}}
	lanes := map[domainipc.MissionLane]int64{}
	lane := func(s app.MissionSegment) int64 {
		key := domainipc.MissionLane{Node: s.Node, DefaultNode: s.DefaultNode}
		i, ok := lanes[key]
		if !ok {
			i = int64(len(wire.Lanes))
			lanes[key] = i
			wire.Lanes = append(wire.Lanes, key)
		}
		return i
	}
	for _, s := range day.Segments {
		wire.Segments = append(wire.Segments, [3]int64{lane(s), s.Start, s.End})
	}
	if day.Open != nil {
		wire.Open = &[3]int64{lane(*day.Open), day.Open.Start, 0}
	}
	for gap := int64(60); fits(wire) == false && gap <= 24*3600; gap *= 2 {
		wire.Segments, wire.Merged = mergeGaps(wire.Segments, gap), true
	}
	return wire
}

func fits(wire domainipc.MissionDay) bool {
	b, _ := json.Marshal(domainipc.Response{ID: 1 << 62, Body: mustJSON(wire)})
	return len(b) <= domainipc.MaxPacket
}

func mustJSON(v any) json.RawMessage {
	b, _ := json.Marshal(v)
	return b
}

// mergeGaps joins consecutive segments of one lane separated by at most gap
// seconds. Input is in start order; output keeps that order.
func mergeGaps(segments [][3]int64, gap int64) [][3]int64 {
	last := map[int64]int{}
	var out [][3]int64
	for _, s := range segments {
		if i, ok := last[s[0]]; ok && s[1]-out[i][2] <= gap {
			out[i][2] = max(out[i][2], s[2])
			continue
		}
		last[s[0]] = len(out)
		out = append(out, s)
	}
	return out
}
