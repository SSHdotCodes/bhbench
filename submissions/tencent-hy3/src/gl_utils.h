#pragma once
// Minimal GL helpers. We render the ray-traced black hole on the CPU into
// an RGBA buffer and upload it with glDrawPixels (no external loader
// needed on macOS since we use the legacy system OpenGL framework entry
// points exposed by <OpenGL/gl.h> via GLFW).
#if defined(__APPLE__)
#  include <OpenGL/gl.h>
#else
#  include <GL/gl.h>
#endif
#include <cstdint>

namespace gl {
// nothing extra needed; GLFW gives us the context.
}
