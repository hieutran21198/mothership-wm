// Command mothership starts one private nested session under an existing
// Wayland compositor. It does not create a desktop login session.
package main

import (
	"bufio"
	"context"
	"errors"
	"fmt"
	"io"
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"strings"
	"syscall"
	"time"

	"mothership-wm/internal/domainipc"
)

const (
	startTimeout = 10 * time.Second
	stopTimeout  = 3 * time.Second
)

type notice struct {
	part string
	line string
	exit bool
	err  error
}

type child struct {
	process *os.Process
	done    chan struct{}
}

type session struct {
	ctx      context.Context
	bin      string
	env      []string
	notices  chan notice
	children []child
	err      io.Writer
}

func main() {
	if len(os.Args) != 1 {
		fmt.Fprintln(os.Stderr, "usage: mothership (run from the existing Wayland session)")
		os.Exit(2)
	}
	binary, err := os.Executable()
	if err == nil {
		ctx, cancel := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
		err = run(ctx, filepath.Dir(binary), os.Environ(), os.Stdout, os.Stderr)
		cancel()
	}
	if err != nil {
		fmt.Fprintln(os.Stderr, "mothership:", err)
		os.Exit(1)
	}
}

func run(ctx context.Context, bin string, original []string, out, stderr io.Writer) error {
	initial := make(map[string]string)
	for _, entry := range original {
		k, v, found := strings.Cut(entry, "=")
		if found {
			initial[k] = v
		}
	}
	runtime := initial["XDG_RUNTIME_DIR"]
	if _, err := domainipc.Path(runtime); err != nil {
		return fmt.Errorf("parent Wayland: %w", err)
	}
	parent, err := parentPath(runtime, initial["WAYLAND_DISPLAY"])
	if err != nil {
		return err
	}
	owned, err := os.MkdirTemp(runtime, "mothership-")
	if err != nil {
		return fmt.Errorf("session runtime: %w", err)
	}
	defer os.RemoveAll(owned)

	env := make([]string, 0, len(original)+2)
	for _, entry := range original {
		k, _, _ := strings.Cut(entry, "=")
		switch k {
		case "XDG_RUNTIME_DIR", "WAYLAND_DISPLAY", "WAYLAND_SOCKET", "DISPLAY":
			continue
		}
		env = append(env, entry)
	}
	env = append(env, "XDG_RUNTIME_DIR="+owned, "WAYLAND_DISPLAY="+parent)
	s := &session{ctx: ctx, bin: bin, env: env, notices: make(chan notice, 32), err: stderr}
	defer s.stop()

	if err := s.start("desktopd", "mothership-desktopd", "serve"); err != nil {
		return err
	}
	if _, err := s.wait("desktopd", "DESKTOPD_READY"); err != nil {
		return startupError(ctx, err)
	}
	if err := s.start("WM", "mothership-wm", "--desktopd", "--parent", parent); err != nil {
		return startupError(ctx, err)
	}
	line, err := s.wait("WM", "WAYLAND_DISPLAY=")
	if err != nil {
		return startupError(ctx, err)
	}
	socket := strings.TrimPrefix(line, "WAYLAND_DISPLAY=")
	if filepath.Base(socket) != socket || !strings.HasPrefix(socket, "wayland-") {
		return errors.New("WM startup: invalid child socket")
	}

	if err := s.start("Shell", "mothership-command-center", "--nested", socket); err != nil {
		return startupError(ctx, err)
	}
	if _, err := s.wait("Shell", "SHELL_READY"); err != nil {
		return startupError(ctx, err)
	}
	fmt.Fprintln(out, "Mothership Shell ready in Home")
	fmt.Fprintf(out, "MOTHERSHIP_WAYLAND=%s\n", filepath.Join(owned, socket))
	return s.monitor()
}

func parentPath(runtime, display string) (string, error) {
	if display == "" {
		return "", errors.New("parent Wayland: run from a Wayland terminal with WAYLAND_DISPLAY set")
	}
	if !filepath.IsAbs(display) {
		if filepath.Base(display) != display || display == "." || display == ".." {
			return "", errors.New("parent Wayland: invalid display")
		}
		display = filepath.Join(runtime, display)
	}
	info, err := os.Stat(display)
	if err != nil || info.Mode()&os.ModeSocket == 0 {
		return "", fmt.Errorf("parent Wayland: display socket unavailable: %s", display)
	}
	return display, nil
}

func startupError(ctx context.Context, err error) error {
	if ctx.Err() != nil {
		return nil
	}
	return err
}

func (s *session) start(part, binary string, args ...string) error {
	cmd := exec.Command(filepath.Join(s.bin, binary), args...)
	cmd.Env, cmd.Stderr = s.env, s.err
	cmd.SysProcAttr = &syscall.SysProcAttr{Setpgid: true}
	stdout, err := cmd.StdoutPipe()
	if err != nil {
		return fmt.Errorf("%s startup: %w", part, err)
	}
	if err := cmd.Start(); err != nil {
		stdout.Close()
		return fmt.Errorf("%s startup: %w", part, err)
	}
	c := child{process: cmd.Process, done: make(chan struct{})}
	s.children = append(s.children, c)
	go func() {
		scan := bufio.NewScanner(stdout)
		for scan.Scan() {
			line := scan.Text()
			switch {
			case line == "DESKTOPD_READY", line == "SHELL_READY",
				line == "WM_STOPPING", strings.HasPrefix(line, "WAYLAND_DISPLAY="):
				s.tell(notice{part: part, line: line})
			}
		}
		err := cmd.Wait()
		close(c.done)
		s.tell(notice{part: part, exit: true, err: err})
	}()
	return nil
}

func (s *session) tell(n notice) {
	select {
	case s.notices <- n:
	case <-s.ctx.Done():
	}
}

func (s *session) wait(part, prefix string) (string, error) {
	timer := time.NewTimer(startTimeout)
	defer timer.Stop()
	for {
		select {
		case <-s.ctx.Done():
			return "", s.ctx.Err()
		case <-timer.C:
			return "", fmt.Errorf("%s startup: readiness timed out", part)
		case n := <-s.notices:
			if n.exit {
				return "", exitError(n, "before readiness")
			}
			source := part
			if prefix == "SHELL_READY" {
				source = "WM" // WM acknowledges the shell buffer mapped.
			}
			if n.part == source && strings.HasPrefix(n.line, prefix) {
				return n.line, nil
			}
		}
	}
}

func (s *session) monitor() error {
	for {
		select {
		case <-s.ctx.Done():
			return nil
		case n := <-s.notices:
			if n.part == "WM" && (n.line == "WM_STOPPING" || n.exit && n.err == nil) {
				return nil
			}
			if n.exit {
				return exitError(n, "while running")
			}
		}
	}
}

func exitError(n notice, phase string) error {
	if n.err == nil {
		return fmt.Errorf("%s exited %s", n.part, phase)
	}
	return fmt.Errorf("%s exited %s: %w", n.part, phase, n.err)
}

func (s *session) stop() {
	for i := len(s.children) - 1; i >= 0; i-- {
		c := s.children[i]
		select {
		case <-c.done:
			continue
		default:
		}
		_ = c.process.Signal(syscall.SIGTERM)
		timer := time.NewTimer(stopTimeout)
		select {
		case <-c.done:
			timer.Stop()
		case <-timer.C:
			_ = c.process.Kill()
			<-c.done
		}
	}
}
