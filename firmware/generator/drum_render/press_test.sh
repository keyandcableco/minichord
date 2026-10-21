#!/usr/bin/env bash
# Checks the drum machine's press calls (bend and choke, DRUM PRESS in main.cpp) and the melodic
# ladder (MELODIC PADS) on this computer:
#     ./press_test.sh
set -e
cd "$(dirname "$0")"
python3 -c "import math; print(','.join(str(round(32767*math.sin(2*math.pi*i/256))) for i in range(257)))" > sine.inc
mkdir -p out
g++ -O2 -std=gnu++17 -I. -I../../include press_test.cpp -o out/press_test
g++ -O2 -std=gnu++17 -I. -I../../include melodic_check.cpp -o out/melodic_check
out/press_test
out/melodic_check
