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
        next.sprites.push_back(std::move(s));
    }

    *this = std::move(next);
    return true;
}

} // namespace nova
