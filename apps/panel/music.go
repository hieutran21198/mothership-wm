package main

import (
	"encoding/json"
	"strconv"
	"strings"
)

// shipAudio is one Node's audio status on the Center Map. A muted kind has
// every playing stream of that kind muted.
type shipAudio struct {
	Comms, CommsMuted bool
	Music, MusicMuted bool
}

// shipAudioNodes reports named Nodes with an identifiable playing stream,
// split into music and comms. Corked (paused) streams are not playing. The
// Default Node is not on the Map.
func shipAudioNodes(routed []nodeAudioStream, isMusic func(audioStream) bool) map[string]shipAudio {
	out := map[string]shipAudio{}
	for _, s := range routed {
		if s.stream.Corked || s.node == "" || s.node == defaultAudioNode {
			continue
		}
		a := out[s.node]
		if isMusic(s.stream) {
			a.MusicMuted = s.stream.Mute && (!a.Music || a.MusicMuted)
			a.Music = true
		} else {
			a.CommsMuted = s.stream.Mute && (!a.Comms || a.CommsMuted)
			a.Comms = true
		}
		out[s.node] = a
	}
	return out
}

// musicStream reports whether a stream is music: tagged media.role=music,
// or played by a process, or one of its ancestors, that owns a media player
// reporting Playing.
func musicStream(s audioStream, players map[int]bool, parent func(int) int) bool {
	if s.Properties["media.role"] == "music" {
		return true
	}
	if len(players) == 0 {
		return false
	}
	pid, err := strconv.Atoi(s.Properties["application.process.id"])
	for n := 0; err == nil && pid > 1 && n < 16; n++ {
		if players[pid] {
			return true
		}
		next := parent(pid)
		if next == pid {
			break
		}
		pid = next
	}
	return false
}

// needsPlayers reports whether any playing, identifiable stream still needs
// the media-player check, so the bus is not queried for nothing.
func needsPlayers(routed []nodeAudioStream) bool {
	for _, s := range routed {
		if !s.stream.Corked && s.node != "" && s.node != defaultAudioNode && s.stream.Properties["media.role"] != "music" {
			return true
		}
	}
	return false
}

func shipAudioLevel(on, muted bool) int {
	switch {
	case !on:
		return 0
	case muted:
		return 2
	}
	return 1
}

// busctl --json=short replies, kept pure for tests.
func parseBusNames(data []byte) []string {
	var reply struct {
		Data [][]string `json:"data"`
	}
	if json.Unmarshal(data, &reply) != nil || len(reply.Data) != 1 {
		return nil
	}
	var players []string
	for _, n := range reply.Data[0] {
		if strings.HasPrefix(n, "org.mpris.MediaPlayer2.") {
			players = append(players, n)
		}
	}
	return players
}

func parseBusPlaying(data []byte) bool {
	var reply struct {
		Data string `json:"data"`
	}
	return json.Unmarshal(data, &reply) == nil && reply.Data == "Playing"
}

func parseBusPID(data []byte) int {
	var reply struct {
		Data []int `json:"data"`
	}
	if json.Unmarshal(data, &reply) != nil || len(reply.Data) != 1 || reply.Data[0] <= 1 {
		return 0
	}
	return reply.Data[0]
}
