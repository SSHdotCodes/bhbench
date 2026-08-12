#pragma once

#ifndef GL_SILENCE_DEPRECATION
#define GL_SILENCE_DEPRECATION
#endif
#include <OpenGL/gl3.h>

#include <cstdio>
#include <cstdlib>

inline void glCheckImpl(const char* file, int line) {
    const GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        std::fprintf(stderr, "OpenGL error 0x%04x at %s:%d\n", err, file, line);
    }
}

#define GL_CHECK() glCheckImpl(__FILE__, __LINE__)
