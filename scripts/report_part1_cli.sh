#!/usr/bin/env bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=./report_common.sh
source "$SCRIPT_DIR/report_common.sh"

print_header "Part 1: CLI and Command Routing"

run_block "Environment and permissions" \
"This confirms the project is being demonstrated on Linux with sudo access available for namespace and store operations." <<'CMDS'
pwd
uname -a
id
sudo -v
CMDS

run_block "Build the docksmith binary" \
"A successful build proves the repository compiles into a single executable CLI." <<'CMDS'
make
ls -l ./build/docksmith
CMDS

run_block "Show the command surface" \
"The usage text shows the CLI exposes build, images, rmi, and run as the public commands." <<'CMDS'
./build/docksmith 2>&1 || true
CMDS

run_block "Route a real CLI command into the image store" \
"Running images proves the CLI is dispatching into the implementation rather than just printing help text." <<'CMDS'
sudo ./build/docksmith images
CMDS

