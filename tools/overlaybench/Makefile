# Host-side overlay-pipeline kernel bench. Build:  make        (-> build/bench)
# Per-agent binary:                                 make BIN=build/bench_myworker
# Standalone kernels, no firmware/Arduino shim needed -- these files are
# self-contained extractions, not the real translation unit.
BIN ?= build/bench
SRCS := bench.cpp gen_inputs.cpp \
	kernels/span_scan.cpp kernels/scrim_build.cpp kernels/overlay_blend.cpp kernels/halfres_expand.cpp
CXXFLAGS := -O2 -g -std=gnu++17 -I. -Wall -Wextra -Wno-unused-parameter

$(BIN): $(SRCS) common.h gen_inputs.h \
	kernels/span_scan.h kernels/scrim_build.h kernels/overlay_blend.h kernels/halfres_expand.h
	@mkdir -p $(dir $(BIN))
	g++ $(CXXFLAGS) $(SRCS) -o $(BIN)

# What CI runs: build, then check every registered variant against golden/.
check: $(BIN)
	./$(BIN) --compare golden --iters 50

golden: $(BIN)
	mkdir -p golden
	./$(BIN) --golden golden --iters 1

clean:
	rm -rf build
.PHONY: check golden clean
