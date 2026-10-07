#pragma once
#include <cmath>
#include "vector.h"

// A map camera (point_viewcontrol: Rexaura's intro and ending, Portal's
// testchmb_a_00 and escape_02) owns the engine view while the engine's view
// entity is not the local player. The head stays free at that camera, but
// nothing else about the player applies: roomscale body-follow and
// auto-calibration pause, since the rendered origin is no longer the
// player's eye.
//
// The engine points its flat view along the camera. In VR the player faces
// wherever they were facing, which can be away from the shot. At each cut
// the scene is therefore turned about the camera once, so the shot starts in
// front of the current gaze. Later turns of an animated camera are not
// followed: rotation the head did not make is what makes cutscenes sickening.
namespace MapCamera
{
inline float NormalizeYaw(float yaw)
{
    yaw = std::fmod(yaw + 180.0f, 360.0f);
    if (yaw < 0.0f)
        yaw += 360.0f;
    return yaw - 180.0f;
}

struct State
{
    bool active = false;
    int entity = 0;
    float yawOffset = 0.0f; // Degrees added to the head's world yaw.

    // viewEntity is the engine's view entity, or -1 when it is unknown.
    // Returns true when a camera was entered, cut to another camera, or left.
    bool Update(int viewEntity, int localPlayer, float cameraYaw, float headYaw, bool align)
    {
        const bool camera = viewEntity > 0 && localPlayer > 0 && viewEntity != localPlayer;
        if (!camera)
        {
            const bool changed = active;
            active = false;
            entity = 0;
            yawOffset = 0.0f;
            return changed;
        }
        if (active && viewEntity == entity)
            return false;
        active = true;
        entity = viewEntity;
        const float offset = NormalizeYaw(cameraYaw - headYaw);
        yawOffset = align && std::isfinite(offset) ? offset : 0.0f;
        return true;
    }

    // An eye position turned about the camera origin by the cut's offset.
    Vector TurnPosition(const Vector &cameraOrigin, Vector eye) const
    {
        if (active && yawOffset != 0.0f)
            VectorPivotXY(eye, cameraOrigin, yawOffset);
        return eye;
    }

    // Head angles (pitch, yaw, roll) turned the same way: about world up only.
    Vector TurnAngles(Vector angles) const
    {
        if (active)
            angles.y = NormalizeYaw(angles.y + yawOffset);
        return angles;
    }
};
}
