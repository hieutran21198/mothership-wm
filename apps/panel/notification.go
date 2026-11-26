package main

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"time"

	"mothership-wm/internal/domainipc"
	"mothership-wm/internal/userconfig"
)

const (
	toastCurrent = iota
	toastAll
	toastOff
)

const (
	noticeHome    = "@home"
	noticeDefault = "@default"
	noticeVoid    = "@void"
	noticeLimit   = 200
)

var toastNames = [...]string{"current_node", "all_nodes", "off"}

func loadToastMode(doc userconfig.Object, warn func(string, ...any)) int {
	obj, err := doc.Child("home_panel")
	if err != nil || obj["show_toasts"] == nil {
		return toastCurrent
	}
	var name string
	if json.Unmarshal(obj["show_toasts"], &name) == nil {
		for i, v := range toastNames {
			if v == name {
				return i
			}
		}
	}
	warn("home_panel.show_toasts must be current_node, all_nodes or off")
	return toastCurrent
}

type notification struct {
	Source, App, Title, Body string
	Received                 time.Time
	Read                     bool
	BusID                    uint32 `json:"-"`
	id                       uint32
}

type notificationLog struct {
	path    string
	entries []notification
	lastID  uint32
}

func openNotificationLog() notificationLog {
	dir := os.Getenv("XDG_STATE_HOME")
	if !filepath.IsAbs(dir) {
		home, _ := os.UserHomeDir()
		if home == "" {
			return notificationLog{}
		}
		dir = filepath.Join(home, ".local", "state")
	}
	l := notificationLog{path: filepath.Join(dir, "mothership-wm", "notifications.json")}
	if st, err := os.Lstat(l.path); err != nil || !st.Mode().IsRegular() || st.Size() > 256<<10 {
		return l
	}
	if b, err := os.ReadFile(l.path); err == nil {
		if json.Unmarshal(b, &l.entries) != nil {
			l.entries = nil
		}
		if len(l.entries) > noticeLimit {
			l.entries = l.entries[:noticeLimit]
		}
		for i := range l.entries {
			l.lastID++
			l.entries[i].id = l.lastID
		}
	}
	return l
}

func (l *notificationLog) prepend(v notification) {
	if v.BusID != 0 {
		for i, prev := range l.entries {
			if prev.BusID == v.BusID {
				v.Source = prev.Source
				v.id = prev.id
				l.entries = append(l.entries[:i], l.entries[i+1:]...)
				break
			}
		}
	}
	if v.id == 0 {
		l.lastID++
		v.id = l.lastID
	}
	l.entries = append([]notification{v}, l.entries...)
	if len(l.entries) > noticeLimit {
		l.entries = l.entries[:noticeLimit]
	}
	l.save()
}

func (l *notificationLog) markRead(source string) {
	for i := range l.entries {
		if source == "" || l.entries[i].Source == source {
			l.entries[i].Read = true
		}
	}
	l.save()
}

func (l *notificationLog) markEntryRead(id uint32) bool {
	for i := range l.entries {
		if l.entries[i].id == id && !l.entries[i].Read {
			l.entries[i].Read = true
			l.save()
			return true
		}
	}
	return false
}

func (l *notificationLog) deleteEntry(id uint32) error {
	return l.remove(func(n notification) bool { return n.id == id })
}

// An empty source means all entries. Do not change the live log when the
// reduced history cannot be saved.
func (l *notificationLog) clear(source string) error {
	return l.remove(func(n notification) bool { return source == "" || n.Source == source })
}

func (l *notificationLog) remove(match func(notification) bool) error {
	kept := make([]notification, 0, len(l.entries))
	for _, n := range l.entries {
		if !match(n) {
			kept = append(kept, n)
		}
	}
	if len(kept) == len(l.entries) {
		return nil
	}
	if l.path != "" {
		if err := userconfig.Write(l.path, kept); err != nil {
			return err
		}
	}
	l.entries = kept
	return nil
}

func (l *notificationLog) save() {
	if l.path == "" {
		return
	}
	size := 2
	for i, n := range l.entries {
		row, _ := json.MarshalIndent(n, "", "  ")
		size += len(row) + 16
		if size > 250<<10 {
			l.entries = l.entries[:i]
			break
		}
	}
	if err := userconfig.Write(l.path, l.entries); err != nil {
		fmt.Fprintf(os.Stderr, "mothership notification log: %v\n", err)
	}
}

func notificationSource(s domainipc.Snapshot, pid int) string {
	if !s.WMReady || pid <= 0 {
		return noticeVoid
	}
	source := ""
	for _, w := range s.Windows {
		if !w.Mapped || w.PID != pid {
			continue
		}
		node := w.Domain
		if node == "" {
			node = noticeDefault
		}
		if source != "" && source != node {
			return noticeVoid
		}
		source = node
	}
	if source == "" {
		return noticeVoid
	}
	return source
}

func noticeText(s string, limit int) string {
	s = strings.Join(strings.Fields(s), " ")
	if len(s) <= limit {
		return s
	}
	end := 0
	for i := range s {
		if i > limit-len("…") {
			break
		}
		end = i
	}
	return s[:end] + "…"
}

func showToast(mode int, source, shownNode string) bool {
	return mode == toastAll || (mode == toastCurrent && source != noticeVoid && source == shownNode)
}
