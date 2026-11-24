// The touch stick's response, kept free of SDL so the host can test it.
//
// The game walks only at strength >= 0.5 and its own gamepad dead zone (25% by default) is applied
// after this. A plain stick therefore feels dead over much of its travel: the 0.66 floor turns the
// first movement past the dead zone into 0.55 in the game, and a full push reaches its 0.9 cap.
#pragma once

namespace TouchCurve {

inline constexpr float kDeadZone = 0.12f;  // fraction of the stick's radius
inline constexpr float kFloor = 0.66f;     // axis magnitude just past the dead zone

// x, y in -1..1 (up is negative, as SDL reports the left stick); writes the axis to send.
void StickAxis(float x, float y, float* outX, float* outY);

}  // namespace TouchCurve
