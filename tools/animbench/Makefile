# Host-side bganim profiler. Build:  make            (-> build/bench)
# Per-agent binary:                  make BIN=build/bench_myanim
# The whole thing compiles monolithically (one g++ call) — ~3 s, no object
# files to race on when several agents build concurrently.
BIN ?= build/bench
ANIM_DIR := ../../src/display/ui/default/bganim
SRCS := bench.cpp mathcount.cpp $(wildcard $(ANIM_DIR)/*.cpp)
CXXFLAGS := -O2 -g -std=gnu++17 -Ishim -I. -include mathcount.h -Wall -Wno-unused-function

$(BIN): $(SRCS) mathcount.h shim/Arduino.h shim/esp_heap_caps.h shim/esp_memory_utils.h
	@mkdir -p $(dir $(BIN))
	g++ $(CXXFLAGS) $(SRCS) -o $(BIN) -lm

# Band call-shape invariance checker. Same monolithic compile, different main.
ICBIN ?= build/interlace_check
IC_SRCS := interlace_check.cpp mathcount.cpp $(wildcard $(ANIM_DIR)/*.cpp)

$(ICBIN): $(IC_SRCS) mathcount.h shim/Arduino.h shim/esp_heap_caps.h shim/esp_memory_utils.h
	@mkdir -p $(dir $(ICBIN))
	g++ $(CXXFLAGS) $(IC_SRCS) -o $(ICBIN) -lm

# Does the firmware still draw what the page designed? The goldens come from
# the firmware, so --compare can only prove the firmware has not changed since
# yesterday. This is the only check that sees a port drifting away from the
# design in web/anim_bench.html. Allowances for the debt recorded on
# 2026-09-12 live in page_exact.json.
pagecheck:
	node web/page_vs_golden.js

# What CI runs. Every part exits non-zero on failure.
check: $(BIN) $(ICBIN)
	python3 ../../scripts/gen_gradients.py --check
	./$(BIN) --compare golden --frames 240
	./$(ICBIN)
	node web/page_vs_golden.js

clean:
	rm -rf build
.PHONY: check pagecheck clean
