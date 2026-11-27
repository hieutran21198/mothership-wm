package main

import (
	"cmp"
	"fmt"
	"slices"
	"strconv"
	"strings"

	"mothership-wm/internal/domainipc"
)

// windowListRow is one live mapped toplevel reported by the WM over the
// private shell protocol. Titles never leave the Panel process.
type windowListRow struct {
	id           uint64
	title, appID string
}

// windowSearchRow joins a WM row with desktopd membership for Slash.
type windowSearchRow struct {
	windowListRow
	node  string // Empty for an Inbox window shown as Default.
	key   int    // Node number key, or 0 when the Node has none.
	inbox bool
}

// parseWindowList reads "id\ttitle\tapp_id" lines. The C side replaces tabs
// and line breaks inside titles before it builds the text.
func parseWindowList(text string) []windowListRow {
	var rows []windowListRow
	for line := range strings.SplitSeq(text, "\n") {
		fields := strings.SplitN(line, "\t", 3)
		if len(fields) != 3 {
			continue
		}
		id, err := strconv.ParseUint(fields[0], 10, 64)
		if err != nil || id == 0 {
			continue
		}
		rows = append(rows, windowListRow{id: id, title: fields[1], appID: fields[2]})
	}
	return rows
}

// windowSearchRows keeps only windows desktopd knows as mapped in the current
// session, labels each with its Node, and orders Nodes by switcher recency,
// then unvisited Nodes by name, then titles. Inbox windows are listed only
// while the Default Node is enabled, because only then can they be shown.
func windowSearchRows(s domainipc.Snapshot, recent []string, rows []windowListRow, includeDefault bool) []windowSearchRow {
	members := make(map[uint64]domainipc.Window, len(s.Windows))
	for _, w := range s.Windows {
		if w.Mapped {
			members[w.ID] = w
		}
	}
	keys := make(map[string]int, len(s.NodeProfiles))
	for _, p := range s.NodeProfiles {
		keys[p.Domain] = p.Shortcut
	}
	rank := func(node string) int {
		if i := slices.Index(recent, node); i >= 0 {
			return i
		}
		return len(recent) + 1
	}
	out := make([]windowSearchRow, 0, len(rows))
	for _, row := range rows {
		w, ok := members[row.id]
		if !ok || (w.Domain == "" && !includeDefault) {
			continue
		}
		r := windowSearchRow{windowListRow: row, node: w.Domain, inbox: w.Domain == ""}
		if r.inbox {
			r.key = 1
		} else {
			r.key = keys[w.Domain]
		}
		out = append(out, r)
	}
	slices.SortStableFunc(out, func(a, b windowSearchRow) int {
		if c := cmp.Compare(rank(a.node), rank(b.node)); c != 0 {
			return c
		}
		// Unvisited Nodes: named ones by name, the Default Node last.
		if a.node != b.node {
			if a.inbox != b.inbox {
				if a.inbox {
					return 1
				}
				return -1
			}
			return cmp.Compare(strings.ToLower(a.node), strings.ToLower(b.node))
		}
		if c := cmp.Compare(strings.ToLower(a.title), strings.ToLower(b.title)); c != 0 {
			return c
		}
		return cmp.Compare(a.id, b.id)
	})
	return out
}

// encodeWindowSearch returns "id\tnode\tkey\ttitle\tapp_id" lines for C.
func encodeWindowSearch(rows []windowSearchRow) string {
	var b strings.Builder
	for _, r := range rows {
		node := r.node
		if r.inbox {
			node = "Default"
		}
		fmt.Fprintf(&b, "%d\t%s\t%d\t%s\t%s\n", r.id, node, r.key, r.title, r.appID)
	}
	return b.String()
}

// windowSearch joins rows under the controller lock with the latest state.
func (c *controller) windowSearch(rows []windowListRow, includeDefault bool) []windowSearchRow {
	c.mu.Lock()
	defer c.mu.Unlock()
	if !c.online || !c.state.WMReady {
		return nil
	}
	return windowSearchRows(c.state, c.nodeSwitch.recent, rows, includeDefault)
}

// windowCommandError rewrites desktopd's window errors for the shell.
func windowCommandError(err error) error {
	if err == nil {
		return nil
	}
	switch err.Error() {
	case "unknown Window ID":
		return fmt.Errorf("Window closed.")
	case "unknown WM session":
		return fmt.Errorf("The session changed. Search again.")
	}
	return err
}
