# Host checks for the tick ring compositor element (gm-2cl.6).
#
#   make -C tools/tickringbench check
#
# Compiles src/display/ui/default/TickRingElement.cpp with the host compiler
# (it has no LVGL or ESP-IDF dependency) and runs test.cpp: the row
# compositor against a reference of LVGL's masked fill plus the overlay
# blend, the lit-range rule, the boundary colour, and row independence.
CXX ?= g++
CXXFLAGS ?= -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-omit-frame-pointer
SRC := ../../src/display/ui/default/TickRingElement.cpp
BUILD := build

$(BUILD)/test: test.cpp $(SRC) ../../src/display/ui/default/TickRingElement.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -I../../src test.cpp $(SRC) -o $@

$(BUILD):
	mkdir -p $(BUILD)

check: $(BUILD)/test
	ASAN_OPTIONS=verify_asan_link_order=0 ./$(BUILD)/test

clean:
	rm -rf $(BUILD)

.PHONY: check clean
