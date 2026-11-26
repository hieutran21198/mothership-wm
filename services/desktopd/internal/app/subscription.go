package app

import (
	"mothership-wm/internal/domainipc"
	"mothership-wm/services/desktopd/internal/domain"
)

const (
	SelectionNotSelected  = "not_selected"
	SelectionAcknowledged = "acknowledged"
	SelectionUnknown      = "unknown"

	WMChanged            = "wm.changed"
	DomainCreated        = "domain.created"
	DomainDeleted        = domainipc.DomainDeletedEvent
	WindowUpdated        = "window.updated"
	WindowClosed         = "window.closed"
	SelectionChanged     = "selection.changed"
	MapVisibilityChanged = "map.visibility.changed"

	queueSize      = 16
	maxSubscribers = 16
)

type Selection struct {
	Home, Overview, DefaultNode bool
	Kind                        string
	Domain                      string
	Mode                        string
}

type Snapshot struct {
	MapVisible bool
	Revision   uint64
	WMReady    bool
	Session    string
	Domains    []string
	Nodes      []domain.Node
	Windows    []domain.Window
	Selection  Selection
}

type Update struct {
	MapVisible bool
	Revision   uint64
	Kind       string
	WMReady    bool
	Session    string
	Domain     string
	Node       domain.Node
	Window     domain.Window
	Selection  Selection
}

// Snapshot returns a copy of the current view, including when WM is absent.
func (s *Service) Snapshot() Snapshot {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.snapshot()
}

func (s *Service) snapshot() Snapshot {
	v := Snapshot{
		Revision: s.revision, WMReady: s.ready, Domains: s.names.Names(), Nodes: s.names.Nodes(), MapVisible: s.mapVisible,
		Windows: make([]domain.Window, 0), Selection: s.currentSelection(),
	}
	if s.ready {
		v.Session, v.Windows = s.active.ID, s.active.Windows()
	}
	return v
}

// SetMapVisible changes only the panel presentation preference.
func (s *Service) SetMapVisible(visible bool) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.mapVisible != visible {
		s.mapVisible = visible
		s.publish(Update{Kind: MapVisibilityChanged, MapVisible: visible})
	}
}

func (s *Service) currentSelection() Selection {
	if s.selection.Kind == "" {
		return Selection{Kind: SelectionNotSelected}
	}
	return s.selection
}

// Subscribe captures a complete view and registers its following deltas under
// one lock. A closed channel means the client should take a fresh snapshot.
func (s *Service) Subscribe() (Snapshot, <-chan Update, func(), error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if len(s.subscribers) >= maxSubscribers {
		return Snapshot{}, nil, nil, ErrSubscriptionLimit
	}
	ch := make(chan Update, queueSize)
	if s.subscribers == nil {
		s.subscribers = make(map[chan Update]struct{})
	}
	s.subscribers[ch] = struct{}{}
	return s.snapshot(), ch, func() {
		s.mu.Lock()
		defer s.mu.Unlock()
		s.drop(ch)
	}, nil
}

// publish is called under the same lock as its mutation. It never waits for
// a socket or a consumer; a subscriber that misses a delta is disconnected.
func (s *Service) publish(update Update) {
	s.revision++
	update.Revision = s.revision
	for ch := range s.subscribers {
		select {
		case ch <- update:
		default:
			s.drop(ch)
		}
	}
}

func (s *Service) drop(ch chan Update) {
	if _, ok := s.subscribers[ch]; ok {
		delete(s.subscribers, ch)
		close(ch)
	}
}

func (s *Service) AcknowledgeView(conn *domain.Session, selection Selection) {
	s.setSelection(conn, selection, false)
}

func (s *Service) AcknowledgeEntry(conn *domain.Session, selection Selection) {
	s.setSelection(conn, selection, true)
}

func (s *Service) MarkFocusUnknown(conn *domain.Session) {
	s.setSelection(conn, Selection{Kind: SelectionUnknown}, false)
}

func (s *Service) setSelection(conn *domain.Session, selection Selection, entry bool) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.ready && s.active == conn && s.selection != selection {
		var n domain.Node
		if entry && selection.Kind == SelectionAcknowledged && selection.Domain != "" && s.selection.Domain != selection.Domain {
			n = s.names.Node(selection.Domain)
			n.LastActive = max(s.unixNow(), n.CreatedAt, n.LastActive)
			s.names.Put(n)
			if s.saveNode != nil {
				if err := s.saveNode(n); err != nil && s.reportPersistence != nil {
					s.reportPersistence(err)
				}
			}
		}
		s.selection = selection
		if selection.Kind == SelectionAcknowledged && (selection.Domain != "" || selection.DefaultNode) && selection.Mode != "" {
			s.layouts[selection.Domain] = selection.Mode
			if domainipc.IsFlow(selection.Mode) {
				s.flowLayouts[selection.Domain] = selection.Mode
			}
		}
		s.publish(Update{Kind: SelectionChanged, Selection: selection, Node: n})
	}
}
