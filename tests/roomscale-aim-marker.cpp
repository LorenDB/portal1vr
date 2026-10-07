#include <Windows.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <utility>
#include <vector>
#include "sdk/sdk.h"
#include "roomscale.h"
#include "aimmarker.h"
#include "debuglog.h"

namespace {
// Source ground movement along one axis: friction, then acceleration toward
// the wish speed. Ducking crops the move commands to a third.
constexpr float kTick = 0.015f, kFriction = 4.0f, kStopSpeed = 100.0f, kAccelerate = 10.0f, kMaxSpeed = 175.0f;
constexpr float kScale = 43.2f;

struct Body {
    float position = 0.0f, velocity = 0.0f;
    void Tick(float wish, bool ducked, float wall) {
        if (ducked) wish /= 3.0f;
        const float speed = fabsf(velocity);
        if (speed > 0.1f)
            velocity *= std::max(0.0f, speed - std::max(speed, kStopSpeed) * kFriction * kTick) / speed;
        const float wishSpeed = std::min(fabsf(wish), kMaxSpeed);
        if (wishSpeed > 0.0f) {
            const float sign = wish > 0.0f ? 1.0f : -1.0f;
            const float add = wishSpeed - velocity * sign;
            if (add > 0.0f) velocity += std::min(kAccelerate * kTick * wishSpeed, add) * sign;
        }
        position += velocity * kTick;
        if (wall >= 0.0f && position > wall) { position = wall; velocity = 0.0f; }
    }
};

struct Scenario {
    std::function<float(double)> head;  // metres along the walk axis
    std::function<float(double)> stick = [](double) { return 0.0f; };  // wish speed along the axis
    std::function<bool(double)> crouch = [](double) { return false; };
    float wall = -1.0f;
    double fps = 90.0, seconds = 6.0;
};

struct Trace {
    std::vector<float> body, camera, head;
    float endGap = 0.0f, maxGap = 0.0f;
    bool blocked = false;
};

// One run of the follow against the mover. The client sees the body one tick
// late and interpolated, as it does in single player; the renderer takes the
// body's movement out of the head offset just before drawing, as
// Hooks::dRenderView does through VR::UpdateRoomscaleFollow.
Trace Run(const Scenario& scenario, bool withHead) {
    const Vector axis(cosf(0.65f), sinf(0.65f), 0.0f);
    Roomscale::Follow follow;
    Body body;
    std::vector<std::pair<double, float>> history{ { 0.0, 0.0f } };
    const auto client = [&](double time) {
        const double at = time - kTick;
        for (size_t i = 0; i + 1 < history.size(); ++i)
            if (history[i].first <= at && at <= history[i + 1].first)
                return history[i].second + (history[i + 1].second - history[i].second)
                    * static_cast<float>((at - history[i].first) / (history[i + 1].first - history[i].first));
        return at > history.back().first ? history.back().second : history.front().second;
    };
    Trace trace;
    float center = 0.0f, offset = 0.0f;
    double nextTick = 0.0;
    const double frame = 1.0 / scenario.fps;
    for (double t = 0.0; t < scenario.seconds; t += frame) {
        const float head = withHead ? scenario.head(t) * kScale : 0.0f;
        while (nextTick <= t) {
            Vector wish(0, 0, 0);
            float command = 0.0f;
            if (follow.Command(t + 100.0, axis * (head - center), axis * client(t),
                    fabsf(scenario.stick(t)) > 1.0f, scenario.crouch(t),
                    wish)) {
                command = wish.x * axis.x + wish.y * axis.y;
                assert(fabsf(wish.x * axis.y - wish.y * axis.x) < 0.01f && wish.z == 0.0f);
                assert(fabsf(command) <= Roomscale::MaxSpeed * Roomscale::SlowBoost::Factor + 0.01f);
            }
            body.Tick(command + scenario.stick(t), scenario.crouch(t - 0.2), scenario.wall);
            nextTick += kTick;
            history.emplace_back(nextTick, body.position);
            if (history.size() > 8) history.erase(history.begin());
        }
        const float seen = client(t);
        const Vector covered = follow.Credit(t + 100.0, true, axis * seen, axis * (head - center));
        assert(covered.z == 0.0f);
        center += covered.x * axis.x + covered.y * axis.y;
        offset = head - center;
        trace.body.push_back(seen);
        trace.camera.push_back(seen + offset);
        trace.head.push_back(head);
        trace.maxGap = std::max(trace.maxGap, fabsf(offset));
    }
    trace.endGap = fabsf(offset);
    trace.blocked = follow.blocked;
    return trace;
}

struct Outcome { float maxError, endError, maxGap, endGap, bodyEnd; bool blocked; };

// The camera should be where the head really is, plus whatever the stick
// alone would have moved the body. Anything else is drift the follow added.
Outcome Measure(const Scenario& scenario) {
    const Trace run = Run(scenario, true), stickOnly = Run(scenario, false);
    Outcome outcome{ 0.0f, 0.0f, run.maxGap, run.endGap, run.body.back(), run.blocked };
    for (size_t i = 0; i < run.camera.size(); ++i) {
        outcome.endError = fabsf(run.camera[i] - (run.head[i] + stickOnly.body[i]));
        outcome.maxError = std::max(outcome.maxError, outcome.endError);
    }
    return outcome;
}

std::function<float(double)> Step(float metres, double duration) {
    return [=](double t) { return metres * static_cast<float>(std::clamp((t - 0.5) / duration, 0.0, 1.0)); };
}

unsigned TestFollow() {
    unsigned scenarios = 0;
    // Free movement: the body arrives, and the camera never leaves the head
    // by more than a couple of centimetres on the way.
    std::vector<Scenario> free;
    free.push_back({ Step(0.30f, 0.4) });
    free.push_back({ Step(1.0f, 1.0) });
    free.push_back({ Step(2.5f, 2.5 / 1.4) });
    free.push_back({ Step(1.0f, 2.0) });
    free.push_back({ Step(1.0f, 4.0) });
    free.push_back({ Step(1.0f, 0.5) });
    for (double fps : { 72.0, 120.0, 144.0 }) { Scenario s{ Step(1.0f, 1.0) }; s.fps = fps; free.push_back(s); }
    { Scenario s{ Step(1.0f, 1.0) }; s.crouch = [](double) { return true; }; free.push_back(s); }
    { Scenario s{ Step(1.5f, 1.5) }; s.crouch = [](double t) { return t < 1.0; }; free.push_back(s); }
    { Scenario s{ Step(1.5f, 1.5) }; s.crouch = [](double t) { return t >= 1.0; }; free.push_back(s); }
    { Scenario s{ Step(2.0f, 2.5) }; s.crouch = [](double t) { return static_cast<int>(t / 0.6) % 2 == 0; }; free.push_back(s); }
    { Scenario s{ [](double t) { return 0.4f * static_cast<float>(std::clamp((t - 0.5) / 0.5, 0.0, 1.0)
        - std::clamp((t - 2.0) / 0.5, 0.0, 1.0)); } }; free.push_back(s); }
    { Scenario s{ [](double t) { return 0.6f * sinf(static_cast<float>(t) * 1.5708f); } }; s.seconds = 12.0; free.push_back(s); }
    // The stick moves the body and the camera together and the follow waits
    // for it, in either direction, then closes the gap afterwards.
    for (float direction : { 1.0f, -1.0f })
        for (double fps : { 72.0, 90.0 }) {
            Scenario s{ Step(0.5f, 0.6) };
            s.stick = [=](double t) { return t > 0.3 && t < 2.0 ? 175.0f * direction : 0.0f; };
            s.fps = fps;
            free.push_back(s);
        }
    for (const Scenario& scenario : free) {
        const Outcome outcome = Measure(scenario);
        assert(outcome.maxError < 0.5f);
        assert(outcome.endError < 0.25f);
        assert(outcome.endGap < Roomscale::StartDistance);
        assert(!outcome.blocked);
        ++scenarios;
    }

    // Sway and a lean stay under the threshold: the body does not move.
    for (const auto& head : { Step(0.12f, 0.3), std::function<float(double)>(
            [](double t) { return 0.05f * sinf(static_cast<float>(t) * 3.1416f); }) }) {
        const Outcome outcome = Measure({ head });
        assert(outcome.bodyEnd == 0.0f && outcome.maxError < 0.5f);
        ++scenarios;
    }

    // A wall stops the body; the follow gives up instead of pushing forever,
    // and the camera is unaffected.
    {
        Scenario s{ Step(1.0f, 1.0) };
        s.wall = 15.0f;
        const Outcome outcome = Measure(s);
        assert(outcome.blocked && fabsf(outcome.bodyEnd - 15.0f) < 0.01f && outcome.maxError < 1.0f);
        ++scenarios;
    }

    // Movement is handed over only as far as the follow's own momentum
    // explains it, in whatever direction that momentum points. Movement
    // before a command, a push across the momentum, a teleport, the stick,
    // and anything once the momentum has died away are not.
    Roomscale::Follow follow;
    Vector wish;
    assert(!follow.Command(100.0, { 20, 0, 0 }, { 0, 0, 0 }, false, false, wish));
    assert(follow.Credit(100.0, true, { 0, 0, 0 }, { 20, 0, 0 }).LengthSqr() == 0.0f);
    assert(follow.Credit(100.015, true, { 1, 0, 0 }, { 20, 0, 0 }).LengthSqr() == 0.0f);
    Vector body(1, 0, 0);
    double now = 100.015;
    const auto agree = [&](const Vector& offset) {
        assert(follow.Command(now, offset, body, false, false, wish) && follow.following);
        const Vector moved = follow.momentum.velocity * Roomscale::TickSeconds;
        body += moved;
        now += Roomscale::TickSeconds;
        const Vector covered = follow.Credit(now, true, body, offset);
        assert((covered - moved).LengthSqr() < 1e-8f);
        ++scenarios;
    };
    for (int tick = 0; tick < 12; ++tick)
        agree({ 20, 0, 0 });
    const Vector straight = follow.momentum.velocity;
    assert(straight.x > 40.0f && fabsf(straight.y) < 1e-6f);
    // Turning: the momentum still points mostly along the old direction,
    // and that part is the follow's too.
    for (int tick = 0; tick < 6; ++tick)
        agree({ 0, 20, 0 });
    const Vector turning = follow.momentum.velocity;
    assert(turning.x > 20.0f && turning.y > 20.0f);
    const auto credit = [&](const Vector& moved) {
        body += moved;
        now += Roomscale::TickSeconds;
        return follow.Credit(now, true, body, { 0, 20, 0 });
    };
    const Vector expected = turning * Roomscale::TickSeconds;
    // A push across the momentum carries the camera with the body.
    const float expectedLength = sqrtf(expected.LengthSqr());
    Vector covered = credit(expected + Vector(-expected.y, expected.x, 0) * 2.0f);
    assert(sqrtf((covered - expected).LengthSqr()) < 0.2f * expectedLength);
    // So does one against it, or one far beyond what the momentum explains.
    assert(credit(expected * -1.0f).LengthSqr() == 0.0f);
    covered = credit(expected * 20.0f);
    assert(sqrtf(covered.LengthSqr()) <= Roomscale::MaxShareRatio * expectedLength + Roomscale::ExplainedSlack + 0.001f);
    // A teleport is not handed over and ends the momentum.
    assert(credit({ 500, 0, 0 }).LengthSqr() == 0.0f && follow.momentum.velocity.LengthSqr() == 0.0f);
    for (int tick = 0; tick < 8; ++tick)
        agree({ 20, 0, 0 });
    // Once the follow stops commanding, friction ends the momentum, and
    // later movement is not handed over, even along the same line.
    for (int tick = 0; tick < 40; ++tick)
        assert(!follow.Command(now + tick * Roomscale::TickSeconds, { 1, 0, 0 }, body, false, false, wish));
    now += 40 * Roomscale::TickSeconds;
    follow.Credit(now, true, body, { 1, 0, 0 });
    assert(follow.momentum.velocity.LengthSqr() == 0.0f);
    assert(credit({ 2, 0, 0 }).LengthSqr() == 0.0f);
    // The stick's movement is never the follow's.
    for (int tick = 0; tick < 8; ++tick)
        agree({ 20, 0, 0 });
    assert(!follow.Command(now, { 20, 0, 0 }, body, true, false, wish));
    assert(follow.momentum.velocity.LengthSqr() == 0.0f && credit({ 1, 0, 0 }).LengthSqr() == 0.0f);
    assert(follow.Command(now + 0.5, { 19, 0, 0 }, body, false, false, wish));
    follow.verticalSpeed = 400.0f;
    assert(!follow.Command(now + 0.51, { 19, 0, 0 }, body, false, false, wish));
    follow.Credit(now + 0.52, false, body, { 19, 0, 0 });
    assert(!follow.originValid && !follow.following && !follow.blocked);
    assert(follow.momentum.velocity.LengthSqr() == 0.0f);
    // A head that is through a portal brings the body after it at once,
    // without the dead zone that applies otherwise.
    Roomscale::Follow crossing;
    crossing.Credit(200.0, true, { 0, 0, 0 }, { 3, 0, 0 });
    assert(!crossing.Command(200.01, { 3, 0, 0 }, { 0, 0, 0 }, false, false, wish));
    crossing.urgent = true;
    assert(crossing.Command(200.03, { 3, 0, 0 }, { 0, 0, 0 }, false, false, wish));
    assert(wish.x >= Roomscale::MinSpeed && wish.y == 0.0f && crossing.following);
    assert(!crossing.Command(200.04, { 0.5f, 0, 0 }, { 0, 0, 0 }, false, false, wish));
    // The stick and a fall still take precedence.
    assert(!crossing.Command(200.05, { 3, 0, 0 }, { 0, 0, 0 }, true, false, wish));
    crossing.stickUntil = -1.0e9;
    crossing.verticalSpeed = 400.0f;
    assert(!crossing.Command(200.06, { 3, 0, 0 }, { 0, 0, 0 }, false, false, wish));
    return scenarios;
}

// The same follow on the floor: Source ground movement in two dimensions,
// optionally on a platform that carries the body. 'interpolated' false shows
// the client the body's last tick as is, a step per tick.
struct Floor {
    std::function<Vector(double)> head;                                       // metres
    std::function<Vector(double)> platform = [](double) { return Vector(0, 0, 0); };  // units/s
    bool interpolated = true;
    float accelerate = kAccelerate;
    double fps = 90.0, seconds = 7.0;
};

struct FloorOutcome { float maxError, endError, maxGap; bool blocked; };

FloorOutcome RunFloor(const Floor& floor) {
    Roomscale::Follow follow;
    Vector position(0, 0, 0), velocity(0, 0, 0), carried(0, 0, 0);
    std::vector<std::pair<double, Vector>> history{ { 0.0, Vector(0, 0, 0) } };
    const auto client = [&](double time) {
        const double at = time - kTick;
        for (size_t i = 0; i + 1 < history.size(); ++i)
            if (history[i].first <= at && at <= history[i + 1].first) {
                if (!floor.interpolated) return history[i].second;
                const float u = static_cast<float>((at - history[i].first) / (history[i + 1].first - history[i].first));
                return history[i].second + (history[i + 1].second - history[i].second) * u;
            }
        return at > history.back().first ? history.back().second : history.front().second;
    };
    Vector center(0, 0, 0);
    double nextTick = 0.0;
    FloorOutcome outcome{ 0.0f, 0.0f, 0.0f, false };
    for (double t = 0.0; t < floor.seconds; t += 1.0 / floor.fps) {
        const Vector head = floor.head(t) * kScale;
        while (nextTick <= t) {
            Vector wish(0, 0, 0);
            if (!follow.Command(t + 100.0, head - center, client(t), false, false, wish))
                wish = Vector(0, 0, 0);
            const float speed = Roomscale::Length2D(velocity);
            if (speed > 0.1f)
                velocity *= std::max(0.0f, speed - std::max(speed, kStopSpeed) * kFriction * kTick) / speed;
            const float wishSpeed = std::min(Roomscale::Length2D(wish), kMaxSpeed);
            if (wishSpeed > 0.0f) {
                const Vector unit = wish * (1.0f / Roomscale::Length2D(wish));
                const float add = wishSpeed - (velocity.x * unit.x + velocity.y * unit.y);
                if (add > 0.0f) velocity += unit * std::min(floor.accelerate * kTick * wishSpeed, add);
            }
            const Vector ride = floor.platform(nextTick) * kTick;
            position += velocity * kTick + ride;
            carried += ride;
            nextTick += kTick;
            history.emplace_back(nextTick, position);
            if (history.size() > 8) history.erase(history.begin());
        }
        const Vector seen = client(t);
        center += follow.Credit(t + 100.0, true, seen, head - center);
        // The camera belongs at the head, carried along with the platform.
        const float error = Roomscale::Length2D(seen + (head - center) - (head + carried));
        outcome.maxError = std::max(outcome.maxError, error);
        outcome.endError = error;
        outcome.maxGap = std::max(outcome.maxGap, Roomscale::Length2D(head - center));
    }
    outcome.blocked = follow.blocked;
    return outcome;
}

unsigned TestFloor() {
    unsigned scenarios = 0;
    const auto after = [](double t) { return std::max(t - 0.5, 0.0); };
    std::vector<std::function<Vector(double)>> walks{
        // A one-metre circle at 1.2 m/s, a half-metre one at 1.4 m/s.
        [=](double t) { const double a = after(t) * 1.2; return Vector(sinf(a), 1.0f - cosf(a), 0); },
        [=](double t) { const double a = after(t) * 2.8; return Vector(0.5f * sinf(a), 0.5f * (1.0f - cosf(a)), 0); },
        // Straight ahead, then a right angle.
        [=](double t) { const float d = static_cast<float>(after(t) * 1.3);
            return d < 1.5f ? Vector(d, 0, 0) : Vector(1.5f, d - 1.5f, 0); },
        // Weaving.
        [=](double t) { const float d = static_cast<float>(after(t)); return Vector(d, 0.3f * sinf(d * 3.0f), 0); },
    };
    for (const auto& walk : walks)
        for (double fps : { 72.0, 90.0, 120.0 }) {
            Floor floor{ walk };
            floor.fps = fps;
            const FloorOutcome outcome = RunFloor(floor);
            assert(outcome.maxError < 0.5f && outcome.endError < 0.25f && !outcome.blocked);
            ++scenarios;
            // A client that shows the body tick by tick, and an engine
            // that accelerates differently, cost some precision only.
            floor.interpolated = false;
            assert(RunFloor(floor).maxError < 4.0f);
            floor.interpolated = true;
            floor.accelerate = 6.0f;
            const FloorOutcome slower = RunFloor(floor);
            assert(slower.maxError < 2.5f && slower.endError < 2.5f);
            scenarios += 2;
        }
    // A platform carries the body, and the camera with it, while the user
    // walks across it. (One that starts while the body is already chasing
    // the head looks like slow ground and is not told apart.)
    for (const Vector& speed : { Vector(0, 60, 0), Vector(60, 0, 0), Vector(-40, 30, 0) }) {
        Floor floor{ [=](double t) { return Vector(static_cast<float>(after(t) * 0.8), 0, 0); } };
        floor.platform = [=](double t) { return t > 0.1 && t < 4.0 ? speed : Vector(0, 0, 0); };
        const FloorOutcome outcome = RunFloor(floor);
        assert(outcome.maxError < 3.0f && outcome.endError < 1.0f && outcome.maxGap < 20.0f);
        ++scenarios;
    }
    return scenarios;
}

unsigned TestHelpers() {
    unsigned checks = 0;
    assert(!Roomscale::ShouldFollow(Roomscale::StartDistance - 0.1f, false));
    assert(Roomscale::ShouldFollow(Roomscale::StartDistance, false));
    assert(Roomscale::ShouldFollow(Roomscale::StopDistance, true));
    assert(!Roomscale::ShouldFollow(Roomscale::StopDistance - 0.1f, true));
    assert(!Roomscale::ShouldFollow(NAN, true));
    float previous = 0.0f;
    for (float distance = 0.0f; distance < 80.0f; distance += 0.25f) {
        const float speed = Roomscale::Speed(distance);
        assert(speed >= Roomscale::MinSpeed && speed <= Roomscale::MaxSpeed && speed >= previous);
        // Friction (at least 400 units/s^2) can always stop it within the gap.
        assert(speed == Roomscale::MinSpeed || speed * speed / 800.0f <= distance);
        previous = speed;
        ++checks;
    }
    // Walking into a wall portal aims into the part of the opening the hull
    // fits through. A head already there, and floor portals, are left alone.
    {
        const float limit = 32.0f - Roomscale::BodyHalfWidth - Roomscale::OpeningEdgeMargin;
        const auto across = [](const Vector& v, const Vector& axis) { return v.x * axis.x + v.y * axis.y; };
        for (float heading : { 0.0f, 0.9f, 2.5f, -1.7f }) {
            const Vector forward(cosf(heading), sinf(heading), 0), left(-sinf(heading), cosf(heading), 0);
            const Vector body = forward * 10.0f + left * 25.0f;
            for (float headAcross : { -38.0f, -14.5f, -10.0f, 0.0f, 13.0f, 20.0f, 38.0f }) {
                const Vector head = forward * -12.0f + left * headAcross;
                const Vector aimed = Roomscale::IntoOpening(head - body, body, forward, left, 32.0f);
                const Vector target = body + aimed;
                assert(fabsf(across(target, forward) + 12.0f) < 0.001f);
                assert(fabsf(across(target, left) - std::clamp(headAcross, -limit, limit)) < 0.001f);
                ++checks;
            }
        }
        const Vector offset(-20, 5, 3);
        const Vector up(0, 0, 1), sideways(0, 1, 0);
        assert((Roomscale::IntoOpening(offset, { 0, 30, 0 }, up, sideways, 32.0f) - offset).LengthSqr() == 0.0f);
        assert((Roomscale::IntoOpening(offset, { 0, 30, 0 }, { 1, 0, 0 }, { 0, 0, 1 }, 32.0f) - offset).LengthSqr() == 0.0f);
        assert((Roomscale::IntoOpening(offset, { NAN, 30, 0 }, { 1, 0, 0 }, sideways, 32.0f) - offset).LengthSqr() == 0.0f);
        assert(Roomscale::IntoOpening(offset, { 0, 30, 0 }, { 1, 0, 0 }, sideways, 32.0f).z == 3.0f);
        checks += 4;
    }
    assert(fabsf(Roomscale::Progress({ 3, 4, 9 }, { 0.6f, 0.8f, 0 }, 10.0f) - 5.0f) < 0.0001f);
    assert(Roomscale::Progress({ 3, 4, 0 }, { 0.6f, 0.8f, 0 }, 2.0f) == 2.0f);
    assert(Roomscale::Progress({ -3, -4, 0 }, { 0.6f, 0.8f, 0 }, 10.0f) == 0.0f);
    assert(Roomscale::Progress({ 3, 4, 0 }, { 0.6f, 0.8f, 0 }, -1.0f) == 0.0f);
    assert(Roomscale::Progress({ NAN, 0, 0 }, { 1, 0, 0 }, 10.0f) == 0.0f);

    // The engine flattens and normalizes its move axes; with head pitch and
    // roll they are skewed. The solved moves must rebuild the wish exactly.
    for (float pitch : { -70.0f, -30.0f, 0.0f, 25.0f, 60.0f, 80.0f })
    for (float yaw : { -170.0f, -45.0f, 0.0f, 30.0f, 95.0f, 180.0f })
    for (float roll : { -40.0f, 0.0f, 15.0f, 40.0f })
    for (float heading : { 0.0f, 1.1f, 2.7f, 4.4f }) {
        Vector forward, right, up;
        QAngle::AngleVectors(QAngle(pitch, yaw, roll), &forward, &right, &up);
        const Vector wish(cosf(heading) * 90.0f, sinf(heading) * 90.0f, 0.0f);
        float forwardMove = 0.0f, sideMove = 0.0f;
        assert(Roomscale::WishToMoves(wish, forward, right, forwardMove, sideMove));
        forward.z = right.z = 0.0f;
        VectorNormalize(forward);
        VectorNormalize(right);
        const Vector rebuilt = forward * forwardMove + right * sideMove;
        assert((rebuilt - wish).LengthSqr() < 0.001f);
        if (pitch == 0.0f && roll == 0.0f)
            assert(fabsf(forwardMove * forwardMove + sideMove * sideMove - 8100.0f) < 0.5f);
        ++checks;
    }
    Vector forward, right, up;
    QAngle::AngleVectors(QAngle(89.9f, 10.0f, 0.0f), &forward, &right, &up);
    float forwardMove = 0.0f, sideMove = 0.0f;
    assert(!Roomscale::WishToMoves({ 50, 0, 0 }, forward, right, forwardMove, sideMove));
    return checks;
}

unsigned TestAimMarker() {
    unsigned checks = 0;
    // Axis convention shared with VR::GetPoseData.
    const Vector ahead = AimMarker::SourceFromTracking({ 0, 0, -1 });
    const Vector toRight = AimMarker::SourceFromTracking({ 1, 0, 0 });
    const Vector above = AimMarker::SourceFromTracking({ 0, 1, 0 });
    assert(ahead.x == 1 && ahead.y == 0 && ahead.z == 0);
    assert(toRight.x == 0 && toRight.y == -1 && toRight.z == 0);
    assert(above.x == 0 && above.y == 0 && above.z == 1);

    // The camera maps a tracked point into the world as: eye, plus the
    // offset from the tracked eye turned by the stick-turn yaw and scaled.
    for (float yaw : { 0.0f, 37.0f, 90.0f, 215.0f, -120.0f })
    for (float scale : { 43.2f, 30.0f })
    for (int sample = 0; sample < 24; ++sample) {
        const float a = sample * 1.37f;
        const Vector eyeTracking(0.3f * cosf(a), 1.2f + 0.4f * sinf(a * 0.7f), -0.5f * sinf(a));
        const Vector eyeWorld(512.0f + 30.0f * cosf(a * 1.9f), -260.0f + 40.0f * sinf(a), 64.0f + 8.0f * cosf(a * 0.3f));
        const Vector pointTracking(eyeTracking.x + 3.0f * cosf(a * 2.3f), eyeTracking.y - 0.9f * sinf(a),
            eyeTracking.z - 4.0f * sinf(a * 1.3f));
        Vector world = AimMarker::SourceFromTracking(pointTracking - eyeTracking) * scale;
        VectorPivotXY(world, { 0, 0, 0 }, yaw);
        world += eyeWorld;
        Vector recovered;
        assert(AimMarker::WorldToTracking(world, eyeWorld, eyeTracking, yaw, scale, recovered));
        assert((recovered - pointTracking).LengthSqr() < 1e-6f);
        ++checks;
    }
    Vector recovered;
    // One metre straight ahead of an unturned head is one scale unit along +x.
    assert(AimMarker::WorldToTracking({ 143.2f, 0, 64 }, { 100, 0, 64 }, { 0, 1.5f, 0 }, 0.0f, 43.2f, recovered));
    assert(fabsf(recovered.x) < 1e-4f && fabsf(recovered.y - 1.5f) < 1e-4f && fabsf(recovered.z + 1.0f) < 1e-4f);
    assert(!AimMarker::WorldToTracking({ 1, 2, 3 }, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f, 0.0f, recovered));
    assert(!AimMarker::WorldToTracking({ NAN, 2, 3 }, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f, 43.2f, recovered));

    // Opaque white centre, dark opaque rim, transparent corners, symmetric.
    static std::uint8_t pixels[AimMarker::TextureSize * AimMarker::TextureSize * 4];
    AimMarker::Paint(pixels);
    const int size = AimMarker::TextureSize;
    const auto at = [&](int x, int y) { return pixels + (y * size + x) * 4; };
    assert(at(size / 2, size / 2)[0] == 255 && at(size / 2, size / 2)[3] == 255);
    assert(at(0, 0)[3] == 0 && at(size - 1, 0)[3] == 0 && at(0, size - 1)[3] == 0 && at(size - 1, size - 1)[3] == 0);
    const auto rim = at(size / 2 + static_cast<int>(size * 0.27f), size / 2);
    assert(rim[0] < 40 && rim[3] > 200);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            assert(at(x, y)[0] == at(size - 1 - x, y)[0] && at(x, y)[3] == at(x, size - 1 - y)[3]);
            assert(at(x, y)[0] == at(x, y)[1] && at(x, y)[1] == at(x, y)[2]);
            ++checks;
        }
    return checks;
}

