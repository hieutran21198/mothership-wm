package main

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"net"
	"slices"
	"sort"
	"strings"
	"sync"
	"time"

	"mothership-wm/internal/domainipc"
)

type controller struct {
	ctx                                       context.Context
	path                                      string
	show                                      func(view)
	showSwitch                                func(*nodeSwitchView)
	nodeSwitch                                nodeSwitch
	mu                                        sync.Mutex
	state                                     domainipc.Snapshot
	online                                    bool
	pending                                   bool
	message                                   string
	next                                      uint64
	connectFailed, preparedRelay, focusTicket uint64
}

type view struct {
	title, message, node, mode                                   string
	domains, emptyDomains                                        []string
	windows                                                      []windowRow
	profiles                                                     []profileRow
	enabled, overview, mapVisible, blankHome, ready, defaultNode bool
	connectFailed, preparedRelay                                 uint64
	// Loadout offer for the current named Node; zero when it has none.
	loadoutMissing, loadoutTotal int
	loadoutRunning               bool
}

// loadoutOffer is set by the native shell; tests leave it nil.
var loadoutOffer func(domainipc.Snapshot, string) (int, int, bool)

// missionSelectionChanged lets the native shell refresh today's Mission Log
// totals when the acknowledged view changes; tests leave it nil.
var missionSelectionChanged func(domainipc.Selection)

// rerender refreshes the view from outside a state update.
func (c *controller) rerender() {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.render()
}

type windowRow struct{ key, label string }

type profileRow struct {
	domainipc.NodeProfile
	empty bool
	apps  []string
}

func profileRows(s domainipc.Snapshot) []profileRow {
	metadata := make(map[string]domainipc.NodeProfile, len(s.NodeProfiles))
	for _, n := range s.NodeProfiles {
		metadata[n.Domain] = n
	}
	occupied := make(map[string]bool)
	apps := make(map[string]map[string]bool)
	if s.WMReady {
		for _, w := range s.Windows {
			if w.Domain == "" {
				continue
			}
			occupied[w.Domain] = true
			if !w.Mapped || w.AppID == "" {
				continue
			}
			id := strings.TrimSuffix(w.AppID, ".desktop")
			if id == "" {
				continue
			}
			if apps[w.Domain] == nil {
				apps[w.Domain] = make(map[string]bool)
			}
			apps[w.Domain][id] = true
		}
	}
	rows := make([]profileRow, 0, len(s.Domains))
	for _, name := range s.Domains {
		row := profileRow{NodeProfile: metadata[name], empty: s.WMReady && !occupied[name]}
		row.Domain = name
		for id := range apps[name] {
			row.apps = append(row.apps, id)
		}
		sort.Strings(row.apps)
		rows = append(rows, row)
	}
	return rows
}

