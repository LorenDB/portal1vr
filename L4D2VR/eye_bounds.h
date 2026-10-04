#pragma once

#include <algorithm>

// Subrect submitted to the SteamVR compositor. u = 0 is the left edge and
// v = 0 is the top of an upright image.
struct EyeTextureBounds
{
    float uMin;
    float vMin;
    float uMax;
    float vMax;
};

// GetProjectionRaw's argument order is (left, right, top, bottom), but the
// vertical outputs are swapped: the top pointer receives the downward tangent
// (negative) and the bottom pointer receives the upward tangent (positive).
// tanHalfX and tanHalfY are the positive symmetric overscan half-tangents,
// max(-left, right) and max(-rawTop, rawBottom) across both eyes.
inline EyeTextureBounds EyeBoundsFromProjectionRaw(
    float left, float right, float rawTop, float rawBottom,
    float tanHalfX, float tanHalfY)
{
    EyeTextureBounds bounds;
    bounds.uMin = 0.5f + 0.5f * left / tanHalfX;
    bounds.uMax = 0.5f + 0.5f * right / tanHalfX;
    bounds.vMin = 0.5f - 0.5f * rawBottom / tanHalfY;
    bounds.vMax = 0.5f - 0.5f * rawTop / tanHalfY;
    return bounds;
}

inline void SymmetricHalfTangents(
    float leftLeft, float leftRight, float leftTop, float leftBottom,
    float rightLeft, float rightRight, float rightTop, float rightBottom,
    float &tanHalfX, float &tanHalfY)
{
    tanHalfX = std::max({ -leftLeft, leftRight, -rightLeft, rightRight });
    tanHalfY = std::max({ -leftTop, leftBottom, -rightTop, rightBottom });
}
