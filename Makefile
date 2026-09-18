CXX ?= g++
CPPFLAGS := -Iinclude
CXXFLAGS ?= -std=c++17 -O2 -g -Wall -Wextra -Wpedantic -Werror -pthread
LDLIBS := -pthread -lrt

APP := build/lumigrid
TEST_APP := build/unit_tests

CORE_SOURCES := \
	src/model.cpp \
	src/ipc.cpp \
	src/hardware_input.cpp \
	src/predictor.cpp \
	src/simulator.cpp \
	src/control_engine.cpp
APP_SOURCES := $(CORE_SOURCES) src/main.cpp
TEST_SOURCES := src/model.cpp src/hardware_input.cpp src/predictor.cpp \
	src/control_engine.cpp tests/unit_tests.cpp

.PHONY: all test integration-test check sanitize demo interactive qnx clean

all: $(APP)

$(APP): $(APP_SOURCES) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(APP_SOURCES) -o $@ $(LDLIBS)

$(TEST_APP): $(TEST_SOURCES) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(TEST_SOURCES) -o $@ $(LDLIBS)

build:
	mkdir -p build

test: $(TEST_APP)
	./$(TEST_APP)

integration-test: $(APP)
	./scripts/integration_test.sh

check: test integration-test

sanitize: | build
	$(CXX) $(CPPFLAGS) -std=c++17 -O1 -g -Wall -Wextra -Wpedantic \
		-fsanitize=address,undefined -fno-omit-frame-pointer -pthread \
		$(TEST_SOURCES) -o build/unit_tests_sanitize $(LDLIBS) \
		-fsanitize=address,undefined
	ASAN_OPTIONS=detect_leaks=0 ./build/unit_tests_sanitize

demo: $(APP)
	./$(APP) --scenario demo --duration 30 --output run/demo

interactive: $(APP)
	./$(APP) --interactive --output run/interactive

# Run this target after sourcing qnxsdp-env.sh. Override QNX_VARIANT if needed.
QNX_CXX ?= q++
QNX_VARIANT ?= gcc_ntoaarch64le
qnx: | build-qnx
	$(QNX_CXX) -V$(QNX_VARIANT) $(CPPFLAGS) \
		-D_POSIX_C_SOURCE=200809L -std=c++17 -O2 -g \
		-Wall -Wextra -Wpedantic $(APP_SOURCES) \
		-o build-qnx/lumigrid-aarch64

build-qnx:
	mkdir -p build-qnx

clean:
	rm -rf build build-qnx run/demo run/interactive run/integration-test
