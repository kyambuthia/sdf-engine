#include "shader.hpp"

#include <SDL3/SDL.h>
#include <fstream>
#include <sstream>
#include <vector>

namespace {

bool readTextFile(const std::string& path, std::string& out) {
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to open shader file: %s", path.c_str());
        return false;
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    out = ss.str();
    return true;
}

GLuint compileStage(GLenum type, const std::string& source, const std::string& label) {
    GLuint shader = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(len > 1 ? len : 1, 0);
        glGetShaderInfoLog(shader, len, nullptr, log.data());
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Shader compile error (%s):\n%s", label.c_str(), log.data());
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

} // namespace

bool ShaderProgram::loadFromFiles(const std::string& vertPath, const std::string& fragPath) {
    std::string vertSrc;
    std::string fragSrc;
    if (!readTextFile(vertPath, vertSrc) || !readTextFile(fragPath, fragSrc)) {
        return false;
    }

    GLuint vs = compileStage(GL_VERTEX_SHADER, vertSrc, vertPath);
    if (!vs) return false;

    GLuint fs = compileStage(GL_FRAGMENT_SHADER, fragSrc, fragPath);
    if (!fs) {
        glDeleteShader(vs);
        return false;
    }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint linked = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    if (!linked) {
        GLint len = 0;
        glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(len > 1 ? len : 1, 0);
        glGetProgramInfoLog(prog, len, nullptr, log.data());
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Program link error:\n%s", log.data());
        glDeleteProgram(prog);
        return false;
    }

    destroy();
    id = prog;
    return true;
}

void ShaderProgram::destroy() {
    if (id != 0) {
        glDeleteProgram(id);
        id = 0;
    }
}

void ShaderProgram::use() const {
    glUseProgram(id);
}

GLint ShaderProgram::uniformLocation(const char* name) const {
    return glGetUniformLocation(id, name);
}
