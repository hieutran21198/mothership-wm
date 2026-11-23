package unixapi

import (
	"context"
	"encoding/json"
	"net"
	"path/filepath"
	"strconv"
	"strings"
	"testing"
	"time"

	"mothership-wm/internal/domainipc"
	"mothership-wm/services/desktopd/internal/app"
)

const sessionID = "0123456789abcdef0123456789abcdef"

type testServer struct {
	t    *testing.T
	ctx  context.Context
	path string
}

func newTestServer(t *testing.T) testServer {
	t.Helper()
	path := filepath.Join(t.TempDir(), "desktopd.sock")
	l, err := net.ListenUnix("unixpacket", &net.UnixAddr{Name: path, Net: "unixpacket"})
	if err != nil {
		t.Fatal(err)
	}
	ctx, cancel := context.WithCancel(context.Background())
	done := make(chan error, 1)
	go func() { done <- Serve(ctx, l, &app.Service{}) }()
	t.Cleanup(func() {
		cancel()
		if err := <-done; err != nil {
			t.Error(err)
		}
	})
	return testServer{t: t, ctx: ctx, path: path}
}

func (s testServer) dial() *net.UnixConn {
	s.t.Helper()
	c, err := domainipc.Dial(s.ctx, s.path)
	if err != nil {
		s.t.Fatal(err)
	}
	_ = c.SetReadDeadline(time.Now().Add(domainipc.Timeout))
	s.t.Cleanup(func() { c.Close() })
	return c
}

func send(t *testing.T, c *net.UnixConn, packet string) {
	t.Helper()
	if err := domainipc.Write(c, packet); err != nil {
		t.Fatal(err)
	}
}

func read(t *testing.T, c *net.UnixConn) string {
	t.Helper()
	s, err := domainipc.Read(c)
	if err != nil {
		t.Fatal(err)
	}
	return s
}

func decode[T any](t *testing.T, b []byte) T {
	t.Helper()
	v, err := domainipc.Decode[T](b)
	if err != nil {
		t.Fatalf("decode %s: %v", b, err)
	}
	return v
}

func receive[T any](t *testing.T, c *net.UnixConn) T {
	t.Helper()
	return decode[T](t, []byte(read(t, c)))
}

func (s testServer) request(id uint64, method string, body string) *net.UnixConn {
	s.t.Helper()
	c := s.dial()
	if err := domainipc.WriteJSON(c, domainipc.Request{ID: id, Method: method, Body: json.RawMessage(body)}); err != nil {
		s.t.Fatal(err)
	}
	return c
}

func reply(t *testing.T, c *net.UnixConn, id uint64) domainipc.Response {
	t.Helper()
	r := receive[domainipc.Response](t, c)
	if r.ID != id {
		t.Fatalf("response ID %d, expected %d", r.ID, id)
	}
	return r
}

func success(t *testing.T, c *net.UnixConn, id uint64) []byte {
	t.Helper()
	r := reply(t, c, id)
	if r.Error != nil {
		t.Fatalf("request %d failed: %+v", id, r.Error)
	}
	return r.Body
}

func (s testServer) register() *net.UnixConn {
	s.t.Helper()
	wm := s.dial()
	send(s.t, wm, "wm "+sessionID)
	if r := read(s.t, wm); r != "ok" {
		s.t.Fatal(r)
	}
	return wm
}

func assertQuiet(t *testing.T, c *net.UnixConn) {
	t.Helper()
	_ = c.SetReadDeadline(time.Now().Add(25 * time.Millisecond))
	if p, err := domainipc.Read(c); err == nil {
		t.Fatalf("unexpected packet %q", p)
	} else if failure, ok := err.(net.Error); !ok || !failure.Timeout() {
		t.Fatalf("expected no packet yet, got %v", err)
	}
	_ = c.SetReadDeadline(time.Now().Add(domainipc.Timeout))
}

func next(t *testing.T, c *net.UnixConn, revision uint64, kind string) []byte {
	t.Helper()
	r := receive[domainipc.Event](t, c)
	if r.Revision != revision || r.Event != kind {
		t.Fatalf("unexpected event %+v, want %d:%s", r, revision, kind)
	}
	return r.Body
}

