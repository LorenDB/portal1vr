#pragma once

#include <Windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <share.h>

// The log keeps one handle for the whole session. Opening and closing the
// file for every line cost a directory lookup (and often an antivirus scan)
// on the render thread, which showed up as a hitch on every portal shot.
struct PortalVrLogState
{
    // Recursive: the vectored exception logger can re-enter on the thread
    // that faulted while it was writing a line.
    std::recursive_mutex mutex;
    FILE *file = nullptr;
    bool failed = false;
};

inline PortalVrLogState &PortalVrLogInstance()
{
    static PortalVrLogState state;
    return state;
}

inline FILE *PortalVrOpenLog(const char *mode)
{
    char currentDir[MAX_PATH] = {};
    if (!GetCurrentDirectoryA(MAX_PATH, currentDir))
        return nullptr;

    char path[MAX_PATH * 2] = {};
    sprintf_s(path, "%s\\portalvr.log", currentDir);

    // Shared access lets the log be read while the game is running.
    return _fsopen(path, mode, _SH_DENYNO);
}

// Periodic state snapshots and per-shot audits are opt-in: launch with
// -portalvr-debug. Startup, state changes, and errors are always logged.
inline bool PortalVrDebugLogging()
{
    static const bool enabled = strstr(GetCommandLineA(), "-portalvr-debug") != nullptr;
    return enabled;
}

inline void PortalVrResetLog()
{
    PortalVrLogState &state = PortalVrLogInstance();
    std::lock_guard<std::recursive_mutex> lock(state.mutex);
    if (state.file)
        fclose(state.file);
    state.file = PortalVrOpenLog("w");
    state.failed = state.file == nullptr;
}

inline void PortalVrLog(const char *format, ...)
{
    PortalVrLogState &state = PortalVrLogInstance();
    std::lock_guard<std::recursive_mutex> lock(state.mutex);
    if (!state.file && !state.failed)
    {
        state.file = PortalVrOpenLog("a");
        state.failed = state.file == nullptr;
    }
    FILE *file = state.file;
    if (!file)
        return;

    SYSTEMTIME localTime = {};
    GetLocalTime(&localTime);

    fprintf(
        file,
        "[%02u:%02u:%02u.%03u][tid=%lu] ",
        localTime.wHour,
        localTime.wMinute,
        localTime.wSecond,
        localTime.wMilliseconds,
        static_cast<unsigned long>(GetCurrentThreadId()));

    va_list args;
    va_start(args, format);
    vfprintf(file, format, args);
    va_end(args);

    fputc('\n', file);
    // Flush every line so the last entries survive a crash.
    fflush(file);
}