func (c *controller) render() {
	s := c.state
	if c.nodeSwitch.reconcile(s, c.online && s.WMReady, c.pending) && c.showSwitch != nil {
		c.showSwitch(nil)
	}
	title := "View unavailable"
	if c.online && s.WMReady {
		switch {
		case s.Selection.State == "acknowledged" && s.Selection.View == "home":
			title = "Home"
		case s.Selection.State == "acknowledged" && s.Selection.View == "home_node":
			title = "Default Node"
		case s.Selection.State == "acknowledged" && s.Selection.View == "overview":
			title = "Overview"
		case s.Selection.State == "acknowledged" && s.Selection.Domain != "":
			title = "Node: " + s.Selection.Domain
		case s.Selection.State == "not_selected":
			title = "No view selected"
		case s.Selection.State == "unknown":
			title = "View outcome unknown"
		}
	}
	v := view{title: title, message: c.message, ready: c.online && s.WMReady, connectFailed: c.connectFailed, preparedRelay: c.preparedRelay, enabled: c.online && s.WMReady && !c.pending, mapVisible: c.online && s.WMReady && s.MapVisible}
	if c.online && s.Selection.State == "acknowledged" {
		v.node, v.mode = s.Selection.Domain, s.Selection.Mode
		v.overview = s.Selection.View == "overview"
	}
	v.blankHome = c.online && s.WMReady && s.Selection.State == "acknowledged" && s.Selection.View == "home"
	if missionSelectionChanged != nil && c.online && s.WMReady {
		missionSelectionChanged(s.Selection)
	}
	if loadoutOffer != nil && c.online && s.WMReady && v.node != "" {
		v.loadoutMissing, v.loadoutTotal, v.loadoutRunning = loadoutOffer(s, v.node)
	}
	v.defaultNode = c.online && s.WMReady && s.Selection.State == "acknowledged" && s.Selection.View == "home_node"
	if c.online {
		v.domains = append([]string(nil), s.Domains...)
		v.profiles = profileRows(s)
		for _, row := range v.profiles {
			if row.empty {
				v.emptyDomains = append(v.emptyDomains, row.Domain)
			}
		}
		for _, w := range s.Windows {
			target := w.Domain
			if target == "" {
				target = "Home / Inbox"
			}
			label := fmt.Sprintf("Window %d  ·  %s", w.ID, target)
			if !w.Mapped {
				label += " (unmapped)"
			}
			v.windows = append(v.windows, windowRow{fmt.Sprintf("%s:%d", s.Session, w.ID), label})
		}
	}
	c.show(v)
}

func (c *controller) run() {
	for c.ctx.Err() == nil {
		err := c.subscribe()
		if c.ctx.Err() != nil {
			return
		}
		c.mu.Lock()
		c.online = false
		c.message = "Disconnected; reconnecting"
		if err != nil {
			c.message += ": " + err.Error()
		}
		c.render()
		c.mu.Unlock()
		select {
		case <-time.After(time.Second):
		case <-c.ctx.Done():
		}
	}
}

func connection(ctx context.Context, path, method string, id uint64, body any) (*net.UnixConn, domainipc.Response, func(), error) {
	var reply domainipc.Response
	socket, err := domainipc.Dial(ctx, path)
	if err != nil {
		return nil, reply, func() {}, err
	}
	stop := context.AfterFunc(ctx, func() { _ = socket.Close() })
	cleanup := func() { stop(); _ = socket.Close() }
	_ = socket.SetReadDeadline(time.Now().Add(2 * domainipc.Timeout))
	data, err := json.Marshal(body)
	if err == nil {
		err = domainipc.WriteJSON(socket, domainipc.Request{ID: id, Method: method, Body: data})
	}
	if err == nil {
		var packet string
		packet, err = domainipc.Read(socket)
		if err == nil {
			reply, err = domainipc.Decode[domainipc.Response]([]byte(packet))
		}
	}
	if err == nil && reply.ID != id {
		err = errors.New("desktopd response ID did not match")
	}
	if err == nil && reply.Error != nil {
		err = errors.New(reply.Error.Message)
	}
	if err != nil {
		cleanup()
		return nil, reply, func() {}, err
	}
	return socket, reply, cleanup, nil
}

func (c *controller) subscribe() error {
	conn, reply, closeConn, err := connection(c.ctx, c.path, domainipc.SubscribeMethod, 1, domainipc.Empty{})
	if err != nil {
		return err
	}
	defer closeConn()
	s, err := domainipc.Decode[domainipc.Snapshot](reply.Body)
	if err != nil {
		return err
	}
	_ = conn.SetReadDeadline(time.Time{})
	c.mu.Lock()
	c.state, c.online, c.message = s, true, "Connected"
	c.render()
	c.mu.Unlock()
	for {
		packet, err := domainipc.Read(conn)
		if err != nil {
			return err
		}
		e, err := domainipc.Decode[domainipc.Event]([]byte(packet))
		if err != nil {
			return err
		}
		c.mu.Lock()
		err = reduce(&c.state, e)
		if err == nil {
			c.render()
		}
		c.mu.Unlock()
		if err != nil {
			return err
		}
	}
}