func TestJSONBodiesAndLegacyCLI(t *testing.T) {
	s := newTestServer(t)
	for idx, test := range []struct{ method, body, code string }{
		{"focus", `{"domain":"A","extra":true}`, "invalid_body"},
		{"create", `null`, "invalid_body"},
		{"assign", `{"domain":"A","window_id":0,"session":"` + sessionID + `"}`, "invalid_body"},
		{"bad", `{}`, "unknown_method"},
		{"focus", `{"domain":"A"}`, "unknown_domain"},
	} {
		id := uint64(42 + idx)
		r := reply(t, s.request(id, test.method, test.body), id)
		if r.Error == nil || r.Error.Code != test.code {
			t.Fatalf("%+v got %+v", test, r)
		}
	}
	wm := s.register()
	sub := s.request(66, "subscribe", "{}")
	success(t, sub, 66)
	send(t, wm, "ready")
	next(t, sub, 1, app.WMChanged)
	if got, err := Request(s.ctx, s.path, []string{"create", "A"}); err != nil || got != "ok" {
		t.Fatalf("CLI create: %q %v", got, err)
	}
	next(t, sub, 2, app.DomainCreated)
	send(t, wm, "new 7")
	send(t, wm, "map 7")
	next(t, sub, 3, app.WindowUpdated)
	next(t, sub, 4, app.WindowUpdated)
	if got, err := Request(s.ctx, s.path, []string{"assign", sessionID, "7", "A"}); err != nil || got != "ok" {
		t.Fatalf("CLI assign: %q %v", got, err)
	}
	next(t, sub, 5, app.WindowUpdated)
	if got, err := Request(s.ctx, s.path, []string{"list"}); err != nil || got != "session "+sessionID+"\nwindow 7 mapped A" {
		t.Fatalf("CLI list: %q %v", got, err)
	}
}

func TestSubscriptionOrderedAndFocusRequiresAck(t *testing.T) {
	s := newTestServer(t)
	wm := s.register()
	sub := s.request(101, "subscribe", "{}")
	snapshot := decode[domainipc.Snapshot](t, success(t, sub, 101))
	if snapshot.Revision != 0 || snapshot.WMReady || len(snapshot.Windows) != 0 || snapshot.Selection.State != app.SelectionNotSelected {
		t.Fatalf("bad initial snapshot %+v", snapshot)
	}
	success(t, s.request(102, "create", `{"domain":"A"}`), 102)
	next(t, sub, 1, app.DomainCreated)
	r := reply(t, s.request(103, "focus", `{"domain":"A"}`), 103)
	if r.Error == nil || r.Error.Code != "wm_unavailable" {
		t.Fatalf("unexpected focus before WM ready: %+v", r)
	}
	send(t, wm, "ready")
	next(t, sub, 2, app.WMChanged)
	send(t, wm, "new 7")
	next(t, sub, 3, app.WindowUpdated)
	assigned := `{"domain":"A","window_id":7,"session":"` + sessionID + `"}`
	success(t, s.request(104, "assign", assigned), 104)
	next(t, sub, 4, app.WindowUpdated)
	send(t, wm, "map 7")
	window := decode[domainipc.Window](t, next(t, sub, 5, app.WindowUpdated))
	if !window.Mapped || window.Domain != "A" || window.ID != 7 {
		t.Fatalf("invalid delta %+v", window)
	}
	sub.Close()

	fresh := s.request(105, "subscribe", "{}")
	snapshot = decode[domainipc.Snapshot](t, success(t, fresh, 105))
	if snapshot.Revision != 5 || snapshot.Session != sessionID || len(snapshot.Domains) != 1 || len(snapshot.Windows) != 1 || snapshot.Windows[0] != window {
		t.Fatalf("bad fresh snapshot %+v", snapshot)
	}
	focus := s.request(106, "focus", `{"domain":"A"}`)
	f := strings.Fields(read(t, wm))
	if len(f) != 4 || f[0] != "focus" || f[2] != "7" || f[3] != "1" {
		t.Fatalf("invalid WM projection: %v", f)
	}
	assertQuiet(t, focus)
	assertQuiet(t, fresh)
	send(t, wm, "applied "+f[1])
	success(t, focus, 106)
	sel := decode[domainipc.Selection](t, next(t, fresh, 6, app.SelectionChanged))
	if sel.State != app.SelectionAcknowledged || sel.Domain != "A" {
		t.Fatalf("unexpected selection %+v", sel)
	}
	send(t, wm, "unmap 7")
	after := decode[domainipc.Window](t, next(t, fresh, 7, app.WindowUpdated))
	if after.Domain != "A" || after.Mapped {
		t.Fatalf("unmap lost membership: %+v", after)
	}
	send(t, wm, "close 7")
	closed := decode[domainipc.WindowClosedBody](t, next(t, fresh, 8, app.WindowClosed))
	if closed.WindowID != 7 {
		t.Fatal(closed)
	}
	if r := reply(t, s.request(107, "assign", assigned), 107); r.Error == nil || r.Error.Code != "unknown_window" {
		t.Fatalf("expected stale closed ID: %+v", r)
	}
}

