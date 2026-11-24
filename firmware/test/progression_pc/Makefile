# The progression upload (include/progression_core.h and include/progression.h) tested on a computer: see README.md.
#   make            build the harness and run it
#   make BUILD=dir  build in dir instead of ./build
FIRMWARE := $(abspath ../..)
BUILD    ?= build
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined

.PHONY: test clean
test: $(BUILD)/harness
	$(BUILD)/harness

$(BUILD)/harness: harness.cpp $(FIRMWARE)/include/progression_core.h $(FIRMWARE)/include/progression.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -I$(FIRMWARE)/include harness.cpp -o $@

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD)
