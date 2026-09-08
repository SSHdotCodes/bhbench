BREWHOME  := $(shell brew --prefix glfw 2>/dev/null || echo /opt/homebrew/opt/glfw)
GLEWHOME  := $(shell brew --prefix glew 2>/dev/null || echo /opt/homebrew/opt/glew)

CXX       ?= c++
CXXFLAGS  := -std=c++17 -O3 -I$(BREWHOME)/include -I$(GLEWHOME)/include
LDFLAGS   := -L$(BREWHOME)/lib -L$(GLEWHOME)/lib -lglfw -lGLEW -framework OpenGL -framework Cocoa

blackhole: src/main.cpp src/shaders.h
	$(CXX) $(CXXFLAGS) src/main.cpp -o blackhole $(LDFLAGS)

run: blackhole
	./blackhole

clean:
	rm -f blackhole