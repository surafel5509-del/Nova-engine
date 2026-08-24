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
    next.mode3d = getBool(root, "mode3d", false);

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

    if (root.contains("objects3d") && root["objects3d"].is_array()) {
        for (const auto& jo : root["objects3d"]) {
            if (!jo.is_object()) continue;
            Object3DRecord o;
            o.id = getString(jo, "id", "");
            o.shape = getString(jo, "shape", "cube");
            o.x = getFloat(jo, "x", 0.0f);
            o.y = getFloat(jo, "y", 0.0f);
            o.z = getFloat(jo, "z", 0.0f);
            o.rx = getFloat(jo, "rx", 0.0f);
            o.ry = getFloat(jo, "ry", 0.0f);
            o.rz = getFloat(jo, "rz", 0.0f);
            o.sx = getFloat(jo, "sx", 1.0f);
            o.sy = getFloat(jo, "sy", 1.0f);
            o.sz = getFloat(jo, "sz", 1.0f);
            o.r = getFloat(jo, "r", 0.7f);
            o.g = getFloat(jo, "g", 0.7f);
            o.b = getFloat(jo, "b", 0.75f);
            o.a = getFloat(jo, "a", 1.0f);
            o.texture = getString(jo, "texture", "");
            o.selected = getBool(jo, "selected", false);
            next.objects3d.push_back(std::move(o));
        }
    }

    if (root.contains("light") && root["light"].is_object()) {
        const auto& jl = root["light"];
        next.light.present = true;
        next.light.dirX = getFloat(jl, "dirX", -0.4f);
        next.light.dirY = getFloat(jl, "dirY", -1.0f);
        next.light.dirZ = getFloat(jl, "dirZ", -0.3f);
        next.light.r = getFloat(jl, "r", 0.95f);
        next.light.g = getFloat(jl, "g", 0.93f);
        next.light.b = getFloat(jl, "b", 0.85f);
        next.light.intensity = getFloat(jl, "intensity", 1.0f);
        next.light.ambientR = getFloat(jl, "ambientR", 0.18f);
        next.light.ambientG = getFloat(jl, "ambientG", 0.18f);
        next.light.ambientB = getFloat(jl, "ambientB", 0.20f);
        next.light.type = getString(jl, "type", "directional");
    }

    if (root.contains("bodies3d") && root["bodies3d"].is_array()) {
        for (const auto& jb : root["bodies3d"]) {
            if (!jb.is_object()) continue;
            Body3DRecord b;
            b.id = getString(jb, "id", "");
            b.bodyType = getInt(jb, "bodyType", 0);
            b.x = getFloat(jb, "x", 0.0f);
            b.y = getFloat(jb, "y", 0.0f);
            b.z = getFloat(jb, "z", 0.0f);
            b.radius = getFloat(jb, "radius", 0.5f);
            b.mass = getFloat(jb, "mass", 1.0f);
            b.gravityScale = getFloat(jb, "gravityScale", 1.0f);
            b.friction = getFloat(jb, "friction", 0.5f);
            b.restitution = getFloat(jb, "restitution", 0.0f);
            next.bodies3d.push_back(std::move(b));
        }
    }

    if (root.contains("world") && root["world"].is_object()) {
        const auto& jw = root["world"];
        next.world.skyR = getFloat(jw, "skyR", 0.08f);
        next.world.skyG = getFloat(jw, "skyG", 0.09f);
        next.world.skyB = getFloat(jw, "skyB", 0.12f);
        next.world.horizonR = getFloat(jw, "horizonR", 0.12f);
        next.world.horizonG = getFloat(jw, "horizonG", 0.13f);
        next.world.horizonB = getFloat(jw, "horizonB", 0.18f);
        next.world.fogDensity = getFloat(jw, "fogDensity", 0.0f);
        next.world.fogR = getFloat(jw, "fogR", 0.5f);
        next.world.fogG = getFloat(jw, "fogG", 0.55f);
        next.world.fogB = getFloat(jw, "fogB", 0.65f);
        next.world.ambientIntensity = getFloat(jw, "ambientIntensity", 1.0f);
    }

    if (root.contains("animations") && root["animations"].is_array()) {
        for (const auto& ja : root["animations"]) {
            if (!ja.is_object()) continue;
            AnimationTrack track;
            track.entityId = getString(ja, "entityId", "");
            track.property = getString(ja, "property", "x");
            track.loop = getBool(ja, "loop", true);
            if (ja.contains("keys") && ja["keys"].is_array()) {
                for (const auto& jk : ja["keys"]) {
                    if (!jk.is_object()) continue;
                    AnimationKey key;
                    key.t = getFloat(jk, "t", 0.0f);
                    key.value = getFloat(jk, "value", 0.0f);
                    track.keys.push_back(key);
                }
            }
            if (!track.entityId.empty() && !track.keys.empty()) {
                next.animations.push_back(std::move(track));
            }
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
