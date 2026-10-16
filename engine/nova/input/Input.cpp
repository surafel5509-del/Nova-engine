#include "input/Input.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <utility>

#include "base/Json.h"

namespace nova {
namespace input {

namespace {

constexpr size_t kMaxPointers = 16;

/// Case-insensitive compare so "Escape" and "escape" are the same key.
bool iequals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const char ca = static_cast<char>(std::tolower(static_cast<unsigned char>(a[i])));
        const char cb = static_cast<char>(std::tolower(static_cast<unsigned char>(b[i])));
        if (ca != cb) return false;
    }
    return true;
}

size_t index(Key k) { return static_cast<size_t>(k); }

size_t index(MouseButton b) { return static_cast<size_t>(b); }

bool valid(Key k) { return k > Key::Unknown && k < Key::Count; }
bool valid(MouseButton b) { return b >= MouseButton::Left && b < MouseButton::Count; }

struct KeyName {
    Key key;
    const char* name;
};

const KeyName kKeyNames[] = {
    {Key::A, "A"}, {Key::B, "B"}, {Key::C, "C"}, {Key::D, "D"}, {Key::E, "E"}, {Key::F, "F"},
    {Key::G, "G"}, {Key::H, "H"}, {Key::I, "I"}, {Key::J, "J"}, {Key::K, "K"}, {Key::L, "L"},
    {Key::M, "M"}, {Key::N, "N"}, {Key::O, "O"}, {Key::P, "P"}, {Key::Q, "Q"}, {Key::R, "R"},
    {Key::S, "S"}, {Key::T, "T"}, {Key::U, "U"}, {Key::V, "V"}, {Key::W, "W"}, {Key::X, "X"},
    {Key::Y, "Y"}, {Key::Z, "Z"},
    {Key::Num0, "0"}, {Key::Num1, "1"}, {Key::Num2, "2"}, {Key::Num3, "3"}, {Key::Num4, "4"},
    {Key::Num5, "5"}, {Key::Num6, "6"}, {Key::Num7, "7"}, {Key::Num8, "8"}, {Key::Num9, "9"},
    {Key::Escape, "Escape"}, {Key::Space, "Space"}, {Key::Return, "Enter"}, {Key::Tab, "Tab"},
    {Key::Backspace, "Backspace"}, {Key::Up, "Up"}, {Key::Down, "Down"}, {Key::Left, "Left"},
    {Key::Right, "Right"}, {Key::Shift, "Shift"}, {Key::Control, "Control"}, {Key::Alt, "Alt"},
    {Key::Super, "Super"},
    {Key::F1, "F1"}, {Key::F2, "F2"}, {Key::F3, "F3"}, {Key::F4, "F4"}, {Key::F5, "F5"},
    {Key::F6, "F6"}, {Key::F7, "F7"}, {Key::F8, "F8"}, {Key::F9, "F9"}, {Key::F10, "F10"},
    {Key::F11, "F11"}, {Key::F12, "F12"},
};

}  // namespace

const char* toString(Key key) {
    for (const KeyName& kn : kKeyNames) {
        if (kn.key == key) return kn.name;
    }
    return "Unknown";
}

const char* toString(MouseButton button) {
    switch (button) {
        case MouseButton::Left: return "Mouse0";
        case MouseButton::Right: return "Mouse1";
        case MouseButton::Middle: return "Mouse2";
        case MouseButton::Extra1: return "Mouse3";
        case MouseButton::Extra2: return "Mouse4";
        default: return "None";
    }
}

bool parseKey(const std::string& name, Key& out) {
    for (const KeyName& kn : kKeyNames) {
        if (iequals(name, kn.name)) {
            out = kn.key;
            return true;
        }
    }
    return false;
}

bool parseMouseButton(const std::string& name, MouseButton& out) {
    for (int i = 0; i < static_cast<int>(MouseButton::Count); ++i) {
        if (iequals(name, toString(static_cast<MouseButton>(i)))) {
            out = static_cast<MouseButton>(i);
            return true;
        }
    }
    return false;
}

// --- InputState -------------------------------------------------------------

void InputState::beginFrame(float dt) {
    // Snapshot where the devices were when the last frame ended, and clear the
    // accumulators the host is about to refill.
    keysPrev = keys;
    mousePrev = mouseButtons;
    pointersPrev = pointers;
    prevMousePosition = mousePosition;

    mouseDelta = Vec2{0, 0};
    wheelDelta = 0.0f;
    frameStep = std::max(0.0f, dt);
}

