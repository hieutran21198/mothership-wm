package unixapi

import (
	"context"
	"encoding/json"
	"errors"
	"net"
	"time"

	"mothership-wm/internal/domainipc"
	"mothership-wm/services/desktopd/internal/app"
	"mothership-wm/services/desktopd/internal/domain"
)

func (d *desktop) request(ctx context.Context, c *net.UnixConn, packet string) {
	req, err := domainipc.Decode[domainipc.Request]([]byte(packet))
	if err != nil || req.ID == 0 {
		sendFailure(c, req.ID, "invalid_request", "expected a request with a positive ID, method and body")
		return
	}
	badBody := func() { sendFailure(c, req.ID, "invalid_body", "invalid request body") }
	switch req.Method {
	case domainipc.SnapshotMethod, domainipc.SubscribeMethod, domainipc.HomeMethod, domainipc.DefaultNodeMethod, domainipc.OverviewMethod, domainipc.MapShowMethod, domainipc.MapHideMethod:
		if _, err := domainipc.Decode[domainipc.Empty](req.Body); err != nil {
			badBody()
			return
		}
		switch req.Method {
		case domainipc.SubscribeMethod:
			d.subscribe(ctx, c, req.ID)
		case domainipc.MapShowMethod, domainipc.MapHideMethod:
			d.mu.Lock()
			if req.Method == domainipc.MapHideMethod {
				d.prepared = nil
			}
			d.service.SetMapVisible(req.Method == domainipc.MapShowMethod)
			d.mu.Unlock()
			sendSuccess(c, req.ID)
		case domainipc.SnapshotMethod:
			sendSnapshot(c, req.ID, d.service.Snapshot())
		case domainipc.HomeMethod, domainipc.DefaultNodeMethod, domainipc.OverviewMethod:
			apply := func() error { return d.focus("") }
			if req.Method == domainipc.DefaultNodeMethod {
				apply = d.defaultNode
			}
			if req.Method == domainipc.OverviewMethod {
				apply = func() error { return d.viewAction(req.Method, "", "", false) }
			}
			if err := apply(); err != nil {
				sendCommandError(c, req.ID, err)
				return
			}
			sendSuccess(c, req.ID)
		}
	case domainipc.ShortcutsGetMethod:
		if _, err := domainipc.Decode[domainipc.Empty](req.Body); err != nil {
			badBody()
			return
		}
		d.mu.Lock()
		next := d.shortcuts
		d.mu.Unlock()
		b, _ := json.Marshal(next)
		_ = domainipc.WriteJSON(c, domainipc.Response{ID: req.ID, Body: b})
	case domainipc.ShortcutsSetMethod:
		next, err := domainipc.Decode[domainipc.Shortcuts](req.Body)
		if err != nil {
			badBody()
			return
		}
		if _, err = next.Keycodes(); err != nil {
			sendFailure(c, req.ID, "invalid_shortcuts", err.Error())
			return
		}
		if err = d.setShortcuts(next); err != nil {
			sendCommandError(c, req.ID, err)
			return
		}
		sendSuccess(c, req.ID)
	case domainipc.CreateMethod:
		b, err := domainipc.Decode[domainipc.CreateBody](req.Body)
		if err != nil || !domain.ValidName(b.Domain) || !domain.ValidDescription(b.Description) {
			badBody()
			return
		}
		if err := d.service.CreateNode(b.Domain, b.Description); err != nil {
			sendCommandError(c, req.ID, err)
			return
		}
		sendSuccess(c, req.ID)
	case domainipc.LoadoutSetMethod:
		b, err := domainipc.Decode[domainipc.LoadoutBody](req.Body)
		if err != nil || !domain.ValidName(b.Domain) || !domain.ValidLoadout(b.Entries) {
			badBody()
			return
		}
		if err := d.service.SetLoadout(b.Domain, b.Entries); err != nil {
			sendCommandError(c, req.ID, err)
			return
		}
		sendSuccess(c, req.ID)
	case domainipc.DeleteMethod:
		b, err := domainipc.Decode[domainipc.DeleteBody](req.Body)
		if err != nil || !domain.ValidName(b.Domain) {
			badBody()
			return
		}
		if err := d.deleteNode(b.Domain, b.Force); err != nil {
			sendCommandError(c, req.ID, err)
			return
		}
		sendSuccess(c, req.ID)
	case domainipc.PrepareFocusMethod:
		b, err := domainipc.Decode[domainipc.DomainBody](req.Body)
		if err != nil || !validWord(b.Domain) {
			badBody()
			return
		}
		ticket, err := d.prepareFocus(b.Domain)
		if err != nil {
			sendCommandError(c, req.ID, err)
			return
		}
		body, _ := json.Marshal(domainipc.FocusTicket{Ticket: ticket})
		_ = domainipc.WriteJSON(c, domainipc.Response{ID: req.ID, Body: body})
	case domainipc.RevealFocusMethod:
		b, err := domainipc.Decode[domainipc.FocusTicket](req.Body)
		if err != nil || b.Ticket == 0 {
			badBody()
			return
		}
		if err := d.focusView("", b.Ticket); err != nil {
			sendCommandError(c, req.ID, err)
			return
		}
		sendSuccess(c, req.ID)
	case domainipc.FocusMethod:
		b, err := domainipc.Decode[domainipc.DomainBody](req.Body)
		if err != nil || !validWord(b.Domain) {
			badBody()
			return
		}
		if err := d.focus(b.Domain); err != nil {
			sendCommandError(c, req.ID, err)
			return
		}
		sendSuccess(c, req.ID)
	case domainipc.LayoutMethod, domainipc.NextMethod:
		var b domainipc.LayoutBody
		if req.Method == domainipc.LayoutMethod {
			b, err = domainipc.Decode[domainipc.LayoutBody](req.Body)
			if err == nil && (!domainipc.ValidLayout(b.Mode) || (b.RestoreFlow && b.Mode != domainipc.Flow)) {
				badBody()
				return
			}
		} else {
			var ref domainipc.DomainBody
			ref, err = domainipc.Decode[domainipc.DomainBody](req.Body)
			b.Domain = ref.Domain
		}
		// An empty Domain addresses the active Default Node only. The
		// service rejects it on blank Home, Overview or a named Node.
		if err != nil || (b.Domain != "" && !validWord(b.Domain)) {
			badBody()
			return
		}
		if err := d.viewAction(req.Method, b.Domain, b.Mode, b.RestoreFlow); err != nil {
			sendCommandError(c, req.ID, err)
			return
		}
		sendSuccess(c, req.ID)
	case domainipc.FocusWindowMethod, domainipc.UnassignMethod:
		b, err := domainipc.Decode[domainipc.WindowRef](req.Body)
		if err != nil || !validSession(b.Session) || b.WindowID == 0 {
			badBody()
			return
		}
		apply := d.focusWindow
		if req.Method == domainipc.UnassignMethod {
			apply = d.unassign
		}
		if err := apply(b.Session, b.WindowID); err != nil {
			sendCommandError(c, req.ID, err)
			return
		}
		sendSuccess(c, req.ID)
	case domainipc.AssignMethod:
		b, err := domainipc.Decode[domainipc.AssignBody](req.Body)
		if err != nil || !validSession(b.Session) || b.WindowID == 0 || !validWord(b.Domain) {
			badBody()
			return
		}
		if err := d.assign(b.Session, b.WindowID, b.Domain); err != nil {
			sendCommandError(c, req.ID, err)
			return
		}
		sendSuccess(c, req.ID)
	default:
		sendFailure(c, req.ID, "unknown_method", "unknown desktopd method")
	}
}

