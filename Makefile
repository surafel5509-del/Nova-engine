# Nova Engine — host build.
#
# The engine targets Android (Gradle + CMake + NDK) for shipping, but the core
# is deliberately free of GL/JNI dependencies so it can be compiled, unit
# tested and profiled on a workstation. This Makefile drives that host build.
#
#   make test        build + run the unit test suite
#   make player      build the standalone engine player
#   make server      build the editor host (engine + web editor)
#   make all         everything
#   make asan        rebuild with AddressSanitizer + UBSan and run tests
#   make clean

CXX      ?= g++
CXXSTD   ?= -std=c++17
OPT      ?= -O2
WARN     := -Wall -Wextra -Wpedantic -Wno-unused-parameter
CXXFLAGS += $(CXXSTD) $(OPT) $(WARN) -fno-strict-aliasing
LDFLAGS  +=
LDLIBS   += -lpthread -lm

ENGINE_DIR := engine/nova
BUILD_DIR  := build/host
OBJ_DIR    := $(BUILD_DIR)/obj
BIN_DIR    := $(BUILD_DIR)/bin

INCLUDE := -I$(ENGINE_DIR)

# Sources are discovered automatically; adding a file to the tree is enough.
# The two entry points each have their own main(), so they are linked into
# their own binary only and never into the library the tests build against.
MAIN_SRCS  := $(ENGINE_DIR)/app/PlayerMain.cpp $(ENGINE_DIR)/server/ServerMain.cpp
CORE_SRCS  := $(filter-out $(MAIN_SRCS),\
               $(shell find $(ENGINE_DIR) -name '*.cpp' -not -path '*/tests/*' | sort))
TEST_SRCS := $(shell find $(ENGINE_DIR)/tests -name '*.cpp' | sort)

CORE_OBJS := $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(CORE_SRCS))
TEST_OBJS := $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(TEST_SRCS))

# Optional: the old JNI/GLES engine sources still compile as a separate target.
LEGACY_SRCS := $(shell find engine -name '*.cpp' \
                  -not -path 'engine/nova/*' \
                  -not -path '*/third_party/*' \
                  -not -path '*/tests/*' | sort)
LEGACY_OBJS := $(patsubst %.cpp,$(OBJ_DIR)/legacy/%.o,$(LEGACY_SRCS))

.PHONY: all test player server clean asan legacy count help

all: test player server

help:
	@grep -E '^#   ' Makefile | sed 's/^#   //'

# Objects do not remember the flags they were compiled with, so switching
# between the normal and the sanitizer build used to silently relink the old
# objects into the new binary -- which fails to link, or worse, links without
# the instrumentation it was asked for. A stamp holding the current flags is
# written next to them, and a mismatch forces a rebuild.
FLAGS_STAMP := $(OBJ_DIR)/.flags
FLAG_SIG := $(CXXSTD)|$(OPT)|$(WARN)|$(LDFLAGS)

$(FLAGS_STAMP):
	@mkdir -p $(OBJ_DIR)
	@echo '$(FLAG_SIG)' > $@

$(OBJ_DIR)/%.o: %.cpp $(FLAGS_STAMP)
	@mkdir -p $(dir $@)
	@if [ "$$(cat $(FLAGS_STAMP) 2>/dev/null)" != '$(FLAG_SIG)' ]; then echo '$(FLAG_SIG)' > $(FLAGS_STAMP); fi
	$(CXX) $(CXXFLAGS) $(INCLUDE) -MMD -MP -c $< -o $@

$(OBJ_DIR)/legacy/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXSTD) $(OPT) -w -Iengine -Iengine/third_party -c $< -o $@

# --- unit tests -------------------------------------------------------------
test: $(BIN_DIR)/nova_tests
	@$(BIN_DIR)/nova_tests

$(BIN_DIR)/nova_tests: $(CORE_OBJS) $(TEST_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS)

# --- standalone player (headless/terminal or with a window backend) ---------
player: $(BIN_DIR)/nova_player

$(BIN_DIR)/nova_player: $(CORE_OBJS) $(OBJ_DIR)/$(ENGINE_DIR)/app/PlayerMain.o
	@mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS)

# --- editor host: engine process + web editor UI ----------------------------
server: $(BIN_DIR)/nova_server

$(BIN_DIR)/nova_server: $(CORE_OBJS) $(OBJ_DIR)/$(ENGINE_DIR)/server/ServerMain.o
	@mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS)

# --- the previous Android/GLES core, built for host regression checks -------
legacy: $(BIN_DIR)/nova_legacy_tests
	@mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) $(LEGACY_OBJS) -o $@ $(LDLIBS) -Iengine -Iengine/third_party

# --- sanitizer build --------------------------------------------------------
asan:
	@$(MAKE) --no-print-directory test OPT="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer" \
	         LDFLAGS="-fsanitize=address,undefined"

count:
	@echo "engine/nova: $(words $(CORE_SRCS)) sources, $$($(CXX) -std=c++17 -E -x c++ /dev/null >/dev/null 2>&1; cat $(CORE_SRCS) $(wildcard $(ENGINE_DIR)/*/*.h) | wc -l) lines"
	@echo "tests:        $(words $(TEST_SRCS)) sources"

clean:
	rm -rf $(BUILD_DIR)

-include $(shell find $(OBJ_DIR) -name '*.d' 2>/dev/null)
