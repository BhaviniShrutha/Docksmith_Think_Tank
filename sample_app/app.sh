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
echo "  Date        : $(date '+%Y-%m-%d %H:%M:%S UTC')"
echo "  Hostname    : $(hostname)"
echo ""
echo "==================================="
