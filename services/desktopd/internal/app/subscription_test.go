package app

import (
	"fmt"
	"sync"
	"testing"
)

func TestSubscribeBoundaryDoesNotLoseMutation(t *testing.T) {
	for i := 0; i < 150; i++ {
		var s Service
		var done sync.WaitGroup
		done.Add(1)
		go func() {
			defer done.Done()
			s.Create("Dev")
		}()
		base, events, closeStream, err := s.Subscribe()
		if err != nil {
			t.Fatal(err)
		}
		done.Wait()
		select {
		case e := <-events:
			if base.Revision != 0 || len(base.Domains) != 0 || e.Revision != 1 || e.Kind != DomainCreated {
				t.Fatalf("bad snapshot/update boundary: %+v / %+v", base, e)
			}
		default:
			if base.Revision != 1 || len(base.Domains) != 1 || base.Domains[0] != "Dev" {
				t.Fatalf("mutation neither in snapshot nor delivered: %+v", base)
			}
		}
		closeStream()
	}
}

func TestSlowSubscriberDoesNotBlockOrLoseFastClient(t *testing.T) {
	var s Service
	wm := s.Connect("test")
	if !s.Ready(wm, false) {
		t.Fatal("WM not ready")
	}
	base, slow, closeSlow, err := s.Subscribe()
	if err != nil {
		t.Fatal(err)
	}
	defer closeSlow()
	_, fast, closeFast, err := s.Subscribe()
	if err != nil {
		t.Fatal(err)
	}
	defer closeFast()
	pull := func(ch <-chan Update) Update {
		t.Helper()
		select {
		case e, ok := <-ch:
			if ok {
				return e
			}
		default:
		}
		t.Fatal("expected queued update")
		return Update{}
	}
	for i := 1; i <= queueSize; i++ {
		s.Create(fmt.Sprintf("D%d", i))
		if e := pull(fast); e.Revision != base.Revision+uint64(i) {
			t.Fatalf("fast subscriber missed revision %d: %+v", i, e)
		}
	}
	if !s.WindowAdded(wm, 7) {
		t.Fatal("live WM mutation failed")
	}
	if got := pull(fast); got.Kind != WindowUpdated || got.Window.ID != 7 {
		t.Fatalf("fast subscriber missed WM mutation: %+v", got)
	}
	for i := 0; i < queueSize; i++ {
		_ = pull(slow)
	}
	select {
	case _, ok := <-slow:
		if ok {
			t.Fatal("slow queue overflow should disconnect it")
		}
	default:
		t.Fatal("slow subscriber did not disconnect")
	}
	v, events, unsubscribe, err := s.Subscribe()
	if err != nil {
		t.Fatal(err)
	}
	defer unsubscribe()
	if v.Revision != base.Revision+queueSize+1 || len(v.Domains) != queueSize || len(v.Windows) != 1 {
		t.Fatalf("fresh snapshot did not replace stale view: %+v", v)
	}
	s.Create("D1") // idempotent, no event
	s.Create("Next")
	if h := pull(events); h.Revision != v.Revision+1 || h.Domain != "Next" {
		t.Fatalf("unexpected replay/change: %+v", h)
	}
}

func TestMapVisibilityDoesNotAlterNodeView(t *testing.T) {
	var s Service
	wm := s.Connect("test")
	if !s.Ready(wm, true) {
		t.Fatal("WM not ready")
	}
	s.Create("Dev")
	s.AcknowledgeView(wm, Selection{Kind: SelectionAcknowledged, Domain: "Dev", Mode: "tiling"})
	if !s.WindowAdded(wm, 4) || !s.WindowMapping(wm, 4, true) {
		t.Fatal("window setup")
	}
	original, events, closeStream, err := s.Subscribe()
	if err != nil {
		t.Fatal(err)
	}
	defer closeStream()
	if original.MapVisible {
		t.Fatal("map should start hidden")
	}
	for _, visible := range []bool{true, false, true} {
		prior := s.Snapshot()
		s.SetMapVisible(visible)
		s.SetMapVisible(visible)
		next := s.Snapshot()
		if next.Revision != prior.Revision+1 || next.Selection != original.Selection || next.Session != original.Session ||
			next.MapVisible != visible || len(next.Domains) != 1 || next.Domains[0] != "Dev" ||
			len(next.Windows) != 1 || next.Windows[0] != original.Windows[0] {
			t.Fatalf("visibility changed view or duplicated revision: %+v", next)
		}
		select {
		case event := <-events:
			if event.Kind != MapVisibilityChanged || event.MapVisible != visible || event.Revision != next.Revision {
				t.Fatalf("wrong event: %+v", event)
			}
		default:
			t.Fatal("missing event")
		}
	}
}