void InputState::endFrame() {
    for (size_t i = 0; i < keys.size(); ++i) {
        keyPressedEdge[i] = keys[i] && !keysPrev[i];
        keyReleasedEdge[i] = !keys[i] && keysPrev[i];
        if (keyPressedEdge[i]) {
            keyRepeat[i] = 0.0f;
        } else if (keys[i]) {
            keyRepeat[i] += frameStep;
        }
    }
    for (size_t i = 0; i < mouseButtons.size(); ++i) {
        mousePressedEdge[i] = mouseButtons[i] && !mousePrev[i];
        mouseReleasedEdge[i] = !mouseButtons[i] && mousePrev[i];
    }

    mouseDelta = mousePosition - prevMousePosition;

    // Match the previous frame by id, not by index: a pointer that appeared
    // this frame has no previous entry, and comparing it against itself would
    // report "nothing changed" instead of a fresh press.
    static const Pointer kUp{};
    for (Pointer& p : pointers) {
        const Pointer* prev = &kUp;
        for (const Pointer& candidate : pointersPrev) {
            if (candidate.id == p.id) {
                prev = &candidate;
                break;
            }
        }
        p.delta = p.position - prev->position;
        p.pressed = p.down && !prev->down;
        p.released = !p.down && prev->down;
        if (p.pressed) {
            p.startPosition = p.position;
            p.pressSeconds = 0.0f;
        } else if (p.down) {
            p.pressSeconds += frameStep;
        }
    }
}

bool InputState::keyDown(Key k) const { return valid(k) && keys[index(k)]; }
bool InputState::keyPressed(Key k) const { return valid(k) && keyPressedEdge[index(k)]; }
bool InputState::keyReleased(Key k) const { return valid(k) && keyReleasedEdge[index(k)]; }
bool InputState::keyRepeating(Key k, float delay, float rate) const {
    if (!keyDown(k)) return false;
    const float held = keyRepeat[index(k)];
    if (held < std::max(0.0f, delay)) return false;
    const float r = (rate > 1e-3f) ? rate : 1e-3f;
    return std::fmod(held - delay, r) < 1e-4f;
}
bool InputState::mouseDown(MouseButton b) const { return valid(b) && mouseButtons[index(b)]; }
bool InputState::mousePressed(MouseButton b) const { return valid(b) && mousePressedEdge[index(b)]; }
bool InputState::mouseReleased(MouseButton b) const {
    return valid(b) && mouseReleasedEdge[index(b)];
}

void InputState::setPointer(int id, Vec2 position, bool down, float pressure) {
    if (id < 0) return;
    auto it = std::find_if(pointers.begin(), pointers.end(),
                           [id](const Pointer& p) { return p.id == id; });
    if (it == pointers.end()) {
        if (!down) return;  // a release for a pointer we never saw
        if (pointers.size() >= kMaxPointers) return;
        Pointer p;
        p.id = id;
        p.startPosition = position;
        pointers.push_back(p);
        it = pointers.end() - 1;
    }
    it->position = position;
    it->down = down;
    it->pressure = pressure;
}

void InputState::releasePointer(int id, Vec2 position) { setPointer(id, position, false); }

void InputState::endAllPointers() {
    for (Pointer& p : pointers) {
        p.down = false;
        p.position = p.startPosition;
    }
}

bool InputState::wasTap(const Pointer& p, float maxDuration, float maxDistance) const {
    if (!p.released) return false;
    if (p.pressSeconds > std::max(0.0f, maxDuration)) return false;
    return p.travel().length() <= std::max(0.0f, maxDistance);
}

// --- ActionMap --------------------------------------------------------------

void ActionMap::bind(const std::string& name, std::vector<Binding> bindings, float axisNegative,
                     float axisPositive) {
    Action a;
    a.bindings = std::move(bindings);
    a.axisNegative = axisNegative;
    a.axisPositive = axisPositive;
    if (actions_.find(name) == actions_.end()) order_.push_back(name);
    actions_[name] = std::move(a);
}

bool ActionMap::anyDown(const InputState& s, const Action& a) const {
    for (const Binding& b : a.bindings) {
        if (b.key != Key::Unknown && s.keyDown(b.key)) return true;
        if (b.mouse != MouseButton::Count && s.mouseDown(b.mouse)) return true;
    }
    return false;
}

bool ActionMap::anyPressed(const InputState& s, const Action& a) const {
    for (const Binding& b : a.bindings) {
        if (b.key != Key::Unknown && s.keyPressed(b.key)) return true;
        if (b.mouse != MouseButton::Count && s.mousePressed(b.mouse)) return true;
    }
    return false;
}

bool ActionMap::anyReleased(const InputState& s, const Action& a) const {
    for (const Binding& b : a.bindings) {
        if (b.key != Key::Unknown && s.keyReleased(b.key)) return true;
        if (b.mouse != MouseButton::Count && s.mouseReleased(b.mouse)) return true;
    }
    return false;
}

