#include "rendering/Shader.h"

#include <vector>

namespace nova {

namespace {
GLuint compileStage(GLenum type, const char* src, std::string* outError) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
        GLint len = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(static_cast<size_t>(len > 0 ? len : 1));
        glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
        if (outError) *outError = log.data();
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}
} // namespace

Shader::~Shader() {
    if (program_ != 0) glDeleteProgram(program_);
}

Shader::Shader(Shader&& other) noexcept : program_(other.program_) {
    other.program_ = 0;
}

Shader& Shader::operator=(Shader&& other) noexcept {
    if (this != &other) {
        if (program_ != 0) glDeleteProgram(program_);
        program_ = other.program_;
        other.program_ = 0;
    }
    return *this;
}

bool Shader::build(const char* vertexSrc, const char* fragmentSrc, std::string* outError) {
    std::string stageError;
    GLuint vs = compileStage(GL_VERTEX_SHADER, vertexSrc, &stageError);
    if (vs == 0) {
        if (outError) *outError = "Vertex shader: " + stageError;
        return false;
    }
    GLuint fs = compileStage(GL_FRAGMENT_SHADER, fragmentSrc, &stageError);
    if (fs == 0) {
        glDeleteShader(vs);
        if (outError) *outError = "Fragment shader: " + stageError;
        return false;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE) {
        GLint len = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(static_cast<size_t>(len > 0 ? len : 1));
        glGetProgramInfoLog(program, static_cast<GLsizei>(log.size()), nullptr, log.data());
        if (outError) *outError = std::string("Link: ") + log.data();
        glDeleteProgram(program);
        return false;
    }

    if (program_ != 0) glDeleteProgram(program_);
    program_ = program;
    return true;
}

void Shader::use() const {
    glUseProgram(program_);
}

GLint Shader::uniformLocation(const char* name) const {
    return glGetUniformLocation(program_, name);
}

} // namespace nova
