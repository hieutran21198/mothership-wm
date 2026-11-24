// Package nodestate stores durable Node metadata, never live WM session state.
package nodestate

import (
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"

	"mothership-wm/services/desktopd/internal/domain"
)

type Store struct{ dir string }

func Open(stateHome, home string) (Store, []domain.Node, error) {
	if stateHome == "" && filepath.IsAbs(home) {
		stateHome = filepath.Join(home, ".local", "state")
	}
	if !filepath.IsAbs(stateHome) {
		return Store{}, nil, errors.New("state home must be absolute")
	}
	s := Store{dir: filepath.Join(stateHome, "mothership-wm", "nodes")}
	var nodes []domain.Node
	entries, err := os.ReadDir(s.dir)
	if errors.Is(err, os.ErrNotExist) {
		return s, nil, nil
	}
	if err != nil {
		return Store{}, nil, err
	}
	for _, e := range entries {
		name := e.Name()
		if filepath.Ext(name) != ".json" {
			continue
		}
		inf, err := e.Info()
		if err != nil {
			return Store{}, nil, err
		}
		if !inf.Mode().IsRegular() || inf.Size() > 4096 {
			return Store{}, nil, fmt.Errorf("invalid Node record %q", name)
		}
		b, err := os.ReadFile(filepath.Join(s.dir, name))
		if err != nil {
			return Store{}, nil, err
		}
		var n domain.Node
		if err := json.Unmarshal(b, &n); err != nil || n.CreatedAt <= 0 || !domain.ValidName(n.Name) || !domain.ValidDescription(n.Description) ||
			name != n.Name+".json" || (n.LastActive != 0 && n.LastActive < n.CreatedAt) {
			return Store{}, nil, fmt.Errorf("invalid Node record %q", name)
		}
		nodes = append(nodes, n)
	}
	return s, nodes, nil
}

func (s Store) Save(n domain.Node) error {
	if !domain.ValidName(n.Name) {
		return errors.New("invalid Node name")
	}
	if err := os.MkdirAll(s.dir, 0700); err != nil {
		return err
	}
	b, err := json.Marshal(n)
	if err != nil {
		return err
	}
	f, err := os.CreateTemp(s.dir, "node-*")
	if err != nil {
		return err
	}
	defer os.Remove(f.Name())
	if _, err = f.Write(append(b, '\n')); err == nil {
		err = f.Sync()
	}
	closeErr := f.Close()
	if err != nil {
		return err
	}
	if closeErr != nil {
		return closeErr
	}
	return os.Rename(f.Name(), filepath.Join(s.dir, n.Name+".json"))
}
