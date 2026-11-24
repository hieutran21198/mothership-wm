package domainipc

import (
	"bytes"
	"encoding/json"
	"errors"
	"io"
	"net"
)

const (
	SnapshotMethod  = "snapshot"
	SubscribeMethod = "subscribe"
	CreateMethod    = "create"
	AssignMethod    = "assign"
	FocusMethod     = "focus"
	HomeMethod      = "home"
	OverviewMethod  = "overview"
	LayoutMethod    = "layout"
	NextMethod      = "stack.next"
	MapShowMethod   = "map.show"
	MapHideMethod   = "map.hide"
	MapChangedEvent = "map.visibility.changed"
)

type Request struct {
	ID     uint64          `json:"id"`
	Method string          `json:"method"`
	Body   json.RawMessage `json:"body"`
}

type Response struct {
	ID    uint64          `json:"id"`
	Body  json.RawMessage `json:"body,omitempty"`
	Error *Failure        `json:"error,omitempty"`
}

type Failure struct {
	Code    string `json:"code"`
	Message string `json:"message"`
}

type Empty struct{}
type MapVisibility struct {
	Visible bool `json:"visible"`
}

type NodeProfile struct {
	Domain      string `json:"domain"`
	Description string `json:"description,omitempty"`
	CreatedAt   int64  `json:"created_at,omitempty"`
	LastActive  int64  `json:"last_active,omitempty"`
}

type CreateBody struct {
	Domain      string `json:"domain"`
	Description string `json:"description,omitempty"`
}

type DomainBody struct {
	Domain string `json:"domain"`
}

type LayoutBody struct {
	Domain string `json:"domain"`
	Mode   string `json:"mode"`
}

const (
	Tiling = "tiling"
	Stack  = "stack"
	Float  = "float"
)

func ValidLayout(mode string) bool {
	return mode == Tiling || mode == Stack || mode == Float
}

type AssignBody struct {
	Session  string `json:"session"`
	WindowID uint64 `json:"window_id"`
	Domain   string `json:"domain"`
}

type Selection struct {
	View       string `json:"view,omitempty"`
	State      string `json:"state"`
	Domain     string `json:"domain,omitempty"`
	Mode       string `json:"mode,omitempty"`
	LastActive int64  `json:"last_active,omitempty"`
}

type Window struct {
	ID     uint64 `json:"id"`
	Domain string `json:"domain,omitempty"`
	Mapped bool   `json:"mapped"`
	AppID  string `json:"app_id,omitempty"`
}

type Snapshot struct {
	Revision     uint64        `json:"revision"`
	WMReady      bool          `json:"wm_ready"`
	Session      string        `json:"session,omitempty"`
	Domains      []string      `json:"domains"`
	NodeProfiles []NodeProfile `json:"node_profiles,omitempty"`
	Windows      []Window      `json:"windows"`
	MapVisible   bool          `json:"map_visible,omitempty"`
	Selection    Selection     `json:"selection"`
}

type Event struct {
	Revision uint64          `json:"revision"`
	Event    string          `json:"event"`
	Body     json.RawMessage `json:"body"`
}

type WMChangedBody struct {
	WMReady   bool      `json:"wm_ready"`
	Session   string    `json:"session,omitempty"`
	Selection Selection `json:"selection"`
}

type WindowClosedBody struct {
	WindowID uint64 `json:"window_id"`
}

// Decode requires exactly one JSON object and does not ignore misspelled fields.
func Decode[T any](packet []byte) (T, error) {
	var v T
	packet = bytes.TrimSpace(packet)
	if len(packet) == 0 || packet[0] != '{' {
		return v, errors.New("expected JSON object")
	}
	decoder := json.NewDecoder(bytes.NewReader(packet))
	decoder.DisallowUnknownFields()
	if err := decoder.Decode(&v); err != nil {
		return v, err
	}
	if decoder.Decode(new(json.RawMessage)) != io.EOF {
		return v, errors.New("expected one object per packet")
	}
	return v, nil
}

func WriteJSON(c *net.UnixConn, value any) error {
	packet, err := json.Marshal(value)
	if err != nil {
		return err
	}
	return Write(c, string(packet))
}
