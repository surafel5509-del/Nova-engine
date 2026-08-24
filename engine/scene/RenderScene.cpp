#include "scene/RenderScene.h"

#include <nlohmann/json.hpp>

namespace nova {

namespace {
float getFloat(const nlohmann::json& j, const char* key, float fallback) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_number()) return fallback;
    return it->get<float>();
}

bool getBool(const nlohmann::json& j, const char* key, bool fallback) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_boolean()) return fallback;
    return it->get<bool>();
}

std::string getString(const nlohmann::json& j, const char* key, const std::string& fallback) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_string()) return fallback;
    return it->get<std::string>();
}

int getInt(const nlohmann::json& j, const char* key, int fallback) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_number()) return fallback;
    return it->get<int>();
}
} // namespace

bool RenderScene::parseFrom(const std::string& jsonText, std::string* outError) {
    nlohmann::json root;
    try {
        root = nlohmann::json::parse(jsonText);
    } catch (const std::exception& e) {
        if (outError) *outError = std::string("JSON parse error: ") + e.what();
        return false;
    }

    if (!root.is_object() || !root.contains("sprites") || !root["sprites"].is_array()) {
        if (outError) *outError = "Render scene JSON must contain a 'sprites' array";
        return false;
    }

    RenderScene next;
    next.version = static_cast<int>(getFloat(root, "version", 1.0f));

    for (const auto& js : root["sprites"]) {
        if (!js.is_object()) continue;
        SpriteInstance s;
        s.id = getString(js, "id", "");
        s.x = getFloat(js, "x", 0.0f);
        s.y = getFloat(js, "y", 0.0f);
        s.rotation = getFloat(js, "rotation", 0.0f);
        s.scaleX = getFloat(js, "scaleX", 1.0f);
        s.scaleY = getFloat(js, "scaleY", 1.0f);
        s.width = getFloat(js, "width", 1.0f);
        s.height = getFloat(js, "height", 1.0f);
        s.r = getFloat(js, "r", 1.0f);
        s.g = getFloat(js, "g", 1.0f);
        s.b = getFloat(js, "b", 1.0f);
        s.a = getFloat(js, "a", 1.0f);
        s.texture = getString(js, "texture", "");
        s.selected = getBool(js, "selected", false);
        s.flipX = getBool(js, "flipX", false);
        s.flipY = getBool(js, "flipY", false);
        s.sortingOrder = getInt(js, "sortingOrder", 0);
        s.parallaxFactor = getFloat(js, "parallaxFactor", 1.0f);
        s.frameCols = getInt(js, "frameCols", 1);
        s.frameRows = getInt(js, "frameRows", 1);
        s.frameIndex = getInt(js, "frameIndex", 0);
        next.sprites.push_back(std::move(s));
    }

    if (root.contains("bodies") && root["bodies"].is_array()) {
        for (const auto& jb : root["bodies"]) {
            if (!jb.is_object()) continue;
            BodyRecord b;
            b.id = getString(jb, "id", "");
            b.bodyType = getInt(jb, "bodyType", 0);
            b.x = getFloat(jb, "x", 0.0f);
            b.y = getFloat(jb, "y", 0.0f);
            b.halfW = getFloat(jb, "halfW", 0.5f);
            b.halfH = getFloat(jb, "halfH", 0.5f);
            b.mass = getFloat(jb, "mass", 1.0f);
            b.gravityScale = getFloat(jb, "gravityScale", 1.0f);
            b.friction = getFloat(jb, "friction", 0.5f);
            b.restitution = getFloat(jb, "restitution", 0.0f);
            next.bodies.push_back(std::move(b));
        }
    }

    if (root.contains("gameCamera") && root["gameCamera"].is_object()) {
        const auto& jc = root["gameCamera"];
        next.gameCamera.present = true;
        next.gameCamera.x = getFloat(jc, "x", 0.0f);
        next.gameCamera.y = getFloat(jc, "y", 0.0f);
        next.gameCamera.zoom = getFloat(jc, "zoom", 100.0f);
        next.gameCamera.width = getFloat(jc, "width", 10.0f);
        next.gameCamera.height = getFloat(jc, "height", 6.0f);
        next.gameCamera.bgR = getFloat(jc, "bgR", 0.09f);
        next.gameCamera.bgG = getFloat(jc, "bgG", 0.10f);
        next.gameCamera.bgB = getFloat(jc, "bgB", 0.13f);
        next.gameCamera.followId = getString(jc, "followId", "");
        next.gameCamera.followLerp = getFloat(jc, "followLerp", 4.0f);
    }

    if (root.contains("uiElements") && root["uiElements"].is_array()) {
        for (const auto& ju : root["uiElements"]) {
            if (!ju.is_object()) continue;
            UiElementRecord u;
            u.id = getString(ju, "id", "");
            u.kind = getString(ju, "kind", "panel");
            u.offsetX = getFloat(ju, "offsetX", 0.0f);
            u.offsetY = getFloat(ju, "offsetY", 0.0f);
            u.width = getFloat(ju, "width", 2.0f);
            u.height = getFloat(ju, "height", 0.6f);
            u.r = getFloat(ju, "r", 0.2f);
            u.g = getFloat(ju, "g", 0.22f);
            u.b = getFloat(ju, "b", 0.28f);
            u.a = getFloat(ju, "a", 0.9f);
            u.textKey = getString(ju, "textKey", "");
            next.uiElements.push_back(std::move(u));
        }
    }

    if (root.contains("emitters") && root["emitters"].is_array()) {
        for (const auto& je : root["emitters"]) {
            if (!je.is_object()) continue;
            ParticleEmitterRecord e;
            e.id = getString(je, "id", "");
            e.x = getFloat(je, "x", 0.0f);
            e.y = getFloat(je, "y", 0.0f);
            e.emissionRate = getFloat(je, "emissionRate", 12.0f);
            e.lifetime = getFloat(je, "lifetime", 1.2f);
            e.speed = getFloat(je, "speed", 3.0f);
            e.gravity = getFloat(je, "gravity", 0.0f);
            e.startSize = getFloat(je, "startSize", 0.25f);
            e.endSize = getFloat(je, "endSize", 0.05f);
            e.spread = getFloat(je, "spread", 3.14159f);
            e.direction = getFloat(je, "direction", 1.5708f);
            e.r = getFloat(je, "r", 1.0f);
            e.g = getFloat(je, "g", 0.7f);
            e.b = getFloat(je, "b", 0.3f);
            e.texture = getString(je, "texture", "");
            next.emitters.push_back(std::move(e));
        }
    }

    if (root.contains("tilemaps") && root["tilemaps"].is_array()) {
        for (const auto& jt : root["tilemaps"]) {
            if (!jt.is_object()) continue;
            TilemapRecord t;
            t.id = getString(jt, "id", "");
            t.x = getFloat(jt, "x", 0.0f);
            t.y = getFloat(jt, "y", 0.0f);
            t.tileSize = getFloat(jt, "tileSize", 1.0f);
            t.cols = getInt(jt, "cols", 0);
            t.rows = getInt(jt, "rows", 0);
            t.tileset = getString(jt, "tileset", "");
            t.tilesetCols = getInt(jt, "tilesetCols", 1);
            t.tilesetRows = getInt(jt, "tilesetRows", 1);
            if (jt.contains("tiles") && jt["tiles"].is_array()) {
                t.tiles.reserve(jt["tiles"].size());
                for (const auto& cell : jt["tiles"]) {
                    t.tiles.push_back(cell.is_number() ? cell.get<int>() : -1);
                }
            }
            const size_t expected = static_cast<size_t>(t.cols) * static_cast<size_t>(t.rows);
            if (t.tiles.size() < expected) t.tiles.resize(expected, -1);
            next.tilemaps.push_back(std::move(t));
        }
    }

    if (root.contains("audioSources") && root["audioSources"].is_array()) {
        for (const auto& ja : root["audioSources"]) {
            if (!ja.is_object()) continue;
            AudioSourceRecord a;
            a.id = getString(ja, "id", "");
            a.path = getString(ja, "path", "");
            a.volume = getFloat(ja, "volume", 1.0f);
            a.pitch = getFloat(ja, "pitch", 1.0f);
            a.loop = getBool(ja, "loop", false);
            a.autoplay = getBool(ja, "autoplay", false);
            a.music = getBool(ja, "music", false);
            next.audioSources.push_back(std::move(a));
        }
    }

    if (root.contains("scripts") && root["scripts"].is_array()) {
        for (const auto& js : root["scripts"]) {
            if (!js.is_object()) continue;
            ScriptRecord r;
            r.id = getString(js, "id", "");
            r.script = getString(js, "script", "");
            next.scripts.push_back(std::move(r));
        }
    }

    *this = std::move(next);
    return true;
}

SpriteInstance* RenderScene::findSprite(const std::string& id) {
    for (auto& s : sprites) {
        if (s.id == id) return &s;
    }
    return nullptr;
}

const SpriteInstance* RenderScene::findSprite(const std::string& id) const {
    for (const auto& s : sprites) {
        if (s.id == id) return &s;
    }
    return nullptr;
}

} // namespace nova
