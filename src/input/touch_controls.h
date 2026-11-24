// The touch controller's virtual gamepad and its on-screen stick and buttons.
#pragma once
#include <cstdint>
#include <string>

class Device;
union SDL_Event;

namespace TouchControls {

// Attaches the virtual pad. Must be called after SDL_Init and before Input::Init so it is pad 0.
// enabled comes from the platform, --touch or the touch_controls setting.
bool Init(bool enabled = false);
// True on Android and on platforms with a direct touch screen (not a trackpad).
bool HasTouchScreen();
// 0 off, 1 automatic (touch screens), 2 on; applies at once (the options menu's row).
void SetMode(int mode);
void Shutdown();
bool Active();

// SDL_GamepadButton index, and the left stick in -1..1 (up is -1, as SDL reports it).
void SetButton(int button, bool down);
void SetStick(float x, float y);
// The axis the virtual pad currently reports (after the response curve).
void Axis(float* x, float* y);
// While the options menu is open the stick acts as a D-pad: one deliberate push, one step.
void SetMenuOpen(bool open);
// The virtual pad's own state, for the scripted runs ("S=1 E=0 ST=0 axis=1.00,0.00").
std::string DebugState();
// Test hook: pretend a physical gamepad is connected (the real one is detected through Input).
void TestPhysicalPad(bool present);

// Finger events from SDL, and the same path with virtual-space coordinates for the scripted
// acceptance runs. Coordinates are virtual units (0..640 wide, 0..480 high, before widescreen).
void HandleEvent(const SDL_Event& event, Device* device);
void HandleFinger(uint64_t id, float x, float y, bool down, bool up);

// Draws the stick and buttons over the finished frame, in the game's 480-unit virtual space.
void Render(Device* device);

}  // namespace TouchControls
