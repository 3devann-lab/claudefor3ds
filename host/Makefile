# Desktop simulator for ClaudeFor3DS screens (renders PNGs; no networking).
#   make -f host/Makefile && host/build/claude3ds-sim shots
CXX ?= g++
CC ?= gcc
BUILD := host/build
SRCS := $(filter-out source/main.cpp source/platform/system_3ds.cpp source/platform/gfx_3ds.cpp, \
          $(wildcard source/*.cpp source/*/*.cpp)) host/gfx_host.cpp host/system_host.cpp host/host_main.cpp
OBJS := $(patsubst %.cpp,$(BUILD)/%.o,$(SRCS)) $(BUILD)/qrcodegen.o
FLAGS := -O1 -g -Isource -Ilibrary -Ilibrary/stb -Ilibrary/qrcodegen -DRAPIDJSON_HAS_STDSTRING=1 -Wall -Wno-unused-function

$(BUILD)/claude3ds-sim: $(OBJS)
	$(CXX) -o $@ $^ -pthread

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) -std=gnu++17 $(FLAGS) -c -o $@ $<

$(BUILD)/qrcodegen.o: library/qrcodegen/qrcodegen.c
	@mkdir -p $(dir $@)
	$(CC) -O1 -c -o $@ $<

clean:
	rm -rf $(BUILD)