func sendCommandError(c *net.UnixConn, id uint64, err error) {
	code := "rejected"
	switch {
	case errors.Is(err, app.ErrUnavailable):
		code = "wm_unavailable"
	case errors.Is(err, domain.ErrStaleSession):
		code = "stale_session"
	case errors.Is(err, domain.ErrUnknownWindow):
		code = "unknown_window"
	case errors.Is(err, domain.ErrUnknownDomain):
		code = "unknown_domain"
	case errors.Is(err, domain.ErrDomainFull):
		code = "domain_full"
	case errors.Is(err, errStalePrepared):
		code = "stale_connect"
	case errors.Is(err, errOutcomeUnknown):
		code = "outcome_unknown"
	case errors.Is(err, errAssignmentViewUnknown):
		code = "assignment_view_unknown"
	case errors.Is(err, errHomeUnavailable):
		code = "home_unavailable"
	}
	sendFailure(c, id, code, err.Error())
}

func sendFailure(c *net.UnixConn, id uint64, code, message string) {
	_ = domainipc.WriteJSON(c, domainipc.Response{ID: id, Error: &domainipc.Failure{Code: code, Message: message}})
}

func sendSuccess(c *net.UnixConn, id uint64) {
	_ = domainipc.WriteJSON(c, domainipc.Response{ID: id, Body: json.RawMessage("{}")})
}

