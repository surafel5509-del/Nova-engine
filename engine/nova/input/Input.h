// Input: the device layer, an action layer, and a frame-lifetime event queue.
//
// Devices report raw state; gameplay asks about named actions. Keeping those
// two apart is what lets the editor rebind a key, or replay a recorded session,
// without touching a single line of game code.
#pragma once

#include <array>
#include <map>
#include <string>
#include <vector>

#include "math/Math.h"

namespace nova {
namespace input {

/// Physical keys, in the layout of a USB HID keyboard. Values are stable: a
/// recorded input file has to mean the same thing on every machine.
enum class Key : int {
    Unknown = 0,
    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    Escape, Space, Return, Tab, Backspace,
    Up, Down, Left, Right,
    Shift, Control, Alt, Super,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    Count
};

enum class MouseButton : int { Left = 0, Right, Middle, Extra1, Extra2, Count };

/// Bit flags for binding qualifiers, so "Ctrl+S" and "S" can coexist.
enum ModifierFlags : uint32_t {
    kModNone = 0,
    kModShift = 1u << 0,
    kModControl = 1u << 1,
    kModAlt = 1u << 2,
    kModSuper = 1u << 3,
};

/// One finger or one pointer. On a desktop this stays index 0.
struct Pointer {
    int id = 0;
    Vec2 position{0, 0};      ///< pixels, origin top-left
    Vec2 delta{0, 0};         ///< movement since the previous frame
    Vec2 startPosition{0, 0}; ///< where this press began, for drags
    bool down = false;
    bool pressed = false;   ///< went down this frame
    bool released = false;  ///< went up this frame
    float pressure = 1.0f;
    /// How long the current (or most recent) press lasted. Kept after the
    /// release so a tap can still be recognised on the frame it ends.
    float pressSeconds = 0.0f;
    /// Movement in pixels since the press started; drag-and-drop uses this.
    Vec2 travel() const { return position - startPosition; }
};

/// A named binding: "Move" is down when Left, Right, A or D are down.
struct Binding {
    Key key = Key::Unknown;
    MouseButton mouse = MouseButton::Count;  ///< Count means "not a mouse binding"
    /// Length of the frame [beginFrame] was told about, kept so [endFrame] can
    /// advance the hold timers without being passed it again.
    float frameStep = 0.0f;

    uint32_t modifiers = kModNone;           ///< see ModifierFlags

    Binding() = default;
    Binding(Key k) : key(k) {}
    Binding(MouseButton m) : mouse(m) {}
    Binding& withModifiers(uint32_t m) {
        modifiers = m;
        return *this;
    }
    bool empty() const { return key == Key::Unknown && mouse == MouseButton::Count; }
};

/// State of one named action, resolved from its bindings this frame.
struct ActionState {
    bool down = false;
    bool pressed = false;   ///< became down this frame
    bool released = false;  ///< stopped being down this frame
    float heldSeconds = 0.0f;
    /// -1 when nothing is driving the axis; otherwise the strongest contributor
    /// in [-1, 1]. Unity calls this the analogue axis of the action.
    float axis = -1.0f;
};

/// Everything the host knows about the input devices this frame.
///
/// Gameplay reads this through an ActionMap; it never touches [keys] directly
/// unless it is the platform layer that fills it in.
struct InputState {
    std::array<bool, static_cast<size_t>(Key::Count)> keys{};
    std::array<bool, static_cast<size_t>(Key::Count)> keysPrev{};
    std::array<bool, static_cast<size_t>(Key::Count)> keyPressedEdge{};
    std::array<bool, static_cast<size_t>(Key::Count)> keyReleasedEdge{};
    std::array<float, static_cast<size_t>(Key::Count)> keyRepeat{};

    std::array<bool, static_cast<size_t>(MouseButton::Count)> mouseButtons{};
    std::array<bool, static_cast<size_t>(MouseButton::Count)> mousePrev{};
    std::array<bool, static_cast<size_t>(MouseButton::Count)> mousePressedEdge{};
    std::array<bool, static_cast<size_t>(MouseButton::Count)> mouseReleasedEdge{};

    Vec2 mousePosition{0, 0};
    Vec2 mouseDelta{0, 0};
    Vec2 prevMousePosition{0, 0};
    float wheelDelta = 0.0f;

    std::vector<Pointer> pointers;
    std::vector<Pointer> pointersPrev;

