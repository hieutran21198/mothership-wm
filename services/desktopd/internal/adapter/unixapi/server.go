package unixapi

import (
	"context"
	"encoding/hex"
	"errors"
	"fmt"
	"net"
	"os"
	"strconv"
	"strings"
	"sync"
	"time"

	"mothership-wm/internal/domainipc"
	"mothership-wm/services/desktopd/internal/app"
	"mothership-wm/services/desktopd/internal/domain"
)

var (
	errOutcomeUnknown        = errors.New("WM disconnected before acknowledging; outcome unknown")
	errAssignmentViewUnknown = errors.New("assignment accepted; WM view outcome unknown, refresh the snapshot")
	errStalePrepared         = errors.New("Map selection changed; connect again")
	errHomeUnavailable       = errors.New("WM does not support Home")
)

type wmPeer struct {
	conn          *net.UnixConn
	session       *domain.Session
	ready         bool
	home          bool
	views         bool
	handoff       bool
	defaultNode   bool
	defaultLayout bool
	returnInbox   bool
	shortcuts     bool
	windowFocus   bool
	done          chan struct{}
	applied       chan struct{}
	pending       uint64
	selection     *app.Selection
	entry         bool
}

type preparedFocus struct {
	peer   *wmPeer
	name   string
	ticket uint64
}

type desktop struct {
	mu         sync.Mutex
	controlMu  sync.Mutex
	service    *app.Service
	removeNode func(domain.Node) error
	wm         *wmPeer
	next       uint64
	prepared   *preparedFocus
	configPath string
	shortcuts  domainipc.Shortcuts
}

func Serve(ctx context.Context, l *net.UnixListener, useCases *app.Service) error {
	return ServeWithNodeStore(ctx, l, useCases, nil)
}

func ServeWithNodeStore(ctx context.Context, l *net.UnixListener, useCases *app.Service, remove func(domain.Node) error) error {
	return ServeWithConfig(ctx, l, useCases, remove, "")
}

func ServeWithConfig(ctx context.Context, l *net.UnixListener, useCases *app.Service, remove func(domain.Node) error, configPath string) error {
	shortcuts, err := readShortcuts(configPath)
	if err != nil {
		fmt.Fprintln(os.Stderr, "desktopd: shortcuts config ignored:", err)
	}
	d := desktop{service: useCases, removeNode: remove, configPath: configPath, shortcuts: shortcuts}
	cancel := context.AfterFunc(ctx, func() {
		l.Close()
		d.mu.Lock()
		defer d.mu.Unlock()
		if d.wm != nil {
			d.wm.conn.Close()
		}
	})
	defer cancel()
	for {
		c, err := l.AcceptUnix()
		if err != nil {
			if ctx.Err() != nil {
				return nil
			}
			return err
		}
		go d.handle(ctx, c)
	}
}

func (d *desktop) handle(ctx context.Context, c *net.UnixConn) {
	defer c.Close()
	_ = c.SetReadDeadline(time.Now().Add(domainipc.Timeout))
	packet, err := domainipc.Read(c)
	if err != nil {
		return
	}
	args := strings.Fields(packet)
	if len(args) == 2 && args[0] == "wm" && validSession(args[1]) {
		d.track(ctx, c, args[1])
		return
	}
	d.request(ctx, c, packet)
}

func (d *desktop) track(ctx context.Context, c *net.UnixConn, session string) {
	d.mu.Lock()
	if d.wm != nil || ctx.Err() != nil {
		d.mu.Unlock()
		_ = domainipc.Write(c, "err WM already connected or daemon stopping")
		return
	}
	appSession := d.service.Connect(session)
	if appSession == nil {
		d.mu.Unlock()
		_ = domainipc.Write(c, "err WM session already exists")
		return
	}
	w := &wmPeer{conn: c, session: appSession,
		done: make(chan struct{})}
	d.wm = w
	d.mu.Unlock()
	defer func() {
		d.mu.Lock()
		if w.pending != 0 && w.selection != nil {
			d.service.MarkFocusUnknown(w.session)
		}
		w.pending = 0
		d.service.Disconnect(w.session)
		d.wm = nil
		close(w.done)
		d.mu.Unlock()
	}()
	if err := domainipc.Write(c, "ok"); err != nil {
		return
	}
	_ = c.SetReadDeadline(time.Time{})
	for {
		packet, err := domainipc.Read(c)
		if err != nil || !d.event(w, strings.Fields(packet)) {
			return
		}
	}
}

