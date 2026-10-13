CXX      := clang++
CXXFLAGS := -std=c++17 -O3 -Wall -Wextra -Isrc $(shell pkg-config --cflags glfw3 glm 2>/dev/null || echo "-I/opt/homebrew/include")
LDFLAGS  := $(shell pkg-config --libs glfw3 2>/dev/null || echo "-L/opt/homebrew/lib -lglfw") \
            -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo

TARGET   := black_hole_sim
SRCS     := src/main.cpp
HEADERS  := src/physics.hpp src/shaders.hpp src/spacetime_grid.hpp src/hud.hpp

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(SRCS) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(SRCS) -o $(TARGET) $(LDFLAGS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
