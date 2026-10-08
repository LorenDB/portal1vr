#pragma once
#include <algorithm>
#include <cmath>

// RenderWindow=2: the desktop window mirrors an eye instead of rendering the
// scene a third time. The eye is drawn with a wider, symmetric field of view
// than the headset shows; SteamVR's texture bounds give the part it does
// show. The mirror takes that part, cropped about its center to the window's
// shape so nothing is stretched.
namespace DesktopMirror {
struct Rect { long left = 0, top = 0, right = 0, bottom = 0; };

inline bool Crop(int eyeWidth, int eyeHeight, float uMin, float vMin, float uMax, float vMax,
                 int windowWidth, int windowHeight, Rect& out) {
    if (eyeWidth <= 0 || eyeHeight <= 0 || windowWidth <= 0 || windowHeight <= 0)
        return false;
    for (float value : { uMin, vMin, uMax, vMax })
        if (!std::isfinite(value))
            return false;
    // Bounds may be given flipped; only the covered region matters here.
    const float u0 = std::clamp(std::min(uMin, uMax), 0.0f, 1.0f), u1 = std::clamp(std::max(uMin, uMax), 0.0f, 1.0f);
    const float v0 = std::clamp(std::min(vMin, vMax), 0.0f, 1.0f), v1 = std::clamp(std::max(vMin, vMax), 0.0f, 1.0f);
    double left = u0 * eyeWidth, right = u1 * eyeWidth, top = v0 * eyeHeight, bottom = v1 * eyeHeight;
    const double width = right - left, height = bottom - top;
    if (width < 1.0 || height < 1.0)
        return false;
    const double aspect = static_cast<double>(windowWidth) / windowHeight;
    if (width / height > aspect) {
        const double cropped = height * aspect, center = (left + right) * 0.5;
        left = center - cropped * 0.5;
        right = center + cropped * 0.5;
    }
    else {
        const double cropped = width / aspect, center = (top + bottom) * 0.5;
        top = center - cropped * 0.5;
        bottom = center + cropped * 0.5;
    }
    out.left = std::lround(left);
    out.top = std::lround(top);
    out.right = std::lround(right);
    out.bottom = std::lround(bottom);
    return out.right > out.left && out.bottom > out.top;
}
}