func (d *desktop) event(w *wmPeer, a []string) bool {
	d.mu.Lock()
	defer d.mu.Unlock()
	if !w.ready {
		if len(a) != 1 || (a[0] != "ready" && a[0] != "ready_home" && a[0] != "ready_views" && a[0] != "ready_handoff" && a[0] != "ready_default_node" && a[0] != "ready_return_inbox" && a[0] != "ready_shortcuts" && a[0] != "ready_node_parity" && a[0] != "ready_window_focus") {
			return false
		}
		// WM registers before serving any Wayland clients: its initial snapshot is empty.
		w.windowFocus = a[0] == "ready_window_focus"
		w.defaultLayout = w.windowFocus || a[0] == "ready_node_parity"
		w.shortcuts = w.defaultLayout || a[0] == "ready_shortcuts"
		w.returnInbox = w.shortcuts || a[0] == "ready_return_inbox"
		w.defaultNode = w.returnInbox || a[0] == "ready_default_node"
		w.handoff = w.defaultNode || a[0] == "ready_handoff"
		w.views = w.handoff || a[0] == "ready_views"
		w.home = w.views || a[0] == "ready_home"
		w.ready = d.service.Ready(w.session, w.home)
		if w.ready && w.shortcuts && d.shortcuts != domainipc.DefaultShortcuts() {
			go func() {
				d.controlMu.Lock()
				defer d.controlMu.Unlock()
				d.mu.Lock()
				s := d.shortcuts
				d.mu.Unlock()
				if err := d.applyShortcuts(w, s); err != nil {
					fmt.Fprintln(os.Stderr, "desktopd: restore shortcuts:", err)
				}
			}()
		}
		return w.ready
	}
	if len(a) != 2 && !(len(a) == 3 && (a[0] == "app" || a[0] == "new")) {
		return false
	}
	id, err := strconv.ParseUint(a[1], 10, 64)
	if err != nil || id == 0 {
		return false
	}
	switch a[0] {
	case "applied":
		if id != w.pending || w.applied == nil {
			return false
		}
		if w.selection != nil {
			if w.entry {
				d.service.AcknowledgeEntry(w.session, *w.selection)
			} else {
				d.service.AcknowledgeView(w.session, *w.selection)
			}
		}
		w.pending = 0
		close(w.applied)
		return true
	case "app":
		appID := ""
		if len(a) == 3 {
			appID = a[2]
			if !validAppID(appID) {
				return false
			}
		}
		return d.service.WindowAppID(w.session, id, appID)
	case "new":
		pid := 0
		if len(a) == 3 {
			value, err := strconv.ParseInt(a[2], 10, 32)
			if err != nil || value <= 0 {
				return false
			}
			pid = int(value)
		}
		return d.service.WindowAddedWithPID(w.session, id, pid)
	case "map", "unmap":
		return d.service.WindowMapping(w.session, id, a[0] == "map")
	case "idle":
		// id is the seconds since the last input; no input data is sent.
		return d.service.SetIdle(w.session, true, time.Duration(id)*time.Second)
	case "active":
		return id == 1 && d.service.SetIdle(w.session, false, 0)
	case "close":
		return d.service.WindowClosed(w.session, id)
	}
	return false
}

func validAppID(s string) bool {
	if len(s) == 0 || len(s) > 128 {
		return false
	}
	for _, b := range []byte(s) {
		if !(b >= 'a' && b <= 'z' || b >= 'A' && b <= 'Z' || b >= '0' && b <= '9' || b == '_' || b == '-' || b == '.') {
			return false
		}
	}
	return true
}

