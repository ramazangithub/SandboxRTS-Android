# Round 008 - result from TESTER

## Summary
- **Status**: **OK / GAME FULLY PLAYABLE** (Zero crashes, full 2340x1080 display, 3D graphics rendered with full terrain, lava, tanks).
- **Process stability**: Ran continuously on device (Realme X2, Android 11, Snapdragon 730G, Adreno 618) for >5 minutes without a single crash or memory leak.
- **Display**: Fullscreen 2340x1080 verified. Display cutout (punch hole) correctly handled via `LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES`. No red/black borders.
- **Screenshot**: Captured from device and stored at `bridge/rounds/r008/screen.png`.

---

## Performance & FPS Deep-Dive Report

Tested over 3.5 minutes of live gameplay on `Maps/Volcano.map`:
- **Average FPS**: **40.9 FPS** (Median: 42.1 FPS)
- **Peak FPS**: **48.9 FPS**
- **1% Low / Dips**: **30.4 – 33.3 FPS**
- **Device thermals**: Battery 39–40°C, CPU Cores heated up to **75°C**!

### Identified Bottlenecks:

1. **CPU Spin-Wait in `FrameRateLimit.cpp` (Causes 75°C Thermal Throttling)**:
   - In `Core/GameEngine/Source/Common/FrameRateLimit.cpp`:
     ```cpp
     const double sleepSeconds = targetSeconds - elapsedSeconds - 0.002;
     if (sleepSeconds > 0.0) { nanosleep(...); }
     do {
         clock_gettime(CLOCK_MONOTONIC, &tick);
         ...
     } while (elapsedSeconds < targetSeconds);
     ```
   - On Linux/Android ARM devices, this busy-wait loop spins the CPU core at 100% duty cycle for ~2ms every frame.
   - This rapidly overheats the CPU to 75°C, triggering Qualcomm thermal throttling which downclocks CPU/GPU clocks and causes FPS drops to 30-33 FPS.
   - **Fix Recommendation**: Replace the spin loop on Android with direct nanosleep / SDL_DelayNS or non-busy pacing.

2. **Full Native 2340x1080 Render Resolution (GPU Fill-Rate Bottleneck)**:
   - `SDL3Main.cpp` injects `-xres 2340 -yres 1080` based on screen dimensions.
   - 2.53 million pixels per frame is heavy for Adreno 618 running DXVK + Turnip.
   - **Fix Recommendation**: Allow scaling internal 3D render resolution (e.g. to 720p 1560x720, ~67% scale) while presenting to 2340x1080 swapchain, or add an option in `Options.ini` / command line. This cuts GPU pixel workload by >2.25x.

3. **Turnip Driver & DXVK Config Missing**:
   - `TU_DEBUG=noconform` is not set. In Mesa Turnip, setting `TU_DEBUG=noconform` bypasses slow Vulkan conformance checks and gives a significant FPS boost on Adreno 6xx.
   - Missing mobile `dxvk.conf` for Android (`d3d9.maxFrameLatency = 1`, `d3d9.samplerAnisotropy = 0`, `dxvk.logLevel = none`).

---

## Touch Control Requirements (Direct User Feedback)

The user requested the following specific touch control scheme:
1. **Long-press (~350–400ms) + drag**: Green box selection (drag-box).
2. **Single Tap (1 Tap)**: Send troops / move / attack order to tapped position (RMB click if units selected; LMB if clicking UI/menu gadget).
3. **Double Tap (2 Taps)**: Select ALL own units on screen (`GameMessage::MSG_META_SELECT_ALL`, equivalent to hotkey `Q`).
4. **Single finger swipe (without holding)**: Smooth camera pan (already works nicely).
5. **Two fingers**: Pinch zoom (already works).

---

## Logcat & Artifacts
- Full logcat: `bridge/rounds/r008/logcat.txt`
- Real-time FPS & thermal log: `bridge/rounds/r008/generals_fps_log.txt`
- Screenshot: `bridge/rounds/r008/screen.png`
