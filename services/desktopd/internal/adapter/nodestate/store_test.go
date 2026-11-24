package nodestate

import (
	"os"
	"path/filepath"
	"reflect"
	"testing"

	"mothership-wm/services/desktopd/internal/domain"
)

func TestNodeRestartAndValidation(t *testing.T) {
	dir := t.TempDir()
	s, nodes, err := Open(dir, "")
	if err != nil || len(nodes) != 0 {
		t.Fatalf("empty state: %v %v", nodes, err)
	}
	initial := domain.Node{Name: "Dev", Description: "Backend Platform", CreatedAt: 10}
	if err = s.Save(initial); err != nil {
		t.Fatal(err)
	}
	active := initial
	active.LastActive = 20
	if err = s.Save(active); err != nil {
		t.Fatal(err)
	}
	_, got, err := Open(dir, "")
	if err != nil || !reflect.DeepEqual(got, []domain.Node{active}) {
		t.Fatalf("restart metadata: %v %v", got, err)
	}
	f := filepath.Join(s.dir, "Dev.json")
	info, err := os.Stat(f)
	if err != nil || info.Mode().Perm() != 0600 {
		t.Fatalf("state permissions: %v %v", info, err)
	}
	if err = s.Save(domain.Node{Name: "../other", CreatedAt: 11}); err == nil {
		t.Fatal("unsafe path accepted")
	}
	if err = os.WriteFile(f, []byte("{"), 0600); err != nil {
		t.Fatal(err)
	}
	if _, _, err = Open(dir, ""); err == nil {
		t.Fatal("malformed record was silently discarded")
	}
}
