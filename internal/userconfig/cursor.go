package userconfig

import "encoding/json"

const (
	CursorSizeDefault = 24
	CursorSizeMin     = 16
	CursorSizeMax     = 64
)

func ValidCursorSize(size int) bool {
	return size >= CursorSizeMin && size <= CursorSizeMax
}

// CursorSize returns the logical size of the Mothership pointer.
func CursorSize(doc Object) int {
	home, err := doc.Child("home_panel")
	if err == nil {
		var size int
		if json.Unmarshal(home["cursor_size"], &size) == nil && ValidCursorSize(size) {
			return size
		}
	}
	return CursorSizeDefault
}