func TestLostWMDoesNotConfirmSelection(t *testing.T) {
	s := newTestServer(t)
	wm := s.register()
	sub := s.request(1, "subscribe", "{}")
	success(t, sub, 1)
	send(t, wm, "ready")
	next(t, sub, 1, app.WMChanged)
	success(t, s.request(2, "create", `{"domain":"A"}`), 2)
	next(t, sub, 2, app.DomainCreated)
	focus := s.request(3, "focus", `{"domain":"A"}`)
	if f := strings.Fields(read(t, wm)); len(f) != 2 || f[0] != "focus" {
		t.Fatalf("expected focus with no windows, got %v", f)
	} else if _, err := strconv.ParseUint(f[1], 10, 64); err != nil {
		t.Fatal(err)
	}
	wm.Close()
	r := reply(t, focus, 3)
	if r.Error == nil || r.Error.Code != "outcome_unknown" {
		t.Fatalf("focus outcome %+v", r)
	}
	sel := decode[domainipc.Selection](t, next(t, sub, 3, app.SelectionChanged))
	if sel.State != app.SelectionUnknown || sel.Domain != "" {
		t.Fatal(sel)
	}
	lost := decode[domainipc.WMChangedBody](t, next(t, sub, 4, app.WMChanged))
	if lost.WMReady || lost.Session != "" || lost.Selection != sel {
		t.Fatal(lost)
	}
}

func TestHomeReadinessAssignmentAndNamedHomeAreDistinct(t *testing.T) {
	s := newTestServer(t)
	wm := s.register()
	sub := s.request(10, "subscribe", "{}")
	success(t, sub, 10)
	send(t, wm, "ready_home")
	changed := decode[domainipc.WMChangedBody](t, next(t, sub, 1, app.WMChanged))
	home := domainipc.Selection{State: app.SelectionAcknowledged, View: "home"}
	if changed.Selection != home {
		t.Fatalf("ready did not confirm Home: %+v", changed)
	}
	success(t, s.request(20, "create", `{"domain":"Home"}`), 20)
	next(t, sub, 2, app.DomainCreated)
	send(t, wm, "new 7")
	next(t, sub, 3, app.WindowUpdated)
	send(t, wm, "map 7")
	next(t, sub, 4, app.WindowUpdated)
	assigned := s.request(30, "assign", `{"domain":"Home","window_id":7,"session":"`+sessionID+`"}`)
	prep := strings.Fields(read(t, wm))
	if len(prep) != 3 || prep[0] != "assigned" || prep[2] != "7" {
		t.Fatalf("expected ordered Home eligibility update: %v", prep)
	}
	next(t, sub, 5, app.WindowUpdated)
	assertQuiet(t, assigned)
	send(t, wm, "applied "+prep[1])
	success(t, assigned, 30)
	named := s.request(40, "focus", `{"domain":"Home"}`)
	prep = strings.Fields(read(t, wm))
	if len(prep) != 4 || prep[0] != "focus" || prep[2] != "7" {
		t.Fatalf("named Home is not a Home view: %v", prep)
	}
	send(t, wm, "applied "+prep[1])
	success(t, named, 40)
	selected := decode[domainipc.Selection](t, next(t, sub, 6, app.SelectionChanged))
	if selected != (domainipc.Selection{State: app.SelectionAcknowledged, Domain: "Home"}) {
		t.Fatalf("unexpected named selection: %+v", selected)
	}
	back := s.request(50, "home", "{}")
	prep = strings.Fields(read(t, wm))
	if len(prep) != 2 || prep[0] != "home" {
		t.Fatalf("expected a separate Home command: %v", prep)
	}
	assertQuiet(t, back)
	assertQuiet(t, sub)
	send(t, wm, "applied "+prep[1])
	success(t, back, 50)
	if sel := decode[domainipc.Selection](t, next(t, sub, 7, app.SelectionChanged)); sel != home {
		t.Fatalf("return Home not confirmed: %+v", sel)
	}
	snap := decode[domainipc.Snapshot](t, success(t, s.request(60, "snapshot", "{}"), 60))
	if snap.Selection != home || len(snap.Windows) != 1 || snap.Windows[0].Domain != "Home" {
		t.Fatalf("Home changed saved membership: %+v", snap)
	}
}

