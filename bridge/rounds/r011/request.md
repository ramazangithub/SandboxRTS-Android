# Round 011 — android-v0.10.0-r011 (main 9616b99)

APK: GeneralsZH-Android-android-v0.10.0-r011-debug.apk (release workflow).

## What changed
- Camera pan rewritten: no RMB at all. Direct drag (ground follows finger x1.7, PAN_GAIN) via screenToTerrain + View::lookAt, world-space inertia glide (GLIDE_START 0.9, GLIDE_DECAY 0.86).
- Two-finger twist rotates camera (View::setAngle, ROTATE_SIGN=+1, deadzone 0.10 rad).
- Fast flick no longer counts as tap/double tap (moved >= threshold or >450 ms -> scroll + glide).
- Double tap = select only own non-structure units visible on screen.
- New right-middle HUD button = select all army (MSG_META_SELECT_ALL).
- Top-left/top-right counters removed (fps[limit], clock, game timer, latency). Big green "FPS NN" top-left, inset.
- Forced once at start: DrawEntireTerrain=No, TerrainLOD=AUTOMATIC, MaxCameraHeight<=380, ShroudOn=TRUE.
- DXVK: maxFrameLatency 2, no forced anisotropy. OpenAL: alsoft.conf resampler=linear, output-limiter=false.

## Please check
1. Fog of war (shroud) visible on SandboxRTS map? If still fully revealed — find what reveals it (map script / sandbox mode) and report.
2. Pan: does the ground follow the finger, direction right, speed OK? Glide after lift; selection and UI must stay after glide.
3. Rotation direction: does camera follow the twist? If inverted -> report (flip ROTATE_SIGN).
4. Fast swipe up -> no select-all.
5. Double tap -> only on-screen units. Right-middle button -> whole army.
6. FPS label readable, not clipped. No other counters.
7. Profile like r010: avg/1% low FPS, frame ms, thermal, simpleperf top; confirm ALSOFT_CONF took effect (resampler in profile).
