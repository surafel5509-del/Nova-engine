#pragma once

#include <GLES3/gl3.h>
#include <string>

namespace nova {

/** Compiled vertex+fragment program. */
class Shader {
public:
    Shader() = default;
    ~Shader();

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;
    Shader(Shader&& other) noexcept;
    Shader& operator=(Shader&& other) noexcept;

    /** Compiles and links; on failure returns false and fills outError. */
    bool build(const char* vertexSrc, const char* fragmentSrc, std::string* outError = nullptr);

    void use() const;
    GLuint program() const { return program_; }
    GLint uniformLocation(const char* name) const;

private:
    GLuint program_ = 0;
};

} // namespace nova