func (c *controller) report(message string) {
	c.mu.Lock()
	defer c.mu.Unlock()
	if c.online {
		c.message = message
		c.render()
	}
}

func (c *controller) openMap(enterOverview bool) {
	c.submitAfter(domainipc.MapShowMethod, domainipc.Empty{}, enterOverview)
}

func (c *controller) blankHome(enterOverview bool) {
	c.submitAfter(domainipc.MapHideMethod, domainipc.Empty{}, enterOverview)
}

func (c *controller) submit(method string, body any) {
	c.submitAfter(method, body, false)
}

func (c *controller) submitAfter(method string, body any, enterOverview bool) {
	c.submitCommand(method, body, enterOverview, 0, "", nil)
}

func (c *controller) connect(name string, relay uint64) {
	method := domainipc.FocusMethod
	if relay != 0 {
		method = domainipc.PrepareFocusMethod
	}
	c.submitCommand(method, domainipc.DomainBody{Domain: name}, false, relay, "", nil)
}

func (c *controller) reveal(relay uint64) {
	c.mu.Lock()
	if relay == 0 || c.preparedRelay != relay || c.focusTicket == 0 {
		c.connectFailed = relay
		c.render()
		c.mu.Unlock()
		return
	}
	ticket := c.focusTicket
	c.focusTicket = 0
	c.mu.Unlock()
	c.submitCommand(domainipc.RevealFocusMethod, domainipc.FocusTicket{Ticket: ticket}, false, relay, "", nil)
}

func (c *controller) createAndEnter(name string, finish func(error)) {
	c.submitCommand(domainipc.CreateMethod, domainipc.CreateBody{Domain: name}, false, 0, name, finish)
}

func (c *controller) submitCommand(method string, body any, enterOverview bool, relay uint64, thenFocus string, finish func(error)) {
	c.mu.Lock()
	started := c.startCommand(method, body, enterOverview, relay, thenFocus, finish)
	c.mu.Unlock()
	if !started && finish != nil {
		finish(errors.New("Desktop is busy or unavailable. Try again."))
	}
}

// startCommand runs with c.mu held.
func (c *controller) startCommand(method string, body any, enterOverview bool, relay uint64, thenFocus string, finish func(error)) bool {
	if !c.online || !c.state.WMReady || c.pending {
		if relay != 0 {
			c.connectFailed = relay
			c.render()
		}
		return false
	}
	c.pending = true
	c.next++
	id := c.next
	if enterOverview || thenFocus != "" {
		c.next++
	}
	c.message = "Applying…"
	c.render()

	go func() {
		var err error
		var ticket uint64
		if enterOverview {
			first := domainipc.OverviewMethod
			if method == domainipc.MapHideMethod {
				// Hide before selecting Overview so Home cannot flash the Map.
				first, method = method, first
			}
			_, _, done, status := connection(c.ctx, c.path, first, id, domainipc.Empty{})
			done()
			err = status
			id++
		}
		if err == nil {
			_, reply, done, status := connection(c.ctx, c.path, method, id, body)
			done()
			err = status
			if err == nil && method == domainipc.PrepareFocusMethod {
				x, decodeErr := domainipc.Decode[domainipc.FocusTicket](reply.Body)
				if decodeErr != nil || x.Ticket == 0 {
					err = errors.New("desktopd sent no prepared workspace")
				} else {
					ticket = x.Ticket
				}
			}
		}
		if err == nil && thenFocus != "" {
			// Enter only after desktopd persisted the Node. A failed entry
			// does not undo creation or change the acknowledged view.
			_, _, done, status := connection(c.ctx, c.path, domainipc.FocusMethod, id+1, domainipc.DomainBody{Domain: thenFocus})
			done()
			if status != nil {
				err = fmt.Errorf("Created %s, but could not enter: %w", thenFocus, status)
			}
		}
		c.mu.Lock()
		defer c.mu.Unlock()
		c.pending = false
		if ticket != 0 {
			c.preparedRelay, c.focusTicket = relay, ticket
		}
		if err != nil && relay != 0 {
			c.connectFailed = relay
		}
		if c.online {
			c.message = "Applied"
			if err != nil {
				c.message = err.Error()
			}
		}
		// Command replies cannot change or override selection. Only the
		// snapshot/event stream is authoritative, including newer events.
		c.render()
		if finish != nil {
			finish(err)
		}
	}()
	return true
}

