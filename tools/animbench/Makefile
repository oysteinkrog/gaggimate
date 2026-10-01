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

# Uptime check (gm-bzu.50): the same frame steps from 1 minute, 10 days and
# across the millis() wrap must render the same pictures, for every animation
# on bganim::AnimClock. Same monolithic compile.
CCBIN ?= build/clock_check
CC_SRCS := clock_check.cpp mathcount.cpp $(wildcard $(ANIM_DIR)/*.cpp)

$(CCBIN): $(CC_SRCS) mathcount.h shim/Arduino.h shim/esp_heap_caps.h shim/esp_memory_utils.h
	@mkdir -p $(dir $(CCBIN))
	g++ $(CXXFLAGS) $(CC_SRCS) -o $(CCBIN) -lm

# Widths the call-shape check runs at: full resolution and the half path's
# rw = w / 2 on both panels (480 and 466), since the half path hands band()
# 240- and 233-wide rows (SleepAnimation.cpp, gm-bzu.34). Every width runs even
# after one fails, so one CI log names every failing animation and width.
IC_WIDTHS ?= 480 240 466 233

# What CI runs. Both halves exit non-zero on failure.
check: $(BIN) $(ICBIN) $(CCBIN)
	./$(BIN) --compare golden --frames 240
	@fail=""; for w in $(IC_WIDTHS); do \
		echo "== interlace_check $$w x $$w"; \
		./$(ICBIN) $$w $$w || fail="$$fail $$w"; \
	done; \
	if [ -n "$$fail" ]; then echo "interlace_check failed at:$$fail"; exit 1; fi
	@fail=""; for w in $(IC_WIDTHS); do \
		echo "== clock_check $$w x $$w"; \
		./$(CCBIN) --size $$w || fail="$$fail $$w"; \
	done; \
	if [ -n "$$fail" ]; then echo "clock_check failed at:$$fail"; exit 1; fi

clean:
	rm -rf build
.PHONY: check clean
