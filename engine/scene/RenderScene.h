#pragma once

#include <string>
#include <vector>

namespace nova {

/** A single world-space sprite to draw, already flattened by the editor. */
struct SpriteInstance {
    std::string id;
    float x = 0.0f;
    float y = 0.0f;
    float rotation = 0.0f;   // degrees, counter-clockwise
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    float width = 1.0f;      // world units
    float height = 1.0f;
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
    std::string texture;     // empty = untextured (white) quad
    bool selected = false;
};

/**
 * Flat render scene pushed from the editor as JSON. Parsing lives on the
 * engine side so the runtime engine and the editor share one format.
 */
struct RenderScene {
    int version = 1;
    std::vector<SpriteInstance> sprites;

    /** Parses JSON; returns false (and leaves *this unchanged) on error. */
    bool parseFrom(const std::string& json, std::string* outError = nullptr);
};

} // namespace nova
