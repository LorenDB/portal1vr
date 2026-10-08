#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "frametiming.h"
#include "gputiming.h"

using namespace FrameTiming;

namespace {
bool Near(double a, double b) { return std::fabs(a - b) < 1e-9; }

// One frame with fixed phase lengths, in milliseconds, starting at 'now'.
double Frame(Recorder& recorder, double now, const double (&ms)[PhaseCount - 1]) {
    for (unsigned phase = 0; phase + 1 < PhaseCount; ++phase) {
        now += ms[phase] / 1000.0;
        recorder.Lap(static_cast<Phase>(phase), now);
    }
    return now;
}
}

int main() {
    unsigned checks = 0;
    const double budget = 1.0 / 90.0;
    const double fast[PhaseCount - 1] = { 2, 3, 3, 0, 0.5, 0.2, 0.3, 0.4, 0.1, 1.5 };  // 11.0 ms with update
    const double slow[PhaseCount - 1] = { 2, 6, 6, 0, 0.5, 0.2, 0.3, 0.4, 0.1, 6.4 };  // 22.0 ms with update

    // Laps before the first frame boundary are ignored.
    Recorder recorder;
    recorder.Lap(Engine, 5.0);
    Window window;
    assert(!recorder.EndFrame(10.0, true, budget, 10.0, window));
    ++checks;

    // Ten seconds of alternating 11 ms and 22 ms frames.
    double now = 10.0;
    unsigned frames = 0;
    bool done = false;
    while (!done) {
        const bool isSlow = frames % 2 == 1;
        now = Frame(recorder, now, isSlow ? slow : fast);
        now += 0.0001;  // update
        ++frames;
        if (frames % 3 == 0)
            recorder.SkippedDesktopPresent();
        done = recorder.EndFrame(now, true, budget, 10.0, window);
    }
    assert(window.frames == frames && window.seconds >= 10.0 && window.seconds < 10.1);
    assert(window.overBudget == frames / 2);
    assert(window.desktopSkipped == frames / 3);
    assert(Near(window.max[LeftEye], 0.006) && Near(window.max[Engine], 0.002));
    assert(Near(window.sum[LeftEye] / window.frames, (frames / 2 * 0.006 + (frames - frames / 2) * 0.003) / frames));
    assert(Near(window.frameMax, 0.022) && window.max[Desktop] == 0.0);
    double phases = 0.0;
    for (double value : window.sum) phases += value;
    assert(Near(phases, window.frameSum));
    checks += 7;

    char line[1024];
    assert(window.Format(line, sizeof(line), budget) > 0);
    assert(strstr(line, "Frame time over 10.") && strstr(line, "over11.1ms=") && strstr(line, " left 4.5") && strstr(line, " update "));
    checks += 2;

    // A menu frame starts the window over; so does a frame long enough to
    // be a load, which is not counted either.
    for (int i = 0; i < 100; ++i) {
        now = Frame(recorder, now, fast);
        assert(!recorder.EndFrame(now, i != 50, budget, 10.0, window));
    }
    now = Frame(recorder, now, fast) + 2.0;
    assert(!recorder.EndFrame(now, true, budget, 10.0, window));
    frames = 0;
    done = false;
    while (!done) {
        now = Frame(recorder, now, fast);
        ++frames;
        done = recorder.EndFrame(now, true, budget, 10.0, window);
    }
    assert(window.frames == frames && window.overBudget == 0 && window.desktopSkipped == 0);
    checks += 2;

    // Out-of-range input is ignored.
    recorder.Lap(PhaseCount, now + 1.0);
    recorder.Lap(Engine, NAN);
    assert(!recorder.EndFrame(now + 0.011, true, budget, 10.0, window));
    ++checks;

    // GPU spans: each runs from the previous mark that was read back; the
    // first from the previous frame's last mark, when that frame was read.
    {
        GpuTiming::Window gpu;
        double time[GpuTiming::MarkCount] = { 1.000, 1.004, 1.008, 1.0081, 1.009, 1.0095 };
        bool valid[GpuTiming::MarkCount] = { true, true, true, true, true, true };
        gpu.Add(time, valid, false, 0.0);
        assert(gpu.frames == 1 && gpu.count[0] == 0 && Near(gpu.sum[GpuTiming::LeftEye], 0.004));
        assert(Near(gpu.frameSum, 0.0095) && Near(gpu.max[GpuTiming::AfterPresent], 0.0005));
        double next[GpuTiming::MarkCount] = { 1.030, 1.035, 1.040, 0.0, 1.041, 1.042 };
        bool partly[GpuTiming::MarkCount] = { true, true, true, false, true, true };
        gpu.Add(next, partly, true, 1.0095);
        assert(gpu.frames == 2 && gpu.count[0] == 1 && Near(gpu.sum[0], 0.0205));
        assert(gpu.count[GpuTiming::Desktop] == 1 && Near(gpu.sum[GpuTiming::BeforePresent], 0.0009 + 0.001));
        bool none[GpuTiming::MarkCount] = {};
        gpu.Add(next, none, true, 1.0);
        assert(gpu.frames == 2 && gpu.incomplete == 1);
        char line[512];
        assert(gpu.Format(line, sizeof(line)) > 0 && strstr(line, "GPU time: frames=2 unread=1") && strstr(line, " between 20.50/20.50"));
        checks += 6;
    }

    printf("{\"frame_timing_checks\":%u,\"passed\":true}\n", checks);
    return 0;
}
