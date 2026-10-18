# Portal 1 VR

VR mod for the Windows x86 Steam version of Portal, based on [Portal2VR](https://github.com/Gistix/portal2vr) and [L4D2VR](https://github.com/sd805/l4d2vr).

Downloads: [LorenDB/portal1vr releases](https://github.com/LorenDB/portal1vr/releases).

The shipped `d3d9.dll` is a modified DXVK (the `msaa` branch of [fholger/dxvk_l4d2vr](https://github.com/fholger/dxvk_l4d2vr), plus the Portal changes in this repo). It is not an upstream DXVK build. The DLL's version resource says Portal1VR.

## Install

On Linux, install with [Kaon](https://github.com/LorenDB/kaon).

On Windows, close Portal, extract the Windows x86 zip, and run `Install.cmd`. For a custom location:

```powershell
.\Install.ps1 -PortalDirectory 'D:\SteamLibrary\steamapps\common\Portal'
```

The installer copies the runtime, `openvr_api.dll`, controller bindings, and the gun and arm material overrides. The hands, portal gun, player body, and radio song stay Portal's. An older release's `portal/custom/bowman_portal1.vpk` is moved to `bin/VR/InstallBackups`.

Start SteamVR, then run `Launch Portal VR.cmd` in the Portal folder, or launch Portal with:

```text
-insecure -fullscreen -novid +mat_queue_mode 0 +mat_vsync 0 +mat_antialias 0
```

Settings live in `bin/VR/config.txt`. The installer keeps values you already set and appends any new keys.

To uninstall, close Portal and run `Uninstall.cmd`. It deletes `bin/d3d9.dll` and the other files this mod installed, so Portal uses the system Direct3D 9 again. `bin/VR/InstallBackups` is left in place. Another `d3d9.dll` proxy at that path is removed too.

## Game version

Hooks are checked against this Steam Windows build:

| DLL | PE timestamp | Date |
| --- | --- | --- |
| `client.dll` | `0x68362d89` | 2025-05-27 |
| `engine.dll` | `0x675781e6` | 2024-12-09 |
| `server.dll` | `0x67578384` | 2024-12-09 |

A newer Portal can still start. A missed signature is logged, shown once in a dialog, and that feature stays off. The portal-environment camera trace also checks the `client.dll` timestamp and disables itself on any other build. Verify the game files after a Steam update.

## Controls

Left stick moves, right stick turns, left-stick click recenters. Gun-hand trigger fires the blue portal, gun-hand grip fires orange. Gun-hand face buttons are jump and use. Off-hand grip ducks, and dropping your head ducks too (`PhysicalCrouch`, default on).

Actions are SteamVR Input bindings in `bin/VR/SteamVRActionManifest`, so they can be rebound per controller. The pause menu entries **VR: use left-handed controls** and **VR: use right-handed controls** swap the gun hand and save the choice.

`ShowArms=false` draws only the portal gun. `true` also draws Portal's stock arms and lets the off hand grab the gun.

`RenderWindow=2` (default) mirrors the left eye on the desktop. `1` renders the desktop view separately. `0` turns it off during play. Menus and credits still use the flat screen in the headset.

## Not supported

Portal with RTX and Portal: Prelude RTX. Both replace `d3d9.dll` with NVIDIA's RTX Remix runtime, which path-traces one camera into the game window. This mod is that `d3d9.dll`.

## Build

`./build.sh` cross-compiles the Release x86 DLL with llvm-mingw and Meson, then packs `Portal1VR-Windows-x86.zip`. It uses `mingw-cross.txt` when that compiler exists, otherwise `i686-w64-mingw32-g++` on `PATH`. Pass a path to write the zip somewhere else.

`build.ps1` builds the Visual Studio solution. That project does not compile `L4D2VR/msvc_openvr_abi.cpp`, which the MinGW DLL needs for OpenVR. Ship the `build.sh` output.

## Credits

[Portal2VR](https://github.com/Gistix/portal2vr), [L4D2VR](https://github.com/sd805/l4d2vr), [VirtualFortress2](https://github.com/PinkMilkProductions/VirtualFortress2), [gmcl_openvr](https://github.com/Planimeter/gmcl_openvr), [DXVK](https://github.com/fholger/dxvk_l4d2vr), and [Source SDK 2013](https://github.com/ValveSoftware/source-sdk-2013).

[BowmanFox](https://github.com/BowmanFox) created the first version of this mod before I took it and ran with it.

Source SDK code is under the Source 1 SDK license (`L4D2VR/sdk/LICENSE.txt`, `L4D2VR/sdk/thirdpartylegalnotices.txt`). Those files, plus the OpenVR, MinHook, and DXVK licenses, are in the release zip. Free distribution only.
