package domainipc

import (
	"bytes"
	"encoding/json"
	"errors"
	"io"
	"net"
)

const (
	SnapshotMethod      = "snapshot"
	SubscribeMethod     = "subscribe"
	CreateMethod        = "create"
	DeleteMethod        = "delete"
	DomainDeletedEvent  = "domain.deleted"
	AssignMethod        = "assign"
	UnassignMethod      = "unassign"
	FocusMethod         = "focus"
	FocusWindowMethod   = "focus.window"
	PrepareFocusMethod  = "focus.prepare"
	RevealFocusMethod   = "focus.reveal"
	HomeMethod          = "home"
	DefaultNodeMethod   = "home.node"
	OverviewMethod      = "overview"
	LayoutMethod        = "layout"
	NextMethod          = "stack.next"
	MapShowMethod       = "map.show"
	MapHideMethod       = "map.hide"
	MapChangedEvent     = "map.visibility.changed"
	LoadoutSetMethod    = "loadout.set"
	MissionsGetMethod   = "missions.get"
	MissionsClearMethod = "missions.clear"
	NodeLoadoutEvent    = "node.loadout"
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
	Domain      string   `json:"domain"`
	Description string   `json:"description,omitempty"`
	CreatedAt   int64    `json:"created_at,omitempty"`
	LastActive  int64    `json:"last_active,omitempty"`
	Shortcut    int      `json:"shortcut,omitempty"`
	Loadout     []string `json:"loadout,omitempty"`
}

// MissionDayBody asks for one local day, as YYYY-MM-DD.
type MissionDayBody struct {
	Day string `json:"day"`
}

// MissionLane names a Node, or the Default Node, in a Mission Log day.
type MissionLane struct {
	Node        string `json:"node,omitempty"`
	DefaultNode bool   `json:"default_node,omitempty"`
}

// MissionDay is one day's time in Node views. Each segment is
// [lane index, start, end] in Unix seconds; Open has end 0. Merged reports
// that short gaps inside a lane were joined to fit one packet.
type MissionDay struct {
	Day      string        `json:"day"`
	Lanes    []MissionLane `json:"lanes"`
	Segments [][3]int64    `json:"segments"`
	Open     *[3]int64     `json:"open,omitempty"`
	Merged   bool          `json:"merged,omitempty"`
}

// LoadoutBody sets a Node's loadout (an empty list clears it) and is the
// node.loadout event body.
type LoadoutBody struct {
	Domain  string   `json:"domain"`
	Entries []string `json:"entries"`
}

type CreateBody struct {
	Domain      string `json:"domain"`
	Description string `json:"description,omitempty"`
}

type DeleteBody struct {
	Domain string `json:"domain"`
	Force  bool   `json:"force,omitempty"`
}

type FocusTicket struct {
	Ticket uint64 `json:"ticket"`
}

type DomainBody struct {
	Domain string `json:"domain"`
}

type LayoutBody struct {
	Domain      string `json:"domain"`
	Mode        string `json:"mode"`
	RestoreFlow bool   `json:"restore_flow,omitempty"`
}

const (
	Flow           = "flow"
	FlowHorizontal = "flow-horizontal"
	FlowVertical   = "flow-vertical"
	Stage          = "stage"
	Tiling         = "tiling"
	Stack          = "stack"
	Float          = "float"
)

func ValidLayout(mode string) bool {
	return IsFlow(mode) || mode == Stage || mode == Tiling || mode == Stack || mode == Float
}

func IsFlow(mode string) bool {
	return mode == Flow || mode == FlowHorizontal || mode == FlowVertical
}

type AssignBody struct {
	Session  string `json:"session"`
	WindowID uint64 `json:"window_id"`
	Domain   string `json:"domain"`
}

// WindowRef names one live window in the current WM session.
type WindowRef struct {
	Session  string `json:"session"`
	WindowID uint64 `json:"window_id"`
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
	PID    int    `json:"pid,omitempty"`
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
