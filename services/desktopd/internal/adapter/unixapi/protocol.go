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
	case domainipc.SnapshotMethod, domainipc.SubscribeMethod, domainipc.HomeMethod:
		if _, err := domainipc.Decode[domainipc.Empty](req.Body); err != nil {
			badBody()
			return
		}
		switch req.Method {
		case domainipc.SubscribeMethod:
			d.subscribe(ctx, c, req.ID)
		case domainipc.SnapshotMethod:
			sendSnapshot(c, req.ID, d.service.Snapshot())
		case domainipc.HomeMethod:
			if err := d.focus(""); err != nil {
				sendCommandError(c, req.ID, err)
				return
			}
			sendSuccess(c, req.ID)
		}
	case domainipc.CreateMethod, domainipc.FocusMethod:
		b, err := domainipc.Decode[domainipc.DomainBody](req.Body)
		if err != nil || !validWord(b.Domain) {
			badBody()
			return
		}
		if req.Method == domainipc.CreateMethod {
			d.service.Create(b.Domain)
			sendSuccess(c, req.ID)
			return
		}
		if err := d.focus(b.Domain); err != nil {
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
	v := domainipc.Selection{State: s.Kind, Domain: s.Domain}
	if s.Home {
		v.View = "home"
	}
	return v
}

func windowWire(w domain.Window) domainipc.Window {
	return domainipc.Window{ID: w.ID, Mapped: w.Mapped, Domain: w.Domain}
}

func sendSnapshot(c *net.UnixConn, id uint64, s app.Snapshot) bool {
	wire := domainipc.Snapshot{
		Revision: s.Revision, WMReady: s.WMReady, Session: s.Session, Domains: s.Domains,
		Windows: make([]domainipc.Window, 0, len(s.Windows)), Selection: selectionWire(s.Selection),
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
		body = domainipc.DomainBody{Domain: u.Domain}
	case app.WindowUpdated:
		body = windowWire(u.Window)
	case app.WindowClosed:
		body = domainipc.WindowClosedBody{WindowID: u.Window.ID}
	case app.SelectionChanged:
		body = selectionWire(u.Selection)
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
