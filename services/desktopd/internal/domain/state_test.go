package domain

import "testing"

func TestExplicitMembershipLifetime(t *testing.T) {
	var state State
	state.Create("A")
	state.Create("B")
	s := NewSession("current")
	for i := uint64(1); i <= 4; i++ {
		if !s.Add(i) {
			t.Fatal("add", i)
		}
	}
	assign := func(id uint64, name string) {
		t.Helper()
		if err := state.Assign(s, "current", id, name); err != nil {
			t.Fatal(err)
		}
	}
	if err := state.Assign(s, "stale", 1, "A"); err == nil {
		t.Fatal("accepted stale session")
	}
	assign(1, "A")
	assign(2, "A")
	assign(3, "B")
	before := s.Windows()[2]
	s.SetMapped(3, true)
	s.SetMapped(3, false)
	if after := s.Windows()[2]; before != after {
		t.Fatalf("unmap changed ownership: before=%+v after=%+v", before, after)
	}
	s.SetMapped(3, true)
	if remap := s.Windows()[2]; remap.Epoch != before.Epoch || !remap.Mapped {
		t.Fatalf("remap lost identity: %+v", remap)
	}
	assign(3, "B")
	if s.Windows()[2].Epoch != before.Epoch {
		t.Fatal("idempotent assignment changed epoch")
	}
	if !s.Remove(3) || s.Remove(3) {
		t.Fatal("expected one live removal")
	}
	if err := state.Assign(s, "current", 3, "A"); err == nil {
		t.Fatal("accepted destroyed ID")
	}
	assign(2, "B")
	assign(2, "A")
	if ws := s.Windows(); ws[1].Epoch <= before.Epoch || ws[1].Domain != "A" || ws[2].Domain != "" {
		t.Fatalf("reassignment or unassigned new ID invalid: %+v", ws)
	}
	if fresh := NewSession("new"); len(fresh.Windows()) != 0 {
		t.Fatal("new session inherited live windows")
	}
}

func TestNodeBoundIncludesUnmappedMembers(t *testing.T) {
	var state State
	state.Create("node")
	s := NewSession("session")
	for id := uint64(1); id <= MaxNodeWindows+1; id++ {
		s.Add(id)
		err := state.Assign(s, s.ID, id, "node")
		if id <= MaxNodeWindows && err != nil || id > MaxNodeWindows && err != ErrDomainFull {
			t.Fatalf("member %d: %v", id, err)
		}
	}
}
