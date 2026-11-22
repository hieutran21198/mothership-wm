package unixapi

import (
	"context"
	"errors"
	"fmt"
	"strings"
	"time"

	"mothership-wm/internal/domainipc"
)

func Request(ctx context.Context, path string, args []string) (string, error) {
	for _, s := range args {
		if !validWord(s) {
			return "", fmt.Errorf("invalid command argument")
		}
	}
	c, err := domainipc.Dial(ctx, path)
	if err != nil {
		return "", err
	}
	defer c.Close()
	stop := context.AfterFunc(ctx, func() { c.Close() })
	defer stop()
	_ = c.SetReadDeadline(time.Now().Add(2 * domainipc.Timeout))
	if err := domainipc.Write(c, strings.Join(args, " ")); err != nil {
		return "", err
	}
	reply, err := domainipc.Read(c)
	if err != nil {
		return "", err
	}
	if msg, bad := strings.CutPrefix(reply, "err "); bad {
		return "", errors.New(msg)
	}
	return reply, nil
}
