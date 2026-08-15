# Host-side bganim profiler. Build:  make            (-> build/bench)
# Per-agent binary:                  make BIN=build/bench_myanim
# The whole thing compiles monolithically (one g++ call) — ~3 s, no object
# files to race on when several agents build concurrently.
BIN ?= build/bench
ANIM_DIR := ../../src/display/ui/default/bganim
SRCS := bench.cpp mathcount.cpp $(wildcard $(ANIM_DIR)/*.cpp)
CXXFLAGS := -O2 -g -std=gnu++17 -Ishim -I. -include mathcount.h -Wall -Wno-unused-function

$(BIN): $(SRCS) mathcount.h shim/Arduino.h shim/esp_heap_caps.h
	@mkdir -p $(dir $(BIN))
	g++ $(CXXFLAGS) $(SRCS) -o $(BIN) -lm

clean:
	rm -rf build
.PHONY: clean
