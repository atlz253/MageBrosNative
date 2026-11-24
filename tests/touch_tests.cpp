// The touch overlay's pure parts: the stick response, the screen mapping and the hit rules.
#include "core/touch_curve.h"
#include "core/touch_layout.h"
#include <cmath>
#include <cstdio>

static int g_fail = 0;
#define CHECK(c)                                                              \
    do {                                                                      \
        if (!(c)) {                                                           \
            std::printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
            g_fail++;                                                         \
        }                                                                     \
    } while (0)

static bool Near(float a, float b) { return std::fabs(a - b) < 0.01f; }

static void TestStickCurve() {
    float x = 1, y = 1;
    TouchCurve::StickAxis(0.f, 0.f, &x, &y);
    CHECK(x == 0.f && y == 0.f);
    TouchCurve::StickAxis(0.10f, 0.f, &x, &y);  // inside the dead zone
    CHECK(x == 0.f && y == 0.f);

    TouchCurve::StickAxis(0.13f, 0.f, &x, &y);  // just past it: the floor clears the game's 0.5 walk threshold
    CHECK(x > 0.65f && x < 0.67f && y == 0.f);

    TouchCurve::StickAxis(1.f, 0.f, &x, &y);
    CHECK(Near(x, 1.f) && Near(y, 0.f));
    TouchCurve::StickAxis(0.f, -1.f, &x, &y);
    CHECK(Near(x, 0.f) && Near(y, -1.f));

    TouchCurve::StickAxis(0.6f, 0.6f, &x, &y);  // diagonal: same magnitude, direction kept
    CHECK(Near(x, y));
    CHECK(std::sqrt(x * x + y * y) > 0.9f && std::sqrt(x * x + y * y) < 0.96f);

    TouchCurve::StickAxis(1.5f, 0.f, &x, &y);  // beyond the edge is clamped
    CHECK(Near(std::sqrt(x * x + y * y), TouchCurve::kFloor + (1.f - TouchCurve::kFloor)) || Near(x, 1.f));
}

static void TestScreenMapping() {
    float x = 0, y = 0;
    // 4:3 letterboxed inside a 1920x1080 window: the image is 1440x1080 at x = 240.
    TouchLayout::WindowToVirtual(240.f, 0.f, 1920.f, 1080.f, 240, 0, 1440, 1080, 640.f, 0.f, &x, &y);
    CHECK(Near(x, 0.f) && Near(y, 0.f));
    TouchLayout::WindowToVirtual(1680.f, 1080.f, 1920.f, 1080.f, 240, 0, 1440, 1080, 640.f, 0.f, &x, &y);
    CHECK(Near(x, 640.f) && Near(y, 480.f));
    TouchLayout::WindowToVirtual(960.f, 540.f, 1920.f, 1080.f, 240, 0, 1440, 1080, 640.f, 0.f, &x, &y);
    CHECK(Near(x, 320.f) && Near(y, 240.f));

    // Full-width widescreen: 480 high, 853.3 wide, starting at virtual x -106.667.
    TouchLayout::WindowToVirtual(960.f, 540.f, 1920.f, 1080.f, 0, 0, 1920, 1080, 853.333f, -106.667f, &x, &y);
    CHECK(Near(x, 320.f) && Near(y, 240.f));
    TouchLayout::WindowToVirtual(0.f, 540.f, 1920.f, 1080.f, 0, 0, 1920, 1080, 853.333f, -106.667f, &x, &y);
    CHECK(Near(x, -106.667f) && Near(y, 240.f));
    TouchLayout::WindowToVirtual(1920.f, 540.f, 1920.f, 1080.f, 0, 0, 1920, 1080, 853.333f, -106.667f, &x, &y);
    CHECK(Near(x, 746.667f) && Near(y, 240.f));
}

static TouchLayout::Layout TestLayout() {
    TouchLayout::Layout l;
    l.left = 44;
    l.right = 596;
    l.top = 44;
    l.bottom = 436;
    l.baseX = 102;
    l.baseY = 378;
    l.jumpX = 562;
    l.jumpY = 402;
    l.fireX = 494;
    l.fireY = 358;
    l.startX = 320;
    l.startTop = 44;
    return l;
}

static void TestHitTest() {
    const TouchLayout::Layout l = TestLayout();
    CHECK(TouchLayout::HitTest(l, l.jumpX, l.jumpY) == TouchLayout::Target::Jump);
    CHECK(TouchLayout::HitTest(l, l.fireX, l.fireY) == TouchLayout::Target::Fire);
    CHECK(TouchLayout::HitTest(l, l.startX, l.startTop + 14.f) == TouchLayout::Target::Start);
    CHECK(TouchLayout::HitTest(l, l.baseX, l.baseY) == TouchLayout::Target::Stick);
    CHECK(TouchLayout::HitTest(l, 150.f, 380.f) == TouchLayout::Target::Stick);  // inside the grab circle
    CHECK(TouchLayout::HitTest(l, 0.f, 380.f) == TouchLayout::Target::None);     // outside the fixed base
    CHECK(TouchLayout::HitTest(l, 300.f, 380.f) == TouchLayout::Target::None);
    CHECK(TouchLayout::HitTest(l, l.startX + 60.f, l.startTop + 14.f) == TouchLayout::Target::None);
    CHECK(TouchLayout::HitTest(l, l.jumpX + 60.f, l.jumpY) == TouchLayout::Target::None);
    // Slide-off: a point outside the button's disc no longer hits it.
    CHECK(TouchLayout::HitTest(l, l.fireX + TouchLayout::kButtonRadius + TouchLayout::kHitSlack + 1.f,
                               l.fireY) == TouchLayout::Target::None);
}

int main() {
    TestStickCurve();
    TestScreenMapping();
    TestHitTest();
    if (g_fail) {
        std::printf("%d check(s) failed\n", g_fail);
        return 1;
    }
    std::printf("touch control tests passed\n");
    return 0;
}