func TestAssignmentMirrorLossIsNotAnUnassignment(t *testing.T) {
	s := newTestServer(t)
	wm := s.register()
	sub := s.request(1, "subscribe", "{}")
	success(t, sub, 1)
	send(t, wm, "ready_home")
	next(t, sub, 1, app.WMChanged)
	success(t, s.request(2, "create", `{"domain":"Dev"}`), 2)
	next(t, sub, 2, app.DomainCreated)
	send(t, wm, "new 7")
	next(t, sub, 3, app.WindowUpdated)
	assigned := s.request(3, "assign", `{"domain":"Dev","window_id":7,"session":"`+sessionID+`"}`)
	if p := read(t, wm); !strings.HasPrefix(p, "assigned ") {
		t.Fatal(p)
	}
	committed := decode[domainipc.Window](t, next(t, sub, 4, app.WindowUpdated))
	if committed.Domain != "Dev" {
		t.Fatal(committed)
	}
	wm.Close()
	r := reply(t, assigned, 3)
	if r.Error == nil || r.Error.Code != "assignment_view_unknown" {
		t.Fatalf("lost apply was falsely acknowledged: %+v", r)
	}
	lost := decode[domainipc.WMChangedBody](t, next(t, sub, 5, app.WMChanged))
	if lost.WMReady || lost.Selection.State != app.SelectionUnknown {
		t.Fatalf("stale WM projection still described as known: %+v", lost)
	}
}

