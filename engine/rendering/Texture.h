#pragma once

#include <GLES3/gl3.h>
#include <string>

namespace nova {

/** 2D RGBA8888 texture with linear filtering. */
class Texture {
public:
    Texture() = default;
    ~Texture();

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&& other) noexcept;
    Texture& operator=(Texture&& other) noexcept;

    /** Creates a 1x1 solid-color texture (used as the white fallback). */
    bool createSolid(unsigned char r, unsigned char g, unsigned char b, unsigned char a);

    bool createFromRgba(const unsigned char* data, int width, int height);

    GLuint id() const { return id_; }

private:
    void release();
    GLuint id_ = 0;
};

} // namespace nova
