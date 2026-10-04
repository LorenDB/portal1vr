#pragma once
#include "handpose.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace OptionalGunGrip {
// A fresh squeeze near the socket is intentional; merely moving a closed hand
// past the weapon must not capture it. Pulling away always releases the hand.
struct State {
    bool active = false;
    bool wasPressed = false;
    bool Update(bool enabled, bool tracked, bool fresh, bool pressed,
                float distance, float radius) {
        const bool rising = pressed && !wasPressed;
        wasPressed = pressed;
        if (!enabled || !tracked || !fresh || !pressed || !std::isfinite(distance)
            || !std::isfinite(radius) || radius <= 0 || distance > radius * 2.5f)
            active = false;
        else if (!active && rising && distance <= radius)
            active = true;
        return active;
    }
};

inline bool Squeeze(bool button, bool skeletonValid, const float *curl, bool previous) {
    if (button) return true;
    if (!skeletonValid) return false;
    float amount = 0;
    for (int i = 2; i < 5; ++i) {
        if (!std::isfinite(curl[i])) return false;
        amount += curl[i] / 3.0f;
    }
    return amount >= (previous ? 0.45f : 0.75f);
}

inline bool Finite(const matrix3x4_t& frame) {
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 4; ++j)
            if (!std::isfinite(frame[i][j])) return false;
    return true;
}

// Portal's gun model has no authored support socket. Seat the left palm under
// the barrel cylinder: fingers across the gun, palm up, thumb toward the
// muzzle. The frame is local to the weapon root bone, in hand-bone axes.
inline matrix3x4_t Socket() {
    return HandPose::Frame({-1,0,0}, {0,-1,0}, {0,0,1}, {2.8f,-1.95f,15.5f});
}
}
