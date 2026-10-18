# Decisions (append-only)

### D-002: Portal with RTX and Portal: Prelude RTX are not supported
Date: 2026-10-06.
Evidence:
- Both games' `bin/d3d9.dll` is the RTX Remix bridge. This runtime is itself a `d3d9.dll`, and its eyes are separate engine RenderViews into eye-sized render targets.
- dxvk-remix keeps the first camera per frame, path-traces once into the backbuffer, and rasterizes other render targets. Eye views would therefore get no RTX.
- Stereo would need a dxvk-remix fork: two path-traced views per frame, with separate NRD/DLSS-RR/ReSTIR/NRC history per eye, and OpenVR submission from the 64-bit bridge process. That fork builds only with Windows/MSVC.
- Performance: an RTX 4090 runs Portal with RTX at about 26 fps at native 4K and about 64 fps with DLSS Balanced. 4K is roughly a stereo headset's pixel count, and VR needs 72–90 Hz.
- Portal with RTX's game DLLs are NVIDIA builds: 3 of the 9 live signatures and all RVA-pinned hooks fail.
Fallback rungs considered:
- Mono path tracing with a depth-reprojected second eye (rung 4): still needs the Remix fork.
- Alternate-eye frames: each frame would read as a 6 cm camera jump to the temporal denoisers, at half the per-eye rate.
- Replacing Remix with this runtime: that is Portal's campaign without RTX, already supported.
Revisit if: NVIDIA adds multi-view or stereo to Remix, or the user wants to fund a dedicated Remix-fork project on a Windows build machine.

### D-003: Map cameras turn once per cut and never follow camera rotation
Date: 2026-10-06. Flag: `MapCameraAlign` (default true).
Chose: while `IVRenderView::GetViewEntity()` is not the local player (and no menu is open):
- Pause roomscale body-follow and auto-calibration, and hide the aim dot.
- At each camera entity change, turn the eyes about the camera by yaw only, so the shot starts in front of the gaze.
- Keep head pitch and roll free, and translation 1:1.
Rejected:
- Following the camera's yaw continuously: that is forced rotation.
- Showing cutscenes on the flat menu screen: a candidate if a moving camera proves uncomfortable.
Guard: slot 27 must be `A1 imm32 C3`; otherwise detection is off and behaviour is unchanged.
