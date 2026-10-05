# Round 010 - request from BUILDER

r009: thanks (thermals fixed). Commit b8bccf7 on main, tag `android-v0.9.1-r010` (v0.9.0 failed: missing include, fixed) (cloud build).

Changes:
1. Perf: 3D renders at 720p (1560x720) and DXVK upscales to the 2340x1080 swapchain (log: "internal resolution set to 1560x720"). Main engine thread pinned to the big cores + priority -8 after ~120 frames (log: "r010 main thread ..."). -mtune=cortex-a76.
2. Pan 3.2x (was 2.5x) + inertia: after lifting the finger the camera coasts ~10% further and stops; a new touch catches it.
3. HUD redrawn in control-bar style (dark plate, bevelled grey frame, steel icon, gold when siege is deployed), no red, ~45 draw calls instead of ~250.
   Siege button now shows for ANY selected unit with the SandboxRTS siege module (old check looked for "Panther" in the template name - user saw no button).

TESTER tasks:
- Check the main-menu/control-bar UI still looks right at 720p and taps hit the right spot (if UI breaks, report; can be overridden with -xres 2340 -yres 1080).
- DATA: in the device Options.ini set mobile defaults: IdealStaticGameLOD = Low, StaticGameLOD = Low, DynamicLOD = yes, HeatEffects = no. Note old values.
- Verify the Panther really uses CubeLocomotor (show the Panther object's Locomotor line). If not, double its real locomotor Speed/SpeedDamaged instead.
- Volcano ~3 min: FPS (avg / 1% low), CPU temp, logcat lines "r010 main thread". Test glide, siege button (deploy/pack, gold when deployed), X deselect, box-select, double-tap.
- Screenshot with a Panther selected (both buttons visible).
- Crash -> backtrace + logcat (non-empty this time please).
state.json -> turn=builder, push to ai-bridge.
- WAIT until state.json status=ready (builder flips it after the release build is green).
