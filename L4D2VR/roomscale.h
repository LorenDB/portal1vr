#pragma once
#include "vector.h"
#include <algorithm>
#include <cmath>

// Roomscale body-follow. The camera already follows the headset through the
// room; this steers the player body after it, so collision, triggers,
// buttons, and portals act where the user is actually standing. Distances are
// Source units and horizontal only; times are seconds.
namespace Roomscale {
    // Head sway and leaning in to look must not walk the body, so following
    // starts late and then closes most of the gap.
    constexpr float StartDistance = 7.0f;
    constexpr float StopDistance = 4.0f;
    // Ground friction swallows wish speeds below about 40 units per second.
    constexpr float MinSpeed = 50.0f;
    constexpr float MaxSpeed = 120.0f;
    constexpr float Gain = 10.0f;
    // Approach no faster than friction can stop the body at the head.
    constexpr float BrakingDeceleration = 300.0f;
    constexpr float BrakingMargin = 2.0f;
    // The body still coasts a little past the head; that much is the
    // follow's doing too.
    constexpr float OvershootAllowance = 4.0f;
    // Falling and flinging: leave the trajectory to the engine.
    constexpr float MaxVerticalSpeed = 150.0f;
    // A command with no progress for this long means a wall or prop is in the way.
    constexpr float StallSeconds = 0.5f;
    constexpr float StallFraction = 0.15f;
    // Blocked following resumes once the head or the body has moved this far.
    constexpr float UnblockHeadDistance = 4.0f;
    constexpr float UnblockBodyDistance = 16.0f;
    // The stick moves the body and the camera together. Wait for that
    // momentum to die before following again.
    constexpr float StickHoldSeconds = 0.4f;
    // Movement is the follow's doing only shortly after it was commanded.
    constexpr float CreditSeconds = 0.15f;
    // With the head already through a portal the body has to come after it:
    // no waiting for the head to get far ahead.
    constexpr float UrgentDistance = 1.0f;
    // Larger jumps between frames are teleports, not locomotion.
    constexpr float TeleportDistance = 64.0f;
    constexpr float MaxFrameSeconds = 0.25f;

    inline float Length2D(const Vector& v) {
        return std::sqrt(v.x * v.x + v.y * v.y);
    }

    inline bool ShouldFollow(float distance, bool following) {
        return std::isfinite(distance) && distance >= (following ? StopDistance : StartDistance);
    }

    inline float Speed(float distance) {
        const float braking = std::sqrt(2.0f * BrakingDeceleration * std::max(distance - BrakingMargin, 0.0f));
        return std::clamp(std::min(distance * Gain, braking), MinSpeed, MaxSpeed);
    }

    // Express a horizontal world-space wish velocity as usercmd forward/side
    // moves. The engine rebuilds its move axes from the command view angles,
    // flattens them, and normalizes each one. With head pitch and roll those
    // axes are not perpendicular, so solve against the same pair.
    inline bool WishToMoves(const Vector& wish, Vector forward, Vector right,
                            float& forwardMove, float& sideMove) {
        const float forwardLength = Length2D(forward);
        const float rightLength = Length2D(right);
        if (!(forwardLength > 0.1f) || !(rightLength > 0.1f))
            return false;
        forward.x /= forwardLength; forward.y /= forwardLength;
        right.x /= rightLength; right.y /= rightLength;
        const float determinant = forward.x * right.y - forward.y * right.x;
        if (!std::isfinite(determinant) || std::fabs(determinant) < 0.1f)
            return false;
        forwardMove = (wish.x * right.y - wish.y * right.x) / determinant;
        sideMove = (forward.x * wish.y - forward.y * wish.x) / determinant;
        return std::isfinite(forwardMove) && std::isfinite(sideMove);
    }

    // How far the body's movement this frame went along the commanded
    // direction, never backwards and never beyond 'limit'. An unrelated push
    // can therefore at most bring the body level with the head.
    inline float Progress(const Vector& moved, const Vector& direction, float limit) {
        const float along = moved.x * direction.x + moved.y * direction.y;
        if (!std::isfinite(along) || !std::isfinite(limit))
            return 0.0f;
        return std::clamp(along, 0.0f, std::max(limit, 0.0f));
    }

    // Ducking crops the engine's move commands to a third, which drops a
    // normal follow command into the friction dead band. Command three times
    // as much while crouched and measurably slow, and stop the moment the
    // body is clearly faster than asked or the crouch ends.
    struct SlowBoost {
        static constexpr float Factor = 3.0f;
        bool active = false;
        float slowFor = 0.0f;
        void Reset() { active = false; slowFor = 0.0f; }
        float Scale() const { return active ? Factor : 1.0f; }
        void Step(float dt, float rate, float speed, bool crouched) {
            if (!active) {
                slowFor = rate < 0.5f * speed ? slowFor + dt : 0.0f;
                if (slowFor > 0.15f && crouched) { active = true; slowFor = 0.0f; }
            }
            else if (rate > 1.6f * speed)
                Reset();
        }
    };

