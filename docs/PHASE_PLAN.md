# Phase plan

Mode: agentic | Gate: hard (the user runs every game launch) | Current work: release notes

## Status: Portal1VR, 2026-10-18
✅ Built and tested: Portal. Stereo, 6DoF, controllers, both hands, the portal gun, pickup and carry, roomscale walking, the ending lift, and the credits were confirmed in headset sessions.
🔧 Built, not yet tested:
- Map-camera handling for Portal's `testchmb_a_00` and `escape_02`.
→ PLAYTEST-multigame items 1–3 are still open.
🚧 In progress: nothing.
⬜ Not started:
- In-VR settings menu (`config.txt` only).
- In-VR bug button.
❌ Won't be supported: Portal with RTX and Portal: Prelude RTX with RTX on. RTX Remix path-traces one camera per frame; see D-002.
❓ Need from you: Q-001 (download Prelude RTX to check a non-RTX path?), Q-003 (GPU, only for future RTX work).
🐞 Open bugs: BUG-004 (Proton desktop present can stall the headset).
▶ Next: release is Portal on the pinned 2025-05-27 client. Map cameras still need a headset pass.

## Map cameras: brief
Goal: when Portal hands the view to a `point_viewcontrol`, keep the head free and don't walk the hidden player.

Unknowns: whether the opening and ending cameras are comfortable.

Hypotheses:
- H2: map cameras need detection, so that roomscale follow does not walk the hidden player.

Plan: map-camera handling (D-003). RTX evaluated and declined (D-002).

Verify: PLAYTEST-multigame items 1–3.

### Exit criteria
- [x] Map-camera logic unit-tested (`tests/map-camera.cpp`); engine slot guard verified on the installed `engine.dll` (`tests/engine-view-entity.cpp`)
- [x] Existing regression suite green against the installed Portal binaries
- [ ] Map cameras are comfortable and leave the player where they were (PLAYTEST-multigame item 3)
- [ ] Portal unchanged (items 1–2)
