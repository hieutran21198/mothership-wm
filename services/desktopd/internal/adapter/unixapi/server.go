package unixapi

import (
	"context"
	"encoding/hex"
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

type wmPeer struct {
	conn    *net.UnixConn
	session *domain.Session
	ready   bool
	done    chan struct{}
	applied chan uint64
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
	if len(args) == 2 && args[0] == "wm" && len(args[1]) == 32 {
		if _, err := hex.DecodeString(args[1]); err == nil {
			d.track(ctx, c, args[1])
			return
		}
	}
	_ = domainipc.Write(c, d.command(args))
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
		done: make(chan struct{}), applied: make(chan uint64, 1)}
	d.wm = w
	d.mu.Unlock()
	defer func() {
		d.mu.Lock()
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
		select {
		case w.applied <- id:
			return true
		default:
			return false
		}
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

func (d *desktop) command(a []string) string {
	if len(a) == 0 {
		return "err expected a command"
	}
	for _, s := range a {
		if !validWord(s) {
			return "err invalid command argument"
		}
	}
	if a[0] == "focus" && len(a) == 2 {
		return d.focus(a[1])
	}
	switch {
	case a[0] == "create" && len(a) == 2:
		d.service.Create(a[1])
		return "ok"
	case a[0] == "assign" && len(a) == 4:
		id, err := strconv.ParseUint(a[2], 10, 64)
		if err != nil {
			return "err invalid Window ID"
		}
		if err := d.service.Assign(a[1], id, a[3]); err != nil {
			return "err " + err.Error()
		}
		return "ok"
	case a[0] == "list" && len(a) == 1:
		session, windows, err := d.service.List()
		if err != nil {
			return "err " + err.Error()
		}
		var out strings.Builder
		fmt.Fprintf(&out, "session %s", session)
		for _, v := range windows {
			mapping := "unmapped"
			if v.Mapped {
				mapping = "mapped"
			}
			line := fmt.Sprintf("\nwindow %d %s %s", v.ID, mapping, v.Domain)
			if out.Len()+len(line) > domainipc.MaxPacket {
				return "err too many live windows to list"
			}
			out.WriteString(strings.TrimRight(line, " "))
		}
		return out.String()
	default:
		return "err expected list, create, assign or focus"
	}
}

func (d *desktop) focus(domain string) string {
	d.focusMu.Lock()
	defer d.focusMu.Unlock()
	d.mu.Lock()
	session, members, err := d.service.ResolveDomain(domain)
	if err != nil {
		d.mu.Unlock()
		return "err " + err.Error()
	}
	w := d.wm
	if w == nil || w.session.ID != session {
		d.mu.Unlock()
		return "err WM disconnected"
	}
	d.next++
	serial := d.next
	packet := fmt.Sprintf("focus %d", serial)
	for _, v := range members {
		packet += fmt.Sprintf(" %d %d", v.ID, v.Epoch)
	}
	d.mu.Unlock()
	if err := domainipc.Write(w.conn, packet); err != nil {
		w.conn.Close()
		return "err WM disconnected before acknowledging; outcome unknown"
	}
	timer := time.NewTimer(domainipc.Timeout)
	defer timer.Stop()
	select {
	case received := <-w.applied:
		if received == serial {
			return "ok"
		}
	case <-timer.C:
	case <-w.done:
		// An acknowledgement can arrive immediately before EOF.
		select {
		case received := <-w.applied:
			if received == serial {
				return "ok"
			}
		default:
		}
	}
	w.conn.Close()
	return "err WM disconnected before acknowledging; outcome unknown"
}
