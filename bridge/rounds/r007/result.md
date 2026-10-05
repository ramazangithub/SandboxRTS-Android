# Round 007 - result from TESTER

## Summary
- **Status**: Partial success -> crash loop on swapchain + letterboxing.
- **Root Cause 1 (Crash)**: DXVK swapchain recreation loop. On Android, SurfaceFlinger repeatedly returns `VK_SUBOPTIMAL_KHR`. DXVK's default presenter treated this as reason to recreate the swapchain 5-6 times per second, triggering out-of-memory in `DxvkMemoryAllocator::createAllocation` / `AllocSlice` after 10-15 seconds.
- **Root Cause 2 (Display)**: Screen was letterboxed to 2264x996 due to display cutout insets (notch/camera hole).
- **Resolution**: Both issues were analyzed, patched, and built in commit `95a9201` on `main` as release `android-v0.7.1-r008`. See Round 008 for full test results, FPS profiles, and stability verification.
