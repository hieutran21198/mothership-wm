package main

import (
	"fmt"
	"slices"
	"strings"
	"sync"

	"mothership-wm/internal/domainipc"
)

// A loadout is what to launch, never which windows belong to a Node.
// Membership always comes from the launch marker or explicit assignment.

// launchedEntries remembers which desktop entry Mothership launched for a
// window it attached, for this WM session only.
type launchedEntries struct {
	mu       sync.Mutex
	session  string
	byWindow map[uint64]string
}

func (l *launchedEntries) record(session string, window uint64, entry string) {
	l.mu.Lock()
	defer l.mu.Unlock()
	if l.session != session || l.byWindow == nil {
		l.session, l.byWindow = session, map[uint64]string{}
	}
	l.byWindow[window] = entry
}

func (l *launchedEntries) lookup(session string, window uint64) string {
	l.mu.Lock()
	defer l.mu.Unlock()
	if l.session != session {
		return ""
	}
	return l.byWindow[window]
}

// loadoutMember is one live member and the entry that can relaunch it.
type loadoutMember struct {
	window domainipc.Window
	entry  string // Empty when it cannot be relaunched.
}

// nodeMembers lists a Node's mapped members in the order they opened and
// resolves each to a desktop entry: the recorded launch entry first, then a
// unique installed entry for the app ID.
func nodeMembers(s domainipc.Snapshot, node string, launched func(uint64) string, byAppID func(string) string) []loadoutMember {
	var out []loadoutMember
	for _, w := range s.Windows {
		if !w.Mapped || w.Domain != node || node == "" {
			continue
		}
		entry := launched(w.ID)
		if entry == "" && w.AppID != "" {
			entry = byAppID(w.AppID)
		}
		out = append(out, loadoutMember{window: w, entry: entry})
	}
	slices.SortFunc(out, func(a, b loadoutMember) int { return int(a.window.ID) - int(b.window.ID) })
	return out
}

// loadoutFromMembers returns the entries to save and the members it skips.
func loadoutFromMembers(members []loadoutMember) (entries []string, skipped []string) {
	for _, m := range members {
		if m.entry == "" {
			name := m.window.AppID
			if name == "" {
				name = fmt.Sprintf("Window %d", m.window.ID)
			}
			skipped = append(skipped, name)
			continue
		}
		if len(entries) < 8 {
			entries = append(entries, m.entry)
		}
	}
	return entries, skipped
}

// missingEntries keeps loadout order and counts repeats: two saved
// terminals with one open leaves one to launch.
func missingEntries(loadout []string, members []loadoutMember) []string {
	open := map[string]int{}
	for _, m := range members {
		if m.entry != "" {
			open[m.entry]++
		}
	}
	var out []string
	for _, e := range loadout {
		if open[e] > 0 {
			open[e]--
			continue
		}
		out = append(out, e)
	}
	return out
}

func profileLoadout(s domainipc.Snapshot, node string) []string {
	for _, p := range s.NodeProfiles {
		if p.Domain == node {
			return p.Loadout
		}
	}
	return nil
}

func loadoutSaveMessage(entries, skipped []string) string {
	saved := "Saved 1 app."
	if len(entries) != 1 {
		saved = fmt.Sprintf("Saved %d apps.", len(entries))
	}
	if len(entries) == 0 {
		saved = "No relaunchable apps to save."
	}
	if len(skipped) == 0 {
		return saved
	}
	return saved + " Not relaunchable: " + strings.Join(skipped, ", ") + "."
}

func appCount(n int) string {
	if n == 1 {
		return "1 app"
	}
	return fmt.Sprintf("%d apps", n)
}
