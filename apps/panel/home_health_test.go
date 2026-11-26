//go:build linux

package main

import (
	"testing"
	"time"
)

func TestHealthCPUUsesWindowOfCounters(t *testing.T) {
	for _, tc := range []struct {
		busy uint64
		want healthLevel
	}{{0, healthNormal}, {94, healthNormal}, {95, healthWarning}, {99, healthCritical}} {
		var h homeHealth
		start := time.Unix(10, 0)
		for i := uint64(0); i <= 10; i++ {
			got, pct := h.update(start.Add(time.Duration(i)*time.Second), cpuCounter{i * tc.busy, i * 100}, true, 40, 100)
			if i < 10 && (got != healthUnknown || pct != -1) {
				t.Fatalf("before full CPU window: got %v,%d", got, pct)
			}
			if i == 10 && (got != tc.want || pct != int(tc.busy)) {
				t.Fatalf("%d%%: got %v,%d; want %v,%d", tc.busy, got, pct, tc.want, tc.busy)
			}
		}
	}
}

func TestHealthRAMThresholdsAndMissingCPU(t *testing.T) {
	for _, tc := range []struct {
		used float64
		want healthLevel
	}{{89.9, healthUnknown}, {90, healthWarning}, {97, healthCritical}} {
		var h homeHealth
		start := time.Unix(10, 0)
		for i := 0; i <= 5; i++ {
			got, _ := h.update(start.Add(time.Duration(i)*time.Second), cpuCounter{}, false, tc.used, 100)
			want := healthUnknown
			if i == 5 {
				want = tc.want
			}
			if got != want {
				t.Fatalf("%v%% at %ds: got %v; want %v", tc.used, i, got, want)
			}
		}
		got, _ := h.update(start.Add(10*time.Second), cpuCounter{}, false, tc.used, 100)
		if got != healthUnknown {
			t.Fatalf("sample gap must clear sustained state: got %v", got)
		}
	}
}

func TestHealthCounterResetAndTransientRAM(t *testing.T) {
	var h homeHealth
	start := time.Unix(10, 0)
	for i := 0; i <= 10; i++ {
		used := 50.
		if i > 7 && i < 10 {
			used = 100
		}
		got, _ := h.update(start.Add(time.Duration(i)*time.Second), cpuCounter{uint64(i * 30), uint64(i * 100)}, true, used, 100)
		if i == 10 && got != healthNormal {
			t.Fatalf("RAM spike should not trigger: %v", got)
		}
	}
	got, pct := h.update(start.Add(11*time.Second), cpuCounter{1, 3}, true, 10, 100)
	if got != healthUnknown || pct != -1 {
		t.Fatalf("counter reset should warm up again: %v,%d", got, pct)
	}
}