func validWord(s string) bool {
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

func validSession(id string) bool {
	if len(id) != 32 {
		return false
	}
	_, err := hex.DecodeString(id)
	return err == nil
}

func (d *desktop) deleteNode(name string, force bool) error {
	d.controlMu.Lock()
	defer d.controlMu.Unlock()
	d.mu.Lock()
	plan, err := d.service.BeginDelete(name, force)
	if err != nil {
		d.mu.Unlock()
		return err
	}
	defer d.service.CancelDelete(plan)
	if len(plan.Windows) != 0 {
		w := d.wm
		if w == nil || !w.ready || !w.returnInbox {
			d.mu.Unlock()
			return errors.New("WM does not support moving windows to Default Node")
		}
		ack := d.beginApply(w, nil)
		packet := fmt.Sprintf("inbox %d", ack.serial)
		for _, win := range plan.Windows {
			packet += fmt.Sprintf(" %d", win.ID)
		}
		d.mu.Unlock()
		if !d.awaitApply(w, ack, packet) {
			return errOutcomeUnknown
		}
		d.mu.Lock()
	}
	if d.prepared != nil && d.prepared.name == name {
		d.prepared = nil
	}
	err = d.service.FinishDelete(plan, d.removeNode)
	d.mu.Unlock()
	return err
}

func (d *desktop) assign(session string, id uint64, name string) error {
	d.controlMu.Lock()
	defer d.controlMu.Unlock()
	d.mu.Lock()
	change, err := d.service.Assign(session, id, name)
	w := d.wm
	if err != nil || (!change.FirstAssignment && !change.RefreshNamed) || w == nil || !w.home {
		d.mu.Unlock()
		return err
	}
	ack := d.beginApply(w, nil)
	packet := fmt.Sprintf("assigned %d %d", ack.serial, id)
	if change.RefreshNamed {
		packet += " project"
		for _, member := range change.Members {
			packet += fmt.Sprintf(" %d %d", member.ID, member.Epoch)
		}
	}
	d.mu.Unlock()
	if !d.awaitApply(w, ack, packet) {
		return errAssignmentViewUnknown
	}
	return nil
}

// unassign returns one window to Inbox. The WM always learns about the move,
// because it keeps its own Inbox projection, and refreshes a shown Node.
func (d *desktop) unassign(session string, id uint64) error {
	d.controlMu.Lock()
	defer d.controlMu.Unlock()
	d.mu.Lock()
	w := d.wm
	if w == nil || !w.ready || !w.windowFocus {
		d.mu.Unlock()
		return errors.New("WM does not support returning a window to Inbox")
	}
	change, moved, err := d.service.Unassign(session, id)
	if err != nil || !moved {
		d.mu.Unlock()
		return err
	}
	ack := d.beginApply(w, nil)
	packet := fmt.Sprintf("inbox %d %d", ack.serial, id)
	if change.RefreshNamed {
		packet += " project"
		for _, member := range change.Members {
			packet += fmt.Sprintf(" %d %d", member.ID, member.Epoch)
		}
	}
	d.mu.Unlock()
	if !d.awaitApply(w, ack, packet) {
		return errAssignmentViewUnknown
	}
	return nil
}

func (d *desktop) prepareFocus(name string) (uint64, error) {
	d.controlMu.Lock()
	defer d.controlMu.Unlock()
	d.mu.Lock()
	d.prepared = nil
	session, _, err := d.service.ResolveDomain(name)
	w := d.wm
	if err != nil || w == nil || w.session.ID != session || !w.handoff || !d.service.MapOverview() {
		d.mu.Unlock()
		if err != nil {
			return 0, err
		}
		return 0, errStalePrepared
	}
	ack := d.beginApply(w, nil)
	d.mu.Unlock()
	if !d.awaitApply(w, ack, fmt.Sprintf("prepare %d", ack.serial)) {
		return 0, errOutcomeUnknown
	}
	d.mu.Lock()
	defer d.mu.Unlock()
	if d.wm != w || !d.service.MapOverview() {
		return 0, errStalePrepared
	}
	d.prepared = &preparedFocus{peer: w, name: name, ticket: ack.serial}
	return ack.serial, nil
}

func (d *desktop) focus(name string) error { return d.focusView(name, 0) }

func (d *desktop) focusView(name string, ticket uint64) error {
	d.controlMu.Lock()
	defer d.controlMu.Unlock()
	return d.focusViewLocked(name, ticket, 0)
}

// focusWindow selects the Node that holds one live window and asks the WM to
// make that window the keyboard target. Membership is resolved under the
// same control lock as the selection, so a concurrent move cannot interleave.
func (d *desktop) focusWindow(session string, id uint64) error {
	d.controlMu.Lock()
	defer d.controlMu.Unlock()
	d.mu.Lock()
	name, err := d.service.WindowDomain(session, id)
	w := d.wm
	d.mu.Unlock()
	if err != nil {
		return err
	}
	if w == nil || !w.ready || !w.windowFocus {
		return errors.New("WM does not support focusing a window")
	}
	if name == "" {
		return d.defaultNodeLocked(id)
	}
	return d.focusViewLocked(name, 0, id)
}

// focusViewLocked runs under controlMu. A nonzero target asks the WM to
// prefer that member as the keyboard target after it applies the view.
func (d *desktop) focusViewLocked(name string, ticket, target uint64) error {
	d.mu.Lock()
	if ticket != 0 {
		if d.prepared == nil || d.prepared.ticket != ticket || d.prepared.peer != d.wm || !d.service.MapOverview() {
			d.mu.Unlock()
			return errStalePrepared
		}
		name = d.prepared.name
	}
	d.prepared = nil
	var session string
	var members []domain.Window
	var err error
	if name == "" {
		session, err = d.service.ResolveHome()
	} else {
		session, members, err = d.service.ResolveDomain(name)
	}
	if err != nil {
		d.mu.Unlock()
		return err
	}
	w := d.wm
	if w == nil || w.session.ID != session {
		d.mu.Unlock()
		return app.ErrUnavailable
	}
	if name == "" && !w.home {
		d.mu.Unlock()
		return errHomeUnavailable
	}
	selection := app.Selection{Kind: app.SelectionAcknowledged, Domain: name, Home: name == ""}
	if w.views && name != "" {
		selection.Mode = d.service.NodeMode(name)
		if selection.Mode == "" {
			selection.Mode = domainipc.Flow
		}
	}
	ack := d.beginApply(w, &selection)
	w.entry = name != ""
	packet := fmt.Sprintf("focus %d", ack.serial)
	if name == "" {
		packet = fmt.Sprintf("home %d", ack.serial)
	} else if ticket != 0 {
		packet = fmt.Sprintf("reveal %d %s", ack.serial, selection.Mode)
	} else if target != 0 {
		packet = fmt.Sprintf("node_window %d %s %d", ack.serial, selection.Mode, target)
	} else if w.views {
		packet = fmt.Sprintf("node %d %s", ack.serial, selection.Mode)
	}
	for _, v := range members {
		packet += fmt.Sprintf(" %d %d", v.ID, v.Epoch)
	}
	d.mu.Unlock()
	if !d.awaitApply(w, ack, packet) {
		return errOutcomeUnknown
	}
	return nil
}

// The reserved Home projection is independent of named Domain ownership.
func (d *desktop) defaultNode() error {
	d.controlMu.Lock()
	defer d.controlMu.Unlock()
	return d.defaultNodeLocked(0)
}

// defaultNodeLocked runs under controlMu; see focusViewLocked for target.
func (d *desktop) defaultNodeLocked(target uint64) error {
	d.mu.Lock()
	w := d.wm
	state := d.service.Snapshot()
	if w == nil || !w.ready || !w.defaultNode {
		d.mu.Unlock()
		return errors.New("Default Node is unavailable")
	}
	if state.Selection.DefaultNode && !state.MapVisible && target == 0 {
		d.mu.Unlock()
		return nil
	}
	selection := app.Selection{Kind: app.SelectionAcknowledged, Home: true, DefaultNode: true}
	if w.defaultLayout {
		selection.Mode = d.service.NodeMode("")
		if selection.Mode == "" {
			selection.Mode = domainipc.Flow
		}
	}
	ack := d.beginApply(w, &selection)
	d.prepared = nil
	packet := fmt.Sprintf("home_node %d", ack.serial)
	if target != 0 {
		packet = fmt.Sprintf("home_node_window %d %s %d", ack.serial, selection.Mode, target)
	} else if selection.Mode != "" {
		packet += " " + selection.Mode
	}
	d.mu.Unlock()
	if !d.awaitApply(w, ack, packet) {
		return errOutcomeUnknown
	}
	return nil
}

type applyAck struct {
	serial uint64
	done   chan struct{}
}

// beginApply runs under d.mu; controlMu serializes assignments and selections.
func (d *desktop) beginApply(w *wmPeer, selection *app.Selection) applyAck {
	d.next++
	ack := applyAck{serial: d.next, done: make(chan struct{})}
	w.pending, w.applied, w.selection, w.entry = ack.serial, ack.done, selection, false
	return ack
}

func (d *desktop) awaitApply(w *wmPeer, ack applyAck, packet string) bool {
	if err := domainipc.Write(w.conn, packet); err == nil {
		timer := time.NewTimer(domainipc.Timeout)
		defer timer.Stop()
		select {
		case <-ack.done:
			return true
		case <-timer.C:
		case <-w.done:
		}
	}
	// Serialize acknowledgement versus timeout/disconnect. Once an outcome
	// is declared unknown, a late packet cannot report success.
	d.mu.Lock()
	defer d.mu.Unlock()
	select {
	case <-ack.done:
		return true
	default:
	}
	if w.pending == ack.serial {
		w.pending = 0
		if w.selection != nil {
			d.service.MarkFocusUnknown(w.session)
		}
	}
	_ = w.conn.Close()
	return false
}

// View actions join the existing serialized WM acknowledgement path.
func (d *desktop) viewAction(method, name, mode string, restoreFlow bool) error {
	d.controlMu.Lock()
	defer d.controlMu.Unlock()
	d.mu.Lock()
	w := d.wm
	if w == nil || !w.ready || !w.views {
		d.mu.Unlock()
		return errors.New("WM does not support Node views")
	}
	var selection *app.Selection
	var command string
	if method == domainipc.OverviewMethod {
		d.prepared = nil
		selection = &app.Selection{Kind: app.SelectionAcknowledged, Overview: true}
		command = "overview"
	} else {
		active, err := d.service.ActiveNode(name)
		if err != nil {
			d.mu.Unlock()
			return err
		}
		if active.DefaultNode && !w.defaultLayout {
			d.mu.Unlock()
			return errors.New("WM does not support Default Node layouts")
		}
		if active.DefaultNode && method == domainipc.LayoutMethod &&
			mode != domainipc.Stage && !domainipc.IsFlow(mode) {
			d.mu.Unlock()
			return errors.New("Default Node supports Flow and Stage")
		}
		if method == domainipc.NextMethod {
			if active.Mode != domainipc.Stack && active.Mode != domainipc.Stage && !domainipc.IsFlow(active.Mode) {
				d.mu.Unlock()
				return errors.New("Flow, Stage or Stack mode is not active")
			}
			command = "next"
		} else {
			if restoreFlow && active.Mode == domainipc.Stage {
				if previous := d.service.LastFlowMode(name); previous != "" {
					mode = previous
				}
			}
			active.Mode = mode
			selection, command = &active, "layout"
		}
	}
	ack := d.beginApply(w, selection)
	packet := fmt.Sprintf("%s %d", command, ack.serial)
	if command == "layout" {
		packet += " " + mode
	}
	d.mu.Unlock()
	if !d.awaitApply(w, ack, packet) {
		return errOutcomeUnknown
	}
	return nil
}
