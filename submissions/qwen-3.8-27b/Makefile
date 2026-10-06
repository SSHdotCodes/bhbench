CC      = clang++
CXXFLAGS = -std=c++17 -O2 -fobjc-arc
SOURCES = src/app.mm src/sim.cpp
HDRS    = src/sim.hpp src/shaders.hpp
FRAMEWORKS = -framework Cocoa -framework Metal -framework MetalKit \
             -framework QuartzCore -framework CoreImage \
             -framework MetalPerformanceShaders
TARGET  = black-hole

$(TARGET): $(SOURCES) $(HDRS)
	$(CC) $(CXXFLAGS) $(SOURCES) $(FRAMEWORKS) -o $(TARGET)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: run clean
