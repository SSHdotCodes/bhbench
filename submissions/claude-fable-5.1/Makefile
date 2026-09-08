CXX        := clang++
CXXFLAGS   := -std=c++17 -O2 -Wall -Wno-unused-function
OBJCXXFLAGS:= $(CXXFLAGS) -fobjc-arc -ObjC++
FRAMEWORKS := -framework Cocoa -framework Metal -framework MetalKit -framework QuartzCore

all: blackhole verify

blackhole: src/main.mm src/renderer.mm src/renderer.h src/physics.hpp src/shader_types.h src/kerr_geodesic.h
	$(CXX) $(OBJCXXFLAGS) -o $@ src/main.mm src/renderer.mm $(FRAMEWORKS)

verify: src/verify.cpp src/kerr_geodesic.h src/physics.hpp
	$(CXX) $(CXXFLAGS) -o $@ src/verify.cpp

run: blackhole
	./blackhole

test: verify
	./verify

clean:
	rm -f blackhole verify

.PHONY: all run test clean
