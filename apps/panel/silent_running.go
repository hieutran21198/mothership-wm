package main

import (
	"encoding/json"
	"fmt"
	"sort"
	"strings"

	"mothership-wm/internal/userconfig"
)

// Silent running is a Panel-owned per-Node preference: while that Node is
// the acknowledged view, toasts are held, and optionally other Nodes' audio
// is muted. It never changes membership, focus or selection.
const (
	silentOff = iota
	silentNotices
	silentAudio
)

var silentNames = [...]string{"off", "notices", "notices_audio"}

func silentModeName(mode int) string {
	if mode < silentOff || mode > silentAudio {
		return silentNames[silentOff]
	}
	return silentNames[mode]
}

func parseSilentMode(raw json.RawMessage) (int, bool) {
	var name string
	if json.Unmarshal(raw, &name) != nil {
		return silentOff, false
	}
	for i, v := range silentNames {
		if v == name {
			return i, true
		}
	}
	return silentOff, false
}

// loadSilentRunning reads the sparse `nodes.<name>.silent_running` and
// `default_node.silent_running` keys. The Default Node uses the reserved
// `@default` key in memory because a named Node may itself be called
// "default". Only enabled modes are kept.
func loadSilentRunning(doc userconfig.Object, warn func(string, ...any)) map[string]int {
	modes := map[string]int{}
	read := func(obj userconfig.Object, where, key string) {
		raw, ok := obj["silent_running"]
		if !ok {
			return
		}
		mode, valid := parseSilentMode(raw)
		if !valid {
			warn("%s.silent_running must be off, notices or notices_audio", where)
			return
		}
		if mode != silentOff {
			modes[key] = mode
		}
	}
	if def, err := doc.Child("default_node"); err != nil {
		warn("default_node: %v", err)
	} else {
		read(def, "default_node", noticeDefault)
	}
	nodes, err := doc.Child("nodes")
	if err != nil {
		warn("nodes: %v", err)
		return modes
	}
	for name := range nodes {
		node, err := nodes.Child(name)
		if err != nil {
			warn("nodes.%s: %v", name, err)
			continue
		}
		read(node, "nodes."+name, name)
	}
	return modes
}

// saveSilentRunning writes one Node's mode and keeps the file sparse: Off
// removes the key, and an emptied per-Node object is removed too.
func (p *panelPreferences) saveSilentRunning(path, view string, mode int) error {
	if mode < silentOff || mode > silentAudio {
		return fmt.Errorf("Unknown Silent running mode")
	}
	if view == "" || view == noticeHome || view == noticeVoid {
		return fmt.Errorf("Silent running applies to a Node")
	}
	doc, _, err := userconfig.Read(path)
	if err != nil {
		return err
	}
	update := func(obj userconfig.Object) error {
		if mode == silentOff {
			delete(obj, "silent_running")
			return nil
		}
		return obj.Set("silent_running", silentModeName(mode))
	}
	if view == noticeDefault {
		def, err := doc.Child("default_node")
		if err != nil {
			return err
		}
		if err := update(def); err != nil {
			return err
		}
		if len(def) == 0 {
			delete(doc, "default_node")
		} else if err := doc.Set("default_node", def); err != nil {
			return err
		}
	} else {
		nodes, err := doc.Child("nodes")
		if err != nil {
			return err
		}
		node, err := nodes.Child(view)
		if err != nil {
			return err
		}
		if err := update(node); err != nil {
			return err
		}
		if len(node) == 0 {
			delete(nodes, view)
		} else if err := nodes.Set(view, node); err != nil {
			return err
		}
		if len(nodes) == 0 {
			delete(doc, "nodes")
		} else if err := doc.Set("nodes", nodes); err != nil {
			return err
		}
	}
	if err := userconfig.Write(path, doc); err != nil {
		return err
	}
	if p.silent == nil {
		p.silent = map[string]int{}
	}
	if mode == silentOff {
		delete(p.silent, view)
	} else {
		p.silent[view] = mode
	}
	return nil
}

// silentNodes lists named Nodes with Silent running for the Center Map.
func silentNodes(modes map[string]int) string {
	var names []string
	for name, mode := range modes {
		if mode != silentOff && name != noticeDefault {
			names = append(names, name)
		}
	}
	sort.Strings(names)
	return strings.Join(names, "\n")
}

// silentHold follows the acknowledged view. A hold covers one Node from
// entry until another view is acknowledged or its mode is turned off.
type silentHold struct {
	node string
	mode int
	held int
}

// apply moves the hold to view with that view's mode. It reports how many
// notices a hold that just ended had held, and whether one ended.
func (h *silentHold) apply(view string, mode int) (held int, ended bool) {
	if h.node != "" && (h.node != view || mode == silentOff) {
		held, ended = h.held, true
		*h = silentHold{}
	}
	if mode != silentOff && view != "" && view != noticeHome && view != noticeVoid {
		if h.node != view {
			*h = silentHold{node: view}
		}
		h.mode = mode
	}
	return held, ended
}

// hold counts one arriving notice and reports whether its toast is held.
func (h *silentHold) hold() bool {
	if h.node == "" {
		return false
	}
	h.held++
	return true
}

// audioTarget names the Node whose audio keeps playing, or "" for none.
func (h *silentHold) audioTarget() string {
	if h.mode == silentAudio {
		return h.node
	}
	return ""
}

func silentDigest(held int) string {
	if held == 1 {
		return "1 notice held"
	}
	return fmt.Sprintf("%d notices held", held)
}
