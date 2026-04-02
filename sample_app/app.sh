#!/bin/sh
# Docksmith sample app — uses ENV vars injected at build/run time

GREETING="${GREETING:-Hello}"
TARGET="${TARGET:-World}"

echo "==================================="
echo "  Docksmith container is running!"
echo "==================================="
echo ""
echo "  ${GREETING}, ${TARGET}!"
echo ""
echo "  Working dir : $(pwd)"
# Use static placeholder during build (BUILD_MODE=1) for reproducible layers.
# At container start time (CMD), live values are printed instead.
if [ "${BUILD_MODE:-0}" = "1" ]; then
  echo "  Date        : 1970-01-01 00:00:00 UTC"
  echo "  Hostname    : docksmith-build"
else
  echo "  Date        : $(date '+%Y-%m-%d %H:%M:%S UTC')"
  echo "  Hostname    : $(hostname)"
fi
echo ""
echo "==================================="
