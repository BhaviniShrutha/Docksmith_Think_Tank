#!/usr/bin/env bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=./report_common.sh
source "$SCRIPT_DIR/report_common.sh"

print_header "Part 2: Docksmithfile Parsing and Instruction Handling"

run_block "Check base image readiness for parser/build demos" \
"These parser demonstrations rely on FROM base:latest, so the base manifest and layer must both exist." <<'CMDS'
sudo test -f /root/.docksmith/images/base_latest.json && echo "base_manifest=present" || echo "base_manifest=missing"
require_base_ready
CMDS

run_block "Show that the sample app uses all 6 required instructions" \
"This is the easiest way to explain the supported Docksmith language before moving into focused parser checks." <<'CMDS'
cat ./sample_app/Docksmithfile
CMDS

run_block "Build a minimal CMD parsing example with commas" \
"If the manifest preserves echo a,b as one argument, then CMD JSON-array parsing is behaving correctly." <<'CMDS'
require_base_ready || exit 1
rm -rf /tmp/docksmith_report_cmd
mkdir -p /tmp/docksmith_report_cmd
cat > /tmp/docksmith_report_cmd/Docksmithfile <<'EOF'
FROM base:latest
CMD ["/bin/sh","-c","echo a,b"]
EOF
sudo ./build/docksmith build -t report-cmdverify:latest /tmp/docksmith_report_cmd
sudo cat /root/.docksmith/images/report-cmdverify_latest.json
CMDS

run_block "Build a COPY glob example and inspect the produced layer" \
"If the COPY layer contains app/a.txt and app/sub/b.txt, then the tested ** glob expansion is working." <<'CMDS'
require_base_ready || exit 1
rm -rf /tmp/docksmith_report_glob
mkdir -p /tmp/docksmith_report_glob/src/sub
printf 'top\n' > /tmp/docksmith_report_glob/src/a.txt
printf 'nested\n' > /tmp/docksmith_report_glob/src/sub/b.txt
printf 'skip\n' > /tmp/docksmith_report_glob/src/sub/c.log
cat > /tmp/docksmith_report_glob/Docksmithfile <<'EOF'
FROM base:latest
COPY src/**/*.txt /app/
CMD ["/bin/sh"]
EOF
sudo ./build/docksmith build -t report-globverify:latest /tmp/docksmith_report_glob
copy_layer="$(sudo awk -F'"' '/"digest"/ {count++; if (count == 3) {print $4; exit}}' /root/.docksmith/images/report-globverify_latest.json)"
echo "copy_layer=${copy_layer}"
sudo tar -tf "/root/.docksmith/layers/${copy_layer}.tar"
CMDS
