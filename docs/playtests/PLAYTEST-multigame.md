# PLAYTEST-multigame: Rexaura VR and map cameras

About 20 minutes. You launch everything; nothing here is started for you.

**Stop at once** if you feel warm, sweaty or dizzy. Don't push through; wait an hour before trying again, and tell me what you were doing when it started.

There is no in-VR bug button yet. When something goes wrong, note the rough time (and the map name if you know it). `portalvr.log` in the Portal folder timestamps every state change.

## Setup (once)
- Already installed in your Portal folder: the new `bin/d3d9.dll` (the previous one is in `bin/VR/InstallBackups/`), the `rexaura_vr/` folder, `rexaura-vr-launch.sh`, and `MapCameraAlign=true` in `bin/VR/config.txt`.
- **Portal:** keep your usual launch options.
- **Rexaura VR:** set Portal's Steam launch options to
  `/mnt/steam/SteamLibrary/steamapps/common/Portal/rexaura-vr-launch.sh %command% -insecure -fullscreen -novid +mat_queue_mode 0 +mat_vsync 0 +mat_antialias 0`
  (with whatever else you normally add). Remove the wrapper again to play Portal.

| # | Do this | Expect | What happened |
|---|---|---|---|
| 1 | Start SteamVR, then Portal VR as usual. Load a mid-game save and play for a minute. | Exactly as before: no change to normal play. | |
| 2 | Quit, then send me `portalvr.log`. | It contains `Map camera detection available=1`. | |
| 3 | Portal: New Game, chapter 1 (`testchmb_a_00`). Watch the opening until you can move. | If a map camera runs, the view starts facing the scene and never turns by itself; your head still looks around freely. Control returns where the player stands. | |
| 4 | Switch the launch options to the Rexaura wrapper and start it. | Rexaura's title and menu scene appear, and the menu lists the three "VR:" entries. | |
| 5 | Choose "VR: recenter headset", then New Game, chapter 1. | `rex_00_intro` starts. | |
| 6 | Watch the intro cutscene through several camera cuts. Turn your head, then take a step in your room. | Each cut starts in front of you. Head motion is 1:1, the camera never rotates by itself, and stepping moves only your view, not the hidden player. | |
| 7 | Comfort: some intro cameras ride on moving doors. | Tell me: fine / a bit off / would rather see these on a flat screen. | |
| 8 | Once you have control, walk around in the room, then use the stick. | Roomscale follow and stick movement work as in Portal; you start where the player stands, not displaced. | |
| 9 | In `rex_01_holder`: fire a blue portal (trigger), and try the orange button. | Blue fires along the gun. Orange does nothing (this gun is blue-only until `rex_09`). | |
| 10 | Pick up a cube, carry it through a portal, set it on a pellet stability unit. | Pickup, carry and drop behave as in Portal. | |
| 11 | Let an energy pellet travel through a portal near you. | It renders correctly in both eyes, with no one-eye artifacts. | |
| 12 | Save, quit to the menu, and load the save. | It loads; textures are intact. The save is in `Portal/rexaura_vr/save`. | |
| 13 | While Rexaura VR runs, check your Steam friends status or Steam window. | Tell me whether it says Portal or Rexaura (Q-002). | |
| 14 | Optional: open the console, run `map rex_19_remote`, and play to the ending. | The ending cameras behave like the intro's (items 6–7). | |

**Comfort check:** any nausea, eye strain, or arm fatigue? When did it start?

Afterwards, send `portalvr.log`, plus anything that went wrong with its approximate time.
