#!/usr/bin/env bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=./report_common.sh
source "$SCRIPT_DIR/report_common.sh"

print_header "Docksmith Report Setup: Base Image Readiness"

run_block "Check whether the base manifest and base layer exist" \
"The presentation scripts need a valid local base image before any FROM base:latest build can work." <<'CMDS'
sudo test -f /root/.docksmith/images/base_latest.json && echo "base_manifest=present" || echo "base_manifest=missing"
if sudo test -f /root/.docksmith/images/base_latest.json; then
    layer_digest="$(sudo awk -F'"' '/"digest"/ {count++; if (count == 2) {print $4; exit}}' /root/.docksmith/images/base_latest.json)"
    echo "base_layer_digest=${layer_digest}"
    sudo test -f "/root/.docksmith/layers/${layer_digest}.tar" && echo "base_layer=present" || echo "base_layer=missing"
fi
CMDS

run_block "Re-import the base image if /tmp/alpine-rootfs.tar is available" \
"If the base layer tar was deleted by a previous rmi, this restores the base image state for all later demos." <<'CMDS'
if test -f /tmp/alpine-rootfs.tar; then
    sudo ./scripts/import_base.sh /tmp/alpine-rootfs.tar base latest
else
    echo "missing_source_tar=/tmp/alpine-rootfs.tar"
    echo "Run:"
    echo "  wget -O /tmp/alpine-rootfs.tar.gz https://dl-cdn.alpinelinux.org/alpine/v3.19/releases/x86_64/alpine-minirootfs-3.19.1-x86_64.tar.gz"
    echo "  gunzip -f /tmp/alpine-rootfs.tar.gz"
    echo "  sudo ./scripts/import_base.sh /tmp/alpine-rootfs.tar base latest"
fi
CMDS

run_block "Show the final base manifest" \
"A valid base manifest plus an existing layer tar means later report scripts can build images from FROM base:latest." <<'CMDS'
if sudo test -f /root/.docksmith/images/base_latest.json; then
    sudo cat /root/.docksmith/images/base_latest.json
else
    echo "base_manifest_still_missing"
fi
CMDS

