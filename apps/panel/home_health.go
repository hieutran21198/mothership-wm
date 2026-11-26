//go:build linux

package main

import "time"

type healthLevel int

const (
	healthUnknown healthLevel = iota
	healthNormal
	healthWarning
	healthCritical
)

type healthCPUPoint struct {
	at      time.Time
	counter cpuCounter
}

type homeHealth struct {
	cpu                           []healthCPUPoint
	ramWarning, ramCritical, last time.Time
}

// Use raw whole-host deltas across the last ~10 seconds, rather than
// averaging rounded percentages or treating the current clock frequency as load.
func (h *homeHealth) update(now time.Time, cpu cpuCounter, cpuOK bool, used, total float64) (healthLevel, int) {
	if !h.last.IsZero() && (now.Sub(h.last) <= 0 || now.Sub(h.last) > 2500*time.Millisecond) {
		h.cpu = h.cpu[:0]
		h.ramWarning, h.ramCritical = time.Time{}, time.Time{}
	}
	h.last = now
	if len(h.cpu) > 0 {
		last := h.cpu[len(h.cpu)-1].counter
		if cpu.total <= last.total || cpu.busy < last.busy || cpu.busy-last.busy > cpu.total-last.total {
			h.cpu = h.cpu[:0]
		}
	}
	cpuPct := -1
	var load float64
	if cpuOK {
		h.cpu = append(h.cpu, healthCPUPoint{now, cpu})
		cut := now.Add(-10 * time.Second)
		for len(h.cpu) > 1 && !h.cpu[1].at.After(cut) {
			copy(h.cpu, h.cpu[1:])
			h.cpu = h.cpu[:len(h.cpu)-1]
		}
		if base := h.cpu[0]; !base.at.After(cut) {
			load = float64(cpu.busy-base.counter.busy) / float64(cpu.total-base.counter.total) * 100
			cpuPct = int(load)
		}
	} else {
		h.cpu = h.cpu[:0]
	}

	ramOK := total > 0 && used >= 0 && used <= total
	sustained := func(since *time.Time, above bool) bool {
		if !above {
			*since = time.Time{}
			return false
		}
		if since.IsZero() {
			*since = now
		}
		return now.Sub(*since) >= 5*time.Second
	}
	ramWarn := sustained(&h.ramWarning, ramOK && used >= .90*total)
	ramRed := sustained(&h.ramCritical, ramOK && used >= .97*total)
	switch {
	case ramRed || cpuPct >= 0 && load >= 99:
		return healthCritical, cpuPct
	case ramWarn || cpuPct >= 0 && load >= 95:
		return healthWarning, cpuPct
	case !ramOK || cpuPct < 0:
		return healthUnknown, cpuPct
	default:
		return healthNormal, cpuPct
	}
}
