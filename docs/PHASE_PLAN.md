# Phase plan

Mode: agentic | Gate: hard (the user runs every game launch) | Current work: multi-game support

## Status: Portal1VR, multi-game support, 2026-10-06
✅ Built and tested: Portal. Stereo, 6DoF, controllers, both hands, the portal gun, pickup and carry, and roomscale were confirmed in earlier headset sessions (see README).
🔧 Built, not yet tested:
- Rexaura VR (`rexaura_vr` mod folder on Portal's engine, launchers, installer).
- Map-camera handling: Rexaura's intro and ending, Portal's `testchmb_a_00` and `escape_02`.
→ PLAYTEST-multigame is waiting on you (about 20 minutes).
🚧 In progress: nothing. Stopped at the gate.
⬜ Not started:
- In-VR settings menu (config.txt only).
- In-VR bug button.
- Controller-matched prompts: Rexaura's "skip intro" hint names a desktop key.
❌ Won't be supported: Portal with RTX and Portal: Prelude RTX with RTX on. RTX Remix path-traces one camera per frame; see D-002.
❓ Need from you: Q-001 (download Prelude RTX to check a non-RTX path?), Q-002 (which game Steam shows), Q-003 (GPU, only for future RTX work).
🐞 Open bugs: none logged.
▶ Next: You run PLAYTEST-multigame. Me: triage the results; if moving cutscene cameras feel bad, add a flat-screen cutscene option (D-003).

## Multi-game support: brief
Goal: play as many of Rexaura, Portal with RTX and Portal: Prelude RTX as possible in VR, with the same features as Portal.

Unknowns: whether Rexaura content runs on Portal's 2024 engine (offline evidence says yes); which Steam app gets achievements (Q-002); Prelude's non-RTX viability (Q-001).

Hypotheses:
- H1: Rexaura has no game code of its own, so Portal's DLLs plus Rexaura's content behave like Rexaura.
- H2: map cameras need detection, so that roomscale follow does not walk the hidden player.

Plan: Rexaura through a wrapper `gameinfo.txt` (D-001), map-camera handling (D-003), RTX evaluated and declined (D-002).

Verify: PLAYTEST-multigame.

Risk: Rexaura's 2014-era content on Portal's 2024 HUD/scheme files. The only differing UI files are `clientscheme.res`, `hudlayout.res` and `hudanimations.txt`, which Rexaura does not override.

### Exit criteria
- [x] Rexaura wrapper resolves every Rexaura file to Rexaura's folder, and Portal's supported DLLs to `portal/bin` (`tests/rexaura-gameinfo.py`)
- [x] Installer finds Rexaura in the Steam libraries and writes a relative or absolute content path; release zip contains the Rexaura files
- [x] Map-camera logic unit-tested (`tests/map-camera.cpp`); engine slot guard verified on the installed `engine.dll` (`tests/engine-view-entity.cpp`)
- [x] Existing regression suite green against the installed Portal binaries
- [ ] Rexaura boots, plays, saves, and loads in the headset (PLAYTEST items 4–12)
- [ ] Map cameras are comfortable and leave the player where they were (items 3, 6–8, 14)
- [ ] Portal unchanged (items 1–2)
