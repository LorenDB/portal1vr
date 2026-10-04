<p align="center">
  <img src="imgs/logo.png" alt="Portal logo" width="480">
</p>

# Portal 1 VR

VR mod for the Windows x86 Steam version of Portal, based on [Portal2VR](https://github.com/Gistix/portal2vr) and [L4D2VR](https://github.com/sd805/l4d2vr).

## Screenshots

Captured while playing Portal 1 VR fullscreen with a Pico headset through SteamVR. These are actual headset views from the playtest, using Portal's stock hand and gun models. Click either picture for the full-size image.

| Portal gun in the test chamber | Both hands in the elevator |
| --- | --- |
| [![Portal 1 VR playtest with the controller-held portal gun and an orange portal across the test chamber](imgs/screenshots/portal1vr-playtest-1.png)](imgs/screenshots/portal1vr-playtest-1.png) | [![Portal 1 VR playtest showing the separate left hand and the right hand holding the portal gun inside an elevator](imgs/screenshots/portal1vr-playtest-2.png)](imgs/screenshots/portal1vr-playtest-2.png) |

## Installation

Download the compiled Windows x86 ZIP from [Releases](https://github.com/BowmanFox/portal1vr/releases). Close Portal, extract the entire ZIP to a folder, and run `Install.cmd`. For a custom game location, run `Install.ps1 -PortalDirectory 'D:\SteamLibrary\steamapps\common\Portal'`. The installer copies the runtime, OpenVR DLL, bindings, and corrected arm materials. The hands, portal gun, player body, and radio song are Portal's originals; replacement packs from earlier releases are moved to `bin/VR/InstallBackups`. Connect the headset through its PC VR software and start SteamVR first.

Run `Launch Portal VR.cmd` for fullscreen using Portal's configured resolution, or launch Portal with:

```text
-insecure -fullscreen -novid +mat_queue_mode 0 +mat_vsync 0 +mat_antialias 0
```

Choose your preferred display resolution in Portal's video options, then load a chapter. The left stick moves, the right stick turns, and clicking the left stick recenters the headset. The gun-hand trigger fires the blue portal, the gun-hand grip fires the orange portal, the gun-hand face buttons jump (`Jump`) and interact (`Use`), and the off-hand grip ducks (`Crouch`); dropping your head IRL also ducks via `PhysicalCrouch=true` (`PhysicalCrouchDrop=0.25` meters, `false` for button-only). `ShowHands=false` hides the hand/arm models and leaves only the portal gun mesh (the off-hand grip then ducks without engaging gun support); both `PhysicalCrouch` and `ShowHands` are optional settings, so older configs load without warnings and the installer appends the defaults. IRL crouching keeps `IN_DUCK` pressed so the server hull shrinks, but the rendered camera adds the engine's duck eye-dip back for physical-only ducks — the view follows your head 1:1 with no double dip, while the crouch button alone keeps the classic view dip. All gameplay inputs are SteamVR Input actions (`Jump`, `Crouch`, `Use`, `PrimaryAttack`, `SecondaryAttack`, `Walk`, `Turn`, `ResetPosition`), so they can be rebound per controller through SteamVR's Controller Binding UI. Default bindings ship in `bin/VR/SteamVRActionManifest`; SteamVR's Pico-to-Oculus compatibility mapping was used during testing. Use **VR: use left-handed controls** or **VR: use right-handed controls** in the main/pause menu to switch and save the gun hand. Left-handed mode mirrors the gameplay buttons and sticks; click the movement stick (right stick in left-handed mode) to recenter, or choose **VR: recenter headset**. Configuration is loaded from `bin/VR/config.txt` at startup.

`RenderWindow=1` provides a separate desktop view and the pause-menu texture. It adds a third scene render. Leave it enabled for the tested configuration.

The installer appends missing settings from the shipped config while preserving existing values and comments. This includes `LeftHandGunGrip=true`, `LeftHandGunGripRadius=6`, and `FirstPersonBodyBackOffset=8` on older installations. `tests/install-config.ps1` verifies that the shipped defaults cover every active runtime setting and that upgrades preserve custom values.

## Runtime fixes

- Keep the blue/orange gun light at its tracked attachment position after Portal's flat-screen glow calculation. Native color, size, material, and world-model effects are retained.
- Preserve the complete camera transform when Portal renders the eye through a linked portal during a crossing. Roomscale and stereo offsets are applied in player space, then the camera, first-person models, and attachments are transformed together. The new crossing correction needs headset confirmation.
- Start VR on the render thread after the engine and D3D device are ready. SteamVR initialization failures can retry without crashing normal rendering.
- Use Portal's x86 engine interfaces and camera structure instead of Portal 2's incompatible layouts.
- Resolve client objects through checked RTTI and validate optional hook signatures.
- Preserve the desktop camera, render each eye separately, and update the engine's view angles from the headset.
- Preserve the monitor resolution during D3D device resets. Eye textures have their own resolution; applying that resolution to the fullscreen desktop could freeze rendering after a focus change.
- Sweep a camera hull from the player's eye position to the room-scale headset position. Solid walls, doors, glass, and props stop the view before either eye or its near plane crosses the surface. Both hands receive the same correction, and leaning back restores the normal tracking position without moving the tracking origin.
- Match Portal 1's collision-ray layout, with the ray/swept flags at bytes 64/65. The extra Portal 2 field made the engine treat moving traces as stationary, allowing the headset through walls.
- Refresh the submitted eye surfaces when Portal reallocates render targets on map load. The old surfaces produced a black headset image even though SteamVR accepted them.
- Retain the named eye targets across save loads so VR does not reopen material-system allocation during restoration; continue refreshing their underlying surfaces on binding.
- Use Portal 1's one-argument player-portal callback to turn tracking and both hands immediately, including crossings with little positional displacement. Rebuild server pickup poses relative to the current player eye pose so an entry-side sample follows a server teleport. Limit eye overrides to use/pickup callbacks so ordinary portal physics sees the player's head.
- Install the action manifest and controller bindings correctly. Guard missing controller poses and use the correct input and cursor interfaces.
- Hook Portal 1's eight-argument, float-returning `TraceFirePortal` so placement follows the controller instead of head aim.
- Use the compiled gun's muzzle centerline for portal shots and the aim marker at every range. Start the ray at wrist depth to retain near-wall checks, and calculate the aim marker after updating the current controller pose.
- Attach the portal gun at its wrist bone, with the barrel aligned to the shot direction. Transform copies of the bone matrices so eye renders do not compound the pose.
- Render bare arms with independently tracked left/right bone chains, and create the player's second viewmodel to retain the left arm while holding the gun.
- Disable world-player bone merging and reconstruct bare arms from their reference skeleton. The old hands animation displaced the wrists from their forearm attachment points. The gun arm keeps Portal's authored grip with its forearm straightened behind the wrist, preserving the gun mesh and firing direction.
- Draw the first-person body before the main world view is popped, separately for each eye and the optional desktop view. Match the model eye midpoint to the center camera horizontally while preserving animated foot height. Show this copy only when looking down at least 30 degrees. With `FirstPersonBodyHideUpper=true`, retain the torso and collapse only the head and arms at their neck/shoulder attachments; normal portal/world renderings retain the complete model.
- Map both bare hands to one controller wrist frame; Valve's mirrored hand bones then place the palms down with the thumbs inward. Finger hinges are derived from the model's joint positions, so differing bone rolls cannot reverse a finger. Valid zero skeletal curl opens a bare hand; missing or inactive skeletal data uses a relaxed fallback. The gun hand keeps Portal's authored grip.
- The first-person body uses the player model's `eyes` attachment for camera alignment, with the eye-bone midpoint as a fallback.
- Resolve the server player's entity handle through its current virtual interface. The old unmatched `entindex` signature silently disabled the controller pickup hooks. Use the controller origin and angles during the pickup trace and held-object solver, while keeping the headset camera independent.
- Override the broken stock arm material path through Portal's custom-content folder, using textures already installed with the game. The gun hand uses a filled skin region because its UV layout does not match this atlas; this avoids black pixels but does not restore its original nail/tattoo detailing.

## Validation and limits

Tested locally against Portal client.dll timestamp `0x68362d89`, with a Pico headset through SteamVR. The tester confirmed a visible room, working head rotation, and left-stick movement. Captured eye render targets contain the scene, and both SteamVR eye submissions return success. This confirms basic VR operation, not a complete campaign playthrough.

The tester also confirmed controller-directed portal placement and independent arm movement in an early room without the gun, using Portal's stock hand and gun models. The runtime later targeted custom replacement models; it now drives the stock `v_hands.mdl` and `v_portalgun.mdl` again. The stock hand frames, finger curl, gun placement, muzzle ray, and left-hand support socket were checked in offline renders of the stock meshes posed by the runtime's bone transforms, and the regression tests pass against the installed gun model. This combination has not been tested in a headset. Live logs also show controller position and angles reaching the pickup hooks. Hand orientation and grip comfort, pickup through portals, and a complete campaign playthrough still require user validation.

The gun is drawn at its authored size with its grip seated in the palm (8.5 Source units back from the tracked controller along the aim axis, paired with the wrist anchor so aim, pickup, and muzzle effects stay glued to the visible gun). `ViewmodelPosCustomOffsetX/Y/Z` add forward/right/up adjustments in Source units; at the default scale, one unit is about 2.3 cm. Restart Portal after changing configuration.

`FirstPersonBody=true` enables the local body when looking down at least 30 degrees, in both VR eyes and the separate desktop mirror (`RenderWindow=1`). Keep `FirstPersonBodyHideUpper=true` to hide the head and untracked arms while showing the chest, torso, and legs. This affects only the temporary first-person rendering. Set `FirstPersonBody=false` to disable the feature, or set `FirstPersonBodyHideUpper=false` only when testing the unmasked full player model.

`FirstPersonBodyBackOffset=8` moves the first-person body backward from its camera-aligned position by eight Source units (about 19 cm at the default VR scale). It uses horizontal heading, preserving foot height as you look down. Values from 0 to 24 are supported; restart Portal after changing it. The tracked hands, gun, collision, and full model seen through portals keep their existing transforms. The default was chosen for the earlier custom body's forward torso placement and has not been re-checked in a headset with Portal's stock Chell model; adjust it if the torso sits too far forward or back.

The camera-collision update passes the Release x86 build and regression checks and has been installed for Pico testing. Its ray flags were checked against the installed engine.dll and corrected to Portal 1's layout. A live downward hull probe returned a floor hit at fraction `0.015059`. During play, the camera then logged blocked contact and recovery to zero correction, with successful submissions to both eyes. The Pico tester confirmed the wall fix works. Passage through active portals with this update still needs verification.

No replacement models are shipped. Portal's stock `v_hands.mdl`, `v_portalgun.mdl`, and Chell model are used for the tracked hands, the gun, the first-person body, portal views, and the death ragdoll. Portal's gun viewmodel was built for the desktop camera: its rear shell is open underneath, so the gripping hand and the inside of the shell are visible from below. The installer moves an earlier release's `portal/custom/bowman_portal1.vpk` to `bin/VR/InstallBackups`; manual installers should remove that file.

Optional left-hand support remains free until a fresh grip squeeze near the underside socket. Release grip or pull away to resume independent tracking. `LeftHandGunGrip=false` disables support; `LeftHandGunGripRadius=6` sets the default engagement distance in Source units. Missing tracking releases support. The supplied controller bindings add support inputs while retaining their existing actions; saved custom layouts may need the support action added manually. Right-hand aiming is unchanged.

## Placement and portal collision

Head collision uses Portal 1's native portal-environment hull trace on the verified client build, so a linked opening is not treated as an ordinary solid wall. Other client builds fall back to the existing wall trace. Startup centering now waits for a valid headset pose, and recentering clears the previous collision correction. Physical room movement can still separate the headset from the player capsule; click the left stick to recenter if a doorway appears blocked. The user confirmed that workaround. The new portal-crossing behavior still needs headset confirmation.

`tests/run.ps1 -PortalClient 'C:\Program Files (x86)\Steam\steamapps\common\Portal\portal\bin\client.dll'` also validates the native trace's installed-binary layout without running game code.

## Radio

Portal's stock radio song is used; the mod ships no radio audio, menu option, or `portal1vr_radio_on/off` commands.

Earlier releases replaced the song. The installer moves their VPK, a leftover loose radio WAV, credit notice, and sound caches to `bin/VR/InstallBackups` so they cannot mask the stock song. Other sounds and the existing VR configuration are preserved. For manual installation, remove `portal/custom/bowman_portal1.vpk` and `portal/custom/bowman_portal1.vpk.sound.cache`, and move any old `portal/custom/portal1vr/sound/ambient/music/looping_radio_mix.wav` and its `sound/sound.cache` outside `portal/custom` before restarting Portal. Run `powershell -NoProfile -File tests/install-stock-content.ps1` to check reversible migration and repeated installation.

## Compiled release

After building Release x86, run `python package-release.py --output Portal1VR-Windows-x86.zip` to package the compiled runtime, OpenVR dependency, config-preserving installer and controller bindings. End users do not need Python, Visual Studio or Blender.

Held objects now use the corrected aim orientation from the visible wrist instead of the raw grip direction, which was 30 degrees higher. Automated checks cover carry height and 900 barrel-ray cases across angles, ranges and axial recoil, with maximum line error below 0.002 Source units. See `docs/avatar-verification/Range_Carry_Verification.json`; that report was produced with the earlier custom gun model and predates the latest headset confirmation of angled aiming and wrist-roll carry stability.

The viewmodel render pass uses the active eye camera at Portal's verified gun-render call site. The native `PortalBlast` travelling effect uses the captured muzzle instead of head-derived launch position and angles, preserving its destination, timing and color. After the user reported an angled orange miss on clear surfaces, paired live logs measured up to 1.81 degrees of launch-direction error introduced when Source transferred the effect to the client. The client now restores the exact captured launch frame for a matching local shot of either color. Matching requires the same color, nearby launch and target positions, and a recent, unconsumed shot; unmatched effects keep the native behavior. Native portal-placement adjustments remain enabled. Headset confirmation of this latest correction is pending.

The optional support hand is seated under the barrel at a built-in socket, because Portal's gun model has no authored one. Cached socket metadata remains valid while the same gun is outside the camera view, avoiding involuntary grip release when drawing pauses; weapon changes and tracking loss still release support. Independent hand tracking resumes on release or pull-away. The socket position was checked in offline renders only and needs headset confirmation.

The carry solver receives zero roll only for its position calculation, preventing wrist twists from triggering Portal's downward view-offset correction; prop orientation still receives the full wrist pose. The user confirmed stable cube holding during wrist twists. See `docs/avatar-verification/Overhead_Calibration_Verification.json` for build and test status.

`AutoCalibration=true` preserves calibration after explicit SteamVR origin/floor/heading changes, including small changes that accumulate slowly. Horizontal recovery requires a still, upright headset, a stationary player, sustained stick input, a blocked route at the player body, and a clear route at the visible head. It eases toward alignment at up to 12 cm/s. Ordinary walls, crouches, head turns, aiming, carrying, portal transitions and unreliable tracking inhibit recovery. Tracking loss or frame gaps reset the movement baseline and require stable tracking before recovery resumes. It does not guess an arbitrary incorrect starting floor or heading. Manual recentering remains available from the menu or movement-stick click; `AutoCalibration=false` disables automatic recovery.

Prop selection uses the barrel centerline separately from the wrist carry pose. Native reach, visibility, usability and contact-pickup checks remain enabled. Pickup accuracy, the new left-handed controls and calibration comfort require headset confirmation.

## Build

The September 23 portal/pickup and save-load changes pass the Release x86 build and automated pose checks. A live Pico run confirmed both new hooks attach, controller pickup reaches the scoped eye overrides, a portal crossing updates tracking by 90 degrees, and both eyes continue receiving frames afterward. Held-prop behavior during crossing and repeated save-load textures still need gameplay confirmation. Check carrying a cube through both directions, releasing it afterward, and loading a save repeatedly while watching chamber textures.

Install Visual Studio 2022 C++ build tools with the Windows SDK, and Git. Clone this repository recursively, then:

```powershell
./build.ps1
./L4D2VR/copy-to-portal.ps1 -SourceDll ./Release/d3d9.dll -PortalDirectory 'C:\Program Files (x86)\Steam\steamapps\common\Portal'
```

`build.ps1` applies `patches/dxvk-portal-startup.patch` to the pinned DXVK submodule before building Release x86. If using the Visual Studio solution directly, first run `patches/apply-dxvk-patch.ps1` and select x86 and your installed toolset. The patch is stored in the parent repository so the DXVK fixes survive a fresh clone.

From an x86 Native Tools Command Prompt, run `powershell -File tests/run.ps1` for the interface-layout, calling-convention, trace-filter, model-name, wrist-transform, independent-arm, first-person-body, and camera-collision regression checks. Camera checks cover the swept-hull layout, eye/near-plane clearance, leaning back, solid starts, floor contact, and resetting after an engine teleport.

For render debugging only, add `-portalvr-debug-textures`. It saves left/right/desktop BMP images at three early frame counts in the Portal directory. Normal launches do not perform these GPU readbacks. Add `-portalvr-debug-collision` to run a downward hull probe against the live engine on the first gameplay frame; `portalvr.log` records its hit fraction and the camera's blocked/recovered state changes.

## Credits

[Portal2VR](https://github.com/Gistix/portal2vr), [L4D2VR](https://github.com/sd805/l4d2vr), [VirtualFortress2](https://github.com/PinkMilkProductions/VirtualFortress2), [gmcl_openvr](https://github.com/Planimeter/gmcl_openvr), [DXVK](https://github.com/fholger/dxvk_l4d2vr), and [Source SDK 2013](https://github.com/ValveSoftware/source-sdk-2013).

Portal logo: Valve. Screenshots captured during local VR playtesting. [Image sources](imgs/SOURCES.md).
