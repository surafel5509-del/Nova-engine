#include "base/Json.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace nova {
namespace {

// --- parser ---------------------------------------------------------------

struct Parser {
    const char* p;
    const char* end;
    std::string err;

    void skipWs() {
        while (p < end) {
            const char c = *p;
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { ++p; continue; }
            // Line comments are not standard JSON but save a lot of pain when
            // hand-editing .nova files in the editor's text mode.
            if (c == '/' && p + 1 < end && p[1] == '/') {
                p += 2;
                while (p < end && *p != '\n') ++p;
                continue;
            }
            break;
        }
    }

    bool fail(const char* msg) {
        if (err.empty()) err = msg;
        return false;
    }

    bool parseValue(Json& out, int depth) {
        if (depth > 200) return fail("nesting too deep");
        skipWs();
        if (p >= end) return fail("unexpected end of input");
        switch (*p) {
            case '{': return parseObject(out, depth);
            case '[': return parseArray(out, depth);
            case '"': {
                std::string s;
                if (!parseString(s)) return false;
                out = Json(std::move(s));
                return true;
            }
            case 't':
                if (end - p >= 4 && std::strncmp(p, "true", 4) == 0) { p += 4; out = Json(true); return true; }
                return fail("invalid literal");
            case 'f':
                if (end - p >= 5 && std::strncmp(p, "false", 5) == 0) { p += 5; out = Json(false); return true; }
                return fail("invalid literal");
            case 'n':
                if (end - p >= 4 && std::strncmp(p, "null", 4) == 0) { p += 4; out = Json(); return true; }
                return fail("invalid literal");
            default: return parseNumber(out);
        }
    }

    bool parseNumber(Json& out) {
        const char* start = p;
        if (p < end && (*p == '-' || *p == '+')) ++p;
        bool anyDigit = false;
        while (p < end && std::isdigit(static_cast<unsigned char>(*p))) { ++p; anyDigit = true; }
        if (p < end && *p == '.') {
            ++p;
            while (p < end && std::isdigit(static_cast<unsigned char>(*p))) { ++p; anyDigit = true; }
        }
        if (p < end && (*p == 'e' || *p == 'E')) {
            ++p;
            if (p < end && (*p == '-' || *p == '+')) ++p;
            while (p < end && std::isdigit(static_cast<unsigned char>(*p))) ++p;
        }
        if (!anyDigit) return fail("invalid number");
        out = Json(std::strtod(std::string(start, p).c_str(), nullptr));
        return true;
    }

