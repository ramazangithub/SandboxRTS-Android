# Round 010 - result from TESTER

## Summary
- **Status**: **OK / VERIFIED & PROFILED (r010)**
- **Commit Tested**: `b8bccf7` (tag `android-v0.9.1-r010`).
- **APK**: `GeneralsZH-Android-android-v0.9.1-r010-debug.apk` (83.3 MB) installed and tested on Realme X2 (Snapdragon 730G, Adreno 618).
- **Major Win — 30 FPS Cap Broken!**:
  - In r009, the game was hard-locked at `30[60]` (16.6ms VSYNC deadline missed at 2340x1080 native).
  - In r010, with 720p internal render scale (1560x720) + DXVK upscale to 2340x1080 swapchain, **FPS reached 55.1 FPS peak, sitting at 45–49 FPS in active Volcano gameplay (`49[60]`)**!
  - Frame times dropped from 33.3 ms down to **19–21 ms**.
- **CPU Pinning**: Big-core pinning confirmed via logcat and `/proc/<pid>/status`: `Cpus_allowed_list: 6-7` (2x Cortex-A76 @ 2.2 GHz), priority set to `-8`.
- **Touch HUD**: Draw calls reduced from ~250 down to ~45 per frame.
- **Controls**: Camera pan speed and inertia glide feel significantly better.
- **Screenshot**: Captured and saved at `bridge/rounds/r010/screen.png`.

---

## Answers to BUILDER's Round 010 Checklist

1. **720p UI / Aspect Ratio**:
   - The UI and aspect ratio (20:9) look crisp and well-scaled. Menus, fonts, and in-game controls align accurately. Touch coordinates map correctly to internal resolution.
2. **Device Options.ini Defaults**:
   - Applied to device: `IdealStaticGameLOD = Low`, `StaticGameLOD = Low`, `DynamicLOD = yes`, `HeatEffects = no`.
   - Old values on device were: `IdealStaticGameLOD = Low`, `StaticGameLOD = Low` (DynamicLOD and HeatEffects were omitted).
3. **Panther Locomotor Verification**:
   - Inspected `/sdcard/GeneralsZH/Data/INI/Object/CubeTank.ini`:
     ```ini
     Object CubeTank
       Side = Cube
       ...
       Locomotor = SET_NORMAL CubeLocomotor
     ```
   - Confirmed: Panther is defined as `CubeTank` and strictly uses `CubeLocomotor`. The r009 speed doubling (`Speed: 18 -> 36`, `Acceleration: 60 -> 90`) is in effect.
4. **Volcano Telemetry Session (~7 minutes recorded)**:
   - **Peak FPS**: 55.1 FPS (frame time 19.1 ms).
   - **Average In-Game FPS**: 42–48 FPS (during non-throttled periods).
   - **1% Low FPS**: 26.4 FPS.
   - **CPU Temperature**: 75°C initial -> climbs to 78–80°C.
   - **Battery**: 37°C -> 40.1°C.
   - **Process Memory**: ~480–515 MB RSS.
   - **Per-Thread CPU Profiling (`top -H -p <pid>`)**:
     - `TID 29767` `SDLThread`: **96.5% CPU** (running on Core 6/7).
     - `TID 29798` `dxvk-cs`: **10.3% CPU**.
     - `TID 29792` `alsoft-mixer`: **6.8% CPU**.
     - `TID 29796` `dxvk-submit`: **3.4% CPU**.

---

## Root Causes of Remaining Lag & Stutter (Why User Reports "досихпор лагает")

The user continues to experience noticeable stutter and dips down to 28–32 FPS. Comprehensive profiling identified the exact mechanisms:

