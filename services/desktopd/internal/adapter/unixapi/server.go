package unixapi

import (
	"context"
	"encoding/hex"
	"errors"
	"fmt"
	"net"
	"strconv"
	"strings"
	"sync"
	"time"

	"mothership-wm/internal/domainipc"
	"mothership-wm/services/desktopd/internal/app"
	"mothership-wm/services/desktopd/internal/domain"
)

var errOutcomeUnknown = errors.New("WM disconnected before acknowledging; outcome unknown")

type wmPeer struct {
	conn    *net.UnixConn
	session *domain.Session
	ready   bool
	done    chan struct{}
	applied chan struct{}
	pending uint64
	name    string
}

type desktop struct {
	mu      sync.Mutex
	focusMu sync.Mutex
	service *app.Service
	wm      *wmPeer
	next    uint64
}

func Serve(ctx context.Context, l *net.UnixListener, useCases *app.Service) error {
	d := desktop{service: useCases}
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
		if w.pending != 0 {
			d.service.MarkFocusUnknown(w.session)
			w.pending = 0
		}
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
		if len(a) != 1 || a[0] != "ready" {
			return false
		}
		// WM registers before serving any Wayland clients: its initial snapshot is empty.
		w.ready = d.service.Ready(w.session)
		return w.ready
	}
	if len(a) != 2 {
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
		d.service.AcknowledgeFocus(w.session, w.name)
		w.pending = 0
		close(w.applied)
		return true
	case "new":
		return d.service.WindowAdded(w.session, id)
	case "map", "unmap":
		return d.service.WindowMapping(w.session, id, a[0] == "map")
	case "close":
		return d.service.WindowClosed(w.session, id)
	}
	return false
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

func (d *desktop) focus(name string) error {
	d.focusMu.Lock()
	defer d.focusMu.Unlock()
	d.mu.Lock()
	session, members, err := d.service.ResolveDomain(name)
	if err != nil {
		d.mu.Unlock()
		return err
	}
	w := d.wm
	if w == nil || w.session.ID != session {
		d.mu.Unlock()
		return app.ErrUnavailable
	}
	d.next++
	serial := d.next
	packet := fmt.Sprintf("focus %d", serial)
	for _, v := range members {
		packet += fmt.Sprintf(" %d %d", v.ID, v.Epoch)
	}
	applied := make(chan struct{})
	w.pending, w.name, w.applied = serial, name, applied
	d.mu.Unlock()

	if err := domainipc.Write(w.conn, packet); err == nil {
		timer := time.NewTimer(domainipc.Timeout)
		defer timer.Stop()
		select {
		case <-applied:
			return nil
		case <-timer.C:
		case <-w.done:
		}
	}
	// Serialize acknowledgement versus timeout/disconnect. Once an outcome
	// is declared unknown, a late packet cannot report a successful selection.
	d.mu.Lock()
	defer d.mu.Unlock()
	select {
	case <-applied:
		return nil
	default:
	}
	if w.pending == serial {
		w.pending = 0
		d.service.MarkFocusUnknown(w.session)
	}
	_ = w.conn.Close()
	return errOutcomeUnknown
}