float ActionMap::computeAxis(const InputState& s, const Action& a) const {
    // Opposing directions cancel. Holding Left and D together has to read as
    // zero movement, not as whichever key happens to be bound first -- that is
    // what a player expects when they let go of a "return to centre" key.
    float best = 0.0f;
    bool negative = false, positive = false;
    for (const Binding& b : a.bindings) {
        bool isDown = false;
        bool isNegative = false;
        if (b.key != Key::Unknown) {
            isDown = s.keyDown(b.key);
            isNegative = (b.key == Key::Left || b.key == Key::Down || b.key == Key::A);
        } else if (b.mouse != MouseButton::Count) {
            isDown = s.mouseDown(b.mouse);
            isNegative = false;
        }
        if (!isDown) continue;
        if (isNegative) {
            negative = true;
        } else {
            positive = true;
        }
        // Strongest magnitude wins when several non-opposing bindings are down.
        const float value = isNegative ? a.axisNegative : a.axisPositive;
        if (std::fabs(value) > std::fabs(best)) best = value;
    }
    if (!negative && !positive) return -1.0f;  // nothing is driving the axis
    if (negative && positive) return 0.0f;
    return negative ? a.axisNegative : a.axisPositive;
}

void ActionMap::update(const InputState& s, float dt) {
    for (const std::string& name : order_) {
        auto it = actions_.find(name);
        if (it == actions_.end()) continue;
        Action& a = it->second;
        const bool wasDown = a.state.down;
        a.state.down = anyDown(s, a);
        a.state.pressed = a.state.down && !wasDown;
        a.state.released = !a.state.down && wasDown;
        a.state.heldSeconds = a.state.down ? a.state.heldSeconds + std::max(0.0f, dt) : 0.0f;
        a.state.axis = computeAxis(s, a);
    }
}

const ActionState& ActionMap::action(const std::string& name) const {
    static const ActionState kEmpty{};
    auto it = actions_.find(name);
    return it == actions_.end() ? kEmpty : it->second.state;
}

std::string ActionMap::toJson() const {
    Json root = Json::object();
    Json actions = Json::array();
    for (const std::string& name : order_) {
        auto it = actions_.find(name);
        if (it == actions_.end()) continue;
        Json entry = Json::object();
        entry.set("name", name);
        Json bindings = Json::array();
        for (const Binding& b : it->second.bindings) {
            Json jb = Json::object();
            jb.set("key", b.key == Key::Unknown ? std::string() : std::string(toString(b.key)));
            jb.set("mouse", b.mouse == MouseButton::Count ? std::string()
                                                           : std::string(toString(b.mouse)));
            jb.set("modifiers", static_cast<int>(b.modifiers));
            bindings.push(jb);
        }
        entry.set("bindings", bindings);
        actions.push(entry);
    }
    root.set("actions", actions);
    return root.dump(0);
}

bool ActionMap::fromJson(const std::string& text) {
    std::string error;
    Json root = Json::parse(text, &error);
    if (!error.empty() || !root.isObject()) return false;
    const Json& actions = root["actions"];
    if (!actions.isArray()) return false;

    // Build into a scratch map first: a half-applied rebind would leave the
    // player with a mix of old and new bindings, which is worse than refusing.
    std::vector<std::string> order;
    std::map<std::string, Action> parsed;
    for (size_t i = 0; i < actions.size(); ++i) {
        const Json& item = actions.at(i);
        if (!item.isObject()) continue;
        const Json& name = item["name"];
        if (!name.isString()) continue;
        Action a;
        const Json& bindings = item["bindings"];
        if (bindings.isArray()) {
            for (size_t j = 0; j < bindings.size(); ++j) {
                const Json& b = bindings.at(j);
                if (!b.isObject()) continue;
                Binding bind;
                const Json& key = b["key"];
                if (key.isString() && !key.asString().empty()) {
                    parseKey(key.asString(), bind.key);
                }
                const Json& mouse = b["mouse"];
                if (mouse.isString() && !mouse.asString().empty()) {
                    parseMouseButton(mouse.asString(), bind.mouse);
                }
                const Json& mods = b["modifiers"];
                if (mods.isNumber()) {
                    bind.modifiers = static_cast<uint32_t>(mods.asInt(0));
                }
                if (!bind.empty()) a.bindings.push_back(bind);
            }
        }
        order.push_back(name.asString());
        parsed[name.asString()] = std::move(a);
    }
    order_ = std::move(order);
    actions_ = std::move(parsed);
    return true;
}

}  // namespace input
}  // namespace nova