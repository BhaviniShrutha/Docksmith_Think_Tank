#!/usr/bin/env bash
set -u

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

print_header() {
    local title="$1"
    printf '\n%s\n' "========================================================================"
    printf '%s\n' "$title"
    printf '%s\n\n' "========================================================================"
}

run_block() {
    local title="$1"
    local inference="$2"
    local tmp shown_tmp output status

    tmp="$(mktemp)"
    shown_tmp="$(mktemp)"
    {
        printf '%s\n' 'set -u'
        printf 'source "%s"\n' "$REPO_ROOT/scripts/report_common.sh"
        cat
    } > "$tmp"
    tail -n +3 "$tmp" > "$shown_tmp"

    printf '%s\n' "------------------------------------------------------------------------"
    printf 'title: %s\n' "$title"
    printf 'cmd:\n'
    sed 's/^/  /' "$shown_tmp"

    output="$(
        cd "$REPO_ROOT" &&
        bash "$tmp" 2>&1
    )"
    status=$?

    printf 'op:\n'
    if [ -n "$output" ]; then
        printf '%s\n' "$output" | sed 's/^/  /'
    else
        printf '  <no output>\n'
    fi

    printf 'inference: %s (exit=%d)\n\n' "$inference" "$status"
    rm -f "$tmp"
    rm -f "$shown_tmp"
}

require_base_ready() {
    if ! sudo test -f /root/.docksmith/images/base_latest.json; then
        echo "base_manifest=missing"
        return 1
    fi

    local layer_digest
    layer_digest="$(sudo awk -F'"' '/"digest"/ {count++; if (count == 2) {print $4; exit}}' /root/.docksmith/images/base_latest.json)"
    echo "base_layer_digest=${layer_digest}"

    if [ -z "$layer_digest" ]; then
        echo "base_layer_digest=unreadable"
        return 1
    fi

    if ! sudo test -f "/root/.docksmith/layers/${layer_digest}.tar"; then
        echo "base_layer=missing"
        echo "Fix:"
        echo "  wget -O /tmp/alpine-rootfs.tar.gz https://dl-cdn.alpinelinux.org/alpine/v3.19/releases/x86_64/alpine-minirootfs-3.19.1-x86_64.tar.gz"
        echo "  gunzip -f /tmp/alpine-rootfs.tar.gz"
        echo "  sudo ./scripts/import_base.sh /tmp/alpine-rootfs.tar base latest"
        return 1
    fi

    echo "base_layer=present"
}
