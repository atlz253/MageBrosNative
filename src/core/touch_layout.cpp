#include "touch_layout.h"
#include <cmath>

namespace TouchLayout {
namespace {

bool InDisc(float x, float y, float cx, float cy, float radius) {
    const float dx = x - cx, dy = y - cy;
    return dx * dx + dy * dy <= radius * radius;
}

}  // namespace

void WindowToVirtual(float x, float y, float windowW, float windowH, int destX, int destY, int destW,
                     int destH, float virtualW, float virtualLeft, float* vx, float* vy) {
    (void)windowW;
    (void)windowH;
    *vx = virtualLeft + (destW > 0 ? (x - destX) / destW * virtualW : 0.f);
    *vy = destH > 0 ? (y - destY) / destH * 480.f : 0.f;
}

Target HitTest(const Layout& layout, float x, float y) {
    const float startY = layout.startTop + kStartHeight * 0.5f;
    if (std::fabs(x - layout.startX) <= kStartWidth * 0.5f + kHitSlack &&
        std::fabs(y - startY) <= kStartHeight * 0.5f + kHitSlack)
        return Target::Start;
    if (InDisc(x, y, layout.jumpX, layout.jumpY, kButtonRadius + kHitSlack)) return Target::Jump;
    if (InDisc(x, y, layout.fireX, layout.fireY, kButtonRadius + kHitSlack)) return Target::Fire;
    if (InDisc(x, y, layout.baseX, layout.baseY, kStickGrabRadius)) return Target::Stick;
    return Target::None;
}

}  // namespace TouchLayout
