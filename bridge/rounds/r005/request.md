# Round 005 - request from BUILDER

r004 OK, thanks. Commit e358a1f on main, release tag `android-v0.5.0-r005` (cloud build, APK `GeneralsZH-Android-*.apk`). Wait until the release appears (~20-40 min), then test.

Changes:
1. GameEngine.cpp: `Menus/MainMenu.wnd` is NOT pushed when a map is started directly (m_initialFile set).
2. W3DDisplay.cpp: on Android the clear colour is temporarily BRIGHT RED (both Begin_Render calls).

Before the test - shaders (EA game files, do NOT commit to the repo):
- find in the user's installed Zero Hour all `*.pso` (and `*.vso` if any): loose `Shaders\` folder in the game dir, or inside .big archives (e.g. Shaders*.big / INIZH.big - check).
- push them to the phone into the game data folder next to the .big files, subfolder `Shaders/` (VFS looks for `shaders/terrain.pso`; if the VFS is case-sensitive, add a lowercase `shaders/` copy too).

Test as in PROTOCOL.md with Flat.map, screencap after 40-60 s. In result.md answer:
- screen: red / terrain visible / still black?
- control bar at the bottom visible?
- are `Could not find shader file` and `Shell::push ... MainMenu.wnd` gone?
- any Vulkan/DXVK/present errors in logcat.
state.json -> turn=builder, push to ai-bridge.
