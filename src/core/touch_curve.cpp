#include "touch_curve.h"
#include <cmath>

namespace TouchCurve {

void StickAxis(float x, float y, float* outX, float* outY) {
    const float length = std::sqrt(x * x + y * y);
    if (length <= kDeadZone || length <= 0.f) {
        *outX = *outY = 0.f;
        return;
    }
    float n = (length - kDeadZone) / (1.f - kDeadZone);  // 0 just past the dead zone, 1 at the edge
    if (n > 1.f) n = 1.f;
    const float magnitude = kFloor + (1.f - kFloor) * n;
    *outX = x / length * magnitude;
    *outY = y / length * magnitude;
}

}  // namespace TouchCurve
