#include <Windows.h>
#include <cassert>
#include <cstdio>
#include "credits.h"
// Optional installed-binary check: map client.dll without running its
// initializer and confirm both credits elements still publish their rolling
// flag where the VR runtime reads it.
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    HMODULE module = LoadLibraryExA(argv[1], nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (!module) return 3;
    const auto client = reinterpret_cast<uintptr_t>(module);
    const auto *portal = Credits::RollingFlag(client, Credits::kElements[0]);
    const auto *hl2 = Credits::RollingFlag(client, Credits::kElements[1]);
    const bool matched = portal && hl2 && portal != hl2
        && reinterpret_cast<uintptr_t>(portal) - client == 0x51af40
        && reinterpret_cast<uintptr_t>(hl2) - client == 0x518fb4
        && !*portal && !*hl2;
    // A different element layout, or no client at all, is refused.
    const bool refused = !Credits::RollingFlag(client, { Credits::kElements[0].shouldDraw, 0x1e4 })
        && !Credits::RollingFlag(client, { Credits::kElements[0].shouldDraw + 1, 0x610 })
        && !Credits::RollingFlag(0, Credits::kElements[0]);
    FreeLibrary(module);
    puts(matched && refused ? "Installed Portal 1 credits flags match" : "Installed client credits layout mismatch");
    return matched && refused ? 0 : 4;
}