    struct Follow {
        static constexpr double CrouchSettleSeconds = 0.3;
        bool following = false;
        bool blocked = false;
        bool originValid = false;
        bool crouching = false;
        // Set by the caller while the head is through a portal the body has
        // not crossed. The body then follows at once.
        bool urgent = false;
        Vector direction = { 0, 0, 0 };
        Vector blockedOffset = { 0, 0, 0 };
        Vector blockedBody = { 0, 0, 0 };
        Vector previousBody = { 0, 0, 0 };
        float commandSpeed = 0.0f;
        float stall = 0.0f;
        float verticalSpeed = 0.0f;
        double time = 0.0, lastCommand = -1.0e9, stickUntil = -1.0e9, crouchSince = 0.0;
        SlowBoost boost;

        // Once per movement command. 'offset' is the head relative to the
        // body and 'body' the body position, both in world units. Returns
        // true with the wish velocity that walks the body toward the head.
        // It follows wherever the head goes, off an edge included: a body
        // left standing on a ledge the head had walked off did not fall.
        bool Command(double now, const Vector& offset, const Vector& body, bool stickWalking,
                     bool crouchHeld, Vector& wish) {
            if (stickWalking)
                stickUntil = now + StickHoldSeconds;
            if (!crouchHeld) {
                crouching = false;
                boost.Reset();
            }
            else if (!crouching) {
                crouching = true;
                crouchSince = now;
            }
            // Following while the stick is in use would fight it, and the two
            // movements could not be told apart afterwards.
            if (!originValid || now < stickUntil) {
                following = false;
                return false;
            }
            if (blocked) {
                if (Length2D(offset - blockedOffset) < UnblockHeadDistance
                    && Length2D(body - blockedBody) < UnblockBodyDistance)
                    return false;
                blocked = false;
            }
            const float distance = Length2D(offset);
            const bool wanted = urgent ? std::isfinite(distance) && distance >= UrgentDistance
                : ShouldFollow(distance, following);
            if (!wanted || std::fabs(verticalSpeed) > MaxVerticalSpeed) {
                following = false;
                return false;
            }
            if (!following)
                stall = 0.0f;
            following = true;
            commandSpeed = Speed(distance);
            direction = Vector(offset.x / distance, offset.y / distance, 0.0f);
            lastCommand = now;
            const float command = commandSpeed * boost.Scale();
            wish = Vector(direction.x * command, direction.y * command, 0.0f);
            return true;
        }

        // Once per rendered frame. Returns how far the body moved toward the
        // head because of the follow. The caller takes that same distance out
        // of the head offset, so the camera does not move a second time.
        Vector Credit(double now, bool usable, const Vector& body, const Vector& offset) {
            const float dt = static_cast<float>(now - time);
            time = now;
            if (!usable || !originValid || !(dt >= 0.0f) || dt > MaxFrameSeconds) {
                previousBody = body;
                originValid = usable;
                verticalSpeed = 0.0f;
                stall = 0.0f;
                if (!usable) {
                    following = blocked = false;
                    boost.Reset();
                }
                return Vector(0, 0, 0);
            }
            const Vector moved = body - previousBody;
            previousBody = body;
            const bool timed = dt > 0.0001f;
            if (timed)
                verticalSpeed = moved.z / dt;
            if (Length2D(moved) > TeleportDistance)
                return Vector(0, 0, 0);
            // Stick locomotion moves the camera with the body, as it always
            // has; only movement the follow asked for is handed over.
            if (now - lastCommand > CreditSeconds) {
                stall = 0.0f;
                boost.slowFor = 0.0f;
                return Vector(0, 0, 0);
            }
            const float ahead = offset.x * direction.x + offset.y * direction.y;
            const float progress = Progress(moved, direction, ahead + OvershootAllowance);
            if (following && timed) {
                const float rate = progress / dt;
                boost.Step(dt, rate, commandSpeed, crouching && now - crouchSince >= CrouchSettleSeconds);
                stall = rate < StallFraction * commandSpeed ? stall + dt : 0.0f;
                if (stall > StallSeconds) {
                    following = false;
                    blocked = true;
                    blockedOffset = offset;
                    blockedBody = body;
                    stall = 0.0f;
                    boost.Reset();
                }
            }
            return Vector(direction.x * progress, direction.y * progress, 0.0f);
        }
    };
}
