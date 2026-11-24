#include "touch_controls.h"
#include "../core/log.h"
#include "../core/touch_curve.h"
#include "../core/touch_layout.h"
#include "../gfx/device.h"
#include "../input/input.h"
#include "../ui/ui_draw.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <utility>

namespace {
SDL_Joystick* g_pad = nullptr;
SDL_JoystickID g_id = 0;
bool g_active = false;
bool g_enabled = false;
bool g_mouseFinger = false;
constexpr uint64_t kMouseFinger = 0x4D4F5553ull;  // one desktop mouse acts as one finger
bool g_fakePhysicalPad = false;
float g_stickX = 0.f, g_stickY = 0.f;  // the finger's position, for the knob
float g_axisX = 0.f, g_axisY = 0.f;    // what the gamepad reports
std::unique_ptr<UIDraw> g_ui;
bool g_layoutLogged = false;

// The virtual mapping assigns physical button indices in SDL's fixed order; including every
// button through the D-pad keeps those indices equal to the SDL_GamepadButton values.
constexpr Uint32 kButtonMask =
    (1u << SDL_GAMEPAD_BUTTON_SOUTH) | (1u << SDL_GAMEPAD_BUTTON_EAST) | (1u << SDL_GAMEPAD_BUTTON_WEST) |
    (1u << SDL_GAMEPAD_BUTTON_NORTH) | (1u << SDL_GAMEPAD_BUTTON_BACK) | (1u << SDL_GAMEPAD_BUTTON_GUIDE) |
    (1u << SDL_GAMEPAD_BUTTON_START) | (1u << SDL_GAMEPAD_BUTTON_LEFT_STICK) |
    (1u << SDL_GAMEPAD_BUTTON_RIGHT_STICK) | (1u << SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) |
    (1u << SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) | (1u << SDL_GAMEPAD_BUTTON_DPAD_UP) |
    (1u << SDL_GAMEPAD_BUTTON_DPAD_DOWN) | (1u << SDL_GAMEPAD_BUTTON_DPAD_LEFT) |
    (1u << SDL_GAMEPAD_BUTTON_DPAD_RIGHT);

constexpr float kMargin = 44.f;
constexpr float kKnobRadius = 24.f;
const UIColor kFill{0.05f, 0.07f, 0.12f, 0.38f};
const UIColor kBorder{0.78f, 0.84f, 0.96f, 0.45f};
const UIColor kKnob{0.86f, 0.90f, 1.0f, 0.75f};
const UIColor kText{1.f, 1.f, 1.f, 0.85f};

using Layout = TouchLayout::Layout;
Layout g_layout;
uint64_t g_stickFinger = 0, g_fireFinger = 0, g_jumpFinger = 0, g_startFinger = 0;
// A quick tap can press and release between two 100 Hz game ticks; the release waits one frame.
bool g_releaseFire = false, g_releaseJump = false, g_releaseStart = false;
bool g_menuOpen = false;
int g_menuDir = -1;  // -1 none, 0 up, 1 right, 2 down, 3 left
constexpr int kMenuButtons[4] = {SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
                                 SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT};


float Clamp(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

uint64_t FingerId(const SDL_Event& e) { return (uint64_t)e.tfinger.fingerID; }

void SafeInsets(const Device& device, float* left, float* top, float* right, float* bottom) {
    *left = *top = *right = *bottom = 0.f;
    if (const char* env = SDL_getenv("MBN_SAFE_INSETS")) {
        if (std::sscanf(env, "%f,%f,%f,%f", left, top, right, bottom) == 4) return;
        *left = *top = *right = *bottom = 0.f;
    }
    SDL_Window* window = device.Window();
    if (!window) return;
    const FrameLayout& L = device.Layout();
    int pointW = 0, pointH = 0;
    SDL_Rect safe{};
    if (L.destW <= 0 || L.destH <= 0) return;
    if (!SDL_GetWindowSize(window, &pointW, &pointH) || pointW <= 0 || pointH <= 0) return;
    if (!SDL_GetWindowSafeArea(window, &safe)) return;

    const float px = (float)L.windowW / pointW, py = (float)L.windowH / pointH;
    const float vx = L.virtualW / L.destW, vy = 480.f / L.destH;
    const float safeLeft = safe.x * px, safeTop = safe.y * py;
    const float safeRight = (pointW - (safe.x + safe.w)) * px;
    const float safeBottom = (pointH - (safe.y + safe.h)) * py;
    const float boxLeft = (float)L.destX, boxTop = (float)L.destY;
    const float boxRight = (float)(L.windowW - L.destX - L.destW);
    const float boxBottom = (float)(L.windowH - L.destY - L.destH);
    *left = std::max(0.f, safeLeft - boxLeft) * vx;
    *top = std::max(0.f, safeTop - boxTop) * vy;
    *right = std::max(0.f, safeRight - boxRight) * vx;
    *bottom = std::max(0.f, safeBottom - boxBottom) * vy;
}

void ComputeLayout(const Device& device, Layout* out) {
    const FrameLayout& L = device.Layout();
    float inL = 0, inT = 0, inR = 0, inB = 0;
    SafeInsets(device, &inL, &inT, &inR, &inB);
    *out = Layout{};
    out->left = -L.offsetX + kMargin + inL;
    out->right = 640.f + L.offsetX - kMargin - inR;
    out->top = kMargin + inT;
    out->bottom = 480.f - kMargin - inB;
    out->baseX = out->left + TouchLayout::kBaseRadius;
    out->baseY = out->bottom - TouchLayout::kBaseRadius;
    out->jumpX = out->right - TouchLayout::kButtonRadius;
    out->jumpY = out->bottom - TouchLayout::kButtonRadius;
    out->fireX = out->jumpX - 2.f * TouchLayout::kButtonRadius - 18.f;
    out->fireY = out->jumpY - 1.3f * TouchLayout::kButtonRadius;
    out->startX = 320.f;  // the centre of the game image
    // Flush with the bottom edge, as low as it can go.
    out->startTop = 480.f - TouchLayout::kStartHeight;
}

void Label(UIDraw& ui, const char* text, float cx, float cy, float scale) {
    float width = (float)std::strlen(text) * 8.f * scale;
    ui.Text(cx - width * 0.5f, cy - 4.f * scale, text, scale, kText);
}

bool InDisc(float x, float y, float cx, float cy, float radius) {
    const float dx = x - cx, dy = y - cy;
    return dx * dx + dy * dy <= radius * radius;
}

}  // namespace

namespace TouchControls {

namespace {

void ReleaseMenuDir() {
    if (g_menuDir >= 0) SetButton(kMenuButtons[g_menuDir], false);
    g_menuDir = -1;
}

void UpdateMenuDir() {
    const float ax = std::fabs(g_stickX), ay = std::fabs(g_stickY);
    const float major = std::max(ax, ay), minor = std::min(ax, ay);
    if (major < 0.35f) {  // released
        ReleaseMenuDir();
        return;
    }
    if (g_menuDir >= 0 && major < 0.55f) return;  // hysteresis: keep the current step
    // One axis at a time: a diagonal push is ambiguous and does nothing (or keeps the
    // direction already held). The major axis must clearly dominate.
    if (major < minor * 1.35f) return;
    const int dir = ax > ay ? (g_stickX > 0.f ? 1 : 3) : (g_stickY > 0.f ? 2 : 0);
    if (dir != g_menuDir) {
        ReleaseMenuDir();
        g_menuDir = dir;
        SetButton(kMenuButtons[dir], true);  // one button-down step, no repeat
    }
}

void ApplyStickAxis() {
    TouchCurve::StickAxis(g_stickX, g_stickY, &g_axisX, &g_axisY);
    if (!g_pad) return;
    SDL_SetJoystickVirtualAxis(g_pad, SDL_GAMEPAD_AXIS_LEFTX, (Sint16)(g_axisX * 32767.f));
    SDL_SetJoystickVirtualAxis(g_pad, SDL_GAMEPAD_AXIS_LEFTY, (Sint16)(g_axisY * 32767.f));
    SDL_UpdateJoysticks();  // the game sees the deflection on this frame
}

bool Attach() {
    if (g_active) return true;
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.name = "Mage Bros. touch controls";
    desc.naxes = 2;
    desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_LEFTX) | (1u << SDL_GAMEPAD_AXIS_LEFTY);
    desc.nbuttons = 15;
    desc.button_mask = kButtonMask;
    g_id = SDL_AttachVirtualJoystick(&desc);
    if (!g_id) {
        Log("Touch controls: could not attach the virtual pad (%s)", SDL_GetError());
        return false;
    }
    g_pad = SDL_OpenJoystick(g_id);
    if (!g_pad) {
        Log("Touch controls: could not open the virtual pad (%s)", SDL_GetError());
        SDL_DetachVirtualJoystick(g_id);
        g_id = 0;
        return false;
    }
    g_active = true;
    Log("Touch controls: virtual pad attached (SDL id %u)", (unsigned)g_id);
    return true;
}

void Detach(const char* why) {
    if (!g_active) return;
    SetStick(0.f, 0.f);
    SetButton(SDL_GAMEPAD_BUTTON_SOUTH, false);
    SetButton(SDL_GAMEPAD_BUTTON_EAST, false);
    SetButton(SDL_GAMEPAD_BUTTON_START, false);
    ReleaseMenuDir();
    g_menuOpen = false;
    g_stickFinger = g_fireFinger = g_jumpFinger = g_startFinger = 0;
    g_releaseFire = g_releaseJump = g_releaseStart = false;
    if (g_pad) SDL_CloseJoystick(g_pad);
    g_pad = nullptr;
    if (g_id) SDL_DetachVirtualJoystick(g_id);
    g_id = 0;
    g_active = false;
    Log("Touch controls: hidden (%s)", why);
}

void SyncPresence() {
    if (!g_enabled) {
        if (g_active) Detach("switched off");
        return;
    }
    bool physical = Input::NumPhysicalPads() > 0 || g_fakePhysicalPad;
    if (const char* fake = SDL_getenv("MBN_FAKE_PHYSICAL_PAD"))
        if (std::atoi(fake) > 0) physical = true;
    if (physical && g_active) {
        Detach("a physical gamepad is connected");
    } else if (!physical && !g_active) {
        if (Attach()) Log("Touch controls: shown (no physical gamepad)");
    }
}

}  // namespace

bool Init(bool enabled) {
    g_enabled = enabled;
    g_mouseFinger = enabled;
    if (!enabled) return false;
    return Attach();
}

void SetMode(int mode) {
    const bool touchScreen = HasTouchScreen();
    g_enabled = mode == 2 || (mode == 1 && touchScreen);
    g_mouseFinger = g_enabled;
    Log("Touch controls: setting %s%s", mode == 0 ? "off" : mode == 2 ? "on" : "automatic",
        mode == 1 && !touchScreen ? " (no touch screen: off)" : "");
    SyncPresence();
}

void Shutdown() {
    g_enabled = false;
    Detach("shutdown");
    g_ui.reset();  // free the font texture while the GL context is alive
    g_stickX = g_stickY = g_axisX = g_axisY = 0.f;
}

bool Active() { return g_active; }

bool HasTouchScreen() {
#if defined(__ANDROID__)
    return true;
#else
    int count = 0;
    SDL_TouchID* ids = SDL_GetTouchDevices(&count);
    bool direct = false;
    for (int i = 0; i < count; i++)
        if (SDL_GetTouchDeviceType(ids[i]) == SDL_TOUCH_DEVICE_DIRECT) direct = true;
    SDL_free(ids);
    return direct;
#endif
}

void SetButton(int button, bool down) {
    if (!g_pad || button < 0 || button >= 15) return;
    if (!SDL_SetJoystickVirtualButton(g_pad, button, down))
        Log("Touch controls: virtual button %d failed (%s)", button, SDL_GetError());
    SDL_UpdateJoysticks();  // the game sees the press on this frame
}

void SetStick(float x, float y) {
    g_stickX = Clamp(x, -1.f, 1.f);
    g_stickY = Clamp(y, -1.f, 1.f);
    if (g_menuOpen) {
        UpdateMenuDir();
        return;  // no analog axis in the menus, so nothing auto-repeats
    }
    ApplyStickAxis();
}

void SetMenuOpen(bool open) {
    if (open == g_menuOpen) return;
    g_menuOpen = open;
    ReleaseMenuDir();
    if (!open) {
        ApplyStickAxis();  // a held stick carries on into the game
    } else {
        g_axisX = g_axisY = 0.f;
        if (g_pad) {
            SDL_SetJoystickVirtualAxis(g_pad, SDL_GAMEPAD_AXIS_LEFTX, 0);
            SDL_SetJoystickVirtualAxis(g_pad, SDL_GAMEPAD_AXIS_LEFTY, 0);
            SDL_UpdateJoysticks();
        }
    }
}

void Axis(float* x, float* y) {
    *x = g_axisX;
    *y = g_axisY;
}

void TestPhysicalPad(bool present) {
    g_fakePhysicalPad = present;
    SyncPresence();
}

std::string DebugState() {
    const bool s = g_pad && SDL_GetJoystickButton(g_pad, SDL_GAMEPAD_BUTTON_SOUTH);
    const bool e = g_pad && SDL_GetJoystickButton(g_pad, SDL_GAMEPAD_BUTTON_EAST);
    const bool st = g_pad && SDL_GetJoystickButton(g_pad, SDL_GAMEPAD_BUTTON_START);
    const Sint16 x = g_pad ? SDL_GetJoystickAxis(g_pad, SDL_GAMEPAD_AXIS_LEFTX) : 0;
    const Sint16 y = g_pad ? SDL_GetJoystickAxis(g_pad, SDL_GAMEPAD_AXIS_LEFTY) : 0;
    char buf[128];
    std::snprintf(buf, sizeof buf, "S=%d E=%d ST=%d axis=%.2f,%.2f", s ? 1 : 0, e ? 1 : 0, st ? 1 : 0,
                  x / 32767.f, y / 32767.f);
    return buf;
}

void HandleFinger(uint64_t id, float x, float y, bool down, bool up) {
    if (!g_active) return;

    if (up) {
        if (id == g_stickFinger) {
            g_stickFinger = 0;
            SetStick(0.f, 0.f);
        }
        if (id == g_fireFinger) {
            g_fireFinger = 0;
            g_releaseFire = true;
        }
        if (id == g_jumpFinger) {
            g_jumpFinger = 0;
            g_releaseJump = true;
        }
        if (id == g_startFinger) {
            g_startFinger = 0;
            g_releaseStart = true;
        }
        return;
    }

    const Layout& L = g_layout;
    if (down) {
        switch (TouchLayout::HitTest(L, x, y)) {
            case TouchLayout::Target::Start:
                if (g_startFinger) return;
                g_startFinger = id;
                g_releaseStart = false;
                SetButton(SDL_GAMEPAD_BUTTON_START, true);
                return;
            case TouchLayout::Target::Jump:
                if (g_jumpFinger) return;
                g_jumpFinger = id;
                g_releaseJump = false;
                SetButton(SDL_GAMEPAD_BUTTON_EAST, true);
                return;
            case TouchLayout::Target::Fire:
                if (g_fireFinger) return;
                g_fireFinger = id;
                g_releaseFire = false;
                SetButton(SDL_GAMEPAD_BUTTON_SOUTH, true);
                return;
            case TouchLayout::Target::Stick:
                if (g_stickFinger) return;
                g_stickFinger = id;  // the base stays where it is drawn
                SetStick(0.f, 0.f);
                return;
            default:
                return;
        }
    }

    // moving
    if (id == g_stickFinger) {
        SetStick((x - L.baseX) / TouchLayout::kBaseRadius, (y - L.baseY) / TouchLayout::kBaseRadius);
        return;
    }
    if (id == g_fireFinger && !InDisc(x, y, L.fireX, L.fireY, TouchLayout::kButtonRadius + TouchLayout::kHitSlack)) {
        g_fireFinger = 0;
        SetButton(SDL_GAMEPAD_BUTTON_SOUTH, false);
    }
    if (id == g_jumpFinger && !InDisc(x, y, L.jumpX, L.jumpY, TouchLayout::kButtonRadius + TouchLayout::kHitSlack)) {
        g_jumpFinger = 0;
        SetButton(SDL_GAMEPAD_BUTTON_EAST, false);
    }
}

void HandleEvent(const SDL_Event& event, Device* device) {
    if (!device) return;
    SyncPresence();
    if (!g_active) return;
    ComputeLayout(*device, &g_layout);
    const FrameLayout& L = device->Layout();

    if (g_mouseFinger) {
        // On a desktop test run the mouse is one finger: press, drag and release the controls.
        const bool mouseMotion = event.type == SDL_EVENT_MOUSE_MOTION;
        const bool mouseDown = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT;
        const bool mouseUp = event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT;
        const bool mouseDrag = mouseMotion && (event.motion.state & SDL_BUTTON_LMASK);
        if (mouseDown || mouseDrag || mouseUp) {
            int pointW = 0, pointH = 0;
            if (SDL_GetWindowSize(device->Window(), &pointW, &pointH) && pointW > 0 && pointH > 0) {
                const float mx = mouseMotion ? event.motion.x : event.button.x;
                const float my = mouseMotion ? event.motion.y : event.button.y;
                const float px = mx * (float)L.windowW / pointW;
                const float py = my * (float)L.windowH / pointH;
                float vx = 0, vy = 0;
                TouchLayout::WindowToVirtual(px, py, (float)L.windowW, (float)L.windowH, L.destX, L.destY,
                                             L.destW, L.destH, L.virtualW, -L.offsetX, &vx, &vy);
                HandleFinger(kMouseFinger, vx, vy, mouseDown, mouseUp);
            }
            return;
        }
    }

    if (event.type != SDL_EVENT_FINGER_DOWN && event.type != SDL_EVENT_FINGER_MOTION &&
        event.type != SDL_EVENT_FINGER_UP)
        return;
    const float px = event.tfinger.x * L.windowW;
    const float py = event.tfinger.y * L.windowH;
    float vx = 0, vy = 0;
    TouchLayout::WindowToVirtual(px, py, (float)L.windowW, (float)L.windowH, L.destX, L.destY, L.destW, L.destH,
                                 L.virtualW, -L.offsetX, &vx, &vy);
    const bool down = event.type == SDL_EVENT_FINGER_DOWN;
    const bool up = event.type == SDL_EVENT_FINGER_UP;
    HandleFinger(FingerId(event), vx, vy, down, up);
}

void Render(Device* device) {
    if (!device) return;
    SyncPresence();
    if (!g_active) return;
    // The game has moved this frame (Render follows FrameMove): a quick tap has now been seen.
    if (g_releaseFire) { SetButton(SDL_GAMEPAD_BUTTON_SOUTH, false); g_releaseFire = false; }
    if (g_releaseJump) { SetButton(SDL_GAMEPAD_BUTTON_EAST, false); g_releaseJump = false; }
    if (g_releaseStart) { SetButton(SDL_GAMEPAD_BUTTON_START, false); g_releaseStart = false; }
    ComputeLayout(*device, &g_layout);
    const Layout& L = g_layout;
    if (!g_layoutLogged) {
        const FrameLayout& F = device->Layout();
        Log("Touch controls: overlay layout, safe insets left %.0f top %.0f right %.0f bottom %.0f (virtual units)",
            L.left + F.offsetX - kMargin, L.top - kMargin, 640.f + F.offsetX - kMargin - L.right,
            480.f - kMargin - L.bottom);
        g_layoutLogged = true;
    }

    if (!g_ui) g_ui = std::make_unique<UIDraw>();
    g_ui->Begin(device);

    g_ui->Disc(L.baseX, L.baseY, TouchLayout::kBaseRadius, kFill);
    g_ui->Ring(L.baseX, L.baseY, TouchLayout::kBaseRadius, TouchLayout::kBaseRadius - 4.f, kBorder);
    const float knobX = L.baseX + g_stickX * (TouchLayout::kBaseRadius - kKnobRadius - 4.f);
    const float knobY = L.baseY + g_stickY * (TouchLayout::kBaseRadius - kKnobRadius - 4.f);
    g_ui->Disc(knobX, knobY, kKnobRadius, kKnob);

    for (const auto& b : {std::pair<float, float>{L.jumpX, L.jumpY}, std::pair<float, float>{L.fireX, L.fireY}}) {
        g_ui->Disc(b.first, b.second, TouchLayout::kButtonRadius, kFill);
        g_ui->Ring(b.first, b.second, TouchLayout::kButtonRadius, TouchLayout::kButtonRadius - 3.f, kBorder);
    }
    Label(*g_ui, "JUMP", L.jumpX, L.jumpY, 1.3f);
    Label(*g_ui, "FIRE", L.fireX, L.fireY, 1.3f);

    const float startX = L.startX, startTop = L.startTop;
    g_ui->Rect(startX - TouchLayout::kStartWidth * 0.5f, startTop, startX + TouchLayout::kStartWidth * 0.5f, startTop + TouchLayout::kStartHeight, kBorder);
    g_ui->Rect(startX - TouchLayout::kStartWidth * 0.5f + 2.f, startTop + 2.f, startX + TouchLayout::kStartWidth * 0.5f - 2.f,
               startTop + TouchLayout::kStartHeight - 2.f, kFill);
    Label(*g_ui, "START", startX, startTop + TouchLayout::kStartHeight * 0.5f, 1.f);
}

}  // namespace TouchControls
