# Target profile

Mode: agentic (Claude Code on the play PC). Entries are **confirmed** (seen in the game, or read from the installed binaries/content) or **assumed**.

## Player setup
- Headset: Pico through SteamVR; controllers use SteamVR's Pico→Touch compatibility mapping. **confirmed** (README playtests)
- PC: Linux; the Windows x86 Steam builds of Portal run under Proton. The mod is built with llvm-mingw, and tests run under wine. **confirmed**
- Game launches: done by the user, never by the agent. **confirmed** (user instruction, 2026-10-06)
- GPU: **unknown** (only matters for any future RTX work; Q-003)

## Portal (Steam app 400): the supported base
- Engine: Source, Windows x86. `engine.dll` PE 0x675781e6 (2024-12-09); `client.dll` 0x68362d89 (2025-05-27); `server.dll` 0x67578384 (2024-12-09). **confirmed**
- Entry: `bin/d3d9.dll` proxy (DXVK fork), OpenVR. Hooks: byte signatures (offsets.h), RTTI vtables, and RVAs pinned to the builds above and guarded by byte checks (portal1.h, nativepose.h, portalcamera.h, portaltrace.h). **confirmed**
- View entity: `IVRenderView::GetViewEntity` is vtable slot 27, the getter `mov eax,[0x1048d0c8]; ret`. **confirmed** offline (tests/engine-view-entity.cpp); headset behaviour **assumed**.
- Map cameras (`point_viewcontrol`): `testchmb_a_00`, `escape_02`, and `background1`/`background2` (the menu scenes, handled by the menu anchor). **confirmed** (entity lumps)

## Portal with RTX (Steam app 2012840)
- `hl2.exe -game portal_rtx`. `bin/d3d9.dll` is the 32-bit RTX Remix bridge (2025-05-16); `bin/.trex/` holds the 64-bit `NvRemixBridge.exe` and the dxvk-remix runtime `d3d9.dll`. **confirmed**
- Game DLLs: NVIDIA builds. `client.dll` 0x6829867d and `server.dll` 0x68298684 (2025-05-18); `engine.dll` 0x67178ffa (2024-10-22). Of the 9 live signatures, 6 match and 3 do not (PlayerPortalled, TraceFirePortalServer, ComputeError); every RVA-pinned hook fails its build guard. **confirmed** (offline scan)
- Rendering: Remix path-traces once per frame, with the first camera it sees, into the backbuffer. Render targets of another size are rasterized. There is no stereo or VR support, and the runtime builds only with MSVC on Windows. **confirmed** (dxvk-remix source; see D-002)

## Portal: Prelude RTX (Steam app 2410180)
- Listed in the library but not downloaded (0 bytes; StateFlags 1026). Its single depot is 26 GB, built 2023-07-19 on Portal with RTX's foundation with an older Remix runtime. **confirmed** (appmanifest, Steam app info)
- Gameplay: starts with no gun, then an orange-only gun, then both. Adds gravity fields, suppressor fields, NPC scientists, and cutscenes; the ending drags the player out. **confirmed** (Portal Wiki); untested here.
- Whether its maps and content run on Portal's engine without RTX: **unknown** (Q-001)
