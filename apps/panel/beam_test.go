package main

import (
	"testing"

	"mothership-wm/internal/domainipc"
)

func TestBeamTargetsListNodesAndMarkTheSource(t *testing.T) {
	s := domainipc.Snapshot{
		Domains:      []string{"Ops", "Dev"},
		NodeProfiles: []domainipc.NodeProfile{{Domain: "Dev", Description: "Backend"}},
		Windows:      []domainipc.Window{{ID: 1, Domain: "Ops"}, {ID: 2, Domain: "Ops"}, {ID: 3}},
	}
	targets, current := beamTargets(s, 1, true)
	if len(targets) != 3 || targets[0].node != "" || targets[1].node != "Dev" || targets[2].node != "Ops" {
		t.Fatalf("targets %+v", targets)
	}
	if current != 2 || targets[2].windows != 2 || targets[0].windows != 1 || targets[1].description != "Backend" {
		t.Fatalf("current %d targets %+v", current, targets)
	}
	if targets, current := beamTargets(s, 3, false); len(targets) != 2 || current != -1 {
		t.Fatalf("without Default: %d %+v", current, targets)
	}
}
