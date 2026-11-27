package main

import "strings"

// pinNoticeBody describes a new comms pin for the notification log.
func pinNoticeBody(app, title string) string {
	subject := app
	if t := strings.TrimSpace(title); t != "" {
		subject += " — " + t
	}
	return subject + " stays in sight in other Nodes. Meta + P or × unpins."
}