func TestMoveReconcilesActiveNamedProjectionWithoutSelectingDestination(t *testing.T) {
	s := newTestServer(t)
	wm := s.register()
	sub := s.request(1, "subscribe", "{}")
	success(t, sub, 1)
	send(t, wm, "ready_home")
	next(t, sub, 1, app.WMChanged)
	for i, name := range []string{"Dev", "Review"} {
		id := uint64(i + 2)
		success(t, s.request(id, "create", `{"domain":"`+name+`"}`), id)
		next(t, sub, id, app.DomainCreated)
	}
	for i := uint64(1); i <= 2; i++ {
		wi := strconv.FormatUint(i, 10)
		send(t, wm, "new "+wi)
		next(t, sub, 3*i+1, app.WindowUpdated)
		send(t, wm, "map "+wi)
		next(t, sub, 3*i+2, app.WindowUpdated)
		id := i + 3
		req := s.request(id, "assign", `{"domain":"Dev","window_id":`+wi+`,"session":"`+sessionID+`"}`)
		pkt := strings.Fields(read(t, wm))
		if len(pkt) != 3 || pkt[0] != "assigned" || pkt[2] != wi {
			t.Fatalf("unexpected first assignment: %v", pkt)
		}
		send(t, wm, "applied "+pkt[1])
		success(t, req, id)
		next(t, sub, 3*i+3, app.WindowUpdated)
	}
	focus := s.request(6, "focus", `{"domain":"Dev"}`)
	packet := strings.Fields(read(t, wm))
	send(t, wm, "applied "+packet[1])
	success(t, focus, 6)
	next(t, sub, 10, app.SelectionChanged)
	for i, wi := range []string{"2", "1"} {
		id := uint64(i + 7)
		move := s.request(id, "assign", `{"domain":"Review","window_id":`+wi+`,"session":"`+sessionID+`"}`)
		pkt := strings.Fields(read(t, wm))
		if len(pkt) < 4 || pkt[0] != "assigned" || pkt[2] != wi || pkt[3] != "project" {
			t.Fatalf("move omitted current projection: %v", pkt)
		}
		want := "1 1"
		if i == 1 {
			want = ""
		}
		if tail := strings.Join(pkt[4:], " "); tail != want {
			t.Fatalf("projection = %q, expected %q", tail, want)
		}
		next(t, sub, uint64(i+11), app.WindowUpdated)
		assertQuiet(t, move)
		send(t, wm, "applied "+pkt[1])
		success(t, move, id)
	}
	snap := decode[domainipc.Snapshot](t, success(t, s.request(9, "snapshot", "{}"), 9))
	if snap.Revision != 12 || snap.Selection.Domain != "Dev" ||
		len(snap.Windows) != 2 || snap.Windows[1].ID != 2 || snap.Windows[1].Domain != "Review" {
		t.Fatalf("move switched view or identity: %+v", snap)
	}
}

func TestNodeOverviewAndLayoutRequireWMAcknowledgement(t *testing.T) {
	s := newTestServer(t)
	wm := s.register()
	sub := s.request(1, "subscribe", "{}")
	success(t, sub, 1)
	send(t, wm, "ready_views")
	next(t, sub, 1, app.WMChanged)
	success(t, s.request(2, "create", `{"domain":"Dev"}`), 2)
	next(t, sub, 2, app.DomainCreated)
	success(t, s.request(3, "create", `{"domain":"Review"}`), 3)
	next(t, sub, 3, app.DomainCreated)

	apply := func(id uint64, method, body, command, suffix string) {
		t.Helper()
		c := s.request(id, method, body)
		a := strings.Fields(read(t, wm))
		if len(a) < 2 || a[0] != command || strings.Join(a[2:], " ") != suffix {
			t.Fatalf("unexpected WM command: %v", a)
		}
		assertQuiet(t, c)
		assertQuiet(t, sub)
		send(t, wm, "applied "+a[1])
		success(t, c, id)
	}
	want := func(rev uint64, v domainipc.Selection) {
		t.Helper()
		got := decode[domainipc.Selection](t, next(t, sub, rev, app.SelectionChanged))
		if got != v {
			t.Fatalf("selection %v; want %v", got, v)
		}
	}
	apply(4, "focus", `{"domain":"Dev"}`, "node", "tiling")
	want(4, domainipc.Selection{State: app.SelectionAcknowledged, Domain: "Dev", Mode: "tiling"})
	apply(5, "layout", `{"domain":"Dev","mode":"stack"}`, "layout", "stack")
	want(5, domainipc.Selection{State: app.SelectionAcknowledged, Domain: "Dev", Mode: "stack"})
	apply(6, "stack.next", `{"domain":"Dev"}`, "next", "")
	assertQuiet(t, sub)
	apply(7, "overview", `{}`, "overview", "")
	want(6, domainipc.Selection{State: app.SelectionAcknowledged, View: "overview"})
	if r := reply(t, s.request(8, "layout", `{"domain":"Dev","mode":"float"}`), 8); r.Error == nil {
		t.Fatal("layout accepted for an inactive Node")
	}
	apply(9, "focus", `{"domain":"Review"}`, "node", "tiling")
	want(7, domainipc.Selection{State: app.SelectionAcknowledged, Domain: "Review", Mode: "tiling"})
	apply(10, "focus", `{"domain":"Dev"}`, "node", "stack")
	want(8, domainipc.Selection{State: app.SelectionAcknowledged, Domain: "Dev", Mode: "stack"})
}
