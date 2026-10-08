# PLAYTEST-framerate: desktop mirror and frame timing

About 10 minutes. You launch everything; nothing here is started for you.

**Stop at once** if you feel warm, sweaty or dizzy.

## Setup (already done)
- New `bin/d3d9.dll` installed; the previous one is `bin/VR/InstallBackups/d3d9-before-frame-rate-20261007-194502.dll`.
- `bin/VR/config.txt` now has `RenderWindow=2`; your previous config is `bin/VR/InstallBackups/config-before-frame-rate-20261007-194502.txt`. Set it back to `1` for the old full desktop view.

| # | Do this | Expect | What happened |
|---|---|---|---|
| 1 | Start Portal VR, load a save in a busy test chamber (goo and portals in view), and play normally for 2-3 minutes, walking and turning. | Smoother than before. The desktop window shows your left eye during play; menus look as before. | |
| 2 | Open the pause menu, then resume. | The flat menu screen looks as it did, with the game view behind the menu. | |
| 3 | Optional: set `RenderWindow=1`, restart, and repeat item 1 in the same spot for a minute. | Gives a direct before/after in the log. | |

Send `portalvr.log` afterwards (from each run, if you do item 3; the file is replaced on every launch). Lines that matter:
- `Headset refresh 90.0 Hz` and `Desktop swap chain vsync=0 present mode=N`: 2 or 3 (FIFO) means the monitor used to pace the headset.
- `Frame time over 10.0s: ...`: average and worst frame, how many went over the headset's frame time, and where the time went (`engine`, `left`, `right`, `desktop`, `present`, `cmdthread`, `gpu`, `submit`, `poses`).
- `Desktop mirror of the left eye result=0x00000000`: the mirror works; anything else means it failed.
- `GPU time: frames=... ours avg=...`: how long the GPU spends on our frame (per eye, desktop, after-render, present) and the gap between frames. Compare `ours` with the `gpu` wait on the `Frame time` line.

While Portal runs, a read-only sampler on this machine records the GPU's clock level and busy percentage every 0.2 s, so nothing needs to be noted by hand.
