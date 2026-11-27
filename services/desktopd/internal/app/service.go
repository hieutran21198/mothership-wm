// Package app coordinates Domain commands and the lifecycle of one WM session.
// Socket adapters translate requests and events into these use cases.
package app

import (
	"errors"
	"fmt"
	"sync"
	"time"

	"mothership-wm/services/desktopd/internal/domain"
)

var (
	ErrUnavailable       = errors.New("WM is not ready")
	ErrUnknownDomain     = domain.ErrUnknownDomain
	ErrSubscriptionLimit = errors.New("too many subscribers")
	ErrUnselectedNode    = errors.New("select this Node before changing its layout")
)

type Service struct {
	mu                sync.Mutex
	names             domain.State
	saveNode          func(domain.Node) error
	now               func() time.Time
	reportPersistence func(error)
	saveLoadout       func(string, []string) error
	missions          MissionLog
	active            *domain.Session
	ready             bool
	selection         Selection
	mapVisible        bool
	deleting          string
	managers          map[string]string
	layouts           map[string]string
	flowLayouts       map[string]string
	revision          uint64
	subscribers       map[chan Update]struct{}
}

// New loads durable Node records; no session reference crosses a restart.
func New(nodes []domain.Node, save func(domain.Node) error, report func(error)) *Service {
	s := &Service{saveNode: save, reportPersistence: report}
	for _, n := range nodes {
		s.names.Put(n)
	}
	return s
}

// UseLoadoutStore sets where SetLoadout commits a Node's loadout.
func (s *Service) UseLoadoutStore(save func(string, []string) error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.saveLoadout = save
}

// SetLoadout replaces one Node's loadout; an empty list clears it. It is
// committed before it is published and changes no membership or view.
func (s *Service) SetLoadout(name string, entries []string) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	if !s.names.Exists(name) || s.deleting == name {
		return ErrUnknownDomain
	}
	if !domain.ValidLoadout(entries) {
		return fmt.Errorf("a loadout has up to %d desktop entry IDs", domain.MaxLoadout)
	}
	entries = append([]string(nil), entries...)
	if len(entries) == 0 {
		entries = nil
	}
	if s.saveLoadout != nil {
		if err := s.saveLoadout(name, entries); err != nil {
			return fmt.Errorf("could not save loadout: %w", err)
		}
	}
	n := s.names.Node(name)
	n.Loadout = entries
	s.names.Put(n)
	s.publish(Update{Kind: NodeLoadoutChanged, Domain: name, Node: n})
	return nil
}

func (s *Service) unixNow() int64 {
	if s.now != nil {
		return s.now().Unix()
	}
	return time.Now().Unix()
}

func (s *Service) Connect(id string) *domain.Session {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.active != nil {
		return nil
	}
	s.active = domain.NewSession(id)
	s.layouts = make(map[string]string)
	s.flowLayouts = make(map[string]string)
	return s.active
}

func (s *Service) Disconnect(conn *domain.Session) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.active == conn {
		wasReady := s.ready
		s.active, s.ready = nil, false
		s.recordMission(Selection{Kind: SelectionUnknown})
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
	return s.WindowAddedWithPID(conn, id, 0)
}

func (s *Service) WindowAddedWithPID(conn *domain.Session, id uint64, pid int) bool {
	s.mu.Lock()
	defer s.mu.Unlock()
	if !s.ready || s.active != conn || !conn.AddProcess(id, pid) {
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

func (s *Service) WindowAppID(conn *domain.Session, id uint64, appID string) bool {
	s.mu.Lock()
	defer s.mu.Unlock()
	if !s.ready || s.active != conn {
		return false
	}
	before := conn.Get(id)
	if !conn.SetAppID(id, appID) {
		return false
	}
	if before.AppID != appID {
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

func (s *Service) Create(name string) { _ = s.CreateNode(name, "") }

func (s *Service) CreateNode(name, description string) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.names.Exists(name) {
		return nil
	}
	n := domain.Node{Name: name, Description: description, CreatedAt: s.unixNow(), Shortcut: s.names.FreeShortcut()}
	if s.saveNode != nil {
		if err := s.saveNode(n); err != nil {
			return fmt.Errorf("could not save Node: %w", err)
		}
	}
	s.names.Put(n)
	s.publish(Update{Kind: DomainCreated, Domain: name, Node: n})
	return nil
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

// Unassign returns one live window to Inbox. A window already in Inbox is
// unchanged. RefreshNamed reports the selected Node's remaining members when
// the window leaves the projection that is currently shown.
func (s *Service) Unassign(sessionID string, id uint64) (Assignment, bool, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if !s.ready {
		return Assignment{}, false, ErrUnavailable
	}
	if s.active.ID != sessionID {
		return Assignment{}, false, domain.ErrStaleSession
	}
	before := s.active.Get(id)
	if before.ID == 0 {
		return Assignment{}, false, domain.ErrUnknownWindow
	}
	moved, ok := s.active.ReturnToInbox(id, before.Domain)
	if !ok {
		return Assignment{}, false, nil
	}
	s.publish(Update{Kind: WindowUpdated, Window: moved})
	var result Assignment
	if selected := s.selection; selected.Kind == SelectionAcknowledged && selected.Domain == before.Domain {
		result.RefreshNamed = true
		result.Members = s.members(selected.Domain)
	}
	return result, true, nil
}

func (s *Service) MapOverview() bool {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.ready && s.mapVisible && s.selection.Kind == SelectionAcknowledged && s.selection.Overview
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

// WindowDomain resolves one live window's current named Domain at command
// time, so a window moved after a client listed it selects its new Node.
// An empty name means the window belongs to Inbox (the Default Node).
func (s *Service) WindowDomain(sessionID string, id uint64) (string, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if !s.ready {
		return "", ErrUnavailable
	}
	if s.active.ID != sessionID {
		return "", domain.ErrStaleSession
	}
	w := s.active.Get(id)
	if w.ID == 0 {
		return "", domain.ErrUnknownWindow
	}
	return w.Domain, nil
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

func (s *Service) LastFlowMode(name string) string {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.flowLayouts[name]
}

func (s *Service) ActiveNode(name string) (Selection, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if !s.ready {
		return Selection{}, ErrUnavailable
	}
	if s.selection.Kind != SelectionAcknowledged || s.selection.Domain != name ||
		(name == "" && !s.selection.DefaultNode) {
		return Selection{}, ErrUnselectedNode
	}
	return s.selection, nil
}
