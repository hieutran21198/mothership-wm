#!/bin/sh
set -eu
cd "$(dirname "$0")"
if [ "$#" -eq 0 ]; then set -- wm panel; fi
for role in "$@"; do
  case "$role" in wm|panel) ;; *) echo "expected wm or panel" >&2; exit 2;; esac
  out="../../apps/$role"
  side=client
  if [ "$role" = wm ]; then side=server; fi
  tag=$role
  if [ "$role" = wm ]; then tag=wlroots; fi
  wayland-scanner "$side-header" mothership-map-pointer-v1.xml "$out/generated-map-pointer.h"
  {
    printf '//go:build linux && cgo && %s\n\n' "$tag"
    wayland-scanner private-code < mothership-map-pointer-v1.xml
  } > "$out/generated-map-pointer.c"
done
