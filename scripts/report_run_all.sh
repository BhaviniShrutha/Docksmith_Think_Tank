#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

bash "$SCRIPT_DIR/report_prepare_base.sh"
bash "$SCRIPT_DIR/report_part1_cli.sh"
bash "$SCRIPT_DIR/report_part2_parser.sh"
bash "$SCRIPT_DIR/report_part3_build_cache.sh"
bash "$SCRIPT_DIR/report_part4_runtime.sh"