func selectionWire(s app.Selection) domainipc.Selection {
	v := domainipc.Selection{State: s.Kind, Domain: s.Domain, Mode: s.Mode}
	if s.DefaultNode {
		v.View = "home_node"
	} else if s.Home {
		v.View = "home"
	} else if s.Overview {
		v.View = "overview"
	}
	return v
}

func windowWire(w domain.Window) domainipc.Window {
	return domainipc.Window{ID: w.ID, Mapped: w.Mapped, Domain: w.Domain, AppID: w.AppID, PID: w.PID}
}

func nodeWire(n domain.Node) domainipc.NodeProfile {
	return domainipc.NodeProfile{Domain: n.Name, Description: n.Description, CreatedAt: n.CreatedAt, LastActive: n.LastActive, Shortcut: n.NumberKey(), Loadout: n.Loadout}
}

func sendSnapshot(c *net.UnixConn, id uint64, s app.Snapshot) bool {
	wire := domainipc.Snapshot{
		Revision: s.Revision, WMReady: s.WMReady, Session: s.Session, Domains: s.Domains, MapVisible: s.MapVisible,
		Windows: make([]domainipc.Window, 0, len(s.Windows)), Selection: selectionWire(s.Selection),
	}
	for _, n := range s.Nodes {
		wire.NodeProfiles = append(wire.NodeProfiles, nodeWire(n))
	}
	for _, w := range s.Windows {
		wire.Windows = append(wire.Windows, windowWire(w))
	}
	body, _ := json.Marshal(wire)
	packet, _ := json.Marshal(domainipc.Response{ID: id, Body: body})
	if len(packet) > domainipc.MaxPacket {
		sendFailure(c, id, "snapshot_too_large", "current snapshot exceeds one packet")
		return false
	}
	return domainipc.Write(c, string(packet)) == nil
}

func eventWire(u app.Update) domainipc.Event {
	var body any
	switch u.Kind {
	case app.WMChanged:
		body = domainipc.WMChangedBody{WMReady: u.WMReady, Session: u.Session, Selection: selectionWire(u.Selection)}
	case app.DomainCreated:
		body = nodeWire(u.Node)
	case app.DomainDeleted:
		body = domainipc.DomainBody{Domain: u.Domain}
	case app.WindowUpdated:
		body = windowWire(u.Window)
	case app.WindowClosed:
		body = domainipc.WindowClosedBody{WindowID: u.Window.ID}
	case app.SelectionChanged:
		sel := selectionWire(u.Selection)
		sel.LastActive = u.Node.LastActive
		body = sel
	case app.MapVisibilityChanged:
		body = domainipc.MapVisibility{Visible: u.MapVisible}
	case app.NodeLoadoutChanged:
		body = domainipc.LoadoutBody{Domain: u.Domain, Entries: append([]string{}, u.Node.Loadout...)}
	}
	raw, _ := json.Marshal(body)
	return domainipc.Event{Revision: u.Revision, Event: u.Kind, Body: raw}
}

func (d *desktop) subscribe(ctx context.Context, c *net.UnixConn, id uint64) {
	snapshot, updates, unsubscribe, err := d.service.Subscribe()
	if err != nil {
		sendFailure(c, id, "subscription_limit", err.Error())
		return
	}
	defer unsubscribe()
	if !sendSnapshot(c, id, snapshot) {
		return
	}

	// This socket only carries event output after its first response. A read
	// detects an idle subscriber closing, or use of a forbidden second request.
	_ = c.SetReadDeadline(time.Time{})
	closed := make(chan struct{})
	go func() {
		var b [1]byte
		_, _ = c.Read(b[:])
		_ = c.Close()
		close(closed)
	}()
	stop := context.AfterFunc(ctx, func() { c.Close() })
	defer stop()
	for {
		select {
		case <-closed:
			return
		case <-ctx.Done():
			return
		case update, active := <-updates:
			if !active || domainipc.WriteJSON(c, eventWire(update)) != nil {
				return
			}
		}
	}
}
