// Package missionlog stores time spent in Node views: one JSON Lines file
// per local day, plus a checkpoint for the open segment so a crash loses
// at most one checkpoint interval. Only Node names and Unix times are kept.
package missionlog

import (
	"bufio"
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"sync"
	"time"

	"mothership-wm/services/desktopd/internal/app"
	"mothership-wm/services/desktopd/internal/domain"
)

const (
	Retention       = 30 * 24 * time.Hour
	MinSegment      = 5 // seconds; shorter visits are not recorded
	checkpointEvery = 30 * time.Second
	maxDayFile      = 1 << 20
	dayLayout       = "2006-01-02"
)

type record struct {
	Node        string `json:"node,omitempty"`
	DefaultNode bool   `json:"default_node,omitempty"`
	Start       int64  `json:"start"`
	End         int64  `json:"end"`
	Seen        int64  `json:"seen,omitempty"` // checkpoint only
}

type Log struct {
	mu     sync.Mutex
	dir    string
	loc    *time.Location
	open   *app.MissionSegment
	unsent map[string][]app.MissionSegment // segments a write failure kept in memory
	report func(error)
	failed bool
	stop   chan struct{}
	done   chan struct{}
}

// Open prepares the directory, closes a segment a previous run left open
// at its last checkpoint, prunes old days and starts checkpointing.
func Open(stateHome, home string, now time.Time, report func(error)) (*Log, error) {
	if stateHome == "" && filepath.IsAbs(home) {
		stateHome = filepath.Join(home, ".local", "state")
	}
	if !filepath.IsAbs(stateHome) {
		return nil, errors.New("state home must be absolute")
	}
	l := &Log{dir: filepath.Join(stateHome, "mothership-wm", "missions"), loc: time.Local,
		unsent: map[string][]app.MissionSegment{}, report: report}
	if err := os.MkdirAll(l.dir, 0700); err != nil {
		return nil, err
	}
	l.recover()
	l.prune(now)
	l.stop, l.done = make(chan struct{}), make(chan struct{})
	go l.checkpoints()
	return l, nil
}

// Close ends the open segment and stops checkpointing.
func (l *Log) Close(now time.Time) {
	close(l.stop)
	<-l.done
	l.End(now)
}

func (l *Log) checkpoint() string { return filepath.Join(l.dir, "open.json") }

func (l *Log) recover() {
	b, err := os.ReadFile(l.checkpoint())
	if errors.Is(err, os.ErrNotExist) {
		return
	}
	var r record
	if err == nil {
		err = json.Unmarshal(b, &r)
	}
	if err == nil && validRecord(r) && r.Seen > r.Start {
		r.End = r.Seen
		l.appendSegment(app.MissionSegment{Node: r.Node, DefaultNode: r.DefaultNode, Start: r.Start, End: r.End})
	} else if err != nil {
		l.fail(fmt.Errorf("Mission Log checkpoint: %w", err))
	}
	_ = os.Remove(l.checkpoint())
}

func (l *Log) checkpoints() {
	defer close(l.done)
	tick := time.NewTicker(checkpointEvery)
	defer tick.Stop()
	for {
		select {
		case <-l.stop:
			return
		case now := <-tick.C:
			l.mu.Lock()
			l.writeCheckpoint(now)
			l.mu.Unlock()
		}
	}
}

// writeCheckpoint runs under l.mu.
func (l *Log) writeCheckpoint(now time.Time) {
	if l.open == nil {
		return
	}
	b, _ := json.Marshal(record{Node: l.open.Node, DefaultNode: l.open.DefaultNode, Start: l.open.Start, Seen: now.Unix()})
	tmp := l.checkpoint() + ".tmp"
	if err := os.WriteFile(tmp, b, 0600); err != nil {
		l.fail(err)
		return
	}
	if err := os.Rename(tmp, l.checkpoint()); err != nil {
		l.fail(err)
	}
}

func (l *Log) fail(err error) {
	if !l.failed && l.report != nil {
		l.report(err)
	}
	l.failed = true
}

func validRecord(r record) bool {
	return r.Start > 0 && (r.DefaultNode != (r.Node != "")) && (r.DefaultNode || domain.ValidName(r.Node))
}

// Begin opens a segment for the view. The same view keeps its segment, so a
// layout change inside a Node does not split it.
func (l *Log) Begin(node string, defaultNode bool, at time.Time) {
	l.mu.Lock()
	defer l.mu.Unlock()
	if l.open != nil && l.open.Node == node && l.open.DefaultNode == defaultNode {
		return
	}
	l.endLocked(at)
	l.open = &app.MissionSegment{Node: node, DefaultNode: defaultNode, Start: at.Unix()}
	l.writeCheckpoint(at)
}

