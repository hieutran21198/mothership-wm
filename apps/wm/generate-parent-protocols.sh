#!/bin/sh
# Generate private client bindings from the pinned Wayland protocol XML.
set -eu
cd "$(dirname "$0")"
protocols=$(pkg-config --variable=pkgdatadir wayland-protocols)
for entry in \
    xdg-shell:stable/xdg-shell/xdg-shell \
    xdg-decoration:unstable/xdg-decoration/xdg-decoration-unstable-v1 \
    fractional-scale:staging/fractional-scale/fractional-scale-v1 \
    viewporter:stable/viewporter/viewporter
do
    name=${entry%%:*}
    xml="$protocols/${entry#*:}.xml"
    wayland-scanner client-header "$xml" "parent-$name-client.h"
    {
        printf '//go:build linux && cgo && wlroots\n\n'
        wayland-scanner private-code < "$xml"
    } > "parent-$name-protocol.c"
done
