// Package userconfig owns the path and file format for Mothership preferences.
package userconfig

import (
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"io/fs"
	"os"
	"path/filepath"
)

const maxBytes = 256 << 10

// Object keeps other module settings intact when one module saves its fields.
type Object map[string]json.RawMessage

func Path(home string) string {
	if home == "" {
		return ""
	}
	return filepath.Join(home, "config", ".mothership", "config.json")
}

func parse(data []byte) (Object, error) {
	var obj Object
	if err := json.Unmarshal(data, &obj); err != nil {
		return nil, err
	}
	if obj == nil {
		return nil, errors.New("expected JSON object")
	}
	return obj, nil
}

func (o Object) Child(name string) (Object, error) {
	if raw, ok := o[name]; ok {
		return parse(raw)
	}
	return Object{}, nil
}

func (o Object) Set(name string, value any) error {
	b, err := json.Marshal(value)
	if err != nil {
		return err
	}
	o[name] = b
	return nil
}

func Read(path string) (Object, bool, error) {
	if path == "" {
		return Object{}, false, nil
	}
	info, err := os.Stat(path)
	if errors.Is(err, fs.ErrNotExist) {
		return Object{}, false, nil
	}
	if err != nil {
		return nil, false, err
	}
	if !info.Mode().IsRegular() || info.Size() > maxBytes {
		return nil, true, errors.New("expected a regular config file of at most 256 KiB")
	}
	f, err := os.Open(path)
	if err != nil {
		return nil, true, err
	}
	defer f.Close()
	data, err := io.ReadAll(io.LimitReader(f, maxBytes+1))
	if err != nil {
		return nil, true, err
	}
	if len(data) > maxBytes {
		return nil, true, errors.New("config exceeds 256 KiB")
	}
	obj, err := parse(data)
	return obj, true, err
}

// Write atomically replaces a regular file, but does not replace a symlink.
func Write(path string, value any) error {
	if path == "" {
		return errors.New("user configuration directory unavailable")
	}
	if info, err := os.Lstat(path); err == nil {
		if !info.Mode().IsRegular() {
			return errors.New("settings path is not a regular file")
		}
	} else if !errors.Is(err, fs.ErrNotExist) {
		return err
	}
	var data bytes.Buffer
	encoder := json.NewEncoder(&data)
	encoder.SetIndent("", "  ")
	if err := encoder.Encode(value); err != nil {
		return err
	}
	if data.Len() > maxBytes {
		return errors.New("config exceeds 256 KiB")
	}
	dir := filepath.Dir(path)
	if err := os.MkdirAll(dir, 0700); err != nil {
		return fmt.Errorf("create settings directory: %w", err)
	}
	file, err := os.CreateTemp(dir, ".mothership-*")
	if err != nil {
		return err
	}
	defer os.Remove(file.Name())
	_, writeErr := file.Write(data.Bytes())
	closeErr := file.Close()
	if writeErr != nil {
		return writeErr
	}
	if closeErr != nil {
		return closeErr
	}
	return os.Rename(file.Name(), path)
}