std::string ReadLog(const char *path) {
    std::string text;
    // The log is still open for writing, so the reader must share the file.
    FILE *file = _fsopen(path, "r", _SH_DENYNO);
    assert(file);
    char buffer[512];
    for (size_t count; (count = fread(buffer, 1, sizeof(buffer), file)) > 0;) text.append(buffer, count);
    fclose(file);
    return text;
}

// The log keeps its handle open. Lines must still reach the file at once
// (crash diagnostics) and stay readable while the game is running.
unsigned TestLog() {
    char directory[MAX_PATH] = {};
    const bool located = GetTempPathA(MAX_PATH, directory) && SetCurrentDirectoryA(directory);
    assert(located);
    PortalVrResetLog();
    PortalVrLog("first %d", 1);
    PortalVrLog("second %s", "line");
    const std::string text = ReadLog("portalvr.log");
    const size_t first = text.find("] first 1\n"), second = text.find("] second line\n");
    assert(text[0] == '[' && first != std::string::npos && second != std::string::npos && first < second);
    for (int i = 0; i < 500; ++i) PortalVrLog("burst %d", i);
    assert(ReadLog("portalvr.log").find("] burst 499\n") != std::string::npos);
    PortalVrResetLog();
    assert(ReadLog("portalvr.log").empty());
    PortalVrLog("after reset");
    assert(ReadLog("portalvr.log").find("] after reset\n") != std::string::npos);
    assert(!PortalVrDebugLogging());
    return 4;
}
}

int main() {
    const unsigned follow = TestFollow() + TestFloor();
    const unsigned helpers = TestHelpers();
    const unsigned marker = TestAimMarker();
    const unsigned log = TestLog();
    printf("{\"roomscale_follow_scenarios\":%u,\"roomscale_helper_checks\":%u,\"aim_marker_checks\":%u,\"log_checks\":%u,\"passed\":true}\n",
        follow, helpers, marker, log);
    return 0;
}
