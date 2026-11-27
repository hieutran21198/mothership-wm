package unixapi

import (
	"context"
	"encoding/json"
	"net"
	"os"
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
	return testServerWithConfig(t, "")
}

func testServerWithConfig(t *testing.T, config string) testServer {
	t.Helper()
	path := filepath.Join(t.TempDir(), "desktopd.sock")
	l, err := net.ListenUnix("unixpacket", &net.UnixAddr{Name: path, Net: "unixpacket"})
	if err != nil {
		t.Fatal(err)
	}
	ctx, cancel := context.WithCancel(context.Background())
	done := make(chan error, 1)
	go func() { done <- ServeWithConfig(ctx, l, &app.Service{}, nil, config) }()
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
	if selected.State != app.SelectionAcknowledged || selected.Domain != "Home" || selected.View != "" || selected.LastActive <= 0 {
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
	want := func(rev uint64, v domainipc.Selection, entry bool) {
		t.Helper()
		got := decode[domainipc.Selection](t, next(t, sub, rev, app.SelectionChanged))
		if (got.LastActive > 0) != entry {
			t.Fatalf("Last active %d on selection %+v; entry %t", got.LastActive, got, entry)
		}
		got.LastActive = 0
		if got != v {
			t.Fatalf("selection %v; want %v", got, v)
		}
	}
	apply(4, "focus", `{"domain":"Dev"}`, "node", "flow")
	want(4, domainipc.Selection{State: app.SelectionAcknowledged, Domain: "Dev", Mode: "flow"}, true)
	apply(5, "layout", `{"domain":"Dev","mode":"stack"}`, "layout", "stack")
	want(5, domainipc.Selection{State: app.SelectionAcknowledged, Domain: "Dev", Mode: "stack"}, false)
	apply(6, "stack.next", `{"domain":"Dev"}`, "next", "")
	assertQuiet(t, sub)
	apply(7, "overview", `{}`, "overview", "")
	want(6, domainipc.Selection{State: app.SelectionAcknowledged, View: "overview"}, false)
	if r := reply(t, s.request(8, "layout", `{"domain":"Dev","mode":"float"}`), 8); r.Error == nil {
		t.Fatal("layout accepted for an inactive Node")
	}
	apply(9, "focus", `{"domain":"Review"}`, "node", "flow")
	want(7, domainipc.Selection{State: app.SelectionAcknowledged, Domain: "Review", Mode: "flow"}, true)
	apply(10, "focus", `{"domain":"Dev"}`, "node", "stack")
	want(8, domainipc.Selection{State: app.SelectionAcknowledged, Domain: "Dev", Mode: "stack"}, true)
	apply(11, "layout", `{"domain":"Dev","mode":"flow-vertical"}`, "layout", "flow-vertical")
	want(9, domainipc.Selection{State: app.SelectionAcknowledged, Domain: "Dev", Mode: "flow-vertical"}, false)
	apply(12, "stack.next", `{"domain":"Dev"}`, "next", "")
	assertQuiet(t, sub)
	apply(13, "focus", `{"domain":"Review"}`, "node", "flow")
	want(10, domainipc.Selection{State: app.SelectionAcknowledged, Domain: "Review", Mode: "flow"}, true)
	apply(14, "focus", `{"domain":"Dev"}`, "node", "flow-vertical")
	want(11, domainipc.Selection{State: app.SelectionAcknowledged, Domain: "Dev", Mode: "flow-vertical"}, true)
	apply(15, "layout", `{"domain":"Dev","mode":"stage"}`, "layout", "stage")
	want(12, domainipc.Selection{State: app.SelectionAcknowledged, Domain: "Dev", Mode: "stage"}, false)
	apply(16, "focus", `{"domain":"Review"}`, "node", "flow")
	want(13, domainipc.Selection{State: app.SelectionAcknowledged, Domain: "Review", Mode: "flow"}, true)
	apply(17, "focus", `{"domain":"Dev"}`, "node", "stage")
	want(14, domainipc.Selection{State: app.SelectionAcknowledged, Domain: "Dev", Mode: "stage"}, true)
	apply(18, "layout", `{"domain":"Dev","mode":"flow","restore_flow":true}`, "layout", "flow-vertical")
	want(15, domainipc.Selection{State: app.SelectionAcknowledged, Domain: "Dev", Mode: "flow-vertical"}, false)
	apply(19, "layout", `{"domain":"Dev","mode":"flow"}`, "layout", "flow")
	want(16, domainipc.Selection{State: app.SelectionAcknowledged, Domain: "Dev", Mode: "flow"}, false)
}

func TestPrepareDoesNotSelectAndRevealUsesSingleLiveTicket(t *testing.T) {
	s := newTestServer(t)
	wm := s.register()
	send(t, wm, "ready_handoff")
	success(t, s.request(1, "create", `{"domain":"Dev"}`), 1)
	success(t, s.request(2, "map.show", "{}"), 2)
	c := s.request(3, "overview", "{}")
	packet := strings.Fields(read(t, wm))
	send(t, wm, "applied "+packet[1])
	success(t, c, 3)
	p := s.request(4, "focus.prepare", `{"domain":"Dev"}`)
	packet = strings.Fields(read(t, wm))
	if packet[0] != "prepare" {
		t.Fatal(packet)
	}
	send(t, wm, "applied "+packet[1])
	ticket := decode[domainipc.FocusTicket](t, success(t, p, 4))
	snap := func() domainipc.Snapshot {
		return decode[domainipc.Snapshot](t, success(t, s.request(5, "snapshot", "{}"), 5))
	}
	if before := snap(); before.Selection.View != "overview" || before.NodeProfiles[0].LastActive != 0 {
		t.Fatalf("prepare changed state: %+v", before)
	}
	body := `{"ticket":` + strconv.FormatUint(ticket.Ticket, 10) + `}`
	r := s.request(6, "focus.reveal", body)
	packet = strings.Fields(read(t, wm))
	if packet[0] != "reveal" || packet[2] != "flow" {
		t.Fatal(packet)
	}
	if snap().Selection.View != "overview" {
		t.Fatal("selection changed without frame ACK")
	}
	send(t, wm, "applied "+packet[1])
	success(t, r, 6)
	if after := snap(); after.Selection.Domain != "Dev" || after.NodeProfiles[0].LastActive == 0 {
		t.Fatalf("no reveal selection: %+v", after)
	}
	if r := reply(t, s.request(7, "focus.reveal", body), 7); r.Error == nil || r.Error.Code != "stale_connect" {
		t.Fatalf("ticket reused: %+v", r)
	}
}

func TestDefaultNodeIsAnAcknowledgedHomeProjectionNotADomain(t *testing.T) {
	s := newTestServer(t)
	wm := s.dial()
	send(t, wm, "wm "+sessionID)
	if got := read(t, wm); got != "ok" {
		t.Fatal(got)
	}
	send(t, wm, "ready_default_node")
	// The stored preference is normally true, but only Overview draws the map.
	success(t, s.request(30, domainipc.MapShowMethod, "{}"), 30)
	req := s.request(31, domainipc.DefaultNodeMethod, "{}")
	cmd := strings.Fields(read(t, wm))
	if len(cmd) != 2 || cmd[0] != "home_node" {
		t.Fatal(cmd)
	}
	before := decode[domainipc.Snapshot](t, success(t, s.request(32, domainipc.SnapshotMethod, "{}"), 32))
	if before.Selection.View != "home" || !before.MapVisible || len(before.Domains) != 0 {
		t.Fatal(before)
	}
	send(t, wm, "applied "+cmd[1])
	success(t, req, 31)
	after := decode[domainipc.Snapshot](t, success(t, s.request(33, domainipc.SnapshotMethod, "{}"), 33))
	if after.Selection.View != "home_node" || after.Selection.Domain != "" || len(after.Domains) != 0 || len(after.NodeProfiles) != 0 {
		t.Fatal(after)
	}
	home := s.request(34, domainipc.HomeMethod, "{}")
	cmd = strings.Fields(read(t, wm))
	if len(cmd) != 2 || cmd[0] != "home" {
		t.Fatal(cmd)
	}
	send(t, wm, "applied "+cmd[1])
	success(t, home, 34)
}

func TestDefaultNodeSharesAcknowledgedLayoutWithoutDomain(t *testing.T) {
	s := newTestServer(t)
	wm := s.register()
	send(t, wm, "ready_node_parity")
	var id uint64
	request := func(method, body string) (*net.UnixConn, uint64) {
		id++
		return s.request(id, method, body), id
	}
	snapshot := func() domainipc.Snapshot {
		c, seq := request(domainipc.SnapshotMethod, "{}")
		return decode[domainipc.Snapshot](t, success(t, c, seq))
	}
	apply := func(method, body, wantOp, wantMode string, before func()) {
		c, seq := request(method, body)
		fields := strings.Fields(read(t, wm))
		wantFields := 2
		if wantMode != "" {
			wantFields++
		}
		if len(fields) != wantFields ||
			fields[0] != wantOp || (wantMode != "" && fields[2] != wantMode) {
			t.Fatalf("WM packet %v, want %s %s", fields, wantOp, wantMode)
		}
		if before != nil {
			before()
		}
		send(t, wm, "applied "+fields[1])
		success(t, c, seq)
	}
	rejected := func(body string) {
		c, seq := request(domainipc.LayoutMethod, body)
		if r := reply(t, c, seq); r.Error == nil {
			t.Fatalf("unexpected layout acceptance: %s", body)
		}
	}
	const stage = `{"domain":"","mode":"stage"}`
	rejected(stage) // Blank Home is not the Default Node.
	apply(domainipc.DefaultNodeMethod, "{}", "home_node", domainipc.Flow, nil)
	if ss := snapshot(); ss.Selection.View != "home_node" || ss.Selection.Mode != domainipc.Flow ||
		ss.Selection.Domain != "" || len(ss.Domains) != 0 || len(ss.NodeProfiles) != 0 {
		t.Fatalf("Default Node gained a named identity: %+v", ss)
	}
	rejected(`{"domain":"","mode":"stack"}`)
	apply(domainipc.LayoutMethod, `{"domain":"","mode":"flow-vertical"}`, "layout", domainipc.FlowVertical, nil)
	apply(domainipc.LayoutMethod, stage, "layout", domainipc.Stage, func() {
		if mode := snapshot().Selection.Mode; mode != domainipc.FlowVertical {
			t.Fatalf("mode changed before acknowledgement: %s", mode)
		}
	})
	c, seq := request(domainipc.CreateMethod, `{"domain":"Dev"}`)
	success(t, c, seq)
	apply(domainipc.FocusMethod, `{"domain":"Dev"}`, "node", domainipc.Flow, nil)
	rejected(stage) // Empty domain cannot target an inactive Default Node.
	apply(domainipc.DefaultNodeMethod, "{}", "home_node", domainipc.Stage, nil)
	apply(domainipc.LayoutMethod, `{"domain":"","mode":"flow","restore_flow":true}`, "layout", domainipc.FlowVertical, nil)
	ss := snapshot()
	if ss.Selection.View != "home_node" || ss.Selection.Mode != domainipc.FlowVertical ||
		len(ss.Domains) != 1 || ss.Domains[0] != "Dev" {
		t.Fatal(ss)
	}
}

func TestForceDeleteWaitsForInboxMove(t *testing.T) {
	t.Run("acknowledged", func(t *testing.T) { testForceDeleteHandoff(t, true) })
	t.Run("disconnected", func(t *testing.T) { testForceDeleteHandoff(t, false) })
}

func testForceDeleteHandoff(t *testing.T, acknowledge bool) {
	s := newTestServer(t)
	sub := s.request(1, domainipc.SubscribeMethod, "{}")
	success(t, sub, 1)
	wm := s.register()
	send(t, wm, "ready_return_inbox")
	next(t, sub, 1, app.WMChanged)
	success(t, s.request(2, domainipc.CreateMethod, `{"domain":"Dev"}`), 2)
	next(t, sub, 2, app.DomainCreated)
	send(t, wm, "new 7")
	next(t, sub, 3, app.WindowUpdated)
	send(t, wm, "map 7")
	next(t, sub, 4, app.WindowUpdated)
	send(t, wm, "new 8")
	next(t, sub, 5, app.WindowUpdated)
	for i, id := range []string{"7", "8"} {
		reqID := uint64(10 + i)
		req := s.request(reqID, domainipc.AssignMethod, `{"domain":"Dev","session":"`+sessionID+`","window_id":`+id+"}")
		cmd := strings.Fields(read(t, wm))
		if len(cmd) != 3 || cmd[0] != "assigned" || cmd[2] != id {
			t.Fatal(cmd)
		}
		send(t, wm, "applied "+cmd[1])
		success(t, req, reqID)
		next(t, sub, uint64(6+i), app.WindowUpdated)
	}
	result := reply(t, s.request(20, domainipc.DeleteMethod, `{"domain":"Dev"}`), 20)
	if result.Error == nil || !strings.Contains(result.Error.Message, "Force Delete") {
		t.Fatal(result)
	}
	req := s.request(21, domainipc.DeleteMethod, `{"domain":"Dev","force":true}`)
	cmd := strings.Fields(read(t, wm))
	if len(cmd) != 4 || cmd[0] != "inbox" || cmd[2] != "7" || cmd[3] != "8" {
		t.Fatal(cmd)
	}
	assertQuiet(t, req)
	before := decode[domainipc.Snapshot](t, success(t, s.request(22, domainipc.SnapshotMethod, "{}"), 22))
	if len(before.Domains) != 1 || before.Windows[0].Domain != "Dev" {
		t.Fatal(before)
	}
	if !acknowledge {
		_ = wm.Close()
		r := reply(t, req, 21)
		if r.Error == nil || r.Error.Code != "outcome_unknown" {
			t.Fatal(r)
		}
		after := decode[domainipc.Snapshot](t, success(t, s.request(23, domainipc.SnapshotMethod, "{}"), 23))
		if len(after.Domains) != 1 || after.Domains[0] != "Dev" {
			t.Fatal(after)
		}
		return
	}
	send(t, wm, "applied "+cmd[1])
	success(t, req, 21)
	for i, id := range []uint64{7, 8} {
		win := decode[domainipc.Window](t, next(t, sub, uint64(8+i), app.WindowUpdated))
		if win.ID != id || win.Domain != "" || win.Mapped != (id == 7) {
			t.Fatal(win)
		}
	}
	deleted := decode[domainipc.DomainBody](t, next(t, sub, 10, app.DomainDeleted))
	if deleted.Domain != "Dev" {
		t.Fatal(deleted)
	}
	after := decode[domainipc.Snapshot](t, success(t, s.request(23, domainipc.SnapshotMethod, "{}"), 23))
	if after.Selection.View != "home" || len(after.Domains) != 0 || len(after.Windows) != 2 {
		t.Fatal(after)
	}
}

func TestShortcutApplyWaitsForWMAndPersistsAcrossConnections(t *testing.T) {
	config := filepath.Join(t.TempDir(), "config.json")
	s := testServerWithConfig(t, config)
	waitReady := func(want bool) {
		t.Helper()
		deadline := time.Now().Add(time.Second)
		for time.Now().Before(deadline) {
			snap := decode[domainipc.Snapshot](t, success(t, s.request(11, "snapshot", "{}"), 11))
			if snap.WMReady == want {
				return
			}
			time.Sleep(5 * time.Millisecond)
		}
		t.Fatal("WM readiness did not change")
	}

	wm := s.dial()
	send(t, wm, "wm "+sessionID)
	if read(t, wm) != "ok" {
		t.Fatal("WM registration failed")
	}
	send(t, wm, "ready_shortcuts")
	waitReady(true)
	def := decode[domainipc.Shortcuts](t, reply(t, s.request(1, "shortcuts.get", `{}`), 1).Body)
	if def != domainipc.DefaultShortcuts() {
		t.Fatalf("unexpected defaults: %+v", def)
	}
	bad := reply(t, s.request(2, "shortcuts.set", `{"close_window":"X","app_grid":"X","node_settings":",","node_keys":"numbers"}`), 2)
	if bad.Error == nil || bad.Error.Code != "invalid_shortcuts" {
		t.Fatalf("duplicate accepted: %+v", bad)
	}
	c := s.request(3, "shortcuts.set", `{"close_window":"X","app_grid":"B","node_settings":"S","node_keys":"function"}`)
	packet := strings.Fields(read(t, wm))
	if len(packet) != 6 || packet[0] != "shortcuts" || strings.Join(packet[2:], " ") != "45 48 31 59" {
		t.Fatalf("bad WM packet %v", packet)
	}
	if _, err := os.Stat(config); !os.IsNotExist(err) {
		t.Fatalf("shortcuts persisted before acknowledgement: %v", err)
	}
	send(t, wm, "applied "+packet[1])
	if r := reply(t, c, 3); r.Error != nil {
		t.Fatal(r.Error)
	}
	want := domainipc.Shortcuts{CloseWindow: "X", AppGrid: "B", NodeSettings: "S", NodeKeys: "function"}
	if persisted, err := readShortcuts(config); err != nil || persisted != want {
		t.Fatalf("saved %+v %v", persisted, err)
	}
	wm.Close()
	waitReady(false)
	next := s.dial()
	send(t, next, "wm "+sessionID)
	if read(t, next) != "ok" {
		t.Fatal("WM re-registration failed")
	}
	send(t, next, "ready_shortcuts")
	packet = strings.Fields(read(t, next))
	if len(packet) != 6 || packet[0] != "shortcuts" || strings.Join(packet[2:], " ") != "45 48 31 59" {
		t.Fatalf("not restored: %v", packet)
	}
	send(t, next, "applied "+packet[1])
}

func TestFocusWindowSelectsItsCurrentNodeWithTarget(t *testing.T) {
	s := newTestServer(t)
	wm := s.register()
	send(t, wm, "ready_window_focus")
	var id uint64
	request := func(method, body string) (*net.UnixConn, uint64) {
		id++
		return s.request(id, method, body), id
	}
	ref := func(window string) string {
		return `{"session":"` + sessionID + `","window_id":` + window + `}`
	}
	// apply acknowledges the WM packet for one request and returns its fields.
	apply := func(c *net.UnixConn, seq uint64) []string {
		fields := strings.Fields(read(t, wm))
		send(t, wm, "applied "+fields[1])
		success(t, c, seq)
		return fields
	}
	for _, name := range []string{"Dev", "Review"} {
		c, seq := request(domainipc.CreateMethod, `{"domain":"`+name+`"}`)
		success(t, c, seq)
	}
	for _, wi := range []string{"1", "2", "3"} {
		send(t, wm, "new "+wi)
		send(t, wm, "map "+wi)
	}
	for _, wi := range []string{"1", "2"} {
		c, seq := request(domainipc.AssignMethod, `{"domain":"Dev","window_id":`+wi+`,"session":"`+sessionID+`"}`)
		apply(c, seq)
	}

	c, seq := request(domainipc.FocusWindowMethod, ref("2"))
	if got := apply(c, seq); len(got) != 8 || got[0] != "node_window" || got[2] != domainipc.Flow ||
		got[3] != "2" || strings.Join(got[4:], " ") != "1 1 2 2" {
		t.Fatalf("named target packet %v", got)
	}
	snap := func() domainipc.Snapshot {
		c, seq := request(domainipc.SnapshotMethod, "{}")
		return decode[domainipc.Snapshot](t, success(t, c, seq))
	}
	if ss := snap(); ss.Selection.Domain != "Dev" || ss.Selection.View != "" {
		t.Fatalf("named target selection %+v", ss.Selection)
	}

	// An Inbox window opens the Default Node, even when it is already shown.
	for i := 0; i < 2; i++ {
		c, seq = request(domainipc.FocusWindowMethod, ref("3"))
		if got := apply(c, seq); len(got) != 4 || got[0] != "home_node_window" || got[2] != domainipc.Flow || got[3] != "3" {
			t.Fatalf("Inbox target packet %v", got)
		}
		if ss := snap(); ss.Selection.View != "home_node" {
			t.Fatalf("Inbox target selection %+v", ss.Selection)
		}
	}

	// A window moved after a client listed it selects its new Node.
	// The Default Node is shown, so moving between hidden Nodes needs no WM apply.
	c, seq = request(domainipc.AssignMethod, `{"domain":"Review","window_id":2,"session":"`+sessionID+`"}`)
	success(t, c, seq)
	c, seq = request(domainipc.FocusWindowMethod, ref("2"))
	if got := apply(c, seq); got[0] != "node_window" || got[3] != "2" {
		t.Fatalf("moved target packet %v", got)
	}
	if ss := snap(); ss.Selection.Domain != "Review" {
		t.Fatalf("moved target selection %+v", ss.Selection)
	}

	send(t, wm, "close 2")
	for body, code := range map[string]string{
		ref("2"): "unknown_window",
		`{"session":"fedcba9876543210fedcba9876543210","window_id":1}`: "stale_session",
	} {
		c, seq = request(domainipc.FocusWindowMethod, body)
		if r := reply(t, c, seq); r.Error == nil || r.Error.Code != code {
			t.Fatalf("%s: expected %s, got %+v", body, code, r.Error)
		}
	}
	assertQuiet(t, wm)
	if ss := snap(); ss.Selection.Domain != "Review" {
		t.Fatalf("rejected target changed selection %+v", ss.Selection)
	}
	c, seq = request(domainipc.FocusWindowMethod, `{"session":"`+sessionID+`","window_id":0}`)
	if r := reply(t, c, seq); r.Error == nil || r.Error.Code != "invalid_body" {
		t.Fatalf("zero window accepted: %+v", r.Error)
	}
}

func TestFocusWindowNeedsCapableWM(t *testing.T) {
	s := newTestServer(t)
	wm := s.register()
	send(t, wm, "ready_node_parity")
	send(t, wm, "new 1")
	send(t, wm, "map 1")
	c := s.request(1, domainipc.FocusWindowMethod, `{"session":"`+sessionID+`","window_id":1}`)
	if r := reply(t, c, 1); r.Error == nil || r.Error.Code != "rejected" {
		t.Fatalf("older WM accepted a window target: %+v", r.Error)
	}
	assertQuiet(t, wm)
}

func TestUnassignReturnsWindowToInboxAndRefreshesShownNode(t *testing.T) {
	s := newTestServer(t)
	wm := s.register()
	send(t, wm, "ready_window_focus")
	var id uint64
	request := func(method, body string) (*net.UnixConn, uint64) {
		id++
		return s.request(id, method, body), id
	}
	ref := func(window string) string {
		return `{"session":"` + sessionID + `","window_id":` + window + `}`
	}
	apply := func(c *net.UnixConn, seq uint64) []string {
		fields := strings.Fields(read(t, wm))
		send(t, wm, "applied "+fields[1])
		success(t, c, seq)
		return fields
	}
	for _, name := range []string{"Dev", "Review"} {
		c, seq := request(domainipc.CreateMethod, `{"domain":"`+name+`"}`)
		success(t, c, seq)
	}
	for _, wi := range []string{"1", "2", "3"} {
		send(t, wm, "new "+wi)
		send(t, wm, "map "+wi)
		domain := "Dev"
		if wi == "3" {
			domain = "Review"
		}
		c, seq := request(domainipc.AssignMethod, `{"domain":"`+domain+`","window_id":`+wi+`,"session":"`+sessionID+`"}`)
		apply(c, seq)
	}
	c, seq := request(domainipc.FocusMethod, `{"domain":"Dev"}`)
	apply(c, seq)

	// Leaving the shown Node carries its remaining members for the WM.
	c, seq = request(domainipc.UnassignMethod, ref("2"))
	if got := apply(c, seq); strings.Join(got[2:], " ") != "2 project 1 1" || got[0] != "inbox" {
		t.Fatalf("shown Node unassign packet %v", got)
	}
	// A hidden Node only tells the WM which window joined Inbox.
	c, seq = request(domainipc.UnassignMethod, ref("3"))
	if got := apply(c, seq); strings.Join(got[2:], " ") != "3" || got[0] != "inbox" {
		t.Fatalf("hidden Node unassign packet %v", got)
	}
	// Already in Inbox: no WM work and no change.
	c, seq = request(domainipc.UnassignMethod, ref("3"))
	success(t, c, seq)
	assertQuiet(t, wm)

	c, seq = request(domainipc.SnapshotMethod, "{}")
	ss := decode[domainipc.Snapshot](t, success(t, c, seq))
	if ss.Selection.Domain != "Dev" {
		t.Fatalf("unassign changed the view: %+v", ss.Selection)
	}
	for _, w := range ss.Windows {
		if want := map[uint64]string{1: "Dev", 2: "", 3: ""}[w.ID]; w.Domain != want {
			t.Fatalf("window %d in %q, expected %q", w.ID, w.Domain, want)
		}
	}
	c, seq = request(domainipc.UnassignMethod, ref("9"))
	if r := reply(t, c, seq); r.Error == nil || r.Error.Code != "unknown_window" {
		t.Fatalf("unknown window accepted: %+v", r.Error)
	}
}

func TestLoadoutSetPublishesAndStaysOutOfMembership(t *testing.T) {
	s := newTestServer(t)
	sub := s.request(1, "subscribe", "{}")
	success(t, sub, 1)
	success(t, s.request(2, "create", `{"domain":"Dev"}`), 2)
	next(t, sub, 1, app.DomainCreated)
	body := `{"domain":"Dev","entries":["com.mitchellh.ghostty.desktop","org.gnome.Nautilus.desktop"]}`
	success(t, s.request(3, domainipc.LoadoutSetMethod, body), 3)
	got := decode[domainipc.LoadoutBody](t, next(t, sub, 2, app.NodeLoadoutChanged))
	if got.Domain != "Dev" || len(got.Entries) != 2 || got.Entries[1] != "org.gnome.Nautilus.desktop" {
		t.Fatalf("event %+v", got)
	}
	snap := decode[domainipc.Snapshot](t, success(t, s.request(4, "snapshot", "{}"), 4))
	if len(snap.NodeProfiles) != 1 || len(snap.NodeProfiles[0].Loadout) != 2 || len(snap.Windows) != 0 {
		t.Fatalf("snapshot %+v", snap)
	}
	success(t, s.request(5, domainipc.LoadoutSetMethod, `{"domain":"Dev","entries":[]}`), 5)
	if cleared := decode[domainipc.LoadoutBody](t, next(t, sub, 3, app.NodeLoadoutChanged)); len(cleared.Entries) != 0 {
		t.Fatalf("clear event %+v", cleared)
	}
	for id, bad := range map[uint64]string{
		6: `{"domain":"Nope","entries":[]}`,
		7: `{"domain":"Dev","entries":["sh -c evil"]}`,
		8: `{"domain":"Dev","entries":["a.desktop","b.desktop","c.desktop","d.desktop","e.desktop","f.desktop","g.desktop","h.desktop","i.desktop"]}`,
	} {
		if r := reply(t, s.request(id, domainipc.LoadoutSetMethod, bad), id); r.Error == nil {
			t.Fatalf("accepted %s", bad)
		}
	}
	assertQuiet(t, sub)
}
