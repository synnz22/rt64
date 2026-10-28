# Changelog — RT64 fork `synnz22/rt64`, branch `dlss-integration`

All notable changes on this branch. Newest first. Upstream RT64 has no version number; this branch
starts at 0.1.0.

## [0.2.0] - 2026-10-28

### Added
- Motion vectors for the high resolution renderer (Phase 1 of the DLSS plan): a per-pixel texture of where each pixel was in
  the previously displayed frame, computed from RT64's draw-call matching (objects) and the previous camera (everything else),
  correct with high framerate interpolation on or off. Off by default; it costs nothing when off.
- "View Motion Vectors" checkbox in the debugger tab of the inspector (developer mode) and the F6 developer shortcut that
  toggles it: hue = direction, brightness = magnitude, white lines every 32 px point to the previous position.
- `RSPPrevPosCS` compute shader, `MotionVectorVS`/`MotionVectorPS` pass and `MotionVectorDebugPS` overlay.

### Changed
- The previous view-projection matrices RT64 already computed are now uploaded to the GPU when motion vectors are on.

### Known limitations
- The pass is skipped while MSAA is on (DLSS needs single-sample depth anyway).
- Only perspective draws that write depth produce vectors; decals, transparent surfaces, 2D and HUD get none.

## [0.1.0] - 2026-10-27

### Changed
- `CMakeLists.txt`: `project(rt64 VERSION 0.1.0 ...)` so the manifest carries this branch's version.

### Added
- `UserConfiguration::deviceName` (JSON key `deviceName`, default empty): the exact name of the GPU the renderer
  should use, forwarded to plume's `createDevice(preferredDeviceName)`. Empty keeps the old behaviour (largest VRAM on D3D12).
- `docs/DLSS_PLAN.md`: reconnaissance of the renderer, the step-by-step plan for DLAA/DLSS
  Super Resolution (motion vectors, jitter, Streamline integration, verification), risks, and the
  DLSS 5 note.
- `LOGIC.md` and this changelog.
- `.gitignore` entries so an out-of-tree Streamline SDK or ROM can never be committed.
