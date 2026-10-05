# Round 006 — Result (TESTER)

## 1. Summary: 🚀 3D Rendering is WORKING!
- `m_breakTheMovie = FALSE` fix in commit `8e222ce` completely unlocked the rendering pipeline!
- Game renders in full 3D via SDL3 + DXVK + Turnip Mesa on Qualcomm Adreno 618 (Realme X2, Android 11).
- **Screen verification**: Tested with `Maps/Volcano.map`. Terrain, ground textures, volcanic rocks, cracked earth, battle tanks in formation, and bright orange lava are all rendered in 3D! (See `screen.png`).
- Temporary red clear colour from r005 was visible on the left border, confirming the frame clear and swapchain.

---

## 2. Answers to BUILDER's questions:
1. **Screen: red / red with terrain or units / still black?**
   - **RED WITH FULL 3D TERRAIN, TEXTURES, AND UNITS!** Black screen is officially solved!
2. **Control bar visible?**
   - Control bar not rendered yet (`WindowLayout::load - Unable to load layout file 'ControlBarPopupDescription.wnd'`), but full in-game 3D world is actively drawn.
3. **Any new errors/crashes in logcat?**
   - Two distinct crash scenarios observed:
     - **Issue A: Screen orientation & swapchain resize crash (Vulkan/DXVK)**:
       Because `GeneralsXZHActivity` did not enforce landscape fullscreen mode, the app started in portrait (`1080x2256`) with Android status/action bar. When user rotated phone or system sent resize events, DXVK swapchain was recreated 4 times in a row (`Presenter: Got VK_SUBOPTIMAL_KHR, recreating swapchain`), and mid-draw DXVK crashed in `DxvkMemoryAllocator::createAllocation` / `AllocSlice`:
       ```text
       #00 pc 00000000001bd1c8 libdxvk_d3d9.so (dxvk::DxvkMemoryAllocator::createAllocation)
       #01 pc 00000000001bcfa0 libdxvk_d3d9.so (dxvk::DxvkMemoryAllocator::allocateMemory)
       #02 pc 00000000001be0e4 libdxvk_d3d9.so (dxvk::DxvkMemoryAllocator::createBufferResource)
       #03 pc 00000000001282d4 libdxvk_d3d9.so (dxvk::D3D9ConstantBuffer::AllocSlice)
       #04 pc 000000000010ac20 libdxvk_d3d9.so (dxvk::D3D9DeviceEx::UpdateFixedFunctionVS)
       #05 pc 0000000000100870 libdxvk_d3d9.so (dxvk::D3D9DeviceEx::PrepareDraw)
       #06 pc 00000000001009fc libdxvk_d3d9.so (dxvk::D3D9DeviceEx::DrawIndexedPrimitive)
       ```
     - **Issue B: Null pointer in `Player::hasUpgradeComplete` during unit command dispatching**:
       On `Flat.map`, when units start moving and playing response voices at t=21s:
       ```text
       #00 pc 0000000000b31a18 libmain.so (Player::hasUpgradeComplete(UpgradeTemplate const*) const+8)
       #01 pc 0000000000c3c024 libmain.so (pickAndPlayUnitVoiceResponse(...) + 952)
       #02 pc 0000000000c3cc94 libmain.so (CommandTranslator::issueMoveToLocationCommand(...) + 332)
       #03 pc 0000000000c40444 libmain.so (CommandTranslator::translateGameMessage(...) + 6120)
       ```

---

## 3. Required Fixes for Round 007 (BUILDER Action Items):

1. **Full-screen & Enforce Landscape Orientation (Prevent Swapchain Crash)**:
   - In `android/app/src/main/AndroidManifest.xml`:
     Add fullscreen theme to activity:
     `android:theme="@android:style/Theme.NoTitleBar.Fullscreen"` (or `@android:style/Theme.Material.NoActionBar.Fullscreen`).
   - In `GeneralsXZHActivity.java`:
     In `onCreate()`, explicitly call:
     `setRequestedOrientation(android.content.pm.ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);`
     This locks the window to landscape from the start and avoids live resize thrashing during DXVK draw calls.

2. **Remove Red Clear Color**:
   - In `W3DDisplay.cpp`, revert the temporary red clear color back to standard black `(0, 0, 0)`. 3D graphics are confirmed working.

3. **Null-guard `Player::hasUpgradeComplete`**:
   - In `Player.cpp`:
     ```cpp
     Bool Player::hasUpgradeComplete(const UpgradeTemplate *upgrade) const
     {
         if (!this || !upgrade) return FALSE;
         ...
     }
     ```
   - Also add null guard in `pickAndPlayUnitVoiceResponse` before querying player upgrades.
