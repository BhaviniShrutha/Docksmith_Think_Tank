#!/usr/bin/env bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=./report_common.sh
source "$SCRIPT_DIR/report_common.sh"

print_header "Part 3: Build Engine, Cache, Layers, and Manifest"

run_block "Prepare a clean temporary build context" \
"Using a temp copy of sample_app lets us demonstrate cache invalidation without editing tracked repo files." <<'CMDS'
rm -rf /tmp/docksmith_report_build
cp -r ./sample_app /tmp/docksmith_report_build
printf 'nonce:%s\n' "$(date +%s%N)" > /tmp/docksmith_report_build/.report_nonce
find /tmp/docksmith_report_build -maxdepth 2 -type f | sort
CMDS

run_block "Check base image readiness for build/cache demos" \
"Build and cache demonstrations cannot run unless FROM base:latest can resolve to a real layer tar." <<'CMDS'
sudo test -f /root/.docksmith/images/base_latest.json && echo "base_manifest=present" || echo "base_manifest=missing"
require_base_ready
CMDS

run_block "Force a real build with --no-cache" \
"This produces actual COPY and RUN layers so we can explain how the build engine writes content-addressed deltas." <<'CMDS'
require_base_ready || exit 1
sudo ./build/docksmith build -t report-myapp:latest --no-cache /tmp/docksmith_report_build
CMDS

run_block "Run a normal rebuild to populate cache entries" \
"Because the previous build used --no-cache, this rebuild is expected to execute normally and write cache entries." <<'CMDS'
require_base_ready || exit 1
sudo ./build/docksmith build -t report-myapp:latest /tmp/docksmith_report_build
CMDS

run_block "Run a warm rebuild to demonstrate cache hits" \
"A successful warm build with [CACHE HIT] shows that the cache key and stored layer reuse path are working." <<'CMDS'
require_base_ready || exit 1
sudo ./build/docksmith build -t report-myapp:latest /tmp/docksmith_report_build
CMDS

run_block "Edit one source file and rebuild to show invalidation" \
"After a source-file change, the affected COPY step and downstream RUN step should become cache misses again." <<'CMDS'
require_base_ready || exit 1
printf '\necho "report edit"\n' >> /tmp/docksmith_report_build/app.sh
sudo ./build/docksmith build -t report-myapp:latest /tmp/docksmith_report_build
CMDS

run_block "Inspect images and the final manifest" \
"The image list and manifest prove that Docksmith stores image metadata separately from immutable layer tars." <<'CMDS'
require_base_ready || exit 1
sudo ./build/docksmith images
sudo cat /root/.docksmith/images/report-myapp_latest.json
CMDS
