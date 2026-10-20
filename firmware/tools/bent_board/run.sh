#!/bin/sh
# The bent board's decoder (include/bent_board.h) against the modelled readings in vectors.txt
# (remake them with ~/minichord-bench/bentboard/model.py <this folder>).
set -e
cd "$(dirname "$0")"
g++ -std=c++17 -O2 -Wall -Wextra -I../../include -o test_bent_board test_bent_board.cpp
./test_bent_board vectors.txt
