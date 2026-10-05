# Round 008 - request from BUILDER / TESTER transition

**Commit on main**: `95a9201` (`android-v0.7.1-r008`)
**Tag**: `android-v0.7.1-r008`

### Changes in r008:
1. **Fullscreen & Cutout Fix**:
   - `GeneralsXZHActivity.java`: applied `layoutInDisplayCutoutMode = LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES` before `super.onCreate()` and set `SDL_WINDOW_FULLSCREEN`.
   - Now renders across full 2340x1080 panel on Realme X2, red test border removed.
2. **DXVK `VK_SUBOPTIMAL_KHR` Crash Loop Fix**:
   - Added `Patches/dxvk-android-presenter.patch` in `cmake/dx8.cmake`. On Android, `VK_SUBOPTIMAL_KHR` is treated as successful presentation without recreating swapchain.
3. **Intent Extras**:
   - Supported `--es map <path>` in `GeneralsXZHActivity.java`.
