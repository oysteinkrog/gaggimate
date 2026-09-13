# Host-side bganim profiler. Build:  make            (-> build/bench)
# Per-agent binary:                  make BIN=build/bench_myanim
# The whole thing compiles monolithically (one g++ call) — ~3 s, no object
# files to race on when several agents build concurrently.
BIN ?= build/bench
ANIM_DIR := ../../src/display/ui/default/bganim
SRCS := bench.cpp mathcount.cpp $(wildcard $(ANIM_DIR)/*.cpp)
CXXFLAGS := -O2 -g -std=gnu++17 -Ishim -I. -include mathcount.h -Wall -Wno-unused-function

# Every binary here links the firmware's own sources, so a header-only change
# to the firmware has to rebuild it. Listing headers by hand does not hold:
# build/ramp_dump, the palette parity oracle, went in without BgAnimCommon.h
# among its prerequisites, and that is the header defining the rgb565 function
# its output is made of, so `make check` would have kept comparing the JS
# sampler against a stale binary (gm-nov3.9).
#
# So the header list is written by the compiler. `depgen` runs the
# preprocessor over the same sources and prints one rule per source, each with
# the binary as its target, and that file is included on the next run. -MP adds
# an empty rule per header so a deleted or renamed header does not wedge make
# with "no rule to make target". The file is named after the binary, because
# agents build concurrently with different BIN values. It cannot be a single
# -MF pass: with several sources on one command line -MF keeps only the last
# one's headers.
#
# The explicit prerequisites below stay as the floor for the first build,
# before any .d exists. The Makefile is one of them, for two reasons: a change
# to CXXFLAGS has to rebuild too, and a binary built by an older version of
# this file has no .d, so that one rebuild is what writes the first header
# list.
depgen = @g++ $(CXXFLAGS) -MM -MP -MT $(1) $(2) > $(1).d.tmp && mv -f $(1).d.tmp $(1).d
BASE_DEPS := mathcount.h shim/Arduino.h shim/esp_heap_caps.h shim/esp_memory_utils.h Makefile

$(BIN): $(SRCS) $(BASE_DEPS)
	@mkdir -p $(dir $(BIN))
	g++ $(CXXFLAGS) $(SRCS) -o $(BIN) -lm
	$(call depgen,$(BIN),$(SRCS))

# Band call-shape invariance checker. Same monolithic compile, different main.
ICBIN ?= build/interlace_check
IC_SRCS := interlace_check.cpp mathcount.cpp $(wildcard $(ANIM_DIR)/*.cpp)

$(ICBIN): $(IC_SRCS) $(BASE_DEPS)
	@mkdir -p $(dir $(ICBIN))
	g++ $(CXXFLAGS) $(IC_SRCS) -o $(ICBIN) -lm
	$(call depgen,$(ICBIN),$(IC_SRCS))

# The firmware's palette arithmetic on the host, so the JS sampler the
# gradient tools use can be proved equal to it instead of assumed equal.
RDBIN ?= build/ramp_dump
RD_SRCS := ramp_dump.cpp mathcount.cpp $(wildcard $(ANIM_DIR)/*.cpp)

$(RDBIN): $(RD_SRCS) $(BASE_DEPS)
	@mkdir -p $(dir $(RDBIN))
	g++ $(CXXFLAGS) $(RD_SRCS) -o $(RDBIN) -lm
	$(call depgen,$(RDBIN),$(RD_SRCS))

# The settings picker's gradient swatch against the panel's own palette. The
# swatch sampler is a transcription (it must not publish a theme, and it has to
# run on the simulator, where BgAnimCommon.cpp does not compile), so it is
# proved equal rather than assumed equal.
SPBIN ?= build/swatch_parity
SP_SRCS := swatch_parity.cpp mathcount.cpp ../../src/display/ui/default/settings/GradientSwatch.cpp \
	$(wildcard $(ANIM_DIR)/*.cpp)

# GradientSwatch.h includes its neighbours the way the firmware does
# (<display/...>), so this one target needs src on the include path. A
# target-specific variable, so the dependency generator below sees it too.
$(SPBIN): CXXFLAGS += -I../../src

$(SPBIN): $(SP_SRCS) $(BASE_DEPS)
	@mkdir -p $(dir $(SPBIN))
	g++ $(CXXFLAGS) $(SP_SRCS) -o $(SPBIN) -lm
	$(call depgen,$(SPBIN),$(SP_SRCS))

# Does the firmware still draw what the page designed? The goldens come from
# the firmware, so --compare can only prove the firmware has not changed since
# yesterday. This is the only check that sees a port drifting away from the
# design in web/anim_bench.html. Allowances for the debt recorded on
# 2026-09-12 live in page_exact.json.
pagecheck:
	node web/page_vs_golden.js

# What CI runs. Every part exits non-zero on failure.
#
# The four gradient steps are in order on purpose: the generated tables must
# match data/gradients.json, the JS sampler and the settings picker's swatch
# sampler must each match the firmware's own palette arithmetic, and only then
# does the set check mean anything, because it measures through the JS one.
check: $(BIN) $(ICBIN) $(RDBIN) $(SPBIN)
	bash dep_check.sh
	python3 ../../scripts/gen_gradients.py --check
	node web/ramp_parity.js
	./$(SPBIN)
	python3 ../gradient_check.py
	python3 ../gradient_check_test.py
	./$(BIN) --compare golden --frames 240
	./$(ICBIN)
	node web/page_vs_golden.js

clean:
	rm -rf build
.PHONY: check pagecheck clean

# Last, after the default goal, so nothing read from a generated file can
# become the target a bare `make` builds. A .d that is not there yet is not an
# error: the first build writes it.
-include $(BIN).d $(ICBIN).d $(RDBIN).d $(SPBIN).d
