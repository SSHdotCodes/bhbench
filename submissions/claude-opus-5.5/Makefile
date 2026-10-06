# Kerr black hole — general-relativistic ray tracer (C++ / Metal, macOS)
#
#   make            build the app, the Metal library and the validation suite
#   make run        open the real-time window
#   make test       physics validation suite + GPU self-test
#   make app        build "Black Hole.app" (double-clickable)
#   make render     example offline still -> renders/example.png

CXX       := clang++
CXXFLAGS  := -std=c++20 -O2 -Wall -Wextra -Isrc
MMFLAGS   := $(CXXFLAGS) -fobjc-arc
FRAMEWORKS:= -framework Cocoa -framework Metal -framework MetalKit -framework QuartzCore \
             -framework CoreGraphics -framework ImageIO -framework UniformTypeIdentifiers
METAL     := xcrun -sdk macosx metal
METALLIB  := xcrun -sdk macosx metallib
B         := build

HDRS      := $(wildcard src/*.h) src/shaders/kerr_core.h src/shaders/bh_shared.h
OBJS      := $(B)/physics.o $(B)/colorimetry.o $(B)/scene.o $(B)/renderer.o $(B)/main.o

.PHONY: all run test app render clean

all: $(B)/bh $(B)/bh.metallib $(B)/test_kerr

$(B):
	@mkdir -p $(B)

$(B)/%.o: src/%.cpp $(HDRS) | $(B)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(B)/%.o: src/%.mm $(HDRS) | $(B)
	$(CXX) $(MMFLAGS) -c $< -o $@

$(B)/bh: $(OBJS)
	$(CXX) $(OBJS) $(FRAMEWORKS) -o $@

$(B)/bh.metallib: src/shaders/bh.metal src/shaders/kerr_core.h src/shaders/bh_shared.h | $(B)
	$(METAL) -O3 -Isrc/shaders -c $< -o $(B)/bh.air
	$(METALLIB) $(B)/bh.air -o $@

$(B)/test_kerr: tests/test_kerr.cpp src/physics.cpp src/colorimetry.cpp $(HDRS) | $(B)
	$(CXX) $(CXXFLAGS) tests/test_kerr.cpp src/physics.cpp src/colorimetry.cpp -o $@

run: $(B)/bh $(B)/bh.metallib
	./$(B)/bh

test: $(B)/test_kerr $(B)/bh $(B)/bh.metallib
	./$(B)/test_kerr
	./$(B)/bh --selftest

APP := $(B)/Black Hole.app
app: $(B)/bh $(B)/bh.metallib
	@mkdir -p "$(APP)/Contents/MacOS" "$(APP)/Contents/Resources"
	cp $(B)/bh "$(APP)/Contents/MacOS/bh"
	cp $(B)/bh.metallib "$(APP)/Contents/Resources/bh.metallib"
	cp packaging/Info.plist "$(APP)/Contents/Info.plist"
	@echo "built $(APP)"

render: $(B)/bh $(B)/bh.metallib
	@mkdir -p renders
	./$(B)/bh --render renders/example.png --size 1920x1080 --spp 64

clean:
	rm -rf $(B)
