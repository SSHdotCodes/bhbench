# Kerr black hole: physics tests, CPU reference renderer and the Metal app (macOS, Apple silicon).
CXX         ?= clang++
CXXFLAGS    := -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -Isrc
OBJCXXFLAGS := -std=c++17 -O2 -Wall -fobjc-arc -Isrc
IMAGEIO     := -framework ImageIO -framework CoreGraphics -framework CoreFoundation
FRAMEWORKS  := -framework Cocoa -framework Metal -framework MetalKit -framework QuartzCore $(IMAGEIO)
SHADER_DIR  := $(CURDIR)/shaders
HEADERS     := $(wildcard src/*.h)

.PHONY: all test clean run

all: bin/blackhole bin/bh_cpu bin/test_physics

test: bin/test_physics
	./bin/test_physics

bin/test_physics: tests/test_physics.cpp $(HEADERS)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) tests/test_physics.cpp -o $@

bin/bh_cpu: src/cpu_render.cpp src/png_out.cpp $(HEADERS)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) src/cpu_render.cpp src/png_out.cpp $(IMAGEIO) -o $@

bin/blackhole: src/app_macos.mm src/png_out.cpp $(HEADERS) $(wildcard shaders/*)
	@mkdir -p bin
	$(CXX) $(OBJCXXFLAGS) -DBH_SHADER_DIR='"$(SHADER_DIR)"' src/app_macos.mm src/png_out.cpp $(FRAMEWORKS) -o $@

run: bin/blackhole
	./bin/blackhole

clean:
	rm -rf bin
