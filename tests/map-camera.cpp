#include <cassert>
#include <cmath>
#include <cstdio>
#include "sdk/sdk.h"
#include "mapcamera.h"

namespace {
bool Near(float a, float b, float tolerance = 1e-3f) { return std::fabs(a - b) <= tolerance; }
bool Near(const Vector &a, const Vector &b) { return Near(a.x, b.x) && Near(a.y, b.y) && Near(a.z, b.z); }

// Source's forward vector for a yaw, in the horizontal plane.
Vector Forward(float yaw) {
    const float r = yaw * 3.14159265f / 180.0f;
    return Vector(std::cos(r), std::sin(r), 0.0f);
}
}

int main() {
    using MapCamera::State;
    constexpr int player = 1;

    // The player's own view, unknown detection, and world (0) are not cameras.
    State state;
    assert(!state.Update(player, player, 90.0f, 0.0f, true) && !state.active);
    assert(!state.Update(-1, player, 90.0f, 0.0f, true) && !state.active);
    assert(!state.Update(0, player, 90.0f, 0.0f, true) && !state.active);
    assert(!state.Update(57, -1, 90.0f, 0.0f, true) && !state.active);

    // Entering: the shot's yaw is put in front of the current gaze, and
    // nothing else about the head changes (pitch and roll are kept).
    assert(state.Update(57, player, 170.0f, -150.0f, true));
    assert(state.active && state.entity == 57 && Near(state.yawOffset, -40.0f));
    const Vector head(-20.0f, -150.0f, 5.0f);
    const Vector turned = state.TurnAngles(head);
    assert(Near(turned.x, -20.0f) && Near(turned.y, 170.0f) && Near(turned.z, 5.0f));

    // An eye ahead of the head stays ahead of the turned gaze, at the same
    // distance and height from the camera.
    const Vector camera(100.0f, -50.0f, 64.0f);
    const Vector eye = camera + Forward(-150.0f) * 3.0f + Vector(0, 0, 2.0f);
    const Vector turnedEye = state.TurnPosition(camera, eye);
    assert(Near(turnedEye, camera + Forward(170.0f) * 3.0f + Vector(0, 0, 2.0f)));

    // The camera's own later turns are not followed; the head is.
    assert(!state.Update(57, player, 120.0f, -100.0f, true));
    assert(Near(state.TurnAngles(Vector(0, -100.0f, 0)).y, -140.0f));

    // A cut to another camera aligns again.
    assert(state.Update(58, player, 0.0f, -100.0f, true));
    assert(state.entity == 58 && Near(state.yawOffset, 100.0f));

    // Leaving restores the head's own view exactly.
    assert(state.Update(player, player, 0.0f, 0.0f, true) && !state.active);
    assert(Near(state.TurnAngles(head), head) && Near(state.TurnPosition(camera, eye), eye));
    assert(!state.Update(player, player, 0.0f, 0.0f, true));

    // MapCameraAlign=false: the camera is still recognised, but not turned.
    assert(state.Update(60, player, 90.0f, 0.0f, false) && state.active);
    assert(Near(state.TurnAngles(head), head) && Near(state.TurnPosition(camera, eye), eye));

    // A non-finite yaw from the engine never reaches the eyes.
    State broken;
    assert(broken.Update(61, player, NAN, 0.0f, true) && broken.yawOffset == 0.0f);

    // Normalisation wraps across +/-180.
    assert(Near(MapCamera::NormalizeYaw(190.0f), -170.0f));
    assert(Near(MapCamera::NormalizeYaw(-190.0f), 170.0f));
    assert(Near(MapCamera::NormalizeYaw(540.0f), -180.0f));
    puts("PASS: map camera detection, cut alignment, head-only turns, release, and disabled alignment");
    return 0;
}
