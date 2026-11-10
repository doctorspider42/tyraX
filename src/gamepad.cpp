#include "gamepad.hpp"

#include <algorithm>
#include <cmath>

#include <GLFW/glfw3.h>

namespace gamepad {

namespace {

// livepad's kPadButtonNames order.
enum { kCross, kSquare, kTriangle, kCircle, kUp, kDown, kLeft, kRight,
       kL1, kL2, kL3, kR1, kR2, kR3, kStart, kSelect };

const int kDefaultMap[kSourceCount] = {kCross, kCircle, kSquare, kTriangle, kL1, kR1, kL2, kR2,
                                       kSelect, kStart, kL3, kR3, kUp, kDown, kLeft, kRight};

// A RADIAL dead zone, rescaled: inside it the stick reads centred, outside it
// the travel that is left maps onto the full range - so there is no jump at
// the edge and a diagonal is not clipped to a square.
void stick(float x, float y, int deadPercent, int8_t& outX, int8_t& outY) {
    const float dead = std::clamp(deadPercent, 0, 90) / 100.0f;
    const float mag = std::sqrt(x * x + y * y);
    if (mag <= dead || mag <= 0.0f) {
        outX = outY = 0;
        return;
    }
    const float scale = (std::min(mag, 1.0f) - dead) / (1.0f - dead) / mag;
    outX = (int8_t)std::clamp((int)std::lround(x * scale * 127.0f), -127, 127);
    outY = (int8_t)std::clamp((int)std::lround(y * scale * 127.0f), -127, 127);
}

}  // namespace

bool Prefs::operator==(const Prefs& o) const {
    return enabled == o.enabled && device == o.device && pad == o.pad && sendMode == o.sendMode &&
           deadZone == o.deadZone && trigger == o.trigger && invertRY == o.invertRY &&
           std::equal(map, map + kSourceCount, o.map);
}

void setFacePreset(Prefs& prefs, bool byLabel) {
    prefs.map[kSrcA] = byLabel ? kCircle : kCross;
    prefs.map[kSrcB] = byLabel ? kCross : kCircle;
}

int facePreset(const Prefs& prefs) {
    if (prefs.map[kSrcA] == kCross && prefs.map[kSrcB] == kCircle) return 0;
    if (prefs.map[kSrcA] == kCircle && prefs.map[kSrcB] == kCross) return 1;
    return -1;
}

void resetMap(Prefs& prefs) { std::copy(kDefaultMap, kDefaultMap + kSourceCount, prefs.map); }

std::vector<std::string> devices() {
    std::vector<std::string> out;
    for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid) {
        if (!glfwJoystickIsGamepad(jid)) continue;
        const char* name = glfwGetGamepadName(jid);
        out.push_back(name ? name : "Controller");
    }
    return out;
}

Sample poll(const Prefs& prefs) {
    Sample s;
    int pick = -1;
    for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid) {
        if (!glfwJoystickIsGamepad(jid)) continue;
        const char* name = glfwGetGamepadName(jid);
        if (pick < 0) pick = jid;  // the first connected one is the fallback
        if (!prefs.device.empty() && name && prefs.device == name) {
            pick = jid;
            break;
        }
    }
    if (pick < 0) return s;
    GLFWgamepadstate st;
    if (!glfwGetGamepadState(pick, &st)) return s;
    s.connected = true;
    const char* name = glfwGetGamepadName(pick);
    s.name = name ? name : "Controller";
    s.lx = st.axes[GLFW_GAMEPAD_AXIS_LEFT_X];
    s.ly = st.axes[GLFW_GAMEPAD_AXIS_LEFT_Y];
    s.rx = st.axes[GLFW_GAMEPAD_AXIS_RIGHT_X];
    s.ry = st.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y];
    // GLFW reports a trigger as -1 (released) .. 1 (fully pressed).
    s.lt = std::clamp((st.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER] + 1.0f) * 0.5f, 0.0f, 1.0f);
    s.rt = std::clamp((st.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] + 1.0f) * 0.5f, 0.0f, 1.0f);
    for (int b = 0; b < 15; ++b) s.buttons[b] = st.buttons[b] == GLFW_PRESS;
    s.guide = s.buttons[GLFW_GAMEPAD_BUTTON_GUIDE];
    return s;
}

bool held(const Sample& s, int source, const Prefs& prefs) {
    if (!s.connected) return false;
    // The PS2 pad's L2 / R2 are buttons on this channel, so a trigger is a
    // button too: held past the threshold.
    const float threshold = std::clamp(prefs.trigger, 5, 95) / 100.0f;
    switch (source) {
        case kSrcA: return s.buttons[GLFW_GAMEPAD_BUTTON_A];
        case kSrcB: return s.buttons[GLFW_GAMEPAD_BUTTON_B];
        case kSrcX: return s.buttons[GLFW_GAMEPAD_BUTTON_X];
        case kSrcY: return s.buttons[GLFW_GAMEPAD_BUTTON_Y];
        case kSrcLB: return s.buttons[GLFW_GAMEPAD_BUTTON_LEFT_BUMPER];
        case kSrcRB: return s.buttons[GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER];
        case kSrcLT: return s.lt >= threshold;
        case kSrcRT: return s.rt >= threshold;
        case kSrcView: return s.buttons[GLFW_GAMEPAD_BUTTON_BACK];
        case kSrcMenu: return s.buttons[GLFW_GAMEPAD_BUTTON_START];
        case kSrcLClick: return s.buttons[GLFW_GAMEPAD_BUTTON_LEFT_THUMB];
        case kSrcRClick: return s.buttons[GLFW_GAMEPAD_BUTTON_RIGHT_THUMB];
        case kSrcUp: return s.buttons[GLFW_GAMEPAD_BUTTON_DPAD_UP];
        case kSrcDown: return s.buttons[GLFW_GAMEPAD_BUTTON_DPAD_DOWN];
        case kSrcLeft: return s.buttons[GLFW_GAMEPAD_BUTTON_DPAD_LEFT];
        case kSrcRight: return s.buttons[GLFW_GAMEPAD_BUTTON_DPAD_RIGHT];
        default: return false;
    }
}

void toPad(const Sample& s, const Prefs& prefs, uint32_t& buttons, int8_t axes[4]) {
    buttons = 0;
    axes[0] = axes[1] = axes[2] = axes[3] = 0;
    if (!s.connected) return;
    for (int src = 0; src < kSourceCount; ++src) {
        const int target = prefs.map[src];
        if (target >= 0 && target < 16 && held(s, src, prefs)) buttons |= 1u << target;
    }
    stick(s.lx, s.ly, prefs.deadZone, axes[0], axes[1]);
    stick(s.rx, prefs.invertRY ? -s.ry : s.ry, prefs.deadZone, axes[2], axes[3]);
}

const char* sourceName(int source) {
    static const char* const kNames[kSourceCount] = {"A", "B", "X", "Y", "LB", "RB", "LT", "RT",
                                                     "View", "Menu", "L click", "R click",
                                                     "Up", "Down", "Left", "Right"};
    return source >= 0 && source < kSourceCount ? kNames[source] : "";
}

const char* targetName(int padButton) {
    static const char* const kNames[16] = {"Cross", "Square", "Triangle", "Circle", "Up", "Down", "Left", "Right",
                                           "L1", "L2", "L3", "R1", "R2", "R3", "Start", "Select"};
    return padButton >= 0 && padButton < 16 ? kNames[padButton] : "Nothing";
}

}  // namespace gamepad
