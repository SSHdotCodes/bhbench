CXX := clang++
CXXFLAGS := $(shell pkg-config --cflags glfw3 glew) -O3 -std=c++17 -Wall -Wextra -Wno-unused-parameter
LDFLAGS := $(shell pkg-config --libs glfw3 glew) -framework OpenGL

TARGET := blackhole
SRCS := src/main.cpp

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $(SRCS) $(LDFLAGS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all run clean
