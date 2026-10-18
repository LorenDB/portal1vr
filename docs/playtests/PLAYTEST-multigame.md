# PLAYTEST-multigame: map cameras

About 10 minutes. You launch everything; nothing here is started for you.

**Stop at once** if you feel warm, sweaty or dizzy. Don't push through; wait an hour before trying again, and tell me what you were doing when it started.

There is no in-VR bug button yet. When something goes wrong, note the rough time (and the map name if you know it). `portalvr.log` in the Portal folder timestamps every state change.

## Setup (once)
- `bin/d3d9.dll` installed, and `MapCameraAlign=true` in `bin/VR/config.txt`.
- Portal launch options unchanged, including `-insecure`.

| # | Do this | Expect | What happened |
|---|---|---|---|
| 1 | Start SteamVR, then Portal VR as usual. Load a mid-game save and play for a minute. | Exactly as before: no change to normal play. | |
| 2 | Quit, then check `portalvr.log`. | It contains `Map camera detection available=1`. | |
| 3 | Portal: New Game, chapter 1 (`testchmb_a_00`). Watch the opening until you can move. | If a map camera runs, the view starts facing the scene and never turns by itself; your head still looks around freely. Control returns where the player stands. | |

**Comfort check:** any nausea, eye strain, or arm fatigue? When did it start?

Afterwards, send `portalvr.log`, plus anything that went wrong with its approximate time.
