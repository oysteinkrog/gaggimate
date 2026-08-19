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

# What CI runs. Both halves exit non-zero on failure.
check: $(BIN) $(ICBIN)
	./$(BIN) --compare golden --frames 240
	./$(ICBIN)

clean:
	rm -rf build
.PHONY: check clean
