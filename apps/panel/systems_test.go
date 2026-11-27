package main

import (
	"reflect"
	"strings"
	"testing"
)

const pactlSinks = `[
 {"name":"alsa_output.pci-0000_00_1f.3.analog-stereo","description":"Built-in Audio Analog Stereo","mute":false,
  "volume":{"front-left":{"value":41943},"front-right":{"value":41943}},
  "properties":{"device.bus":"pci","alsa.card":"0","device.form_factor":"internal"},
  "ports":[{"name":"analog-output-speaker","description":"Speakers","type":"Speaker"}],"active_port":"analog-output-speaker"},
 {"name":"bluez_output.AC_80_0A_00_00_01.1","description":"WH-1000XM5","mute":true,
  "volume":{"mono":{"value":32768}},"properties":{"device.bus":"bluetooth","api.bluez5.address":"AC:80:0A:00:00:01","device.form_factor":"headset"},
  "ports":[],"active_port":""},
 {"name":"alsa_output.pci-0000_01_00.1.hdmi-stereo","description":"DELL\tU2723QE","mute":false,
  "volume":{"front-left":{"value":65536}},"properties":{"device.bus":"pci","alsa.card":"1"},
  "ports":[{"name":"hdmi-output-0","description":"HDMI / DisplayPort","type":"HDMI"}],"active_port":"hdmi-output-0"}
]`

const pactlSources = `[
 {"name":"alsa_output.pci-0000_00_1f.3.analog-stereo.monitor","description":"Monitor of Built-in Audio","mute":false,
  "volume":{"mono":{"value":65536}},"properties":{"device.class":"monitor"},"monitor_of_sink":"alsa_output.pci-0000_00_1f.3.analog-stereo"},
 {"name":"alsa_input.pci-0000_00_1f.3.analog-stereo","description":"Built-in Audio Analog Stereo","mute":true,
  "volume":{"mono":{"value":52429}},"properties":{"device.bus":"pci","alsa.card":"0"},"monitor_of_sink":"n/a"}
]`

func TestSoundDevicesClassifyAndHideMonitors(t *testing.T) {
	sinks, err := soundDevices([]byte(pactlSinks), "bluez_output.AC_80_0A_00_00_01.1", false)
	if err != nil {
		t.Fatal(err)
	}
	want := []soundDevice{
		{Name: "alsa_output.pci-0000_00_1f.3.analog-stereo", Description: "Built-in Audio Analog Stereo", Kind: "Built-in", Icon: "speaker", Percent: 64},
		{Name: "bluez_output.AC_80_0A_00_00_01.1", Description: "WH-1000XM5", Kind: "Bluetooth", Icon: "headphones", Percent: 50, Muted: true, Default: true},
		{Name: "alsa_output.pci-0000_01_00.1.hdmi-stereo", Description: "DELL U2723QE", Kind: "HDMI", Icon: "monitor", Percent: 100},
	}
	if !reflect.DeepEqual(sinks, want) {
		t.Fatalf("sinks = %#v\nwant %#v", sinks, want)
	}
	sources, err := soundDevices([]byte(pactlSources), "alsa_input.pci-0000_00_1f.3.analog-stereo", true)
	if err != nil {
		t.Fatal(err)
	}
	if len(sources) != 1 || sources[0].Icon != "mic" || !sources[0].Muted || !sources[0].Default || sources[0].Percent != 80 {
		t.Fatalf("sources = %#v", sources)
	}
}

func TestSoundPostAndArgs(t *testing.T) {
	text := soundPost([]soundDevice{{Name: "a", Description: "A", Kind: "USB", Icon: "speaker", Percent: 5, Default: true}}, nil, "")
	if text != "sound\tok\nsink\ta\tA\tUSB\tspeaker\t5\t0\t1\n" {
		t.Fatalf("post = %q", text)
	}
	if got := soundPost(nil, nil, "no\tserver"); got != "error\tno server\n" {
		t.Fatalf("error post = %q", got)
	}
	cases := map[int][]string{
		0: {"set-default-sink", "a"}, 1: {"set-sink-volume", "a", "100%"}, 2: {"set-sink-mute", "a", "1"},
		3: {"set-default-source", "a"}, 4: {"set-source-volume", "a", "100%"}, 5: {"set-source-mute", "a", "1"},
	}
	for op, want := range cases {
		if got, ok := soundArgs(op, "a", 140); !ok || !reflect.DeepEqual(got, want) {
			t.Errorf("soundArgs(%d) = %v %v, want %v", op, got, ok, want)
		}
	}
	if _, ok := soundArgs(0, "bad\nname", 0); ok {
		t.Error("accepted a name with a newline")
	}
}

func TestDeviceEventIgnoresStreams(t *testing.T) {
	for line, want := range map[string]bool{
		"Event 'change' on sink #52":           true,
		"Event 'change' on source #12":         true,
		"Event 'change' on server #4294967295": true,
		"Event 'new' on sink-input #300":       false,
		"Event 'remove' on source-output #9":   false,
	} {
		if got := deviceEvent(line); got != want {
			t.Errorf("deviceEvent(%q) = %v", line, got)
		}
	}
	if strings.Contains(oneLine("a\tb\nc"), "\t") {
		t.Error("oneLine kept a tab")
	}
}
