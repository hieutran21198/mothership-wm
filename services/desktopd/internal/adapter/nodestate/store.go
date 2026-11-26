// Package nodestate stores durable Node metadata, never live WM session state.
package nodestate

import (
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"sort"

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
	legacy := make(map[string]bool)
	var seen [domain.ShortcutSlots + 1]bool
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
		var record struct {
			Shortcut *int `json:"shortcut"`
		}
		if err := json.Unmarshal(b, &n); err != nil || json.Unmarshal(b, &record) != nil || n.CreatedAt <= 0 || !domain.ValidName(n.Name) || !domain.ValidDescription(n.Description) ||
			name != n.Name+".json" || (n.LastActive != 0 && n.LastActive < n.CreatedAt) {
			return Store{}, nil, fmt.Errorf("invalid Node record %q", name)
		}
		if n.Shortcut < 0 || n.Shortcut > domain.ShortcutSlots || (n.Shortcut > 0 && seen[n.Shortcut]) {
			return Store{}, nil, fmt.Errorf("invalid shortcut in Node record %q", name)
		}
		if record.Shortcut == nil {
			legacy[n.Name] = true
		}
		if n.Shortcut > 0 {
			seen[n.Shortcut] = true
		}
		nodes = append(nodes, n)
	}
	// Keep legacy name-only slots deterministic on first launch, then persist.
	sort.Slice(nodes, func(i, j int) bool {
		if nodes[i].CreatedAt == nodes[j].CreatedAt {
			return nodes[i].Name < nodes[j].Name
		}
		return nodes[i].CreatedAt < nodes[j].CreatedAt
	})
	for i, n := range nodes {
		if !legacy[n.Name] {
			continue
		}
		for slot := 1; slot <= domain.ShortcutSlots; slot++ {
			if !seen[slot] {
				n.Shortcut = slot
				seen[slot] = true
				break
			}
		}
		if err := s.Save(n); err != nil {
			return Store{}, nil, err
		}
		nodes[i] = n
	}
	return s, nodes, nil
}

func (s Store) Delete(n domain.Node) error {
	if !domain.ValidName(n.Name) {
		return errors.New("invalid Node name")
	}
	return os.Remove(filepath.Join(s.dir, n.Name+".json"))
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
