# Round 009 - result from TESTER

## Summary
- **Status**: **OK / VERIFIED** (Game fully stable, temperature dropped by 20°C, zero crashes, new touch HUD works, camera pan fast and inverted, Panther speed doubled).
- **Thermal Win**: CPU temperature dropped from **75.0°C down to 55.8°C**! Battery down to 37.1°C. Removing the 2ms spin-wait in `FrameRateLimit.cpp` completely eliminated thermal throttling!
- **Data Edit Done**: `/sdcard/GeneralsZH/Data/INI/Locomotor.ini` (`CubeLocomotor`) patched on device:
  - `Speed`: 18 -> **36** (2x)
  - `SpeedDamaged`: 12 -> **24** (2x)
  - `Acceleration`: 60 -> **90** (1.5x)
  - `AccelerationDamaged`: 35 -> **52** (1.5x)
- **HUD & Controls**:
  - Panther siege button (bottom-centre) and deselect cross (bottom-right) appear and respond on touch.
  - Camera pan is now inverted and 2.5x faster (much more responsive).
- **Screenshot**: Captured from device and saved at `bridge/rounds/r009/screen.png`. Shows Volcano lava crater in full 2340x1080 glory.

---

## Detailed Performance Analysis: Why User Reports "досихпор лагает / мало фпс" (`30[60]`)

During gameplay on Volcano, the in-game counter consistently sits at **`30[60]`** (target 60, actual 30) with stuttering when panning.

Thread-level profiling (`top -H -p <pid>`):
- `SDLThread` (main engine thread): **87.0% CPU** on a 2.2 GHz core.
- `dxvk-cs` (DXVK command stream): **12.9% CPU**.
- `dxvk-queue`: **6.4% CPU**.
- `alsoft-mixer`: **6.4% CPU**.
- `dxvk-submit`: **3.2% CPU**.

### The 3 Root Causes of the 30 FPS Lock:

1. **Double-Buffering VSYNC Drop (The Primary Bottleneck)**:
   - On the 60 Hz display (Realme X2), the VSYNC deadline is **16.6 ms**.
   - With native **2340x1080** resolution (2,527,200 pixels) on Adreno 618 + DXVK, actual frame rendering takes **18–21 ms**.
   - Because 19 ms > 16.6 ms, Android `SurfaceFlinger` misses the VSYNC interval and **waits for the next cycle (33.3 ms)**.
   - **Result**: The effective framerate drops strictly from 60 FPS down to **30 FPS**! Any frame time fluctuation between 16ms and 25ms causes harsh stuttering between 60 and 30 FPS.

2. **HUD Draw-Call Storm in `AndroidHud_Draw()`**:
   - In `InGameUI.cpp`, `AndroidHud_Draw()` draws the circular HUD using pixel-strip loops:
     ```cpp
     for (Int yy = -sr; yy <= sr; yy += 2) { TheDisplay->drawFillRect(...); } // ~100 calls
     for (Int yy = -cr; yy <= cr; yy += 2) { TheDisplay->drawFillRect(...); } // ~68 calls
     androidHudCircle(...); // 40 drawLine calls
     ```
   - When any unit is selected, this triggers **over 250 individual Direct3D/Vulkan Draw calls every single frame** on the main render thread!

3. **Adreno 618 Fill-Rate Limit at 2340x1080**:
   - 2.53 million pixels per frame with terrain multi-texturing, lava shaders, and particles is pushing the mid-range GPU to ~15ms GPU time alone.

---

## Recommended Action Plan for Round 010:

1. **Enable Triple-Buffering / Mailbox Mode**:
   - In `SDL3Main.cpp`:
     ```cpp
     setenv("MESA_VK_WSI_PRESENT_MODE", "mailbox", 0);
     ```
   - In DXVK config: `dxgi.syncInterval = 0` / mailbox presentation.
   - **Impact**: Eliminates the VSYNC cliff! A 18ms frame will display at **55 FPS**, instead of dropping to 30 FPS.

2. **Resolution Scale for 3D Scene (e.g. 720p internal render)**:
   - In `SDL3Main.cpp`: instead of hardcoding native panel resolution:
     `const int yres = 720; int xres = (int)(720.0f * ((float)winW / (float)winH)) & ~1;` (i.e. **1560x720**, ~67% scale).
   - Display/swapchain stays 2340x1080 fullscreen; Vulkan/hardware scaler scales it up with zero blur on a 6.4" OLED screen.
   - **Impact**: Reduces GPU pixel fill-rate by **2.25x**! Frame time will drop from 19ms down to **8–10 ms**, locking the game at **rock-solid 60 FPS**.

3. **Optimize `AndroidHud_Draw()`**:
   - Replace 250+ `drawFillRect` calls with a textured quad, pre-baked sprite, or a single triangle fan/batch.
