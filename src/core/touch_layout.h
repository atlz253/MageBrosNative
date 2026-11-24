// The touch overlay's geometry, kept free of SDL so the host can test the mapping and hit rules.
#pragma once

namespace TouchLayout {

inline constexpr float kButtonRadius = 34.f;
inline constexpr float kHitSlack = 10.f;
inline constexpr float kStartWidth = 84.f;
inline constexpr float kStartHeight = 28.f;
inline constexpr float kBaseRadius = 58.f;
inline constexpr float kStickGrabRadius = kBaseRadius + 32.f;

struct Layout {
    float left = 0, right = 640, top = 44, bottom = 436;
    float baseX = 102, baseY = 378;
    float jumpX = 562, jumpY = 402;
    float fireX = 494, fireY = 358;
    float startX = 320, startTop = 44;
};

enum class Target { None, Stick, Start, Jump, Fire };

// Converts a point in drawable pixels to the game's 480-high virtual space, honouring the
// letterboxed/widescreen destination rectangle. virtualLeft is the virtual coordinate at the
// destination's left edge (the visible widescreen area starts left of 0).
void WindowToVirtual(float x, float y, float windowW, float windowH, int destX, int destY, int destW,
                     int destH, float virtualW, float virtualLeft, float* vx, float* vy);

// What a touch at (x, y) in virtual units lands on. The start pill is checked before the
// buttons, and the buttons before the fixed stick base.
Target HitTest(const Layout& layout, float x, float y);

}  // namespace TouchLayout
