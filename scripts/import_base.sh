#!/bin/bash
# import_base.sh — Import a rootfs tar as a Docksmith base image
#
# ONE-TIME SETUP: Download Alpine minimal rootfs tar, then run this script.
#
# How to get Alpine Linux rootfs:
#   wget -O /tmp/alpine-rootfs.tar.gz \
#     https://dl-cdn.alpinelinux.org/alpine/v3.19/releases/x86_64/alpine-minirootfs-3.19.1-x86_64.tar.gz
#   gunzip /tmp/alpine-rootfs.tar.gz
#   # This gives you /tmp/alpine-rootfs.tar
#
# Usage:
#   ./scripts/import_base.sh <rootfs.tar> <name> <tag>
# Example:
#   ./scripts/import_base.sh /tmp/alpine-rootfs.tar base latest
#

set -euo pipefail

if [ "$#" -ne 3 ]; then
    echo "Usage: $0 <rootfs.tar> <name> <tag>"
    echo ""
    echo "Example:"
    echo "  $0 /tmp/alpine-rootfs.tar base latest"
    echo ""
    echo "To get Alpine Linux rootfs:"
    echo "  wget -O /tmp/alpine-rootfs.tar.gz \\"
    echo "    https://dl-cdn.alpinelinux.org/alpine/v3.19/releases/x86_64/alpine-minirootfs-3.19.1-x86_64.tar.gz"
    echo "  gunzip /tmp/alpine-rootfs.tar.gz"
    exit 1
fi

TARFILE="$1"
IMG_NAME="$2"
IMG_TAG="$3"

if [ ! -f "$TARFILE" ]; then
    echo "Error: File not found: $TARFILE"
    exit 1
fi

DOCKSMITH_HOME="${HOME}/.docksmith"
LAYERS_DIR="${DOCKSMITH_HOME}/layers"
IMAGES_DIR="${DOCKSMITH_HOME}/images"
mkdir -p "$LAYERS_DIR" "$IMAGES_DIR"

echo "Importing ${TARFILE} as ${IMG_NAME}:${IMG_TAG} ..."

# Compute SHA-256 of the tar file
TAR_HASH=$(sha256sum "$TARFILE" | awk '{print $1}')
DIGEST="sha256:${TAR_HASH}"
DEST_TAR="${LAYERS_DIR}/${DIGEST}.tar"

# Copy tar to the layers directory (or skip if already there)
if [ -f "$DEST_TAR" ]; then
    echo "Layer already exists: ${DIGEST}"
else
    cp "$TARFILE" "$DEST_TAR"
    echo "Layer stored: ${DIGEST}"
fi

FILE_SIZE=$(stat -c%s "$DEST_TAR")
CREATED=$(date -u '+%Y-%m-%dT%H:%M:%SZ')

# Build manifest JSON
MANIFEST_PATH="${IMAGES_DIR}/${IMG_NAME}_${IMG_TAG}.json"

cat > "$MANIFEST_PATH" <<EOF
{
  "name": "${IMG_NAME}",
  "tag": "${IMG_TAG}",
  "digest": "${DIGEST}",
  "created": "${CREATED}",
  "config": {
    "Env": [],
    "Cmd": ["/bin/sh"],
    "WorkingDir": "/"
  },
  "layers": [
    {"digest": "${DIGEST}", "size": ${FILE_SIZE}, "createdBy": "import_base.sh"}
  ]
}
EOF

echo "Manifest written: ${MANIFEST_PATH}"
echo ""
echo "Import complete!"
echo "  Image : ${IMG_NAME}:${IMG_TAG}"
echo "  Digest: ${DIGEST:0:19}..."
echo "  Size  : ${FILE_SIZE} bytes"
