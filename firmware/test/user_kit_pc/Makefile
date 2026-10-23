# The user drum kit's slot (include/user_kit_core.h) tested on a computer: see README.md.
#   make            build the harness and run it against test/kits/
#   make BUILD=dir  build in dir instead of ./build
FIRMWARE := $(abspath ../..)
KITS     := $(FIRMWARE)/test/kits
BUILD    ?= build
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined
PYTHON   ?= python3

.PHONY: test cross clean
test: $(BUILD)/harness $(BUILD)/voices.txt
	$(BUILD)/harness $(KITS) $(BUILD)/voices.txt $(FIRMWARE)/include/drum_patterns.h

$(BUILD)/harness: harness.cpp $(FIRMWARE)/include/user_kit_core.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -I$(FIRMWARE)/include harness.cpp -o $@

$(BUILD)/voices.txt: expect.py $(KITS)/synthetic.json | $(BUILD)
	$(PYTHON) expect.py $(KITS)/synthetic.json > $@

# minictl's kit.py and minicontrol's kits.js against the vectors, where they're there
cross:
	$(PYTHON) cross_check.py $(KITS)

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD)
