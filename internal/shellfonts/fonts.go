// Package shellfonts supplies the same bundled typefaces to the Panel and WM.
package shellfonts

import (
	"embed"
	"io/fs"
	"os"
	"path/filepath"
)

//go:embed fonts/*/*.ttf fonts/*/OFL.txt
var bundled embed.FS

// Extract writes private font files that must stay available until the
// caller's process-local Pango renderer has finished using them.
func Extract(runtimeDir string) ([]string, func(), error) {
	dir, err := os.MkdirTemp(runtimeDir, "shell-fonts-")
	if err != nil {
		return nil, nil, err
	}
	cleanup := func() { _ = os.RemoveAll(dir) }
	names, err := fs.Glob(bundled, "fonts/*/*.ttf")
	if err != nil {
		cleanup()
		return nil, nil, err
	}
	paths := make([]string, 0, len(names))
	for _, name := range names {
		path := filepath.Join(dir, filepath.Base(name))
		data, err := bundled.ReadFile(name)
		if err == nil {
			err = os.WriteFile(path, data, 0600)
		}
		if err != nil {
			cleanup()
			return nil, nil, err
		}
		paths = append(paths, path)
	}
	return paths, cleanup, nil
}
