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
    int sortingOrder = 0;
    float parallaxFactor = 1.0f;  // 1 = moves with world, 0 = fixed to camera
    // Sprite-sheet animation (1x1 = single frame).
    int frameCols = 1;
    int frameRows = 1;
    int frameIndex = 0;
};

/** Physics body record parsed from the render scene (drives simulation). */
struct BodyRecord {
    std::string id;
    int bodyType = 0;            // 0 static, 1 dynamic, 2 kinematic
    float x = 0.0f;
    float y = 0.0f;
    float halfW = 0.5f;
    float halfH = 0.5f;
    float mass = 1.0f;
    float gravityScale = 1.0f;
    float friction = 0.5f;
    float restitution = 0.0f;
};

/** The game's own camera (scene camera entity), used for Game View/runtime. */
struct GameCamera {
    bool present = false;
    float x = 0.0f;
    float y = 0.0f;
    float zoom = 100.0f;         // pixels per world unit
    float width = 10.0f;         // frustum preview extents (world units)
    float height = 6.0f;
    float bgR = 0.09f;
    float bgG = 0.10f;
    float bgB = 0.13f;
};

/**
 * Flat render scene pushed from the editor as JSON. Parsing lives on the
 * engine side so the runtime engine and the editor share one format.
 */
struct RenderScene {
    int version = 1;
    std::vector<SpriteInstance> sprites;
    std::vector<BodyRecord> bodies;
    GameCamera gameCamera;

    /** Parses JSON; returns false (and leaves *this unchanged) on error. */
    bool parseFrom(const std::string& json, std::string* outError = nullptr);
};

} // namespace nova
