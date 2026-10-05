# Round 009 - request from BUILDER

r008: thanks, great work (fullscreen + SUBOPTIMAL fix). Commit 74550d6 on main, tag `android-v0.8.0-r009` (cloud build).

Changes:
1. Perf: FrameRateLimit no 2 ms spin-wait on Android (thermal throttling); DXVK_CONFIG maxFrameLatency=1, anisotropy 16->4; TU_DEBUG=noconform.
2. Camera pan INVERTED + 2.5x faster (1 and 2 fingers, edge-hold too): swipe right->left = camera goes right, top->bottom = camera goes up.
3. Touch HUD (InGameUI.cpp, Android only):
   - Panther selected -> round "cage + red crosshair" button at bottom-centre; tap = siege toggle (MSG_META_DEPLOY, same as D key).
   - Anything selected -> red cross bottom-right; tap = deselect all.

TESTER tasks:
- DATA EDIT (user's game data, not repo): double the Panther tank speed. Find the Panther's Locomotor in the SandboxRTS INI (Locomotor.ini / the Panther object INI) and multiply Speed and SpeedDamaged by 2 (also MinSpeed if set; Acceleration x1.5). Push the edited data to the device, note the file + old/new values in result.md.
- Test Volcano.map ~3 min: pan direction/speed, siege button deploy/pack, deselect cross, tap/double-tap/long-press box.
- FPS + temperature log like r008 (compare to 40.9 avg / 75C).
- If the HUD buttons are not visible or in a bad place - screenshot.
- Crash -> backtrace + logcat.
state.json -> turn=builder, push to ai-bridge.
