#!/bin/sh
# The chord walk (include/chord_walk.h) against the chords it should make
set -e
cd "$(dirname "$0")"
g++ -std=c++17 -O1 -Wall -Wextra -Werror -fsanitize=address,undefined -I../../include -o test_chord_walk test_chord_walk.cpp
./test_chord_walk
