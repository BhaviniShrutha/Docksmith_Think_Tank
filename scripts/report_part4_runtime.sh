#!/usr/bin/env bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=./report_common.sh
source "$SCRIPT_DIR/report_common.sh"

print_header "Part 4: Runtime, Isolation, and the Sample App"

run_block "Check base image readiness for runtime demos" \
"The runtime demo image cannot be built unless the base manifest and referenced layer tar are both present." <<'CMDS'
sudo test -f /root/.docksmith/images/base_latest.json && echo "base_manifest=present" || echo "base_manifest=missing"
require_base_ready
CMDS

run_block "Prepare a runtime demo image from the sample app" \
"Building a dedicated report-runtime image makes the runtime demo self-contained." <<'CMDS'
require_base_ready || exit 1
rm -rf /tmp/docksmith_report_runtime
cp -r ./sample_app /tmp/docksmith_report_runtime
sudo ./build/docksmith build -t report-runtime:latest --no-cache /tmp/docksmith_report_runtime
CMDS

run_block "Run the image with its default CMD" \
"This demonstrates normal container startup using the image's stored WorkingDir, Env, and Cmd values." <<'CMDS'
sudo test -f /root/.docksmith/images/report-runtime_latest.json || { echo "report_runtime_image=missing"; exit 1; }
sudo ./build/docksmith run report-runtime:latest
CMDS

run_block "Run the image with runtime environment overrides" \
"If the greeting changes, then docksmith run -e is correctly overriding or adding container environment variables." <<'CMDS'
sudo test -f /root/.docksmith/images/report-runtime_latest.json || { echo "report_runtime_image=missing"; exit 1; }
sudo ./build/docksmith run -e GREETING=Goodbye -e TARGET=Universe report-runtime:latest
CMDS

run_block "Prove filesystem isolation with a host-side check" \
"PASS means a file written inside the container did not leak onto the host filesystem." <<'CMDS'
sudo test -f /root/.docksmith/images/report-runtime_latest.json || { echo "report_runtime_image=missing"; exit 1; }
sudo rm -f /proof.txt
sudo ./build/docksmith run report-runtime:latest /bin/sh -c 'echo isolated > /proof.txt'
sudo test ! -e /proof.txt && echo PASS || echo FAIL
CMDS
