package main

import (
	"encoding/json"
	"fmt"
	"strings"
)

// soundDevice is one sink or source as the Systems board shows it.
type soundDevice struct {
	Name, Description, Kind, Icon string
	Percent                       int
	Muted, Default                bool
}

type pactlVolume struct {
	Value int `json:"value"`
}

type pactlPort struct {
	Name        string `json:"name"`
	Description string `json:"description"`
	Type        string `json:"type"`
}

type pactlDevice struct {
	Name        string                 `json:"name"`
	Description string                 `json:"description"`
	Mute        bool                   `json:"mute"`
	Volume      map[string]pactlVolume `json:"volume"`
	Properties  map[string]string      `json:"properties"`
	Ports       []pactlPort            `json:"ports"`
	ActivePort  string                 `json:"active_port"`
	MonitorOf   string                 `json:"monitor_of_sink"`
}

type pactlInfo struct {
	DefaultSink   string `json:"default_sink_name"`
	DefaultSource string `json:"default_source_name"`
}

// soundDevices turns `pactl --format=json list sinks|sources` into rows.
// Monitor sources are hidden: they are recordings of an output, not a mic.
func soundDevices(raw []byte, defaultName string, sources bool) ([]soundDevice, error) {
	var all []pactlDevice
	if err := json.Unmarshal(raw, &all); err != nil {
		return nil, err
	}
	out := make([]soundDevice, 0, len(all))
	for _, d := range all {
		if sources && (d.Properties["device.class"] == "monitor" || (d.MonitorOf != "" && d.MonitorOf != "n/a") ||
			strings.HasSuffix(d.Name, ".monitor")) {
			continue
		}
		port := ""
		for _, p := range d.Ports {
			if p.Name == d.ActivePort {
				port = strings.ToLower(p.Name + " " + p.Description + " " + p.Type)
			}
		}
		kind := soundKind(d.Name, d.Description, port, d.Properties)
		desc := oneLine(d.Description)
		if desc == "" {
			desc = d.Name
		}
		out = append(out, soundDevice{
			Name: d.Name, Description: desc, Kind: kind, Icon: soundIcon(kind, d.Properties, sources),
			Percent: volumePercent(d.Volume), Muted: d.Mute, Default: d.Name == defaultName,
		})
	}
	// Keep the server's order so rows do not jump when the default changes.
	return out, nil
}

func soundKind(name, desc, port string, props map[string]string) string {
	bus := props["device.bus"]
	lname, ldesc := strings.ToLower(name), strings.ToLower(desc)
	switch {
	case bus == "bluetooth" || props["api.bluez5.address"] != "" || strings.HasPrefix(lname, "bluez"):
		return "Bluetooth"
	case strings.Contains(port, "hdmi") || strings.Contains(port, "displayport") ||
		strings.Contains(ldesc, "hdmi") || strings.Contains(ldesc, "displayport"):
		return "HDMI"
	case bus == "usb":
		return "USB"
	case bus == "" && props["alsa.card"] == "" && props["device.api"] == "":
		return "Virtual"
	default:
		return "Built-in"
	}
}

func soundIcon(kind string, props map[string]string, source bool) string {
	switch props["device.form_factor"] {
	case "headset", "headphone", "hands-free":
		return "headphones"
	case "speaker":
		return "speaker"
	}
	switch {
	case kind == "HDMI":
		return "monitor"
	case kind == "Bluetooth":
		return "headphones"
	case source:
		return "mic"
	default:
		return "speaker"
	}
}

func volumePercent(v map[string]pactlVolume) int {
	if len(v) == 0 {
		return 0
	}
	sum := 0
	for _, c := range v {
		sum += c.Value
	}
	return (sum*100/len(v) + 32768) / 65536
}

func oneLine(s string) string {
	return strings.Join(strings.Fields(strings.NewReplacer("\t", " ", "\n", " ").Replace(s)), " ")
}

// soundPost formats the board's sound state: one record per line, tab
// separated, read by systems-board.c.
func soundPost(sinks, sources []soundDevice, failure string) string {
	var b strings.Builder
	if failure != "" {
		fmt.Fprintf(&b, "error\t%s\n", oneLine(failure))
		return b.String()
	}
	b.WriteString("sound\tok\n")
	for _, set := range []struct {
		tag  string
		rows []soundDevice
	}{{"sink", sinks}, {"source", sources}} {
		for _, d := range set.rows {
			fmt.Fprintf(&b, "%s\t%s\t%s\t%s\t%s\t%d\t%d\t%d\n", set.tag, d.Name, d.Description, d.Kind, d.Icon,
				d.Percent, btoi(d.Muted), btoi(d.Default))
		}
	}
	return b.String()
}

// soundArgs maps a board request to pactl arguments.
func soundArgs(op int, name string, value int) ([]string, bool) {
	if name == "" || strings.ContainsAny(name, "\n\t") {
		return nil, false
	}
	value = max(0, min(100, value))
	switch op {
	case 0:
		return []string{"set-default-sink", name}, true
	case 1:
		return []string{"set-sink-volume", name, fmt.Sprintf("%d%%", value)}, true
	case 2:
		return []string{"set-sink-mute", name, fmt.Sprint(btoi(value != 0))}, true
	case 3:
		return []string{"set-default-source", name}, true
	case 4:
		return []string{"set-source-volume", name, fmt.Sprintf("%d%%", value)}, true
	case 5:
		return []string{"set-source-mute", name, fmt.Sprint(btoi(value != 0))}, true
	}
	return nil, false
}

// deviceEvent reports whether a `pactl subscribe` line changes devices or
// defaults. Stream events (sink-input, source-output) are ignored.
func deviceEvent(line string) bool {
	for _, kind := range []string{" on sink #", " on source #", " on server", " on card #"} {
		if strings.Contains(line, kind) {
			return true
		}
	}
	return false
}

func defaultDevice(rows []soundDevice) (soundDevice, bool) {
	for _, d := range rows {
		if d.Default {
			return d, true
		}
	}
	return soundDevice{}, false
}
