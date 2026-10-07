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
    // The body still coasts a little past the head; that much counts as
    // progress when judging a stall.
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
    // A stall is judged only shortly after a command.
    constexpr float CreditSeconds = 0.15f;
    // With the head already through a portal the body has to come after it:
    // no waiting for the head to get far ahead.
    constexpr float UrgentDistance = 1.0f;
    // Larger jumps between frames are teleports, not locomotion.
    constexpr float TeleportDistance = 64.0f;
    constexpr float MaxFrameSeconds = 0.25f;
    // Source ground movement as Portal runs it (server.dll defaults
    // sv_friction 4, sv_stopspeed 100, sv_accelerate 10) at 66 ticks a
    // second. A ducked player's moves are cropped to a third.
    constexpr float TickSeconds = 0.015f;
    constexpr float GroundFriction = 4.0f;
    constexpr float StopSpeed = 100.0f;
    constexpr float GroundAccelerate = 10.0f;
    constexpr float DuckedMoveScale = 1.0f / 3.0f;
    constexpr float PlayerMaxSpeed = 175.0f;
    // A frame's movement is all the follow's when it lands this close to
    // what the follow's momentum predicts. The slack covers the client
    // seeing the body a tick late.
    constexpr float ExplainedFraction = 0.35f;
    constexpr float ExplainedSlack = 0.1f;
    // Otherwise only movement along the momentum is, and at most this
    // multiple of it: a frame without a tick shows no movement, the next
    // one more than a frame's worth.
    constexpr float MaxShareRatio = 2.0f;
    // Each frame's share in the measured speed of whatever carries the body.
    constexpr float CarriedBlend = 0.5f;
    // Units per second between frames that still count as the same speed.
    constexpr float CarriedSteadiness = 5.0f;
    // A wall portal opening takes the 32-unit hull with this much to spare.
    constexpr float BodyHalfWidth = 16.0f;
    constexpr float OpeningEdgeMargin = 2.0f;

    inline float Length2D(const Vector& v) {
        return std::sqrt(v.x * v.x + v.y * v.y);
    }

    // A wall portal takes the body only where the whole hull fits inside its
    // opening. Steered straight at a head leaning through near an edge, the
    // body met the wall beside the opening and stayed pinned there. Aim at
    // the head moved sideways into the part of the opening the hull fits
    // through instead. 'body' is the body relative to the opening's center,
    // 'forward' and 'left' the opening's axes.
    inline Vector IntoOpening(const Vector& offset, const Vector& body, const Vector& forward,
                              const Vector& left, float openingHalfWidth) {
        const float leftLength = Length2D(left);
        if (!(std::fabs(forward.z) < 0.5f) || !(leftLength > 0.5f))
            return offset;
        const Vector across(left.x / leftLength, left.y / leftLength, 0.0f);
        const float head = (body.x + offset.x) * across.x + (body.y + offset.y) * across.y;
        const float limit = std::max(openingHalfWidth - BodyHalfWidth - OpeningEdgeMargin, 0.0f);
        const float shift = head - std::clamp(head, -limit, limit);
        if (!std::isfinite(shift))
            return offset;
        return Vector(offset.x - across.x * shift, offset.y - across.y * shift, offset.z);
    }

    // The velocity the follow's own commands gave the body: Source ground
    // movement run on those commands alone, one step per movement tick.
    // Walking a curve leaves the body still moving partly along an earlier
    // direction. That is the follow's momentum all the same; handing over
    // only the part along the current direction let the rest carry the
    // camera off the head, by up to 20 units around a one-metre circle.
    struct Momentum {
        // After the latest movement tick, and before it: the client may
        // still be showing the body a tick behind.
        Vector velocity = { 0, 0, 0 };
        Vector previous = { 0, 0, 0 };

        void Reset() { velocity = previous = Vector(0, 0, 0); }
        bool Idle() const { return velocity.LengthSqr() == 0.0f && previous.LengthSqr() == 0.0f; }

        void Step(const Vector& wish) {
            previous = velocity;
            const float speed = Length2D(velocity);
            if (!(speed > 0.1f))
                velocity = Vector(0, 0, 0);
            else {
                const float scale = std::max(speed - std::max(speed, StopSpeed) * GroundFriction * TickSeconds, 0.0f) / speed;
                velocity = Vector(velocity.x * scale, velocity.y * scale, 0.0f);
            }
            const float wishLength = Length2D(wish);
            if (!(wishLength > 0.0f) || !std::isfinite(wishLength))
                return;
            const float wishSpeed = std::min(wishLength, PlayerMaxSpeed);
            const Vector unit(wish.x / wishLength, wish.y / wishLength, 0.0f);
            const float add = wishSpeed - (velocity.x * unit.x + velocity.y * unit.y);
            if (add > 0.0f) {
                const float gain = std::min(GroundAccelerate * TickSeconds * wishSpeed, add);
                velocity = Vector(velocity.x + unit.x * gain, velocity.y + unit.y * gain, 0.0f);
            }
        }

        // How far a frame's horizontal movement lands from the predictions
        // between the two ticks; infinite without a prediction.
        float Miss(const Vector& moved, float dt) const {
            const Vector early(previous.x * dt, previous.y * dt, 0.0f);
            const Vector late(velocity.x * dt, velocity.y * dt, 0.0f);
            const float length = std::max(Length2D(early), Length2D(late));
            if (!(length > 0.01f) || !std::isfinite(length) || !std::isfinite(Length2D(moved)))
                return INFINITY;
            const Vector span(late.x - early.x, late.y - early.y, 0.0f);
            const float spanSquared = span.x * span.x + span.y * span.y;
            const float at = spanSquared > 1e-8f ? std::clamp(((moved.x - early.x) * span.x
                + (moved.y - early.y) * span.y) / spanSquared, 0.0f, 1.0f) : 0.0f;
            return Length2D(Vector(moved.x - early.x - span.x * at, moved.y - early.y - span.y * at, 0.0f));
        }

        // The share of a frame's horizontal movement this momentum explains:
        // all of it when it lands close to the prediction, otherwise only
        // its part along the momentum. A push or a moving platform then
        // still carries the camera with the body.
        Vector Share(const Vector& moved, float dt) const {
            const Vector early(previous.x * dt, previous.y * dt, 0.0f);
            const Vector late(velocity.x * dt, velocity.y * dt, 0.0f);
            const float length = std::max(Length2D(early), Length2D(late));
            const Vector actual(moved.x, moved.y, 0.0f);
            const float miss = Miss(actual, dt);
            if (!std::isfinite(miss))
                return Vector(0, 0, 0);
            if (miss <= ExplainedFraction * length + ExplainedSlack)
                return actual;
            const Vector sum(early.x + late.x, early.y + late.y, 0.0f);
            const float sumLength = Length2D(sum);
            if (!(sumLength > 0.0f))
                return Vector(0, 0, 0);
            const Vector unit(sum.x / sumLength, sum.y / sumLength, 0.0f);
            const float along = std::clamp(actual.x * unit.x + actual.y * unit.y,
                0.0f, MaxShareRatio * length + ExplainedSlack);
            return Vector(unit.x * along, unit.y * along, 0.0f);
        }
    };

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
        Momentum momentum;
        // How fast something else moves the body, in units per second: a
        // moving platform, measured while the follow has no momentum of
        // its own and assumed to carry on while the follow walks the body
        // across it.
        Vector carried = { 0, 0, 0 };
        // A platform moves the body steadily; the coast after the stick or a
        // landing slows down every tick, and is not carrying anything.
        bool carriedSteady = false;

        bool Ducked(double now) const {
            return crouching && now - crouchSince >= CrouchSettleSeconds;
        }

        // Once per movement command. 'offset' is the head relative to the
        // body and 'body' the body position, both in world units. Returns
        // true with the wish velocity that walks the body toward the head.
        // It follows wherever the head goes, off an edge included: a body
        // left standing on a ledge the head had walked off did not fall.
        bool Command(double now, const Vector& offset, const Vector& body, bool stickWalking,
                     bool crouchHeld, Vector& wish) {
            const bool commanded = Decide(now, offset, body, stickWalking, crouchHeld, wish);
            // The stick's movement is not the follow's, nor is anything
            // before the body's position is known.
            if (!originValid || now < stickUntil)
                momentum.Reset();
            else if (!commanded)
                momentum.Step(Vector(0, 0, 0));
            else {
                const float scale = Ducked(now) ? DuckedMoveScale : 1.0f;
                momentum.Step(Vector(wish.x * scale, wish.y * scale, 0.0f));
            }
            return commanded;
        }

        bool Decide(double now, const Vector& offset, const Vector& body, bool stickWalking,
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

        // Once per rendered frame, with the body position the eyes are about
        // to be drawn from. Returns how far the body moved because of the
        // follow. The caller takes that same distance out of the head offset
        // before drawing, so the camera does not move a second time. Taken
        // out a frame later, the camera ran ahead by the last frame's body
        // movement whenever the body started or stopped.
        Vector Credit(double now, bool usable, const Vector& body, const Vector& offset) {
            const float dt = static_cast<float>(now - time);
            time = now;
            if (!usable || !originValid || !(dt >= 0.0f) || dt > MaxFrameSeconds) {
                previousBody = body;
                if (!usable || !originValid) {
                    momentum.Reset();
                    carried = Vector(0, 0, 0);
                    carriedSteady = false;
                }
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
            if (Length2D(moved) > TeleportDistance) {
                momentum.Reset();
                carried = Vector(0, 0, 0);
                carriedSteady = false;
                return Vector(0, 0, 0);
            }
            if (now - lastCommand > CreditSeconds) {
                stall = 0.0f;
                boost.slowFor = 0.0f;
            }
            else if (following && timed) {
                const float ahead = offset.x * direction.x + offset.y * direction.y;
                const float rate = Progress(moved, direction, ahead + OvershootAllowance) / dt;
                boost.Step(dt, rate, commandSpeed, Ducked(now));
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
            // Stick locomotion moves the camera with the body, as it always
            // has; only movement the follow's own momentum explains is
            // handed over, in whatever direction that momentum now points.
            if (!timed)
                return Vector(0, 0, 0);
            if (momentum.Idle()) {
                const Vector measured(moved.x / dt, moved.y / dt, 0.0f);
                carriedSteady = Length2D(measured - carried) <= CarriedSteadiness;
                carried = carried * (1.0f - CarriedBlend) + measured * CarriedBlend;
                return Vector(0, 0, 0);
            }
            if (!carriedSteady)
                carried = Vector(0, 0, 0);
            // Whichever explains the movement better: the body carried as
            // measured, or not carried at all. The coast after the stick, say,
            // ends while the follow is under way.
            const Vector own(moved.x - carried.x * dt, moved.y - carried.y * dt, 0.0f);
            if (momentum.Miss(moved, dt) < momentum.Miss(own, dt)) {
                carried = Vector(0, 0, 0);
                return momentum.Share(moved, dt);
            }
            return momentum.Share(own, dt);
        }
    };
}
