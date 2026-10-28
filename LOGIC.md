# LOGIC.md — RT64 (fork `synnz22/rt64`, branch `dlss-integration`)

This file explains how this fork of RT64 works and why its DLSS-related additions were designed the
way they were. It is a living document: sections are rewritten when the logic changes, and every
decision is dated. The detailed code map, phase plan, and verification results live in
[docs/DLSS_PLAN.md](docs/DLSS_PLAN.md).

## What RT64 is (unchanged upstream behaviour)

RT64 is an N64 graphics renderer used by native ports such as Zelda64Recomp. It never draws a game's
display list directly. Instead it:

1. **Records** every draw call of a game frame into a *workload* (deferred frames, `src/hle/rt64_workload.*`).
2. Runs a **native-resolution renderer** (`State::framebufferRenderer`) only when the game needs the
   framebuffer back in RAM, so gameplay that reads pixels stays exact.
3. Runs a **high-resolution renderer** (`WorkloadQueue::framebufferRenderer`) that replays the same
   draw calls at a scaled resolution and, with High Framerate (HFR) on, several times per game frame.
4. **Matches** draw calls between consecutive game frames (`GameFrame::match`) so the high-res renderer
   can interpolate transforms and produce the extra HFR frames.
5. Transforms vertices on the GPU in a compute shader (`RSPProcessCS.hlsl`), rasterises with one
   "ubershader" per combiner configuration, and presents through a Video Interface pass.

The GPU backend is the external `plume` library (`src/contrib/plume`): D3D12, Vulkan, Metal.

## Decisions on this branch

### 2026-10-27 — Base the DLSS work on rt64 `main` (d9817c6), not the commit Zelda64Recomp pins
Zelda64Recomp pins an older RT64 (23cab603, 2025-08) that still had an in-tree GPU backend. The
newer `main` moved the backend into `plume`. Working on `main` keeps the fork close to upstream;
the cost is a small port of Zelda64Recomp (plume namespace, one include path, wider
`gEXMatrixGroup` macros), kept as a local branch in the recomp checkout.

### 2026-10-27 — DLSS is a Streamline integration loaded at runtime, D3D12 first
NVIDIA's Streamline SDK is the supported route to DLSS Super Resolution. It is loaded with
`LoadLibrary` at runtime so the build runs unchanged on AMD/Intel/non-RTX GPUs and on Linux
(where the option simply reports "unsupported"). Build option `RT64_ENABLE_DLSS` is OFF by default
and the SDK headers are private to one translation unit, so no public RT64 header changes with the option.
Vulkan DLSS needs a change inside plume (no extension/queue injection point) and is deferred.

### 2026-10-27 — Everything new is gated to the high-res renderer and is off by default
Jitter, motion vectors and DLSS are enabled only through flags that `WorkloadQueue` sets; the
native renderer shares the same `FramebufferRenderer` class but never sees them. With the
features off, the output must be pixel-identical to upstream. RT64's rule of "no game-specific
hacks" applies: nothing here knows about Majora's Mask.

### 2026-10-27 — DLAA first, scaling modes second
DLAA (render size = output size) needs no resolution plumbing and proves the whole chain
(inputs, constants, history resets). Quality/Balanced/Performance come afterwards by rendering the
3D scenes into a smaller region of the same grow-only render targets.

### 2026-10-28 — Motion vectors come from what RT64 already computes, in one extra small pass
RT64 matches draw calls between game frames and already uploads each object's *previous* world transform; it also computed the
previous view-projection on the CPU but never uploaded it. The high-res renderer now (only when motion vectors are on):
1. uploads those previous view-projection matrices (`ProjectionProcessor::upload`, `uploadPrev`);
2. runs `RSPPrevPosCS`, a sibling of the vertex compute shader, that writes each vertex's previous-frame screen position into
   `OutputBuffers::prevScreenPosBuffer` (the native renderer keeps its own descriptor set untouched);
3. splits the framebuffer pair's raster scene right after the last perspective projection that writes depth, and draws those
   draws again with `MotionVectorVS`/`MotionVectorPS` into an R16G16_FLOAT texture using the depth buffer read-only with an EQUAL
   test, so only the visible surface writes its vector (previous minus current position, in pixels).
Unmatched objects bind the current transforms as "previous", which yields camera-only motion for free. With high framerate
interpolation the "previous frame" is the frame that was actually rendered last (`WorkloadQueue::mvState`), not the tick-derived
weight RT64 uses elsewhere, which drifts when frames are skipped. The ubershader uses dual-source blending, which is why the
vectors cannot be a second render target of the main pass. The pass is skipped under MSAA.

### 2026-10-28 — Jitter goes through the existing per-draw screen offset, perspective projections only
`RasterVS` already applies a per-draw NDC translation (`RasterParams::screenOffset`, the half-pixel offset) after the N64 screen
space mapping, so a sub-pixel jitter is just an extra NDC offset `(2*jx/viewportWidth, -2*jy/viewportHeight)` added to it per
projection in `addFramebuffer`; no shader or push-constant layout change. It is added only for `Projection::Type::Perspective`,
because orthographic projections, rects and raw triangles are the game's 2D content (HUD, text boxes, framebuffer effects) and
a temporal upscaler wants those unjittered. The sequence is Halton (2,3) indexed by the renderer's displayed-frame counter
(`HaltonJitter`), with `HaltonJitterPhaseCount` = 8 x (display / render)^2 as DLSS recommends (8 for DLAA). Nothing is added at
all when jitter is off, which keeps the off path bit-identical; the motion vector pass always uses the unjittered offset.

### 2026-10-28 — Developer shortcuts F6 / F9 / F10 (motion vectors, jitter, pause)
The inspector checkboxes are the primary toggles, but keys make testing from scripts and from ports without a mouse-driven
inspector practical. Like F1-F4 they only work with `developerMode` on. F5/F7/F8/F11 are avoided because Zelda64Recomp uses
them (quick-save, quick-load, UI debugger, fullscreen). F10 toggles the same `DebuggerInspector::paused` flag as the debugger
tab's Pause button, which otherwise only reacts to F4 while that tab is open.

### 2026-10-27 — Add a GPU preference setting
plume picks the D3D12 adapter with the most VRAM, which on a two-GPU machine can be the slower
card or one not driving the monitor. `UserConfiguration::deviceName` (empty = unchanged behaviour)
is forwarded to plume's existing `createDevice(preferredDeviceName)`.
