# Changelog — RT64 fork `synnz22/rt64`, branch `dlss-integration`

All notable changes on this branch. Newest first. Upstream RT64 has no version number; this branch
starts at 0.1.0.

## [0.4.0] - 2026-10-29

### Added
- NVIDIA DLSS through the Streamline SDK, DLAA mode (Phase 3 of the DLSS plan): the 3D scene is anti-aliased by DLSS at full
  resolution, before the HUD and 2D elements are drawn, so those stay pixel exact. Build option `RT64_ENABLE_DLSS` (off by
  default) plus `RT64_STREAMLINE_SDK_DIR`; `sl.interposer.dll` is loaded at runtime from the executable's directory after its
  NVIDIA signature is verified, so a build with the option on still runs without the DLLs or on non-RTX GPUs (the option
  then reports why it is unavailable). D3D12 only for now.
- `upscaler` setting (`None`, `DLAA`, `DLSSQuality`, `DLSSBalanced`, `DLSSPerformance`; JSON key `upscaler`), always present
  in the configuration whatever the build. A "DLSS" combo in the inspector's configuration tab (greyed out with the reason
  when unavailable) and the F12 developer shortcut toggle DLAA at runtime without restarting.
- DLSS history resets on camera cuts (a camera whose transform group skips interpolation, or that could not be matched to
  the previous frame), when the previous displayed frame did not go through DLSS (pause menu, scene without 3D content),
  and when the mode or output size changes.
- `DynamicLibraries::moduleDirectory()` helper (the executable's directory).

### Changed
- Nothing in the output when the upscaler is `None`: the native renderer and the high resolution renderer are untouched
  unless DLSS is active. DLSS is inactive while MSAA is on (its depth input must be single sampled); the application decides
  how to resolve that (Zelda64Recomp turns MSAA off when DLSS is selected).

### Known limitations
- The scaling modes (Quality / Balanced / Performance) are accepted by the setting but render at full resolution like DLAA
  until the lower internal render size is implemented (next checkpoint).
- Vulkan: DLSS reports itself unavailable (needs a plume change to inject Streamline's device extensions and queues).

## [0.3.0] - 2026-10-28

### Added
- Sub-pixel camera jitter for the high resolution renderer (Phase 2 of the DLSS plan): a Halton (2,3) offset per displayed
  frame, applied only to perspective (3D) draws and never to 2D elements or the HUD. Off by default; when off the output is
  bit-identical to before (verified on a frozen frame).
- "Force Jitter" checkbox next to "View Motion Vectors" in the debugger tab, and the F9 developer shortcut for it.
- F10 developer shortcut: pause/resume rendering (the same flag as the debugger tab's Pause button) so a frame can be frozen
  from the keyboard.

### Changed
- The motion vector pass uses the unjittered positions, so jitter never leaks into the motion vectors.
- Developer shortcuts: F6 motion vectors, F9 jitter, F10 pause (F5/F7 are left to the recomp's quick-save/quick-load keys).

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