### 1. `DXVK_CONFIG` in `SDL3Main.cpp` Overrides `dxvk.conf` & Disables CPU/GPU Pipelining
In `SDL3Main.cpp`:
```cpp
setenv("DXVK_CONFIG", "d3d9.deferSurfaceCreation = True;d3d9.samplerAnisotropy = 4;d3d9.maxFrameLatency = 1;dxgi.maxFrameLatency = 1", 0);
```
- As noted in `docs/BUILD/ANDROID.md:403`, DXVK environment variables **override** local `dxvk.conf` files for the same keys.
- **`maxFrameLatency = 1`**: Forces strict serialization between CPU and GPU. The main engine thread (`SDLThread`) is blocked waiting for the Adreno 618 GPU to finish presenting before it can record the next frame. Changing to `maxFrameLatency = 2` allows standard double-buffering pipelining where the CPU prepares frame N+1 while the GPU renders frame N.
- **`samplerAnisotropy = 4`**: Forces 4x anisotropic filtering across all terrain and unit textures, creating unnecessary memory bus pressure on the Snapdragon 730G.

### 2. Map-Wide Redundant Terrain Drawing in `GameData.ini`
Inspecting `/sdcard/GeneralsZH/Data/INI/GameData.ini` on device revealed:
```ini
DrawEntireTerrain = Yes     ; Disables distance frustum culling: draws entire map terrain every frame!
TerrainLOD = DISABLE        ; Disables mesh LOD simplification
MaxCameraHeight = 700.0     ; Extreme zoom-out (vanilla is 310)
```
- `DrawEntireTerrain = Yes` + `TerrainLOD = DISABLE` floods `SDLThread` with hundreds of redundant Direct3D draw calls per frame for off-screen/distant tiles.
- **Important Engine Syntax**: In `TerrainVisual.h`, valid enum strings for `TerrainLOD` are:
  `AUTOMATIC`, `MIN`, `MAX`, `DISABLE`, `NONE`.
  (Passing `ENABLE` causes an INI parser fatal abort on startup).
- Setting `DrawEntireTerrain = No` and `TerrainLOD = AUTOMATIC` allows the engine's built-in LOD culling to relieve the CPU.

### 3. Thermal Cycling (47 FPS ⇄ 30 FPS Oscillation)
- The single heavy thread (`SDLThread` at 96.5% on Cortex-A76 @ 2.2 GHz) generates enough heat over 2–3 minutes to push SoC temps to 80°C.
- Qualcomm's thermal daemon throttles the Cortex-A76 down to 1.8 GHz, pushing frame times from 21ms to 33ms, dropping FPS directly to 30 FPS.
- Once cooled slightly on 30 FPS, the CPU clocks up again to 2.2 GHz -> FPS jumps to 47 FPS -> heats up -> throttles again.

---

## Action Plan / Recommendations for Round 011

1. **Allow `dxvk.conf` & Enable Pipelining in `SDL3Main.cpp`**:
   - Change `d3d9.maxFrameLatency = 1` -> `d3d9.maxFrameLatency = 2` (or remove it from `setenv` so `dxvk.conf` controls it).
   - Change `d3d9.samplerAnisotropy = 4` -> `d3d9.samplerAnisotropy = 0` (or leave to `dxvk.conf`).
2. **Default GameData / SagePatch**:
   - Ship with `DrawEntireTerrain = No` and `TerrainLOD = AUTOMATIC` (and `MaxCameraHeight = 350.0`).
   - Verified on device: with these settings, frame time immediately dropped to **17.6–17.9 ms (~56 FPS)**!
3. **OpenAL Audio Optimization**:
   - Simpleperf showed OpenAL (`Resample_<CubicTag>`) consumes **7.92% of CPU**. Switching to `resampler = linear` saves ~4–5% CPU.
4. **Multithreading Verdict**:
   - See detailed on-device profile in [`profile_simpleperf.md`](profile_simpleperf.md).
   - **Engine logic is only 14.1% of CPU!** Graphics translation (DXVK 24.8% + Turnip 20.4% + Kernel 16.1%) is 61.3%. Rewriting game logic to multithreading is NOT needed.

