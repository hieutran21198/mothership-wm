//go:build linux

package main

import (
	"bufio"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"time"

	"mothership-wm/internal/userconfig"
)

const (
	defaultHomeWidth = 840
	minHomeWidth     = 520
	maxHomeWidth     = 1440
)

func loadHomeWidth(doc userconfig.Object, warn func(string, ...any)) int {
	obj, err := doc.Child("home_panel")
	width := defaultHomeWidth
	if err == nil && obj["width"] != nil {
		// Pointer distinguishes a legitimate JSON integer from null.
		var value *int
		if err = json.Unmarshal(obj["width"], &value); err == nil {
			if value == nil || *value < minHomeWidth || *value > maxHomeWidth {
				err = fmt.Errorf("expected an integer %d–%d", minHomeWidth, maxHomeWidth)
			} else {
				width = *value
			}
		}
	}
	if err != nil {
		warn("home_panel.width: %v", err)
	}
	return width
}

func loadHomeGuide(doc userconfig.Object, warn func(string, ...any)) bool {
	obj, err := doc.Child("home_panel")
	if err != nil || obj["show_keyboard_guide"] == nil {
		return true
	}
	var value *bool
	if err := json.Unmarshal(obj["show_keyboard_guide"], &value); err != nil || value == nil {
		warn("home_panel.show_keyboard_guide must be true or false")
		return true
	}
	return *value
}

// A first save also migrates existing legacy Map preferences
// so that creating the canonical file does not discard the current Map.
func (p *panelPreferences) saveHomeWidth(path string, width int) error {
	return p.saveHomeSettings(path, width, nil, nil)
}

func (p *panelPreferences) saveHomeSettings(path string, width int, video, guide *bool) error {
	return p.saveHomeAndToastSettings(path, width, video, guide, nil)
}

func (p *panelPreferences) saveHomeAndToastSettings(path string, width int, video, guide *bool, mode *int) error {
	return p.saveHomePreferences(path, width, video, guide, mode, nil)
}

func (p *panelPreferences) saveHomePreferences(path string, width int, video, guide *bool, mode, cursor *int) error {
	if cursor != nil && !userconfig.ValidCursorSize(*cursor) {
		return fmt.Errorf("invalid cursor size")
	}
	if mode != nil && (*mode < 0 || *mode >= len(toastNames)) {
		return fmt.Errorf("invalid toast mode")
	}
	if width < minHomeWidth || width > maxHomeWidth {
		return fmt.Errorf("expected width %d–%d", minHomeWidth, maxHomeWidth)
	}
	doc, exists, err := userconfig.Read(path)
	if err != nil {
		return err
	}
	obj, err := doc.Child("home_panel")
	if err != nil {
		return err
	}
	if !exists && p.usedLegacy {
		center, _ := doc.Child("center_map")
		if err := patchMapSettings(center, p.settingsBase, p.settings); err != nil {
			return err
		}
		if len(center) != 0 {
			if err := doc.Set("center_map", center); err != nil {
				return err
			}
		}
	}
	homeChanged := false
	if _, present := obj["width"]; width != p.homeWidth || present {
		if err := obj.Set("width", width); err != nil {
			return err
		}
		homeChanged = true
	}
	if guide != nil && *guide != p.homeGuide {
		if err := obj.Set("show_keyboard_guide", *guide); err != nil {
			return err
		}
		homeChanged = true
	}
	if mode != nil && *mode != p.toastMode {
		if err := obj.Set("show_toasts", toastNames[*mode]); err != nil {
			return err
		}
		homeChanged = true
	}
	if cursor != nil && *cursor != p.cursorSize {
		if err := obj.Set("cursor_size", *cursor); err != nil {
			return err
		}
		homeChanged = true
	}
	if homeChanged {
		if err := doc.Set("home_panel", obj); err != nil {
			return err
		}
	}
	if video != nil && *video != p.wallpaper.allowsVideo() {
		wall, err := doc.Child("wallpaper")
		if err != nil {
			return err
		}
		if err := wall.Set("video_enabled", *video); err != nil {
			return err
		}
		if err := doc.Set("wallpaper", wall); err != nil {
			return err
		}
	}
	if !exists && len(doc) == 0 {
		return nil
	}
	if err := userconfig.Write(path, doc); err != nil {
		return err
	}
	p.homeWidth, p.usedLegacy = width, false
	if cursor != nil {
		p.cursorSize = *cursor
	}
	if mode != nil {
		p.toastMode = *mode
	}
	if guide != nil {
		p.homeGuide = *guide
	}
	if video != nil && *video != p.wallpaper.allowsVideo() {
		p.wallpaper.VideoEnabled = video
	}
	return nil
}

type cpuCounter struct{ busy, total uint64 }

