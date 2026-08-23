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
    }

    *this = std::move(next);
    return true;
}

} // namespace nova
