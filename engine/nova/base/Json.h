// Nova Engine — JSON document wrapper.
//
// Wraps the vendored nlohmann/json so the rest of the engine depends on a
// small stable API (Value + parse + dump) rather than the third-party header
// directly. Every .nova scene/project file goes through this.
#pragma once

#include <string>
#include <vector>

#include "math/Math.h"

namespace nova {

namespace nlohmann_json_placeholder {}

class Json;

/// Thin, move-only value type. Null-initialised; conversion operators never
/// throw, they fall back to a supplied default.
class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Json() = default;
    Json(bool v) : type_(Type::Bool), bool_(v) {}
    Json(int v) : type_(Type::Number), num_(static_cast<double>(v)) {}
    Json(float v) : type_(Type::Number), num_(static_cast<double>(v)) {}
    Json(double v) : type_(Type::Number), num_(v) {}
    Json(const char* v) : type_(Type::String), str_(v ? v : "") {}
    Json(std::string v) : type_(Type::String), str_(std::move(v)) {}

    static Json array() { Json j; j.type_ = Type::Array; return j; }
    static Json object() { Json j; j.type_ = Type::Object; return j; }
    static Json vec2(const Vec2& v) { return array().push(v.x).push(v.y); }
    static Json vec3(const Vec3& v) { return array().push(v.x).push(v.y).push(v.z); }
    static Json color(const Color& c) { return array().push(c.r).push(c.g).push(c.b).push(c.a); }

    /// Parses [text]; returns a null Json and fills [error] on failure.
    static Json parse(const std::string& text, std::string* error = nullptr);
    /// Pretty-prints with [indent] spaces (0 = compact).
    std::string dump(int indent = 2) const;

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    // Accessors with defaults — no exceptions, safe on any shape.
    bool asBool(bool def = false) const;
    float asFloat(float def = 0.0f) const;
    int asInt(int def = 0) const;
    double asDouble(double def = 0.0) const;
    std::string asString(const std::string& def = "") const;

    Vec2 asVec2(const Vec2& def = {}) const;
    Vec3 asVec3(const Vec3& def = {}) const;
    Color asColor(const Color& def = Color(1, 1, 1, 1)) const;

    // Object access.
    bool has(const std::string& key) const;
    const Json& operator[](const std::string& key) const;
    Json& operator[](const std::string& key);
    void set(const std::string& key, Json value);
    bool erase(const std::string& key);
    /// Object keys in insertion order.
    std::vector<std::string> keys() const;
    size_t size() const;

    /// Keyed reads: obj.asFloat("hp", 100) returns the default when absent
    /// or when the stored value has an incompatible type.
    float asFloat(const std::string& key, float def) const { return (*this)[key].asFloat(def); }
    int asInt(const std::string& key, int def) const { return (*this)[key].asInt(def); }
    bool asBool(const std::string& key, bool def) const { return (*this)[key].asBool(def); }
    std::string asString(const std::string& key, const std::string& def) const {
        return (*this)[key].asString(def);
    }

    // Array access.
    const Json& at(size_t index) const;
    Json& at(size_t index);
    /// Appends [value] and returns *this so builders can chain:
    ///     Json::array().push(1).push(2).push(3)
    Json& push(Json value);

    // Typed field helpers: obj.get("x", 1.0f) reads with a default.
    template <typename T>
    T get(const std::string& key, T def) const {
        const Json& v = (*this)[key];
        if (v.isNull()) return def;
        return static_cast<T>(v.asNumberAs(def));
    }
    std::string getString(const std::string& key, const std::string& def = "") const {
        const Json& v = (*this)[key];
        return v.isString() ? v.str_ : (v.isNumber() ? std::to_string(v.asInt()) : def);
    }
    bool getBool(const std::string& key, bool def = false) const {
        const Json& v = (*this)[key];
        return v.isNull() ? def : v.asBool(def);
    }

    /// Serializes this value to a compact binary blob (nlohmann CBOR-free:
    /// simply a UTF-8 dump, which keeps snapshots diffable and portable).
    std::vector<uint8_t> encode() const;
    static Json decode(const uint8_t* data, size_t size, std::string* error = nullptr);

private:
    double asNumberAs(double def) const { return isNumber() ? num_ : def; }

    // --- Implementation detail -------------------------------------------
    // Public so the parser/writer in Json.cpp (translation-unit-local helpers)
    // can touch the payload without a pile of friend declarations. Treat these
    // as private: go through the accessors.
public:
    Type type_ = Type::Null;
    bool bool_ = false;
    double num_ = 0.0;
    std::string str_;
    std::vector<Json> arr_;
    std::vector<std::pair<std::string, Json>> obj_;  // ordered: stable output
};

}  // namespace nova