func cpuCounters(r io.Reader) map[string]cpuCounter {
	values := make(map[string]cpuCounter)
	s := bufio.NewScanner(r)
	for s.Scan() {
		f := strings.Fields(s.Text())
		if len(f) < 9 || !strings.HasPrefix(f[0], "cpu") {
			continue
		}
		if f[0] != "cpu" {
			if _, err := strconv.Atoi(strings.TrimPrefix(f[0], "cpu")); err != nil {
				continue
			}
		}
		var c cpuCounter
		ok := true
		for i, field := range f[1:9] { // guest counters already appear in user/nice
			n, err := strconv.ParseUint(field, 10, 64)
			if err != nil {
				ok = false
				break
			}
			c.total += n
			if i != 3 && i != 4 {
				c.busy += n
			}
		}
		if ok {
			values[f[0]] = c
		}
	}
	return values
}

func orderedCPUs(values map[string]cpuCounter) []string {
	keys := make([]string, 0, len(values))
	for k := range values {
		if k != "cpu" {
			keys = append(keys, k)
		}
	}
	sort.Slice(keys, func(i, j int) bool {
		a, _ := strconv.Atoi(keys[i][3:])
		b, _ := strconv.Atoi(keys[j][3:])
		return a < b
	})
	return keys
}

func cpuPercent(old, current map[string]cpuCounter, key string) int {
	b, ok := old[key]
	a, present := current[key]
	if !ok || !present || a.total <= b.total || a.busy < b.busy || a.busy-b.busy > a.total-b.total {
		return -1
	}
	return int(100 * (a.busy - b.busy) / (a.total - b.total))
}

// A physical core combines the busy and total time of its online siblings.
func physicalCPUUsage(old, current map[string]cpuCounter, topology map[string]string) []int {
	var order []string
	sums := make(map[string]cpuCounter)
	for _, key := range orderedCPUs(current) {
		id := topology[key]
		if id == "" {
			id = key
		}
		if _, known := sums[id]; !known {
			order = append(order, id)
			sums[id] = cpuCounter{}
		}
		if previous, known := old[key]; known && cpuPercent(old, current, key) >= 0 {
			v, sum := current[key], sums[id]
			sums[id] = cpuCounter{sum.busy + v.busy - previous.busy, sum.total + v.total - previous.total}
		}
	}
	var result []int
	for _, id := range order {
		c := sums[id]
		percent := -1
		if c.total > 0 {
			percent = int(100 * c.busy / c.total)
		}
		result = append(result, percent)
	}
	return result
}

func ramUsage(r io.Reader) (used, total uint64, ok bool) {
	var avail uint64
	haveAvailable := false
	s := bufio.NewScanner(r)
	for s.Scan() {
		f := strings.Fields(s.Text())
		if len(f) < 3 || f[2] != "kB" {
			continue
		}
		switch f[0] {
		case "MemTotal:":
			total, _ = strconv.ParseUint(f[1], 10, 64)
		case "MemAvailable:":
			var err error
			avail, err = strconv.ParseUint(f[1], 10, 64)
			haveAvailable = err == nil
		}
	}
	if !haveAvailable || total == 0 || avail > total {
		return 0, 0, false
	}
	return total - avail, total, true
}

func bestInterface(r io.Reader, physical func(string) bool) string {
	s := bufio.NewScanner(r)
	var selected string
	metric := uint64(^uint64(0))
	for s.Scan() {
		f := strings.Fields(s.Text())
		if len(f) < 8 || f[1] != "00000000" || f[7] != "00000000" || f[0] == "." || strings.ContainsAny(f[0], "/\\") {
			continue
		}
		flags, err := strconv.ParseUint(f[3], 16, 64)
		if err != nil || flags&1 == 0 {
			continue
		}
		m, err := strconv.ParseUint(f[6], 10, 64)
		if err == nil && m < metric && physical(f[0]) {
			selected, metric = f[0], m
		}
	}
	return selected
}

type linkCounter struct {
	name   string
	at     time.Time
	rx, tx uint64
}

func linkRates(old, cur linkCounter) (rx, tx float64, ok bool) {
	seconds := cur.at.Sub(old.at).Seconds()
	if old.name == "" || cur.name != old.name || seconds <= 0 || cur.rx < old.rx || cur.tx < old.tx {
		return 0, 0, false
	}
	return float64(cur.rx-old.rx) * 8 / seconds / 1e6, float64(cur.tx-old.tx) * 8 / seconds / 1e6, true
}

type homeSampler struct {
	health   homeHealth
	cpu      map[string]cpuCounter
	topology map[string]string
	link     linkCounter
}
type homeSample struct {
	health      healthLevel
	healthCPU   int
	cpuPct      int
	cores       []int
	used, total float64
	rx, tx      float64
	networkOK   bool
	now         time.Time
}

