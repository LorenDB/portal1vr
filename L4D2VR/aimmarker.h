#pragma once
#include "vector.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

// AimMode 2 marker. The ping-pointer beam this mod inherited from Portal 2
// does not exist in Portal 1, so the aim point is drawn as a small SteamVR
// overlay, placed in tracking space where the rendered world shows the point
// the gun is aiming at.
namespace AimMarker {
    constexpr int TextureSize = 64;
    // Quad width per meter of distance, about 1.2 degrees across.
    constexpr float WidthPerMeter = 0.021f;
    constexpr float MinimumWidth = 0.003f;
    // Sit just in front of the surface the gun hit.
    constexpr float DepthBias = 0.99f;

    // OpenVR tracking axes are x right, y up, z back; Source is x forward,
    // y left, z up. Matches VR::GetPoseData.
    inline Vector SourceFromTracking(const Vector& v) { return { -v.z, -v.x, v.y }; }
    inline Vector TrackingFromSource(const Vector& v) { return { -v.y, v.z, -v.x }; }

    // Invert the camera transform. The eyes are rendered at 'cameraWorld' for
    // a headset eye point at 'cameraTracking' (meters), with the world turned
    // 'yawOffset' degrees about up and 'scale' Source units per meter.
    inline bool WorldToTracking(const Vector& world, const Vector& cameraWorld,
                                const Vector& cameraTracking, float yawOffset,
                                float scale, Vector& tracking) {
        if (!(scale > 0.001f))
            return false;
        Vector delta = world - cameraWorld;
        delta.x /= scale; delta.y /= scale; delta.z /= scale;
        VectorPivotXY(delta, { 0, 0, 0 }, -yawOffset);
        tracking = cameraTracking + TrackingFromSource(delta);
        return std::isfinite(tracking.x) && std::isfinite(tracking.y) && std::isfinite(tracking.z);
    }

    inline float Smoothstep(float low, float high, float value) {
        const float t = std::clamp((value - low) / (high - low), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    // A white dot with a dark rim stays readable on Aperture's white panels
    // and on its dark metal ones. RGBA with straight alpha, TextureSize square.
    inline void Paint(std::uint8_t* pixels) {
        constexpr float core = 0.38f, rim = 0.70f, soften = 0.06f;
        for (int y = 0; y < TextureSize; ++y) {
            for (int x = 0; x < TextureSize; ++x) {
                const float dx = (x + 0.5f) / (TextureSize * 0.5f) - 1.0f;
                const float dy = (y + 0.5f) / (TextureSize * 0.5f) - 1.0f;
                const float radius = std::sqrt(dx * dx + dy * dy);
                const float inCore = 1.0f - Smoothstep(core - soften, core + soften, radius);
                const float inDisc = 1.0f - Smoothstep(rim - soften, rim + soften, radius);
                const auto shade = static_cast<std::uint8_t>(20.0f + 235.0f * inCore + 0.5f);
                std::uint8_t* pixel = pixels + (y * TextureSize + x) * 4;
                pixel[0] = pixel[1] = pixel[2] = shade;
                pixel[3] = static_cast<std::uint8_t>(255.0f * inDisc * (0.85f + 0.15f * inCore) + 0.5f);
            }
        }
    }
}
