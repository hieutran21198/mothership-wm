// Package app coordinates Domain commands and the lifecycle of one WM session.
// Socket adapters translate requests and events into these use cases.
package app

import (
	"errors"
	"sync"

	"mothership-wm/services/desktopd/internal/domain"
)

var (
	ErrUnavailable       = errors.New("WM is not ready")
	ErrUnknownDomain     = domain.ErrUnknownDomain
	ErrSubscriptionLimit = errors.New("too many subscribers")
)

type Service struct {
	mu          sync.Mutex
	names       domain.State
	active      *domain.Session
	ready       bool
	selection   Selection
	revision    uint64
	subscribers map[chan Update]struct{}
}

func (s *Service) Connect(id string) *domain.Session {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.active != nil {
		return nil
	}
	s.active = domain.NewSession(id)
	return s.active
}

func (s *Service) Disconnect(conn *domain.Session) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.active == conn {
		wasReady := s.ready
		s.active, s.ready = nil, false
		if wasReady {
			if s.selection.Kind == SelectionAcknowledged {
				s.selection = Selection{Kind: SelectionUnknown}
			}
			s.publish(Update{Kind: WMChanged, Selection: s.currentSelection()})
		}
	}
}

func (s *Service) Ready(conn *domain.Session, home bool) bool {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.active != conn || s.ready {
		return false
	}
	s.ready = true
	s.selection = Selection{Kind: SelectionNotSelected}
	if home {
		s.selection = Selection{Kind: SelectionAcknowledged, Home: true}
	}
	s.publish(Update{Kind: WMChanged, Session: conn.ID, WMReady: true, Selection: s.selection})
	return true
}

func (s *Service) WindowAdded(conn *domain.Session, id uint64) bool {
	s.mu.Lock()
	defer s.mu.Unlock()
	if !s.ready || s.active != conn || !conn.Add(id) {
		return false
	}
	s.publish(Update{Kind: WindowUpdated, Window: conn.Get(id)})
	return true
}

func (s *Service) WindowMapping(conn *domain.Session, id uint64, visible bool) bool {
	s.mu.Lock()
	defer s.mu.Unlock()
	if !s.ready || s.active != conn {
		return false
	}
	before := conn.Get(id)
	if !conn.SetMapped(id, visible) {
		return false
	}
	if before.Mapped != visible {
		s.publish(Update{Kind: WindowUpdated, Window: conn.Get(id)})
	}
	return true
}

func (s *Service) WindowClosed(conn *domain.Session, id uint64) bool {
	s.mu.Lock()
	defer s.mu.Unlock()
	if !s.ready || s.active != conn || !conn.Remove(id) {
		return false
	}
	s.publish(Update{Kind: WindowClosed, Window: domain.Window{ID: id}})
	return true
}

func (s *Service) Create(name string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.names.Create(name) {
		s.publish(Update{Kind: DomainCreated, Domain: name})
	}
}

func (s *Service) Assign(sessionID string, id uint64, name string) (bool, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if !s.ready {
		return false, ErrUnavailable
	}
	before := s.active.Get(id)
	if err := s.names.Assign(s.active, sessionID, id, name); err != nil {
		return false, err
	}
	after := s.active.Get(id)
	if after != before {
		s.publish(Update{Kind: WindowUpdated, Window: after})
	}
	// Only the initial assignment changes Home eligibility. Reassignment does
	// not remove it from Home again, and the named view already has a snapshot.
	return before.Domain == "" && after.Domain != "", nil
}

func (s *Service) ResolveHome() (string, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if !s.ready {
		return "", ErrUnavailable
	}
	return s.active.ID, nil
}

func (s *Service) ResolveDomain(name string) (string, []domain.Window, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if !s.names.Exists(name) {
		return "", nil, ErrUnknownDomain
	}
	if !s.ready {
		return "", nil, ErrUnavailable
	}
	members := make([]domain.Window, 0, 2)
	for _, w := range s.active.Windows() {
		if w.Domain == name {
			members = append(members, w)
		}
	}
	return s.active.ID, members, nil
}