func reduce(s *domainipc.Snapshot, e domainipc.Event) error {
	if e.Revision != s.Revision+1 {
		return fmt.Errorf("subscription revision gap")
	}
	switch e.Event {
	case "wm.changed":
		v, err := domainipc.Decode[domainipc.WMChangedBody](e.Body)
		if err != nil {
			return err
		}
		s.WMReady, s.Session, s.Selection, s.Windows = v.WMReady, v.Session, v.Selection, nil
	case "domain.created":
		v, err := domainipc.Decode[domainipc.NodeProfile](e.Body)
		if err != nil {
			return err
		}
		s.Domains = append(s.Domains, v.Domain)
		s.NodeProfiles = append(s.NodeProfiles, v)
		sort.Strings(s.Domains)
	case domainipc.DomainDeletedEvent:
		v, err := domainipc.Decode[domainipc.DomainBody](e.Body)
		if err != nil {
			return err
		}
		s.Domains = slices.DeleteFunc(s.Domains, func(name string) bool { return name == v.Domain })
		s.NodeProfiles = slices.DeleteFunc(s.NodeProfiles, func(n domainipc.NodeProfile) bool { return n.Domain == v.Domain })
	case "window.updated":
		v, err := domainipc.Decode[domainipc.Window](e.Body)
		if err != nil {
			return err
		}
		i := sort.Search(len(s.Windows), func(i int) bool { return s.Windows[i].ID >= v.ID })
		if i < len(s.Windows) && s.Windows[i].ID == v.ID {
			s.Windows[i] = v
		} else {
			s.Windows = append(s.Windows, domainipc.Window{})
			copy(s.Windows[i+1:], s.Windows[i:])
			s.Windows[i] = v
		}
	case "window.closed":
		v, err := domainipc.Decode[domainipc.WindowClosedBody](e.Body)
		if err != nil {
			return err
		}
		for i, w := range s.Windows {
			if w.ID == v.WindowID {
				s.Windows = append(s.Windows[:i], s.Windows[i+1:]...)
				break
			}
		}
	case domainipc.MapChangedEvent:
		v, err := domainipc.Decode[domainipc.MapVisibility](e.Body)
		if err != nil {
			return err
		}
		s.MapVisible = v.Visible
	case domainipc.NodeLoadoutEvent:
		v, err := domainipc.Decode[domainipc.LoadoutBody](e.Body)
		if err != nil {
			return err
		}
		for i := range s.NodeProfiles {
			if s.NodeProfiles[i].Domain == v.Domain {
				s.NodeProfiles[i].Loadout = v.Entries
				break
			}
		}
	case "selection.changed":
		v, err := domainipc.Decode[domainipc.Selection](e.Body)
		if err != nil {
			return err
		}
		if v.Domain != "" && v.LastActive > 0 {
			for i := range s.NodeProfiles {
				if s.NodeProfiles[i].Domain == v.Domain {
					s.NodeProfiles[i].LastActive = v.LastActive
					break
				}
			}
		}
		v.LastActive = 0
		s.Selection = v
	default:
		return fmt.Errorf("unknown desktopd event %q", e.Event)
	}
	s.Revision = e.Revision
	return nil
}
