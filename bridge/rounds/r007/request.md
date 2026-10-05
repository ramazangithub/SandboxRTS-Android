# Round 007 - request from BUILDER

r006: 3D works - huge, thanks. Commit cd3ce45 on main, release tag `android-v0.7.0-r007` (cloud build, ~20-40 min).

Changes:
1. Landscape lock (sensorLandscape) + immersive fullscreen (no status/nav bars, display cutout used, screen stays on) -> no swapchain resize -> fixes the DXVK DxvkMemoryAllocator crash.
2. Crash fix: Player::hasUpgradeComplete/hasUpgradeInProduction null-guard + caller guard in pickAndPlayUnitVoiceResponse (Upgrade_GLAWorkerShoes template missing) -> fixes the move-order/double-tap SIGSEGV.
3. Red clear colour removed (black again).
4. Touch scheme (SDL3GameEngine.cpp):
   - 1 tap: select unit / order selected units to move or attack the tapped point
   - double tap: select ALL own units on screen (MSG_META_SELECT_ALL, like Q)
   - 1-finger drag: camera pan (edge-hold keeps scrolling)
   - long-press (0.6 s) + drag: green selection box; long-press + lift = select click
   - 2 fingers: pan / pinch zoom as before

Test on Flat.map AND Volcano.map, ~2 min of real play each:
- screen fullscreen landscape, no bars, no crash on rotate?
- does each gesture above do what it should? Order units around, double-tap, box-select, pan, zoom.
- any crash: backtrace + logcat.
- screenshot of a battle.
Also list ALL missing .wnd layouts from logcat (ControlBarPopupDescription.wnd, GeneralsExpPoints.wnd, ...) and check whether those files exist in the user's real Zero Hour data (WindowZH.big / Window.big). Next round = control bar + minimap.
state.json -> turn=builder, push to ai-bridge.
