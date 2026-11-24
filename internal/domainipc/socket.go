// Package domainipc defines the private local control socket for the nested WM.
// The wire uses short, one-command Unix packets, never input or framebuffer data.
package domainipc

import (
	"context"
	"fmt"
	"net"
	"os"
	"path/filepath"
	"syscall"
	"time"
)

const (
	MaxPacket = 16384
	Timeout   = 3 * time.Second
)

func Path(runtime string) (string, error) {
	if !filepath.IsAbs(runtime) {
		return "", fmt.Errorf("XDG_RUNTIME_DIR must be absolute")
	}
	st, err := os.Lstat(runtime)
	if err != nil {
		return "", err
	}
	if !st.IsDir() || st.Mode().Perm() != 0700 || st.Sys().(*syscall.Stat_t).Uid != uint32(os.Geteuid()) {
		return "", fmt.Errorf("XDG_RUNTIME_DIR must be owned by this user and have mode 0700")
	}
	return filepath.Join(runtime, "mothership-desktopd.sock"), nil
}

func Dial(ctx context.Context, path string) (*net.UnixConn, error) {
	c, err := (&net.Dialer{Timeout: Timeout}).DialContext(ctx, "unixpacket", path)
	if err != nil {
		return nil, err
	}
	return c.(*net.UnixConn), nil
}

func Read(c *net.UnixConn) (string, error) {
	var buf [MaxPacket]byte
	n, _, flags, _, err := c.ReadMsgUnix(buf[:], nil)
	if err != nil {
		return "", err
	}
	if n == 0 || flags&syscall.MSG_TRUNC != 0 {
		return "", fmt.Errorf("invalid control packet size")
	}
	return string(buf[:n]), nil
}

func Write(c *net.UnixConn, packet string) error {
	if len(packet) == 0 || len(packet) > MaxPacket {
		return fmt.Errorf("invalid control packet size")
	}
	_ = c.SetWriteDeadline(time.Now().Add(Timeout))
	_, err := c.Write([]byte(packet))
	return err
}