func (l *Log) End(at time.Time) {
	l.mu.Lock()
	defer l.mu.Unlock()
	l.endLocked(at)
}

func (l *Log) endLocked(at time.Time) {
	if l.open == nil {
		return
	}
	seg := *l.open
	seg.End = at.Unix()
	l.open = nil
	_ = os.Remove(l.checkpoint())
	l.appendSegment(seg)
}

// appendSegment drops short visits and splits at local midnight.
func (l *Log) appendSegment(seg app.MissionSegment) {
	if seg.End-seg.Start < MinSegment {
		return
	}
	for start := seg.Start; start < seg.End; {
		t := time.Unix(start, 0).In(l.loc)
		y, m, d := t.Date()
		next := time.Date(y, m, d+1, 0, 0, 0, 0, l.loc).Unix()
		part := seg
		part.Start, part.End = start, min(seg.End, next)
		l.write(t.Format(dayLayout), part)
		start = part.End
	}
}

func (l *Log) write(day string, seg app.MissionSegment) {
	b, _ := json.Marshal(record{Node: seg.Node, DefaultNode: seg.DefaultNode, Start: seg.Start, End: seg.End})
	f, err := os.OpenFile(filepath.Join(l.dir, day+".jsonl"), os.O_CREATE|os.O_APPEND|os.O_WRONLY, 0600)
	if err == nil {
		_, err = f.Write(append(b, '\n'))
		if closeErr := f.Close(); err == nil {
			err = closeErr
		}
	}
	if err != nil {
		l.unsent[day] = append(l.unsent[day], seg)
		l.fail(fmt.Errorf("Mission Log write: %w", err))
	}
}

func (l *Log) prune(now time.Time) {
	entries, err := os.ReadDir(l.dir)
	if err != nil {
		return
	}
	cutoff := now.Add(-Retention).In(l.loc).Format(dayLayout)
	for _, e := range entries {
		day, ok := strings.CutSuffix(e.Name(), ".jsonl")
		if ok && day < cutoff {
			_ = os.Remove(filepath.Join(l.dir, e.Name()))
		}
	}
}

// Day returns one local day. The open segment is returned as Open on the
// current day; on an earlier day the part it covers is a closed segment.
func (l *Log) Day(day string, now time.Time) (app.MissionDay, error) {
	start, err := time.ParseInLocation(dayLayout, day, l.loc)
	if err != nil || start.Format(dayLayout) != day {
		return app.MissionDay{}, fmt.Errorf("day must be YYYY-MM-DD")
	}
	y, m, d := start.Date()
	end := time.Date(y, m, d+1, 0, 0, 0, 0, l.loc).Unix()
	l.mu.Lock()
	defer l.mu.Unlock()
	out := app.MissionDay{Day: day}
	b, err := os.ReadFile(filepath.Join(l.dir, day+".jsonl"))
	if err != nil && !errors.Is(err, os.ErrNotExist) {
		return out, err
	}
	if len(b) > maxDayFile {
		return out, fmt.Errorf("Mission Log day %s is too large", day)
	}
	scan := bufio.NewScanner(bytes.NewReader(b))
	for scan.Scan() {
		var r record
		if json.Unmarshal(scan.Bytes(), &r) != nil || !validRecord(r) || r.End <= r.Start {
			continue
		}
		out.Segments = append(out.Segments, app.MissionSegment{Node: r.Node, DefaultNode: r.DefaultNode, Start: r.Start, End: r.End})
	}
	out.Segments = append(out.Segments, l.unsent[day]...)
	if l.open != nil && l.open.Start < end && start.Unix() <= now.Unix() {
		open := *l.open
		open.Start = max(open.Start, start.Unix())
		if now.Unix() < end {
			out.Open = &open
		} else {
			open.End = end
			out.Segments = append(out.Segments, open)
		}
	}
	sort.Slice(out.Segments, func(i, j int) bool { return out.Segments[i].Start < out.Segments[j].Start })
	return out, nil
}

// Clear deletes every stored day. An open segment restarts now.
func (l *Log) Clear(now time.Time) error {
	l.mu.Lock()
	defer l.mu.Unlock()
	entries, err := os.ReadDir(l.dir)
	if err != nil {
		return err
	}
	var failure error
	for _, e := range entries {
		if strings.HasSuffix(e.Name(), ".jsonl") {
			if err := os.Remove(filepath.Join(l.dir, e.Name())); err != nil {
				failure = err
			}
		}
	}
	l.unsent = map[string][]app.MissionSegment{}
	if l.open != nil {
		l.open.Start = now.Unix()
		l.writeCheckpoint(now)
	}
	return failure
}
