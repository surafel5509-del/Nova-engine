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
    bool flipX = false;
    bool flipY = false;
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
    std::string followId;        // optional: camera centers on this entity each frame
    float followLerp = 4.0f;     // smoothing (higher = snappier)
};

/** Screen-space UI element (anchored to the camera center). */
struct UiElementRecord {
    std::string id;
    std::string kind;            // label | button | panel
    float offsetX = 0.0f;        // world-unit offset from camera center
    float offsetY = 0.0f;
    float width = 2.0f;
    float height = 0.6f;
    float r = 0.2f;
    float g = 0.22f;
    float b = 0.28f;
    float a = 0.9f;
    std::string textKey;         // texture key of the pre-rendered text (may be empty)
};

/** Particle emitter parameters parsed from the render scene. */
struct ParticleEmitterRecord {
    std::string id;
    float x = 0.0f;
    float y = 0.0f;
    float emissionRate = 12.0f;  // particles per second
    float lifetime = 1.2f;       // seconds per particle
    float speed = 3.0f;
    float gravity = 0.0f;        // extra gravity on particles
    float startSize = 0.25f;
    float endSize = 0.05f;
    float spread = 3.14159f;     // emission cone (radians), full circle default
    float direction = 1.5708f;   // base emission angle (radians, up)
    float r = 1.0f;
    float g = 0.7f;
    float b = 0.3f;
    std::string texture;         // optional particle texture
};

/** A tile layer: grid of tile indices into a tileset atlas. -1 = empty. */
struct TilemapRecord {
    std::string id;
    float x = 0.0f;              // world position of grid origin (bottom-left of cell 0,0)
    float y = 0.0f;
    float tileSize = 1.0f;       // world units per tile
    int cols = 0;
    int rows = 0;
    std::string tileset;         // atlas texture key
    int tilesetCols = 1;         // atlas grid
    int tilesetRows = 1;
    std::vector<int> tiles;      // row-major, rows*cols entries, -1 = empty
};

/** Audio source parsed from the scene (playback is host-side via events). */
struct AudioSourceRecord {
    std::string id;
    std::string path;
    float volume = 1.0f;
    float pitch = 1.0f;
    bool loop = false;
    bool autoplay = false;
    bool music = false;          // music streams, sfx is preloaded
};

/** Script binding: entity id -> script name (source pushed separately). */
struct ScriptRecord {
    std::string id;
    std::string script;          // script asset name, e.g. "scripts/player.lua"
};

/**
 * Flat render scene pushed from the editor as JSON. Parsing lives on the
 * engine side so the runtime engine and the editor share one format.
 */
struct RenderScene {
    int version = 1;
    std::vector<SpriteInstance> sprites;
    std::vector<BodyRecord> bodies;
    std::vector<ParticleEmitterRecord> emitters;
    std::vector<TilemapRecord> tilemaps;
    std::vector<AudioSourceRecord> audioSources;
    std::vector<ScriptRecord> scripts;
    std::vector<UiElementRecord> uiElements;
    GameCamera gameCamera;

    /** Parses JSON; returns false (and leaves *this unchanged) on error. */
    bool parseFrom(const std::string& json, std::string* outError = nullptr);

    const SpriteInstance* findSprite(const std::string& id) const;
    SpriteInstance* findSprite(const std::string& id);
};

} // namespace nova