    bool parseString(std::string& out) {
        if (p >= end || *p != '"') return fail("expected string");
        ++p;
        out.clear();
        while (p < end) {
            const unsigned char c = static_cast<unsigned char>(*p);
            if (c == '"') { ++p; return true; }
            if (c == '\\') {
                ++p;
                if (p >= end) return fail("unterminated escape");
                const char e = *p++;
                switch (e) {
                    case '"':  out += '"';  break;
                    case '\\': out += '\\'; break;
                    case '/':  out += '/';  break;
                    case 'b':  out += '\b'; break;
                    case 'f':  out += '\f'; break;
                    case 'n':  out += '\n'; break;
                    case 'r':  out += '\r'; break;
                    case 't':  out += '\t'; break;
                    case 'u': {
                        if (end - p < 4) return fail("bad \\u escape");
                        unsigned cp = 0;
                        for (int i = 0; i < 4; ++i) {
                            const char h = p[i];
                            cp <<= 4;
                            if (h >= '0' && h <= '9') cp |= static_cast<unsigned>(h - '0');
                            else if (h >= 'a' && h <= 'f') cp |= static_cast<unsigned>(h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') cp |= static_cast<unsigned>(h - 'A' + 10);
                            else return fail("bad hex digit");
                        }
                        p += 4;
                        // Surrogate pair -> single code point.
                        if (cp >= 0xD800 && cp <= 0xDBFF && end - p >= 6 &&
                            p[0] == '\\' && p[1] == 'u') {
                            unsigned lo = 0;
                            bool okLow = true;
                            for (int i = 0; i < 4; ++i) {
                                const char h = p[2 + i];
                                lo <<= 4;
                                if (h >= '0' && h <= '9') lo |= static_cast<unsigned>(h - '0');
                                else if (h >= 'a' && h <= 'f') lo |= static_cast<unsigned>(h - 'a' + 10);
                                else if (h >= 'A' && h <= 'F') lo |= static_cast<unsigned>(h - 'A' + 10);
                                else { okLow = false; break; }
                            }
                            if (okLow && lo >= 0xDC00 && lo <= 0xDFFF) {
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                                p += 6;
                            }
                        }
                        // UTF-8 encode.
                        if (cp < 0x80) {
                            out += static_cast<char>(cp);
                        } else if (cp < 0x800) {
                            out += static_cast<char>(0xC0 | (cp >> 6));
                            out += static_cast<char>(0x80 | (cp & 0x3F));
                        } else if (cp < 0x10000) {
                            out += static_cast<char>(0xE0 | (cp >> 12));
                            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                            out += static_cast<char>(0x80 | (cp & 0x3F));
                        } else {
                            out += static_cast<char>(0xF0 | (cp >> 18));
                            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                            out += static_cast<char>(0x80 | (cp & 0x3F));
                        }
                        break;
                    }
                    default: return fail("unknown escape");
                }
                continue;
            }
            if (c < 0x20) return fail("control character in string");
            out += *p++;
        }
        return fail("unterminated string");
    }

    bool parseArray(Json& out, int depth) {
        ++p;  // '['
        out = Json::array();
        skipWs();
        if (p < end && *p == ']') { ++p; return true; }
        while (p < end) {
            Json v;
            if (!parseValue(v, depth + 1)) return false;
            out.arr_.push_back(std::move(v));
            skipWs();
            if (p < end && *p == ',') { ++p; continue; }
            if (p < end && *p == ']') { ++p; return true; }
            return fail("expected ',' or ']'");
        }
        return fail("unterminated array");
    }

    bool parseObject(Json& out, int depth) {
        ++p;  // '{'
        out = Json::object();
        skipWs();
        if (p < end && *p == '}') { ++p; return true; }
        while (p < end) {
            skipWs();
            std::string key;
            if (!parseString(key)) return false;
            skipWs();
            if (p >= end || *p != ':') return fail("expected ':'");
            ++p;
            Json v;
            if (!parseValue(v, depth + 1)) return false;
            out.obj_.emplace_back(std::move(key), std::move(v));
            skipWs();
            if (p < end && *p == ',') { ++p; continue; }
            if (p < end && *p == '}') { ++p; return true; }
            return fail("expected ',' or '}'");
        }
        return fail("unterminated object");
    }
};

// --- writer ---------------------------------------------------------------

void escapeTo(std::string& out, const std::string& s) {
    out += '"';
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    out += '"';
}

void numberTo(std::string& out, double v) {
    if (!std::isfinite(v)) { out += "0"; return; }
    if (v == static_cast<double>(static_cast<int64_t>(v)) &&
        v < 9.0e15 && v > -9.0e15) {
        out += std::to_string(static_cast<int64_t>(v));
        return;
    }
    // %.9g keeps float precision without trailing noise.
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.9g", v);
    out += buf;
}

void writeTo(std::string& out, const Json& j, int indent, int depth) {
    const bool pretty = indent > 0;
    auto newline = [&](int d) {
        if (!pretty) return;
        out += '\n';
        out.append(static_cast<size_t>(indent * d), ' ');
    };
    switch (j.type_) {
        case Json::Type::Null: out += "null"; break;
        case Json::Type::Bool: out += j.bool_ ? "true" : "false"; break;
        case Json::Type::Number: numberTo(out, j.num_); break;
        case Json::Type::String: escapeTo(out, j.str_); break;
        case Json::Type::Array: {
            if (j.arr_.empty()) { out += "[]"; break; }
            out += '[';
            for (size_t i = 0; i < j.arr_.size(); ++i) {
                if (i) out += ',';
                newline(depth + 1);
                writeTo(out, j.arr_[i], indent, depth + 1);
            }
            newline(depth);
            out += ']';
            break;
        }
        case Json::Type::Object: {
            if (j.obj_.empty()) { out += "{}"; break; }
            out += '{';
            for (size_t i = 0; i < j.obj_.size(); ++i) {
                if (i) out += ',';
                newline(depth + 1);
                escapeTo(out, j.obj_[i].first);
                out += ':';
                if (pretty) out += ' ';
                writeTo(out, j.obj_[i].second, indent, depth + 1);
            }
            newline(depth);
            out += '}';
            break;
        }
    }
}

const Json& nullJson() {
    static const Json kNull;
    return kNull;
}

}  // namespace

// --- Json implementation --------------------------------------------------

Json Json::parse(const std::string& text, std::string* error) {
    Parser parser{text.data(), text.data() + text.size(), {}};
    Json out;
    if (!parser.parseValue(out, 0)) {
        if (error) {
            const size_t off = static_cast<size_t>(parser.p - text.data());
            *error = parser.err + " at offset " + std::to_string(off);
        }
        return Json();
    }
    parser.skipWs();
    if (parser.p != parser.end) {
        if (error) *error = "trailing characters after JSON value";
        return Json();
    }
    if (error) error->clear();
    return out;
}

std::string Json::dump(int indent) const {
    std::string out;
    out.reserve(256);
    writeTo(out, *this, indent, 0);
    return out;
}

bool Json::asBool(bool def) const {
    switch (type_) {
        case Type::Bool:   return bool_;
        case Type::Number: return num_ != 0.0;
        case Type::String: return !str_.empty() && str_ != "false" && str_ != "0";
        case Type::Array:  return !arr_.empty();
        case Type::Object: return !obj_.empty();
        default: return def;
    }
}

float Json::asFloat(float def) const {
    switch (type_) {
        case Type::Number: return static_cast<float>(num_);
        case Type::Bool:   return bool_ ? 1.0f : 0.0f;
        case Type::String: {
            char* endp = nullptr;
            const float v = std::strtof(str_.c_str(), &endp);
            return (endp && endp != str_.c_str()) ? v : def;
        }
        default: return def;
    }
}

int Json::asInt(int def) const {
    switch (type_) {
        case Type::Number: return static_cast<int>(std::llround(num_));
        case Type::Bool:   return bool_ ? 1 : 0;
        case Type::String: {
            char* endp = nullptr;
            const long v = std::strtol(str_.c_str(), &endp, 10);
            return (endp && endp != str_.c_str()) ? static_cast<int>(v) : def;
        }
        default: return def;
    }
}

double Json::asDouble(double def) const { return type_ == Type::Number ? num_ : def; }

std::string Json::asString(const std::string& def) const {
    switch (type_) {
        case Type::String: return str_;
        case Type::Bool:   return bool_ ? "true" : "false";
        case Type::Number: {
            std::string out;
            numberTo(out, num_);
            return out;
        }
        default: return def;
    }
}

Vec2 Json::asVec2(const Vec2& def) const {
    if (type_ == Type::Array && arr_.size() >= 2)
        return {arr_[0].asFloat(def.x), arr_[1].asFloat(def.y)};
    if (type_ == Type::Object)
        return {asFloat("x", def.x), asFloat("y", def.y)};
    return def;
}

Vec3 Json::asVec3(const Vec3& def) const {
    if (type_ == Type::Array && arr_.size() >= 3)
        return {arr_[0].asFloat(def.x), arr_[1].asFloat(def.y), arr_[2].asFloat(def.z)};
    if (type_ == Type::Object)
        return {asFloat("x", def.x), asFloat("y", def.y), asFloat("z", def.z)};
    return def;
}

Color Json::asColor(const Color& def) const {
    if (type_ == Type::Number) return Color::fromRgba8(static_cast<uint32_t>(num_));
    if (type_ == Type::String) {
        // "#rrggbb" or "rrggbb"
        const std::string h = str_.size() > 0 && str_[0] == '#' ? str_.substr(1) : str_;
        if (h.size() == 6) {
            return Color::fromHex(static_cast<uint32_t>(std::strtoul(h.c_str(), nullptr, 16)), def.a);
        }
    }
    if (type_ == Type::Array && arr_.size() >= 3)
        return {arr_[0].asFloat(def.r), arr_[1].asFloat(def.g), arr_[2].asFloat(def.b),
                arr_.size() >= 4 ? arr_[3].asFloat(def.a) : def.a};
    if (type_ == Type::Object)
        return {asFloat("r", def.r), asFloat("g", def.g), asFloat("b", def.b), asFloat("a", def.a)};
    return def;
}

bool Json::has(const std::string& key) const {
    if (type_ != Type::Object) return false;
    for (const auto& kv : obj_) if (kv.first == key) return true;
    return false;
}

const Json& Json::operator[](const std::string& key) const {
    if (type_ == Type::Object) {
        for (const auto& kv : obj_) if (kv.first == key) return kv.second;
    }
    return nullJson();
}

Json& Json::operator[](const std::string& key) {
    if (type_ != Type::Object) { type_ = Type::Object; obj_.clear(); }
    for (auto& kv : obj_) if (kv.first == key) return kv.second;
    obj_.emplace_back(key, Json());
    return obj_.back().second;
}

void Json::set(const std::string& key, Json value) { (*this)[key] = std::move(value); }

bool Json::erase(const std::string& key) {
    if (type_ != Type::Object) return false;
    for (auto it = obj_.begin(); it != obj_.end(); ++it) {
        if (it->first == key) { obj_.erase(it); return true; }
    }
    return false;
}

std::vector<std::string> Json::keys() const {
    std::vector<std::string> out;
    out.reserve(obj_.size());
    for (const auto& kv : obj_) out.push_back(kv.first);
    return out;
}

size_t Json::size() const {
    if (type_ == Type::Array) return arr_.size();
    if (type_ == Type::Object) return obj_.size();
    if (type_ == Type::String) return str_.size();
    return 0;
}

const Json& Json::at(size_t index) const {
    if (type_ == Type::Array && index < arr_.size()) return arr_[index];
    return nullJson();
}

Json& Json::at(size_t index) {
    if (type_ != Type::Array) { type_ = Type::Array; arr_.clear(); }
    if (index >= arr_.size()) arr_.resize(index + 1);
    return arr_[index];
}

Json& Json::push(Json value) {
    if (type_ != Type::Array) { type_ = Type::Array; arr_.clear(); }
    arr_.push_back(std::move(value));
    return *this;  // return the array, not the element we just appended
}

std::vector<uint8_t> Json::encode() const {
    const std::string s = dump(0);
    return std::vector<uint8_t>(s.begin(), s.end());
}

Json Json::decode(const uint8_t* data, size_t size, std::string* error) {
    if (!data || size == 0) {
        if (error) *error = "empty buffer";
        return Json();
    }
    return parse(std::string(reinterpret_cast<const char*>(data), size), error);
}

}  // namespace nova
