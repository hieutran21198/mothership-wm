// Package domain owns desktopd's in-memory Domain and live Window membership.
package domain

import (
	"errors"
	"sort"
	"unicode"
	"unicode/utf8"
)

const MaxNodeWindows = 8

var (
	ErrStaleSession  = errors.New("unknown WM session")
	ErrUnknownWindow = errors.New("unknown Window ID")
	ErrUnknownDomain = errors.New("unknown Domain")
	ErrDomainFull    = errors.New("Node already has eight live members")
)

// State is used by one serialized owner. Domain names are independent of any
// WM; a Session is discarded in its entirety when its WM disconnects.
type State struct {
	nodes map[string]Node
}

type Node struct {
	Name        string `json:"name"`
	Description string `json:"description,omitempty"`
	CreatedAt   int64  `json:"created_at"`
	LastActive  int64  `json:"last_active,omitempty"`
}

type Session struct {
	ID      string
	windows map[uint64]Window
	epoch   uint64
}

type Window struct {
	ID, Epoch     uint64
	Domain, AppID string
	Mapped        bool
}

func ValidName(s string) bool {
	if len(s) == 0 || len(s) > 32 {
		return false
	}
	for _, b := range []byte(s) {
		if !(b >= 'a' && b <= 'z' || b >= 'A' && b <= 'Z' || b >= '0' && b <= '9' || b == '_' || b == '-') {
			return false
		}
	}
	return true
}

func ValidDescription(s string) bool {
	if len(s) > 160 || !utf8.ValidString(s) {
		return false
	}
	for _, r := range s {
		if unicode.IsControl(r) || r == '\u2028' || r == '\u2029' {
			return false
		}
	}
	return true
}

func (s *State) Create(name string) bool {
	if s.Exists(name) {
		return false
	}
	s.Put(Node{Name: name})
	return true
}

func (s *State) Exists(name string) bool { _, ok := s.nodes[name]; return ok }
func (s *State) Node(name string) Node   { return s.nodes[name] }
func (s *State) Put(n Node) {
	if s.nodes == nil {
		s.nodes = make(map[string]Node)
	}
	s.nodes[n.Name] = n
}
func (s *State) Nodes() []Node {
	out := make([]Node, 0, len(s.nodes))
	for _, name := range s.Names() {
		out = append(out, s.nodes[name])
	}
	return out
}

func (s *State) Names() []string {
	names := make([]string, 0, len(s.nodes))
	for name := range s.nodes {
		names = append(names, name)
	}
	sort.Strings(names)
	return names
}

func (s *Session) Get(id uint64) Window { return s.windows[id] }

func NewSession(id string) *Session {
	return &Session{ID: id, windows: make(map[uint64]Window)}
}

func (s *Session) Add(id uint64) bool {
	if _, exists := s.windows[id]; exists || id == 0 {
		return false
	}
	s.windows[id] = Window{ID: id}
	return true
}

// Map and unmap affect visibility, not identity or membership.
func (s *Session) SetMapped(id uint64, mapped bool) bool {
	w, exists := s.windows[id]
	if exists {
		w.Mapped = mapped
		s.windows[id] = w
	}
	return exists
}

func (s *Session) SetAppID(id uint64, appID string) bool {
	w, ok := s.windows[id]
	if !ok {
		return false
	}
	w.AppID = appID
	s.windows[id] = w
	return true
}

func (s *Session) Remove(id uint64) bool {
	_, exists := s.windows[id]
	delete(s.windows, id)
	return exists
}

func (s *State) Assign(session *Session, sessionID string, id uint64, name string) error {
	if session == nil || session.ID != sessionID {
		return ErrStaleSession
	}
	w, exists := session.windows[id]
	if !exists {
		return ErrUnknownWindow
	}
	if !s.Exists(name) {
		return ErrUnknownDomain
	}
	if w.Domain == name {
		return nil
	}
	count := 0
	for _, other := range session.windows {
		if other.Domain == name {
			count++
		}
	}
	if count >= MaxNodeWindows {
		return ErrDomainFull
	}
	session.epoch++
	w.Domain, w.Epoch = name, session.epoch
	session.windows[id] = w
	return nil
}

// Windows returns ordered copies for deterministic projection, including
// temporarily unmapped members.
func (s *Session) Windows() []Window {
	out := make([]Window, 0, len(s.windows))
	for _, w := range s.windows {
		out = append(out, w)
	}
	sort.Slice(out, func(i, j int) bool { return out[i].ID < out[j].ID })
	return out
}
