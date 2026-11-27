package main

import (
	"slices"

	"mothership-wm/internal/domainipc"
)

// beamTarget is one drop target in the tractor-beam strip. An empty node
// is the Default Node, which returns the window to Inbox.
type beamTarget struct {
	node, label, description string
	windows                  int
}

// beamTargets lists the Default Node (when enabled) and every named Node by
// name, with live member counts, and the index of the window's own Node.
func beamTargets(s domainipc.Snapshot, window uint64, includeDefault bool) ([]beamTarget, int) {
	counts := map[string]int{}
	from, found := "", false
	for _, w := range s.Windows {
		counts[w.Domain]++
		if w.ID == window {
			from, found = w.Domain, true
		}
	}
	descriptions := map[string]string{}
	for _, p := range s.NodeProfiles {
		descriptions[p.Domain] = p.Description
	}
	var out []beamTarget
	if includeDefault {
		out = append(out, beamTarget{label: "Default Node", description: "Inbox", windows: counts[""]})
	}
	names := slices.Clone(s.Domains)
	slices.Sort(names)
	for _, n := range names {
		out = append(out, beamTarget{node: n, label: n, description: descriptions[n], windows: counts[n]})
	}
	current := -1
	for i, t := range out {
		if found && t.node == from {
			current = i
		}
	}
	return out, current
}
