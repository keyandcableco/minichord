#!/bin/bash
# The boot-switch spike, built from clean and checked: the updater first (the app carries it), then the
# switch-test app, then scripts/check_spike.py. Compiles only: nothing here flashes a minichord.
set -euo pipefail
cd "$(dirname "$0")"
pio run -t clean -e updater -e switch_test > /dev/null
for env in updater switch_test; do
  pio run -e "$env" 2>&1 | tee ".pio/build_$env.log" | grep -E 'warning|error|embed_updater|teensy_size|SUCCESS|FAILED' || true
  grep -q '\[SUCCESS\]' ".pio/build_$env.log"
done
python3 scripts/check_spike.py
