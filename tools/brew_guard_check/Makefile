# Host checks for gm-warz: the controller's brew bound and the settings
# runner's input guards. No board, no PlatformIO env (adding a native env
# changes the project checksum and wipes every env's build tree).
#
#   make -C tools/brew_guard_check check

ROOT := ../..
CXX ?= g++
CXXFLAGS ?= -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined
BUILD := build

.PHONY: check clean
check: $(BUILD)/brew_guard_check
	ASAN_OPTIONS=verify_asan_link_order=0:detect_leaks=0 ./$(BUILD)/brew_guard_check
	cd $(ROOT) && python3 tools/brew_guard_check/rig_guard_check.py

$(BUILD)/brew_guard_check: brew_guard_check.cpp $(ROOT)/lib/GaggiMateController/src/BrewGuard.h
	mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -I $(ROOT)/lib/GaggiMateController/src -o $@ brew_guard_check.cpp

clean:
	rm -rf $(BUILD)
