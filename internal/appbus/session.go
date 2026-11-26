// Package appbus isolates application integration from the parent desktop.
package appbus

import (
	"bufio"
	"context"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"time"
)

const AddressEnv = "MOTHERSHIP_APP_BUS_ADDRESS"

func Start(ctx context.Context, runtime string) (string, func(), error) {
	dir, err := os.MkdirTemp(runtime, "mht-bus-")
	if err != nil {
		return "", nil, err
	}
	// Only owned app launches receive this address. Do not export the host's
	// service directories or session activation environment to the nested bus.
	conf := `<busconfig><type>session</type><listen>unix:path=SOCKET</listen><auth>EXTERNAL</auth>
<policy context="default">
<allow send_destination="*"/><allow receive_sender="*"/><allow own="*"/>
</policy></busconfig>`
	sock := filepath.Join(dir, "bus")
	conf = strings.Replace(conf, "SOCKET", strings.NewReplacer("&", "&amp;", "<", "&lt;", ">", "&gt;").Replace(sock), 1)
	path := filepath.Join(dir, "bus.conf")
	if err = os.WriteFile(path, []byte(conf), 0600); err != nil {
		_ = os.RemoveAll(dir)
		return "", nil, err
	}
	cmd := exec.CommandContext(ctx, "dbus-daemon", "--nofork", "--config-file="+path, "--print-address=1")
	out, err := cmd.StdoutPipe()
	if err != nil {
		_ = os.RemoveAll(dir)
		return "", nil, err
	}
	cmd.Stderr = os.Stderr
	if err = cmd.Start(); err != nil {
		_ = os.RemoveAll(dir)
		return "", nil, err
	}
	stop := func() { _ = cmd.Process.Kill(); _ = cmd.Wait(); _ = os.RemoveAll(dir) }
	got := make(chan string, 1)
	go func() {
		r := bufio.NewReader(out)
		line, _ := r.ReadString('\n')
		got <- strings.TrimSpace(line)
		_, _ = io.Copy(io.Discard, r)
	}()
	select {
	case address := <-got:
		if strings.HasPrefix(address, "unix:path=") {
			return address, stop, nil
		}
	case <-ctx.Done():
	case <-time.After(3 * time.Second):
	}
	stop()
	return "", nil, fmt.Errorf("private application bus did not start")
}
