# Changelog — RT64 fork `synnz22/rt64`, branch `dlss-integration`

All notable changes on this branch. Newest first. Upstream RT64 has no version number; this branch
starts at 0.1.0.

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
