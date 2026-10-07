#include <Windows.h>
#include <cstdio>
#include "sdk/sdk.h"
#include "portal1.h"
#include "sigscanner.h"
// Optional installed-binary check: map engine.dll without running its
// initializer and confirm that the render view's GetViewEntity slot has the
// getter shape Game::GetViewEntity requires before it calls it.
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    HMODULE engine = LoadLibraryExA(argv[1], nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (!engine) return 3;
    const uintptr_t table = SigScanner::FindRttiVtable("engine.dll", ".?AVCVRenderView@@");
    const auto slot = [&](size_t index) {
        uintptr_t t = table;
        return reinterpret_cast<const unsigned char *>(table ? SigScanner::GetVirtualFunction(&t, index) : 0);
    };
    const unsigned char *getter = slot(Portal1::VTableIndex::kRenderView_GetViewEntity);
    const unsigned char *oldProjection = slot(25); // OLD_SetProjectionMatrix(float, float, float)
    const bool matched = getter && getter[0] == 0xA1 && getter[5] == 0xC3
        && oldProjection && oldProjection[0] == 0xC2 && oldProjection[1] == 12 && oldProjection[2] == 0;
    FreeLibrary(engine);
    puts(matched ? "Installed engine GetViewEntity slot matches" : "Installed engine render view ABI mismatch");
    return matched ? 0 : 4;
}