    /// Length of the frame [beginFrame] was told about, kept so [endFrame] can
    /// advance the hold timers without being passed it again.
    float frameStep = 0.0f;

    uint32_t modifiers = kModNone;
    /// Set by the host when the platform's own text-entry field is focused, so
    /// gameplay can ignore shortcuts typed into a text box.
    bool textFieldFocused = false;

    /// Frame boundaries are two calls, because a frame has two ends and input
    /// lands in between:
    ///
    ///   beginFrame(dt)  -- snapshot last frame's device state and zero the
    ///                      per-frame accumulators (wheel, mouse delta).
    ///   ...host feeds key, pointer and button events...
    ///   endFrame()      -- derive this frame's edges, movement and key-repeat
    ///                      timers by comparing against that snapshot.
    ///
    /// Gameplay reads the state after [endFrame]. Deriving the edges inside
    /// [beginFrame] instead would consume the very transition it just saw.
    void beginFrame(float dt);
    void endFrame();

    // --- raw queries --------------------------------------------------------
    bool keyDown(Key k) const;
    bool keyPressed(Key k) const;
    bool keyReleased(Key k) const;
    /// True while the key is held and the repeat timer has elapsed.
    bool keyRepeating(Key k, float delay, float rate) const;
    bool mouseDown(MouseButton b) const;
    bool mousePressed(MouseButton b) const;
    bool mouseReleased(MouseButton b) const;
    bool shiftDown() const { return (modifiers & kModShift) != 0; }
    bool controlDown() const { return (modifiers & kModControl) != 0; }
    bool altDown() const { return (modifiers & kModAlt) != 0; }

    // --- event injection ----------------------------------------------------
    /// A press, release or move for one pointer. [id] distinguishes fingers on
    /// a touch screen. Out-of-range ids are ignored rather than growing the
    /// array without bound.
    void setPointer(int id, Vec2 position, bool down, float pressure = 1.0f);
    void releasePointer(int id, Vec2 position);
    void endAllPointers();

    /// True when the pointer's press just ended, lasted no longer than
    /// [maxDuration] seconds and never travelled further than [maxDistance]
    /// pixels: a tap rather than a drag.
    bool wasTap(const Pointer& p, float maxDuration = 0.3f, float maxDistance = 24.0f) const;

    size_t pointerCount() const { return pointers.size(); }
};

/// Maps names to devices. One instance per player.
class ActionMap {
public:
    ActionMap() = default;

    /// Declares [name] and its bindings. Calling it again replaces the bindings,
    /// which is how an editor rebinds.
    void bind(const std::string& name, std::vector<Binding> bindings,
              float axisNegative = -1.0f, float axisPositive = 1.0f);
    void clear() { order_.clear(); actions_.clear(); }
    bool has(const std::string& name) const { return actions_.count(name) != 0; }

    /// Recomputes every action from [state]. Call once per frame, after the
    /// device state is final. [dt] accumulates held time.
    void update(const InputState& state, float dt);

    const ActionState& action(const std::string& name) const;
    bool down(const std::string& name) const { return action(name).down; }
    bool pressed(const std::string& name) const { return action(name).pressed; }
    bool released(const std::string& name) const { return action(name).released; }
    float held(const std::string& name) const { return action(name).heldSeconds; }
    float axis(const std::string& name) const { return action(name).axis; }

    const std::vector<std::string>& names() const { return order_; }
    size_t size() const { return order_.size(); }

    /// Serialises the map so a rebind can be saved next to the project.
    std::string toJson() const;
    /// Replaces the map from a saved string. Unknown entries are ignored, and a
    /// malformed document leaves the map untouched.
    bool fromJson(const std::string& text);

private:
    struct Action {
        std::vector<Binding> bindings;
        ActionState state;
        float axisNegative = -1.0f;
        float axisPositive = 1.0f;
    };

    bool anyDown(const InputState& s, const Action& a) const;
    bool anyPressed(const InputState& s, const Action& a) const;
    bool anyReleased(const InputState& s, const Action& a) const;
    float computeAxis(const InputState& s, const Action& a) const;

    std::vector<std::string> order_;
    std::map<std::string, Action> actions_;
};

/// Human-readable names, for the editor UI and for debugging.
const char* toString(Key key);
const char* toString(MouseButton button);
bool parseKey(const std::string& name, Key& out);
bool parseMouseButton(const std::string& name, MouseButton& out);

}  // namespace input
}  // namespace nova