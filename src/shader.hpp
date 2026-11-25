#pragma once

#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
#else
#define GL_GLEXT_PROTOTYPES
#include <SDL3/SDL_opengl.h>
#endif

#include <string>

struct ShaderProgram {
    GLuint id = 0;

    bool loadFromFiles(const std::string& vertPath, const std::string& fragPath);
    void destroy();
    void use() const;

    GLint uniformLocation(const char* name) const;
};
