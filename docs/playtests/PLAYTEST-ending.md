# PLAYTEST-ending: escape_02 ending, credits, and walking in the room

About 20 minutes. You launch everything; nothing here is started for you.

**Stop at once** if you feel warm, sweaty or dizzy. Don't push through; wait an hour before trying again, and tell me what you were doing when it started.

There is no in-VR bug button yet. When something goes wrong, note the rough time. `portalvr.log` in the Portal folder timestamps every state change.

## Setup (once)
- Already installed in your Portal folder: the new `bin/d3d9.dll` (the previous one is `bin/VR/InstallBackups/d3d9-before-ending-fixes-20261007-175235.dll`).
- Keep your usual Portal launch options. Leave the desktop window **off** (`RenderWindow=0`), as it was when the credits failed.

| # | Do this | Expect | What happened |
|---|---|---|---|
| 1 | Load any mid-game save. Walk around your room: straight lines, a slow curve, a full circle, and a sharp turn. | The view stays with your head the whole time. It does not slide on by itself after you stop, and it does not drift sideways when you turn while walking. (BUG-003) | |
| 2 | Place a portal low on a wall, its bottom edge at the floor. Walk into it in the room at an angle, aiming near one edge. | The body follows you through instead of sticking to the wall beside it. (BUG-003) | |
| 3 | Load a save from the escape_02 boss fight and destroy the last core. | About 30 s later you float up off the floor toward the ceiling with GLaDOS, then the white flash. (BUG-001) | |
| 4 | Watch the parking-lot scene and the ride down to the cake. | Same as before. | |
| 5 | After the ride, when the screen goes black. | A flat screen appears in front of you with the Aperture terminal credits on an opaque black background, and "Still Alive" plays. When it ends you get the main menu. (BUG-002) If not: did you see anything at all (a faint panel, text, a laser pointer), and did the song play? | |
| 6 | Quick re-check of 5 without the fight: console `map escape_02`; once it has loaded, `sv_cheats 1`, then `ent_fire relay_blackout trigger`. | The screen goes black, the credits screen appears about a second later, and the song starts some 7 s after that. | |

**Comfort check:** any nausea, eye strain, or arm fatigue? When did it start?

Afterwards, send `portalvr.log`, plus anything that went wrong with its approximate time. Lines worth a look:
- `End credits rolling` (item 5/6), and `End credits detection portal=1 hl2=1` once per launch.
- `Flat screen shown for credits`, `Flat screen ignores texture alpha result=0`, and `Flat screen placed in front of the head; show result=0 visible=1`. A nonzero result, `visible=0`, or that last line repeating means the compositor refuses or hides the screen.
- `Frame pacing over 10.0s: ... reprojected=N` appears only when the compositor had to reproject or drop frames. If walking still feels laggy and these show up at the same time, the remaining lag is frame rate, not the follow.
- `Roomscale follow blocked at a portal: ...` gives the body and head positions against the opening if item 2 still sticks.
