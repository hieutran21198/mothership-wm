package main

import (
	"cmp"
	"slices"

	"mothership-wm/internal/domainipc"
)

type switchNodeRow struct {
	name, description string
	windows           int
}

type nodeSwitchView struct {
	rows              []switchNodeRow
	current, selected int
}

type nodeSwitch struct {
	session string
	recent  []string // An empty name identifies the reserved Default Node.

	held           bool
	origin         domainipc.Selection
	mapVisible     bool
	includeDefault bool
	order          []string
	current        int
	selected       int
}

func selectedSwitchNode(s domainipc.Selection) (string, bool) {
	if s.State != "acknowledged" {
		return "", false
	}
	return s.Domain, s.Domain != "" || s.View == "home_node"
}

func (n *nodeSwitch) reconcile(s domainipc.Snapshot, usable, pending bool) bool {
	sessionChanged := n.session != s.Session
	if sessionChanged {
		n.session = s.Session
		n.recent = nil
	}
	if usable {
		n.recent = slices.DeleteFunc(n.recent, func(name string) bool {
			return name != "" && !slices.Contains(s.Domains, name)
		})
		name, selected := selectedSwitchNode(s.Selection)
		if selected && (name == "" || slices.Contains(s.Domains, name)) &&
			(len(n.recent) == 0 || n.recent[0] != name) {
			n.recent = slices.DeleteFunc(n.recent, func(old string) bool { return old == name })
			n.recent = append([]string{name}, n.recent...)
		}
	}
	if n.order == nil {
		return false
	}
	if !usable || pending || sessionChanged || n.origin != s.Selection || n.mapVisible != s.MapVisible {
		n.order = nil
		return true
	}
	for _, name := range n.order {
		if name != "" && !slices.Contains(s.Domains, name) {
			n.order = nil
			return true
		}
	}
	return false
}

func (n *nodeSwitch) step(s domainipc.Snapshot, direction int, allowed, includeDefault bool) *nodeSwitchView {
	if !n.held {
		n.held = true
		if !allowed {
			return nil
		}
		n.origin, n.mapVisible, n.includeDefault = s.Selection, s.MapVisible, includeDefault
		n.order = make([]string, 0, len(s.Domains)+1)
		for _, name := range n.recent {
			if name != "" || includeDefault {
				n.order = append(n.order, name)
			}
		}
		unseen := slices.Clone(s.Domains)
		if includeDefault {
			unseen = append(unseen, "")
		}
		slices.SortFunc(unseen, func(a, b string) int {
			return cmp.Compare(switchNodeName(a), switchNodeName(b))
		})
		for _, name := range unseen {
			if !slices.Contains(n.order, name) {
				n.order = append(n.order, name)
			}
		}
		n.current = -1
		if name, ok := selectedSwitchNode(s.Selection); ok {
			n.current = slices.Index(n.order, name)
		}
		if len(n.order) == 0 || (len(n.order) == 1 && n.current == 0) {
			n.order = nil
			return nil
		}
		n.selected = n.current
		if n.current < 0 && direction < 0 {
			n.selected = 0
		}
	}
	if !allowed || includeDefault != n.includeDefault {
		n.order = nil
	}
	if len(n.order) == 0 {
		return nil
	}
	n.selected = (n.selected + direction + len(n.order)) % len(n.order)
	descriptions := make(map[string]string, len(s.NodeProfiles))
	for _, profile := range s.NodeProfiles {
		descriptions[profile.Domain] = profile.Description
	}
	counts := make(map[string]int)
	for _, window := range s.Windows {
		if window.Mapped {
			counts[window.Domain]++
		}
	}
	rows := make([]switchNodeRow, len(n.order))
	for i, name := range n.order {
		row := switchNodeRow{name: switchNodeName(name), description: descriptions[name], windows: counts[name]}
		if name == "" {
			row.description = "Unassigned windows"
		}
		rows[i] = row
	}
	return &nodeSwitchView{rows: rows, current: n.current, selected: n.selected}
}

func switchNodeName(name string) string {
	if name == "" {
		return "Default Node"
	}
	return name
}

func (n *nodeSwitch) finish(commit, includeDefault bool) (string, bool) {
	defer func() {
		n.held, n.order = false, nil
	}()
	if !commit || len(n.order) == 0 || n.selected == n.current || n.includeDefault != includeDefault {
		return "", false
	}
	return n.order[n.selected], true
}

func (c *controller) stepNodeSwitch(direction int, allowed, includeDefault bool) {
	if direction != -1 && direction != 1 {
		return
	}
	c.mu.Lock()
	defer c.mu.Unlock()
	allowed = allowed && c.online && c.state.WMReady && !c.pending && c.state.Selection.State == "acknowledged"
	c.showSwitch(c.nodeSwitch.step(c.state, direction, allowed, includeDefault))
}

func (c *controller) finishNodeSwitch(commit, includeDefault bool) {
	c.mu.Lock()
	defer c.mu.Unlock()
	name, selected := c.nodeSwitch.finish(commit && c.online && c.state.WMReady && !c.pending, includeDefault)
	c.showSwitch(nil)
	if !selected {
		return
	}
	if name == "" {
		c.startCommand(domainipc.DefaultNodeMethod, domainipc.Empty{}, false, 0, "", nil)
	} else {
		c.startCommand(domainipc.FocusMethod, domainipc.DomainBody{Domain: name}, false, 0, "", nil)
	}
}
