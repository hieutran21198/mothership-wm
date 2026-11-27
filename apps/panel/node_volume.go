package main

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"

	"mothership-wm/internal/domainipc"
)

type audioStream struct {
	Index          uint32 `json:"index"`
	Mute           bool   `json:"mute"`
	Corked         bool   `json:"corked"`
	HasVolume      *bool  `json:"has_volume"`
	VolumeWritable *bool  `json:"volume_writable"`
	Volume         map[string]struct {
		ValuePercent string `json:"value_percent"`
	} `json:"volume"`
	Properties map[string]string `json:"properties"`
}

type audioKey struct {
	Index  uint32
	Serial uint64
}
type nodeAudioStream struct {
	stream audioStream
	key    audioKey
	owner  int
	node   string
}
type audioGroup struct {
	PID      int
	Name     string
	Icon     string
	Volume   int
	Mute     bool
	Writable bool
	Streams  []nodeAudioStream
}

func parseAudioStreams(data []byte) ([]audioStream, error) {
	var streams []audioStream
	if err := json.Unmarshal(data, &streams); err != nil {
		return nil, err
	}
	return streams, nil
}

func streamKey(s audioStream) (audioKey, bool) {
	serial, err := strconv.ParseUint(s.Properties["object.serial"], 10, 64)
	return audioKey{s.Index, serial}, err == nil && serial != 0
}

func streamPercent(s audioStream) int {
	var max int
	for _, v := range s.Volume {
		n, err := strconv.Atoi(strings.TrimSuffix(v.ValuePercent, "%"))
		if err == nil && n > max {
			max = n
		}
	}
	return max
}

const defaultAudioNode = "@default"

func audioNodeSelected(s domainipc.Snapshot) string {
	if s.Selection.State != "acknowledged" {
		return ""
	}
	if s.Selection.Domain != "" {
		return s.Selection.Domain
	}
	if s.Selection.View == "home_node" {
		return defaultAudioNode
	}
	return ""
}

// Search from each playback process to the first ancestor with a live window.
// A single process whose windows span Nodes cannot be scoped safely.
func routeAudio(streams []audioStream, windows []domainipc.Window, parent func(int) int) []nodeAudioStream {
	type owner struct {
		node      string
		ambiguous bool
	}
	owners := make(map[int]owner)
	for _, w := range windows {
		if !w.Mapped || w.PID <= 0 {
			continue
		}
		node := w.Domain
		if node == "" {
			node = defaultAudioNode
		}
		if prev, ok := owners[w.PID]; ok {
			if prev.node != node {
				prev.ambiguous = true
			}
			owners[w.PID] = prev
		} else {
			owners[w.PID] = owner{node: node}
		}
	}
	var result []nodeAudioStream
	for _, s := range streams {
		key, ok := streamKey(s)
		if !ok {
			continue
		}
		pid, err := strconv.Atoi(s.Properties["application.process.id"])
		if err != nil || pid <= 1 {
			continue
		}
		for n := 0; pid > 1 && n < 16; n++ {
			if o, ok := owners[pid]; ok {
				if !o.ambiguous {
					result = append(result, nodeAudioStream{s, key, pid, o.node})
				}
				break
			}
			next := parent(pid)
			if next == pid {
				break
			}
			pid = next
		}
	}
	sort.Slice(result, func(i, j int) bool { return result[i].key.Serial < result[j].key.Serial })
	return result
}

func procAudioParent(pid int) int {
	data, err := os.ReadFile(filepath.Join("/proc", strconv.Itoa(pid), "stat"))
	if err != nil || len(data) > 4096 {
		return 0
	}
	tail := strings.LastIndexByte(string(data), ')')
	if tail < 0 {
		return 0
	}
	fields := strings.Fields(string(data[tail+1:]))
	if len(fields) < 2 {
		return 0
	}
	n, _ := strconv.Atoi(fields[1])
	return n
}

func audioGroups(streams []nodeAudioStream, node string) []audioGroup {
	by := make(map[int]int)
	var result []audioGroup
	for _, s := range streams {
		if s.node != node {
			continue
		}
		i, ok := by[s.owner]
		if !ok {
			i = len(result)
			by[s.owner] = i
			name := strings.TrimSpace(s.stream.Properties["application.name"])
			if name == "" {
				name = strings.TrimSpace(s.stream.Properties["media.name"])
			}
			if name == "" {
				name = fmt.Sprintf("App %d", s.owner)
			}
			result = append(result, audioGroup{PID: s.owner, Name: name, Icon: s.stream.Properties["application.icon_name"], Mute: true, Writable: true})
		}
		g := &result[i]
		g.Mute = g.Mute && s.stream.Mute
		g.Writable = g.Writable && len(s.stream.Volume) > 0 &&
			(s.stream.HasVolume == nil || *s.stream.HasVolume) && (s.stream.VolumeWritable == nil || *s.stream.VolumeWritable)
		g.Volume = max(g.Volume, streamPercent(s.stream))
		g.Streams = append(g.Streams, s)
	}
	sort.Slice(result, func(i, j int) bool {
		if result[i].Name == result[j].Name {
			return result[i].PID < result[j].PID
		}
		return result[i].Name < result[j].Name
	})
	return result
}

type audioMuteState struct {
	nodes   map[string]bool
	restore map[audioKey]string // Streams muted here, never pre-muted inputs.
	// hold is the Node whose audio keeps playing during Silent running with
	// audio; every other identifiable Node is muted. Empty means no hold.
	hold string
}

// muted reports whether the Node toggle or a Silent running hold wants a
// Node's streams muted. Unrouted streams have no Node and are never muted.
func (m *audioMuteState) muted(node string) bool {
	return node != "" && (m.nodes[node] || (m.hold != "" && node != m.hold))
}

func (m *audioMuteState) reconcile(all []audioStream, routed []nodeAudioStream, setMute func(audioKey, bool) error) error {
	present := make(map[audioKey]audioStream)
	for _, s := range all {
		if k, ok := streamKey(s); ok {
			present[k] = s
		}
	}
	owner := make(map[audioKey]string)
	for _, s := range routed {
		owner[s.key] = s.node
	}
	var failure error
	for key, node := range m.restore {
		s, ok := present[key]
		if !ok {
			delete(m.restore, key)
			continue
		}
		if target := owner[key]; m.muted(target) {
			// Transfer ownership without unmuting during a move.
			if target != node {
				m.restore[key] = target
			}
			continue
		}
		if s.Mute {
			if err := setMute(key, false); err != nil {
				failure = err
				continue
			}
		}
		delete(m.restore, key)
	}
	for _, s := range routed {
		if !m.muted(s.node) || s.stream.Mute {
			continue
		}
		if err := setMute(s.key, true); err != nil {
			failure = err
			continue
		}
		m.restore[s.key] = s.node
	}
	return failure
}
