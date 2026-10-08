#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>

// Where a VR frame's time goes. The frame runs from WaitGetPoses returning to
// it returning again, on the game thread; each phase ends with a lap. Laps
// arrive in phase order, and a phase a frame skips (no world drawn, say)
// simply records nothing. Times are seconds.
namespace FrameTiming {
enum Phase : unsigned {
    Engine,         // input, simulation and prediction, until RenderView
    LeftEye,
    RightEye,
    Desktop,        // the desktop pass, when it is drawn
    AfterRender,    // the rest of the engine frame up to Present
    Present,        // the desktop swap chain's Present
    CommandThread,  // waiting for DXVK's command thread to catch up
    GpuIdle,        // waiting for the GPU to finish the frame
    Submit,         // handing the eyes and overlays to SteamVR
    Poses,          // WaitGetPoses: until SteamVR starts the next frame
    Update,         // tracking and input
    PhaseCount
};

inline const char* Name(unsigned phase) {
    static const char* const names[PhaseCount] = { "engine", "left", "right", "desktop", "after",
        "present", "cmdthread", "gpu", "submit", "poses", "update" };
    return phase < PhaseCount ? names[phase] : "?";
}

struct Window {
    double sum[PhaseCount] = {}, max[PhaseCount] = {};
    double frameSum = 0.0, frameMax = 0.0, seconds = 0.0;
    unsigned frames = 0, overBudget = 0, desktopSkipped = 0;

    void Add(const double (&phase)[PhaseCount], double frame, double budget) {
        for (unsigned i = 0; i < PhaseCount; ++i) {
            sum[i] += phase[i];
            max[i] = std::max(max[i], phase[i]);
        }
        frameSum += frame;
        frameMax = std::max(frameMax, frame);
        seconds += frame;
        ++frames;
        // A little slack: SteamVR's own pacing jitters by a fraction of a millisecond.
        if (budget > 0.0 && frame > budget * 1.05)
            ++overBudget;
    }

    // One line: frame time, the share over the headset's budget, and each
    // phase's average and worst case, in milliseconds.
    int Format(char* out, size_t size, double budget) const {
        if (!frames || !size)
            return 0;
        const double n = frames;
        int written = snprintf(out, size, "Frame time over %.1fs: frames=%u avg=%.2fms max=%.2fms over%.1fms=%u desktopSkipped=%u |",
            seconds, frames, frameSum / n * 1000.0, frameMax * 1000.0, budget * 1000.0, overBudget, desktopSkipped);
        for (unsigned i = 0; i < PhaseCount && written > 0 && static_cast<size_t>(written) < size; ++i)
            written += snprintf(out + written, size - written, " %s %.2f/%.2f", Name(i), sum[i] / n * 1000.0, max[i] * 1000.0);
        return written;
    }
};

class Recorder {
public:
    // A lap ends the phase: the time since the previous lap belongs to it.
    void Lap(Phase phase, double now) {
        if (!m_Started || phase >= PhaseCount || !std::isfinite(now))
            return;
        const double elapsed = now - m_Last;
        if (elapsed > 0.0)
            m_Phase[phase] += elapsed;
        m_Last = now;
    }

    void SkippedDesktopPresent() { ++m_Window.desktopSkipped; }

    // The end of a frame. Gameplay frames go into the window; anything else
    // (menus, loading) starts it over. Returns true with a finished window
    // once it covers at least 'span' seconds.
    bool EndFrame(double now, bool gameplay, double budget, double span, Window& finished) {
        bool done = false;
        if (m_Started && std::isfinite(now)) {
            Lap(Update, now);
            const double frame = now - m_FrameStart;
            // Longer than this is a load or a pause, not a frame.
            if (gameplay && frame > 0.0 && frame < 1.0) {
                m_Window.Add(m_Phase, frame, budget);
                if (m_Window.seconds >= span) {
                    finished = m_Window;
                    m_Window = Window();
                    done = true;
                }
            }
            else
                m_Window = Window();
        }
        m_Started = std::isfinite(now);
        m_FrameStart = m_Last = now;
        for (double& value : m_Phase)
            value = 0.0;
        return done;
    }

private:
    bool m_Started = false;
    double m_FrameStart = 0.0, m_Last = 0.0;
    double m_Phase[PhaseCount] = {};
    Window m_Window;
};
}
