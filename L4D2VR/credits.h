#pragma once
#include <cstdint>
#include <cstring>
#include "sigscanner.h"

// End credits. Portal's "Still Alive" roll (CHudPortalCredits) and the HL2
// roll it is built on (CHudCredits) are 2D HUD elements whose Paint does all
// the work: it starts the song through vgui::surface()->PlaySound, scrolls the
// lyrics and ends the roll. The eyes are drawn without the HUD, so with the
// desktop window off nothing painted them: escape_02 faded to black and the
// credits never started. Each element's ShouldDraw, called every HUD think
// whether anything paints or not, publishes whether credits other than the
// intro names are rolling in a client global (HL2's g_bRollingCredits).
namespace Credits {
struct Element {
    uintptr_t shouldDraw;   // client.dll RVA of the element's ShouldDraw
    uint32_t creditsType;   // its m_iCreditsType offset; type 2 is the intro
};
// CHudPortalCredits (vtable client.dll 0x103fc1b0, slot 9), then CHudCredits
// (0x103f2ba4, slot 9). Each writes its own flag.
inline constexpr Element kElements[] = { { 0x236380, 0x610 }, { 0x21b100, 0x1e4 } };
inline constexpr size_t kElementCount = sizeof(kElements) / sizeof(kElements[0]);

// push esi; mov esi,ecx; mov eax,[esi]; call [eax+28h] (IsActive);
// mov [flag],al; test al,al; je; xor ecx,ecx; movzx eax,al;
// cmp dword [esi+type],2; cmove eax,ecx; mov [flag],al
inline const volatile bool* RollingFlag(uintptr_t client, const Element& element) {
    constexpr size_t length = 39;
    if (!client || !SigScanner::IsReadable(client + element.shouldDraw, length))
        return nullptr;
    const auto* code = reinterpret_cast<const unsigned char*>(client + element.shouldDraw);
    static const unsigned char call[] = { 0x56, 0x8b, 0xf1, 0x8b, 0x06, 0x8b, 0x40, 0x28, 0xff, 0xd0, 0xa2 };
    static const unsigned char test[] = { 0x84, 0xc0, 0x74, 0x14, 0x33, 0xc9, 0x0f, 0xb6, 0xc0, 0x83, 0xbe };
    static const unsigned char keep[] = { 0x02, 0x0f, 0x44, 0xc1, 0xa2 };
    uint32_t first = 0, type = 0, second = 0;
    memcpy(&first, code + 11, 4);
    memcpy(&type, code + 26, 4);
    memcpy(&second, code + 35, 4);
    if (memcmp(code, call, sizeof(call)) || memcmp(code + 15, test, sizeof(test))
        || memcmp(code + 30, keep, sizeof(keep)) || type != element.creditsType || first != second)
        return nullptr;
    // The flag lives in the client's own image (code above is relocated).
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(client);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(client + dos->e_lfanew);
    if (first <= client || first >= client + nt->OptionalHeader.SizeOfImage
        || !SigScanner::IsReadable(first, 1))
        return nullptr;
    return reinterpret_cast<const volatile bool*>(static_cast<uintptr_t>(first));
}
}