func (s *homeSampler) sample(now time.Time) homeSample {
	out := homeSample{now: now, cpuPct: -1}
	if f, err := os.Open("/proc/stat"); err == nil {
		current := cpuCounters(f)
		_ = f.Close()
		out.cpuPct = cpuPercent(s.cpu, current, "cpu")
		if s.topology == nil {
			s.topology = make(map[string]string)
		}
		for _, key := range orderedCPUs(current) {
			if _, cached := s.topology[key]; cached {
				continue
			}
			dir := filepath.Join("/sys/devices/system/cpu", key, "topology")
			pkg, ep := os.ReadFile(filepath.Join(dir, "physical_package_id"))
			core, ec := os.ReadFile(filepath.Join(dir, "core_id"))
			id := key
			if ep == nil && ec == nil && strings.TrimSpace(string(pkg)) != "-1" && strings.TrimSpace(string(core)) != "-1" {
				id = strings.TrimSpace(string(pkg)) + "/" + strings.TrimSpace(string(core))
			}
			s.topology[key] = id
		}
		out.cores = physicalCPUUsage(s.cpu, current, s.topology)
		s.cpu = current
	} else {
		s.cpu = nil
	}
	if f, err := os.Open("/proc/meminfo"); err == nil {
		used, total, ok := ramUsage(f)
		_ = f.Close()
		if ok {
			out.used, out.total = float64(used)/(1<<20), float64(total)/(1<<20)
		}
	}
	all, present := s.cpu["cpu"]
	out.health, out.healthCPU = s.health.update(now, all, present, out.used, out.total)
	if f, err := os.Open("/proc/net/route"); err == nil {
		name := bestInterface(f, func(iface string) bool {
			_, err := os.Stat(filepath.Join("/sys/class/net", iface, "device"))
			return err == nil
		})
		_ = f.Close()
		read := func(counter string) (uint64, error) {
			b, err := os.ReadFile(filepath.Join("/sys/class/net", name, "statistics", counter))
			if err != nil {
				return 0, err
			}
			return strconv.ParseUint(strings.TrimSpace(string(b)), 10, 64)
		}
		rx, erx := read("rx_bytes")
		tx, etx := read("tx_bytes")
		if name != "" && erx == nil && etx == nil {
			current := linkCounter{name: name, at: now, rx: rx, tx: tx}
			out.rx, out.tx, out.networkOK = linkRates(s.link, current)
			s.link = current
			return out
		}
	}
	s.link = linkCounter{}
	return out
}

type homeCommand func(context.Context, string, ...string) (string, error)

func runHomeCommand(ctx context.Context, executable string, args ...string) (string, error) {
	data, err := exec.CommandContext(ctx, executable, args...).Output()
	return strings.TrimSpace(string(data)), err
}

func lockHost(ctx context.Context, session string, uid int, cmd homeCommand) string {
	if session == "" {
		session = "self"
	}
	// Reject option-like strings and do not find or guess a different session.
	if session[0] == '-' || strings.ContainsAny(session, "/\\ \t\n") {
		return "Lock unavailable: invalid host session."
	}
	read := func() map[string]string {
		out, err := cmd(ctx, "loginctl", "show-session", session, "-p", "User", "-p", "Type", "-p", "Remote", "-p", "Active", "-p", "LockedHint")
		if err != nil {
			return nil
		}
		properties := map[string]string{}
		for _, line := range strings.Split(out, "\n") {
			k, v, _ := strings.Cut(line, "=")
			properties[k] = v
		}
		return properties
	}
	p := read()
	if p["User"] != strconv.Itoa(uid) || p["Type"] != "wayland" || p["Remote"] != "no" || p["Active"] != "yes" {
		return "Lock unavailable: no active local Wayland login for this shell."
	}
	if p["LockedHint"] == "yes" {
		return "Host session is already locked."
	}
	if _, err := cmd(ctx, "loginctl", "lock-session", session); err != nil {
		return "Host lock request failed."
	}
	ticker := time.NewTicker(120 * time.Millisecond)
	defer ticker.Stop()
	for {
		if read()["LockedHint"] == "yes" {
			return "Host session locked."
		}
		select {
		case <-ctx.Done():
			return "Host lock requested; confirmation unavailable."
		case <-ticker.C:
		}
	}
}

func audioStatus(ctx context.Context, cmd homeCommand) string {
	out, err := cmd(ctx, "wpctl", "get-volume", "@DEFAULT_AUDIO_SINK@")
	if err != nil {
		return "Audio unavailable: no PipeWire output or wpctl."
	}
	f := strings.Fields(out)
	if len(f) < 2 || f[0] != "Volume:" {
		return "Audio level unavailable."
	}
	level, err := strconv.ParseFloat(f[1], 64)
	if err != nil || level < 0 || level > 10 {
		return "Audio level unavailable."
	}
	suffix := ""
	if strings.Contains(out, "[MUTED]") {
		suffix = " · Muted"
	}
	return fmt.Sprintf("Default output · %.0f%%%s", level*100, suffix)
}
