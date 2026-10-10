#!/usr/bin/env bash
# Renders the drum machine (include/drum_machine.h) and every kit and groove (include/drum_patterns.h, from
# ../drums.py) on this computer, with stand-ins for the Teensy core (Arduino.h, AudioStream.h here), to
# out/grooves/NN-name.wav (four bars at 96 bpm, the fill, the crash) and out/kits/NN-name.wav (each voice in
# turn), printing each one's peak. For tuning by ear without flashing:
#     python3 ../drums.py && ./render.sh
set -e
cd "$(dirname "$0")"
python3 -c "import math; print(','.join(str(round(32767*math.sin(2*math.pi*i/256))) for i in range(257)))" > sine.inc
mkdir -p out/grooves out/kits
g++ -O2 -std=gnu++17 -I. -I../../include render.cpp -o out/render
cd out && ./render
