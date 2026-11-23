package unixapi

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"strconv"
	"strings"
	"time"

	"mothership-wm/internal/domainipc"
)

// Request keeps the existing CLI syntax and output. Its wire uses the same
// JSON request/response contract as other desktopd clients.
func Request(ctx context.Context, path string, args []string) (string, error) {
	if len(args) == 0 {
		return "", errors.New("expected list, create, assign, focus or home")
	}
	for _, s := range args {
		if !validWord(s) {
			return "", errors.New("invalid command argument")
		}
	}
	method := args[0]
	var body any
	switch {
	case method == "home" && len(args) == 1:
		body = domainipc.Empty{}
	case method == "list" && len(args) == 1:
		method, body = domainipc.SnapshotMethod, domainipc.Empty{}
	case (method == "create" || method == "focus") && len(args) == 2:
		body = domainipc.DomainBody{Domain: args[1]}
	case method == "assign" && len(args) == 4:
		id, err := strconv.ParseUint(args[2], 10, 64)
		if err != nil || id == 0 {
			return "", errors.New("invalid Window ID")
		}
		if !validSession(args[1]) {
			return "", errors.New("unknown WM session")
		}
		body = domainipc.AssignBody{Session: args[1], WindowID: id, Domain: args[3]}
	default:
		return "", errors.New("expected list, create, assign, focus or home")
	}
	c, err := domainipc.Dial(ctx, path)
	if err != nil {
		return "", err
	}
	defer c.Close()
	stop := context.AfterFunc(ctx, func() { c.Close() })
	defer stop()
	_ = c.SetReadDeadline(time.Now().Add(2 * domainipc.Timeout))
	raw, err := json.Marshal(body)
	if err != nil {
		return "", err
	}
	const id = 1 // one request on this connection
	if err := domainipc.WriteJSON(c, domainipc.Request{ID: id, Method: method, Body: raw}); err != nil {
		return "", err
	}
	packet, err := domainipc.Read(c)
	if err != nil {
		return "", err
	}
	reply, err := domainipc.Decode[domainipc.Response]([]byte(packet))
	if err != nil || reply.ID != id {
		return "", errors.New("invalid desktopd response")
	}
	if reply.Error != nil {
		return "", errors.New(reply.Error.Message)
	}
	if method != domainipc.SnapshotMethod {
		return "ok", nil
	}
	s, err := domainipc.Decode[domainipc.Snapshot](reply.Body)
	if err != nil {
		return "", err
	}
	if !s.WMReady {
		return "", errors.New("WM is not ready")
	}
	var out strings.Builder
	fmt.Fprintf(&out, "session %s", s.Session)
	for _, w := range s.Windows {
		mapping := "unmapped"
		if w.Mapped {
			mapping = "mapped"
		}
		line := fmt.Sprintf("\nwindow %d %s %s", w.ID, mapping, w.Domain)
		out.WriteString(strings.TrimRight(line, " "))
	}
	return out.String(), nil
}
