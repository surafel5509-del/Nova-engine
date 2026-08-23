#include "rendering/Texture.h"

namespace nova {

Texture::~Texture() { release(); }

Texture::Texture(Texture&& other) noexcept : id_(other.id_) {
    other.id_ = 0;
}

Texture& Texture::operator=(Texture&& other) noexcept {
    if (this != &other) {
        release();
        id_ = other.id_;
        other.id_ = 0;
    }
    return *this;
}

bool Texture::createSolid(unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
    const unsigned char pixel[4] = { r, g, b, a };
    return createFromRgba(pixel, 1, 1);
}

bool Texture::createFromRgba(const unsigned char* data, int width, int height) {
    if (data == nullptr || width <= 0 || height <= 0) return false;

    release();
    glGenTextures(1, &id_);
    glBindTexture(GL_TEXTURE_2D, id_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return id_ != 0;
}

void Texture::release() {
    if (id_ != 0) {
        glDeleteTextures(1, &id_);
        id_ = 0;
    }
}

} // namespace nova
