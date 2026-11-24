# The progression upload, tested on a computer

generator/progression.md is the contract. harness.cpp compiles include/progression_core.h and the
Teensy's side, include/progression.h, with the computer's g++ against a fake of the parts of main.cpp it
uses (chord memory, the chord tables, the rhythm's bar, USB MIDI), sends it every message the contract has
and checks each reply byte for byte. It builds its messages with its own CRC and 7-bit numbers, not the
core's. Nothing here talks to a minichord.

    make                        # builds build/harness (ASan and UBSan on, warnings are errors) and runs it
    make BUILD=/tmp/somewhere   # the same, built outside the repository

It needs g++ (C++17). The last line says how many checks failed; the exit status is 1 if any did.
