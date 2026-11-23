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
	ErrUnselectedNode    = errors.New("select this Node before changing its layout")
)

type Service struct {
	mu          sync.Mutex
	names       domain.State
	active      *domain.Session
	ready       bool
	selection   Selection
	mapVisible  bool
	layouts     map[string]string
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
	s.layouts = make(map[string]string)
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

// Assignment describes the WM projection work after membership is committed.
type Assignment struct {
	FirstAssignment bool
	RefreshNamed    bool
	Members         []domain.Window
}

func (s *Service) Assign(sessionID string, id uint64, name string) (Assignment, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if !s.ready {
		return Assignment{}, ErrUnavailable
	}
	before := s.active.Get(id)
	if err := s.names.Assign(s.active, sessionID, id, name); err != nil {
		return Assignment{}, err
	}
	after := s.active.Get(id)
	if after == before {
		return Assignment{}, nil
	}
	s.publish(Update{Kind: WindowUpdated, Window: after})
	result := Assignment{FirstAssignment: before.Domain == ""}
	if selected := s.selection; selected.Kind == SelectionAcknowledged && selected.Domain != "" &&
		(selected.Domain == before.Domain || selected.Domain == after.Domain) {
		result.RefreshNamed = true
		result.Members = s.members(selected.Domain)
	}
	return result, nil
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
	return s.active.ID, s.members(name), nil
}

func (s *Service) members(name string) []domain.Window {
	members := make([]domain.Window, 0, domain.MaxNodeWindows)
	for _, w := range s.active.Windows() {
		if w.Domain == name {
			members = append(members, w)
		}
	}
	return members
}

func (s *Service) NodeMode(name string) string {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.layouts[name]
}

func (s *Service) ActiveNode(name string) (Selection, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if !s.ready {
		return Selection{}, ErrUnavailable
	}
	if s.selection.Kind != SelectionAcknowledged || s.selection.Domain != name {
		return Selection{}, ErrUnselectedNode
	}
	return s.selection, nil
}
