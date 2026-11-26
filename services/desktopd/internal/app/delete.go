package app

import (
	"errors"
	"fmt"

	"mothership-wm/services/desktopd/internal/domain"
)

var (
	ErrActiveDelete   = errors.New("leave this Node before deleting it")
	ErrNonemptyDelete = errors.New("Node still has windows. Use Force Delete to move them to Default Node.")
)

// NodeDeletion is valid until FinishDelete or CancelDelete. The adapter
// serializes assignments and selections with the WM handoff.
type NodeDeletion struct {
	name    string
	session *domain.Session
	Windows []domain.Window
}

// SetNodeManager records the attached manager for a trusted agent lifecycle
// integration. A display badge or the presence of app windows is not ownership.
func (s *Service) SetNodeManager(name, manager string) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	if !s.names.Exists(name) {
		return ErrUnknownDomain
	}
	if s.deleting == name {
		return errors.New("Node deletion in progress")
	}
	if manager == "" {
		delete(s.managers, name)
		return nil
	}
	if s.managers == nil {
		s.managers = make(map[string]string)
	}
	s.managers[name] = manager
	return nil
}

func (s *Service) BeginDelete(name string, force bool) (NodeDeletion, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if !s.names.Exists(name) {
		return NodeDeletion{}, ErrUnknownDomain
	}
	if s.deleting != "" {
		return NodeDeletion{}, errors.New("Node deletion in progress")
	}
	if agent := s.managers[name]; agent != "" {
		return NodeDeletion{}, fmt.Errorf("Node is managed by %s. Detach the agent or end its session first.", agent)
	}
	if s.ready {
		if s.selection.Kind == SelectionUnknown {
			return NodeDeletion{}, errors.New("current view is unknown; return to Home first")
		}
		if s.selection.Domain == name {
			return NodeDeletion{}, ErrActiveDelete
		}
	}
	plan := NodeDeletion{name: name, session: s.active}
	if s.ready {
		plan.Windows = s.members(name)
	}
	if len(plan.Windows) > 0 && !force {
		return NodeDeletion{}, ErrNonemptyDelete
	}
	s.deleting = name
	return plan, nil
}

func (s *Service) CancelDelete(plan NodeDeletion) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.deleting == plan.name {
		s.deleting = ""
	}
}

// FinishDelete runs only after the WM accepted the Inbox move, if any.
func (s *Service) FinishDelete(plan NodeDeletion, remove func(domain.Node) error) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	if plan.name == "" || s.deleting != plan.name {
		return ErrUnknownDomain
	}
	defer func() { s.deleting = "" }()
	if len(plan.Windows) != 0 {
		if !s.ready || s.active != plan.session {
			return ErrUnavailable
		}
		for _, w := range plan.Windows {
			if moved, ok := s.active.ReturnToInbox(w.ID, plan.name); ok {
				s.publish(Update{Kind: WindowUpdated, Window: moved})
			}
		}
	}
	if remove != nil {
		if err := remove(s.names.Node(plan.name)); err != nil {
			if len(plan.Windows) != 0 {
				return fmt.Errorf("windows moved to Default Node, but the Node was kept: %w", err)
			}
			return fmt.Errorf("could not delete Node: %w", err)
		}
	}
	s.names.Delete(plan.name)
	delete(s.layouts, plan.name)
	delete(s.flowLayouts, plan.name)
	s.publish(Update{Kind: DomainDeleted, Domain: plan.name})
	return nil
}
