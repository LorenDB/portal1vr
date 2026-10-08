#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>

// How long the GPU spends on each part of a frame, from timestamps written
// into the command stream at the same points the CPU laps are taken
// (frametiming.h). Read back a few frames later; times are seconds.
namespace GpuTiming {
enum Mark : unsigned {
    Start,          // RenderView begins
    LeftEye,
    RightEye,
    Desktop,        // the desktop pass or mirror
    BeforePresent,  // the rest of the engine frame
    AfterPresent,   // the desktop swap chain's copy to the window
    MarkCount
};

// Span i ends at mark i. Span 0 runs from the previous frame's last mark to
// this frame's start: time the GPU spent idle, or on someone else's work.
inline const char* SpanName(unsigned span) {
    static const char* const names[MarkCount] = { "between", "left", "right", "desktop", "after", "present" };
    return span < MarkCount ? names[span] : "?";
}

struct Window {
    double sum[MarkCount] = {}, max[MarkCount] = {};
    unsigned count[MarkCount] = {};
    unsigned frames = 0, incomplete = 0;
    double frameSum = 0.0, frameMax = 0.0;

    // 'previousEnd' is the previous frame's last mark, when that frame was
    // read back too; otherwise the gap before this one is not known.
    void Add(const double (&time)[MarkCount], const bool (&valid)[MarkCount], bool havePrevious, double previousEnd) {
        if (!valid[Start]) {
            ++incomplete;
            return;
        }
        ++frames;
        auto record = [&](unsigned span, double seconds) {
            if (!std::isfinite(seconds) || seconds < 0.0 || seconds > 1.0)
                return;
            sum[span] += seconds;
            max[span] = std::max(max[span], seconds);
            ++count[span];
        };
        if (havePrevious)
            record(0, time[Start] - previousEnd);
        double last = time[Start];
        for (unsigned mark = Start + 1; mark < MarkCount; ++mark) {
            if (!valid[mark])
                continue;
            record(mark, time[mark] - last);
            last = time[mark];
        }
        const double frame = last - time[Start];
        if (std::isfinite(frame) && frame >= 0.0 && frame < 1.0) {
            frameSum += frame;
            frameMax = std::max(frameMax, frame);
        }
    }

    int Format(char* out, size_t size) const {
        if (!frames || !size)
            return 0;
        int written = snprintf(out, size, "GPU time: frames=%u unread=%u ours avg=%.2fms max=%.2fms |",
            frames, incomplete, frameSum / frames * 1000.0, frameMax * 1000.0);
        for (unsigned span = 0; span < MarkCount && written > 0 && static_cast<size_t>(written) < size; ++span) {
            if (!count[span])
                continue;
            written += snprintf(out + written, size - written, " %s %.2f/%.2f",
                SpanName(span), sum[span] / count[span] * 1000.0, max[span] * 1000.0);
        }
        return written;
    }
};
}
