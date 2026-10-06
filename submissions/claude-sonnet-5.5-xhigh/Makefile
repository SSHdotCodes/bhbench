# Kerr black hole -- real-time Metal ray tracer (macOS, Apple silicon or any Metal GPU)
CXX       := clang++
CXXFLAGS  := -std=c++17 -O3 -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable -fobjc-arc -Isrc -Ibuild
FRAMEWORKS:= -framework Cocoa -framework Metal -framework MetalKit -framework QuartzCore \
             -framework CoreText -framework CoreGraphics -framework ImageIO
SHADERS   := src/kerr_shared.h src/shared_types.h $(wildcard src/shaders/*.metal) tools/bundle_shaders.sh
OBJS      := build/main.o build/renderer.o build/physics.o build/selftest.o

all: build/blackhole

build/shader_source.h: $(SHADERS)
	@mkdir -p build
	./tools/bundle_shaders.sh build/shaders_bundle.metal
	{ echo 'static const char* kShaderSource = R"MSL('; cat build/shaders_bundle.metal; echo ')MSL";'; } > $@

build/main.o: src/main.mm src/renderer.h src/physics.hpp src/selftest.hpp src/kerr_shared.h
	$(CXX) $(CXXFLAGS) -c $< -o $@
build/renderer.o: src/renderer.mm src/renderer.h src/physics.hpp src/shared_types.h src/kerr_shared.h build/shader_source.h
	$(CXX) $(CXXFLAGS) -c $< -o $@
build/physics.o: src/physics.cpp src/physics.hpp src/kerr_shared.h
	$(CXX) $(CXXFLAGS) -c $< -o $@
build/selftest.o: src/selftest.cpp src/selftest.hpp src/physics.hpp src/kerr_shared.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/blackhole: $(OBJS)
	$(CXX) $(OBJS) $(FRAMEWORKS) -o $@

# CPU-only validation binary (no Metal needed)
build/selftest: src/selftest_cli.cpp src/selftest.cpp src/physics.cpp src/kerr_shared.h
	$(CXX) -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -Isrc src/selftest_cli.cpp src/selftest.cpp src/physics.cpp -o $@

run: build/blackhole
	./build/blackhole

test: build/blackhole
	./build/blackhole --selftest

clean:
	rm -rf build/*

.PHONY: all run test clean
