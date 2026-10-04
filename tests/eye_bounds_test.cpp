#include "../L4D2VR/eye_bounds.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{
    bool near(float value, float expected)
    {
        return std::fabs(value - expected) < 1e-4f;
    }

    bool expectBounds(const char *name, const EyeTextureBounds &bounds,
        float uMin, float vMin, float uMax, float vMax)
    {
        if (near(bounds.uMin, uMin) && near(bounds.vMin, vMin)
            && near(bounds.uMax, uMax) && near(bounds.vMax, vMax)
            && bounds.uMin < bounds.uMax && bounds.vMin < bounds.vMax)
            return true;

        std::cerr << name << " got u[" << bounds.uMin << ", " << bounds.uMax
            << "] v[" << bounds.vMin << ", " << bounds.vMax << "] expected u["
            << uMin << ", " << uMax << "] v[" << vMin << ", " << vMax << "]\n";
        return false;
    }
}

int main()
{
    bool ok = true;

    // Symmetric raw report. API top is the downward tangent.
    float tanX = 0.0f, tanY = 0.0f;
    SymmetricHalfTangents(-1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, tanX, tanY);
    ok &= near(tanX, 1.0f) && near(tanY, 1.0f);
    ok &= expectBounds("symmetric",
        EyeBoundsFromProjectionRaw(-1.0f, 1.0f, -1.0f, 1.0f, tanX, tanY),
        0.0f, 0.0f, 1.0f, 1.0f);

    // Pico 4 native tangents are ±1.279942 on both axes, so the crop is a no-op.
    const float pico = 1.279942f;
    SymmetricHalfTangents(-pico, pico, -pico, pico, -pico, pico, -pico, pico, tanX, tanY);
    ok &= expectBounds("pico",
        EyeBoundsFromProjectionRaw(-pico, pico, -pico, pico, tanX, tanY),
        0.0f, 0.0f, 1.0f, 1.0f);

    // Quest 3 native 72 Hz. Geometric left/right/up/down, stored with OpenVR's
    // swapped vertical outputs. Right eye mirrors horizontally.
    const float qLeft = -1.376382f;
    const float qRight = 0.839100f;
    const float qDown = -1.428148f;
    const float qUp = 0.965689f;
    SymmetricHalfTangents(qLeft, qRight, qDown, qUp, -qRight, -qLeft, qDown, qUp, tanX, tanY);
    ok &= near(tanX, 1.376382f) && near(tanY, 1.428148f);
    ok &= expectBounds("quest-left",
        EyeBoundsFromProjectionRaw(qLeft, qRight, qDown, qUp, tanX, tanY),
        0.0f, 0.161909f, 0.804821f, 1.0f);
    ok &= expectBounds("quest-right",
        EyeBoundsFromProjectionRaw(-qRight, -qLeft, qDown, qUp, tanX, tanY),
        0.195179f, 0.161909f, 1.0f, 1.0f);

    if (!ok)
        return EXIT_FAILURE;
    std::cout << "eye bounds ok\n";
    return EXIT_SUCCESS;
}
