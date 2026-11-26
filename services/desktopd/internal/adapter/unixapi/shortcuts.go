package unixapi

import (
	"encoding/json"
	"fmt"

	"mothership-wm/internal/domainipc"
	"mothership-wm/internal/userconfig"
)

func readShortcuts(path string) (domainipc.Shortcuts, error) {
	s := domainipc.DefaultShortcuts()
	doc, _, err := userconfig.Read(path)
	if err != nil {
		return s, err
	}
	if raw := doc["shortcuts"]; raw != nil {
		next := s
		if err := json.Unmarshal(raw, &next); err != nil {
			return s, err
		}
		if _, err := next.Keycodes(); err != nil {
			return s, err
		}
		s = next
	}
	return s, nil
}

func writeShortcuts(path string, s domainipc.Shortcuts) error {
	doc, _, err := userconfig.Read(path)
	if err != nil {
		return err
	}
	mod, err := doc.Child("shortcuts")
	if err != nil {
		return err
	}
	for key, value := range map[string]string{"close_window": s.CloseWindow, "app_grid": s.AppGrid, "node_settings": s.NodeSettings, "node_keys": s.NodeKeys} {
		if err = mod.Set(key, value); err != nil {
			return err
		}
	}
	if err = doc.Set("shortcuts", mod); err != nil {
		return err
	}
	return userconfig.Write(path, doc)
}

func (d *desktop) applyShortcuts(w *wmPeer, next domainipc.Shortcuts) error {
	keys, err := next.Keycodes()
	if err != nil {
		return err
	}
	d.mu.Lock()
	if w == nil || d.wm != w || !w.ready || !w.shortcuts {
		d.mu.Unlock()
		return fmt.Errorf("The connected WM does not support editable shortcuts.")
	}
	ack := d.beginApply(w, nil)
	d.mu.Unlock()
	if !d.awaitApply(w, ack, fmt.Sprintf("shortcuts %d %d %d %d %d", ack.serial, keys[0], keys[1], keys[2], keys[3])) {
		return errOutcomeUnknown
	}
	return nil
}

func (d *desktop) setShortcuts(next domainipc.Shortcuts) error {
	d.controlMu.Lock()
	defer d.controlMu.Unlock()
	d.mu.Lock()
	w, old := d.wm, d.shortcuts
	d.mu.Unlock()
	if err := d.applyShortcuts(w, next); err != nil {
		return err
	}
	if err := writeShortcuts(d.configPath, next); err != nil {
		if rollback := d.applyShortcuts(w, old); rollback != nil {
			return fmt.Errorf("Could not save shortcuts; the active table is unknown: %v", err)
		}
		return fmt.Errorf("Could not save shortcuts; previous bindings restored: %v", err)
	}
	d.mu.Lock()
	d.shortcuts = next
	d.mu.Unlock()
	return nil
}
