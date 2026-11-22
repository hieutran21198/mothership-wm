// Package app coordinates Domain commands and the lifecycle of one WM session.
// Socket adapters translate requests and events into these use cases.
package app

import (
	"errors"
	"sync"

	"mothership-wm/services/desktopd/internal/domain"
)

var ErrUnavailable = errors.New("WM is not ready")

type Service struct {
	mu     sync.Mutex
	names  domain.State
	active *domain.Session
	ready  bool
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
		s.active, s.ready = nil, false
	}
}

func (s *Service) Ready(conn *domain.Session) bool {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.active != conn || s.ready {
		return false
	}
	s.ready = true
	return true
}

func (s *Service) WindowAdded(conn *domain.Session, id uint64) bool {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.ready && s.active == conn && conn.Add(id)
}

func (s *Service) WindowMapping(conn *domain.Session, id uint64, visible bool) bool {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.ready && s.active == conn && conn.SetMapped(id, visible)
}

func (s *Service) WindowClosed(conn *domain.Session, id uint64) bool {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.ready && s.active == conn && conn.Remove(id)
}

func (s *Service) Create(name string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.names.Create(name)
}

func (s *Service) Assign(sessionID string, id uint64, name string) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	if !s.ready {
		return ErrUnavailable
	}
	return s.names.Assign(s.active, sessionID, id, name)
}

func (s *Service) List() (string, []domain.Window, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if !s.ready {
		return "", nil, ErrUnavailable
	}
	return s.active.ID, s.active.Windows(), nil
}

func (s *Service) ResolveDomain(name string) (string, []domain.Window, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if !s.names.Exists(name) {
		return "", nil, errors.New("unknown Domain")
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
