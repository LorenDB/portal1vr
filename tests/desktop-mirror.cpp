#include <cassert>
#include <cmath>
#include <cstdio>
#include "desktopmirror.h"

using DesktopMirror::Crop;
using DesktopMirror::Rect;

int main() {
    unsigned checks = 0;
    Rect rect;
    // The Quest 3 left eye as SteamVR reported it: 2624x2740, showing
    // u 0..0.805 and v 0.162..1. A 3840x2160 window takes a 16:9 band
    // across the full visible width, centered in the visible height.
    assert(Crop(2624, 2740, 0.0f, 0.162f, 0.805f, 1.0f, 3840, 2160, rect));
    const double width = rect.right - rect.left, height = rect.bottom - rect.top;
    assert(rect.left == 0 && rect.right == std::lround(0.805 * 2624));
    assert(std::fabs(width / height - 16.0 / 9.0) < 0.002);
    assert(std::fabs((rect.top + rect.bottom) * 0.5 - (0.162 + 1.0) * 0.5 * 2740) <= 1.0);
    assert(rect.top >= std::lround(0.162 * 2740) && rect.bottom <= 2740);
    checks += 4;

    // A window taller than the visible region crops the sides instead.
    assert(Crop(2000, 2000, 0.0f, 0.0f, 1.0f, 1.0f, 1000, 2000, rect));
    assert(rect.top == 0 && rect.bottom == 2000 && rect.left == 500 && rect.right == 1500);
    checks += 2;

    // Flipped bounds cover the same region.
    Rect flipped;
    assert(Crop(2624, 2740, 0.805f, 1.0f, 0.0f, 0.162f, 3840, 2160, flipped));
    assert(Crop(2624, 2740, 0.0f, 0.162f, 0.805f, 1.0f, 3840, 2160, rect));
    assert(flipped.left == rect.left && flipped.top == rect.top && flipped.right == rect.right && flipped.bottom == rect.bottom);
    checks += 2;

    // Nothing to mirror, or nowhere to put it.
    assert(!Crop(0, 2740, 0.0f, 0.0f, 1.0f, 1.0f, 3840, 2160, rect));
    assert(!Crop(2624, 2740, 0.0f, 0.0f, 1.0f, 1.0f, 3840, 0, rect));
    assert(!Crop(2624, 2740, 0.5f, 0.0f, 0.5f, 1.0f, 3840, 2160, rect));
    assert(!Crop(2624, 2740, NAN, 0.0f, 1.0f, 1.0f, 3840, 2160, rect));
    checks += 4;

    printf("{\"desktop_mirror_checks\":%u,\"passed\":true}\n", checks);
    return 0;
}
