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
    bool ground = true;
    double fps = 90.0, seconds = 6.0;
};

struct Trace {
    std::vector<float> body, camera, head;
    float endGap = 0.0f, maxGap = 0.0f;
    bool blocked = false;
};

// One run of the follow against the mover. The client sees the body one tick
// late and interpolated, as it does in single player; the renderer uses the
// head offset computed at the previous update, as VR::UpdateTracking does.
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
                    [&](const Vector&) { return scenario.ground; }, wish)) {
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
        trace.body.push_back(seen);
        trace.camera.push_back(seen + offset);
        trace.head.push_back(head);
        const Vector covered = follow.Credit(t + 100.0, true, axis * seen, axis * (head - center));
        assert(covered.z == 0.0f);
        center += covered.x * axis.x + covered.y * axis.y;
        offset = head - center;
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
        assert(outcome.maxError < 2.0f);
        assert(outcome.endError < 0.5f);
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
    // No ground under the head: the body stays on the ledge.
    {
        Scenario s{ Step(1.0f, 1.0) };
        s.ground = false;
        const Outcome outcome = Measure(s);
        assert(outcome.bodyEnd == 0.0f && outcome.maxError < 0.5f && !outcome.blocked);
        ++scenarios;
    }

    // Not usable (menu, lost tracking) forgets everything, and a teleport or
    // movement nobody commanded is never handed over.
    Roomscale::Follow follow;
    const auto ground = [](const Vector&) { return true; };
    Vector wish;
    assert(!follow.Command(100.0, { 20, 0, 0 }, { 0, 0, 0 }, false, false, ground, wish));
    assert(follow.Credit(100.0, true, { 0, 0, 0 }, { 20, 0, 0 }).LengthSqr() == 0.0f);
    assert(follow.Command(100.01, { 20, 0, 0 }, { 0, 0, 0 }, false, false, ground, wish));
    assert(wish.x > 0.0f && wish.y == 0.0f && follow.following);
    assert(follow.Credit(100.02, true, { 500, 0, 0 }, { 20, 0, 0 }).LengthSqr() == 0.0f);
    const Vector covered = follow.Credit(100.03, true, { 501, 0, 0 }, { 20, 0, 0 });
    assert(fabsf(covered.x - 1.0f) < 0.001f && covered.y == 0.0f);
    assert(follow.Credit(100.04, true, { 500, 0, 0 }, { 19, 0, 0 }).LengthSqr() == 0.0f);
    // Later movement the follow did not command, even along the same line.
    assert(follow.Credit(100.2, true, { 505, 0, 0 }, { 19, 0, 0 }).LengthSqr() == 0.0f);
    assert(follow.Command(100.51, { 19, 0, 0 }, { 505, 0, 0 }, true, false, ground, wish) == false);
    assert(follow.Command(100.8, { 19, 0, 0 }, { 505, 0, 0 }, false, false, ground, wish) == false);
    assert(follow.Command(100.92, { 19, 0, 0 }, { 505, 0, 0 }, false, false, ground, wish));
    follow.verticalSpeed = 400.0f;
    assert(!follow.Command(100.93, { 19, 0, 0 }, { 505, 0, 0 }, false, false, ground, wish));
    follow.Credit(100.94, false, { 505, 0, 0 }, { 19, 0, 0 });
    assert(!follow.originValid && !follow.following && !follow.blocked);
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
    const unsigned follow = TestFollow();
    const unsigned helpers = TestHelpers();
    const unsigned marker = TestAimMarker();
    const unsigned log = TestLog();
    printf("{\"roomscale_follow_scenarios\":%u,\"roomscale_helper_checks\":%u,\"aim_marker_checks\":%u,\"log_checks\":%u,\"passed\":true}\n",
        follow, helpers, marker, log);
    return 0;
}
