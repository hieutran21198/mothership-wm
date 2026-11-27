package app

import (
	"errors"
	"time"
)

// ErrNoMissionLog reports that this desktopd has no Mission Log store.
var ErrNoMissionLog = errors.New("Mission Log is unavailable")

// MissionSegment is time spent in one Node view. Node is empty for the
// Default Node. End is zero for the open segment.
type MissionSegment struct {
	Node        string
	DefaultNode bool
	Start, End  int64
}

// MissionDay is one local day's closed segments plus the open one, if any.
type MissionDay struct {
	Day      string
	Segments []MissionSegment
	Open     *MissionSegment
}

// MissionLog records acknowledged Node views. The service calls Begin and
// End under its own lock, so implementations must not call back into it.
type MissionLog interface {
	Begin(node string, defaultNode bool, at time.Time)
	End(at time.Time)
	Day(day string, now time.Time) (MissionDay, error)
	Clear(now time.Time) error
}

// UseMissionLog records Node views from the next acknowledged selection.
func (s *Service) UseMissionLog(m MissionLog) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.missions = m
}

func (s *Service) MissionLog() (MissionLog, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.missions == nil {
		return nil, ErrNoMissionLog
	}
	return s.missions, nil
}

// recordMission follows the acknowledged view: named Nodes and the Default
// Node count; Home, Overview, the Map and an unknown outcome do not.
func (s *Service) recordMission(selection Selection) {
	if s.missions == nil {
		return
	}
	now := time.Now()
	if s.now != nil {
		now = s.now()
	}
	switch {
	case selection.Kind == SelectionAcknowledged && selection.Domain != "":
		s.missions.Begin(selection.Domain, false, now)
	case selection.Kind == SelectionAcknowledged && selection.DefaultNode:
		s.missions.Begin("", true, now)
	default:
		s.missions.End(now)
	}
}
