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

### 2026-10-29 — Streamline is loaded at runtime, in manual hooking mode, with only the DXGI factory proxied
`src/render/rt64_dlss_streamline.cpp` is the only file that includes a Streamline header; the build option `RT64_ENABLE_DLSS`
turns into the private define `RT64_DLSS_ENABLED` on the rt64 target only, and the header (and `UserConfiguration::upscaler`)
look the same in both builds, so an application linking RT64 never changes shape with the option. Nothing is linked:
`sl.interposer.dll` is loaded from the executable's directory with `LoadLibrary` after `sl::security::verifyEmbeddedSignature`
(NVIDIA's shipping requirement, and it rejects a tampered DLL), and every entry point comes from `GetProcAddress`. Streamline
runs in manual hooking mode so plume stays untouched: `slInit` after the device exists (allowed on D3D12), `slSetD3DDevice`,
`slIsFeatureSupported` with the adapter's LUID. Manual hooking still requires Streamline to see every Present for its per-frame
bookkeeping, and the documented way is to create the swap chain through a Streamline factory proxy, so `attachDevice` swaps
plume's `D3D12Interface::dxgiFactory` for the proxy before the swap chain exists; the proxy forwards everything else and plume
releases it like the original. The DLL is never unloaded for that reason. Over the air updates are off (what ships is what
runs; the NVIDIA driver's own global DLSS override can still substitute its copies, which is the user's choice). Streamline's
log goes to RT64's data directory (`sl.log`). Any failure along the way only leaves the option reported as unavailable with
a plain-language reason; rendering never depends on it.

### 2026-10-29 — DLAA runs after the motion vector pass, into its own texture, copied back over the frame region
DLSS evaluates on the same scene boundary the motion vector pass uses (after the last perspective projection that writes
depth), so the HUD and all 2D content drawn afterwards land on top of the anti-aliased 3D frame untouched. The colour target
cannot be the output (it is the input, and plume creates it without unordered access), so DLSS writes a texture of the same
size and format (`DLSSStreamline::OutputTarget`, recreated on size or format change) and the frame region is copied back with
`copyTextureRegion`. Inputs: the colour target, the single sampled `D32_FLOAT` depth (which is why DLSS is inactive under
MSAA), the `R16G16_FLOAT` motion vectors in pixels (previous minus current, y down, `mvecScale = 1/size`), and the Phase 2
jitter in pixels. The camera constants come from the *main* perspective projection (the one with the most depth-writing
draws), not from the marker projection, because a frame can end its 3D content with a small secondary projection. RT64's
matrices are row-major in the row vector convention, which is exactly what Streamline documents, so they are passed as
stored (`cameraViewToClip` = the interpolated projection, `clipToPrevClip` = inverse(viewProj) x prevViewProj). The 16-bit
UNORM targets the recomp uses by default are accepted by DLSS directly. After `slEvaluateFeature` the pass only invalidates
plume's cached descriptor heap, topology and stencil state; the next raster scene re-binds everything else itself.

### 2026-10-29 — History resets come from the camera's transform group and from gaps, not from the matrix ID alone
Majora's Mask tags its camera with one constant ID and marks a cut by asking the extended GBI to *skip* interpolating the
camera's position and rotation that frame, so `viewProjections[i].mapped` stays true across cuts. The reset rule is therefore:
the main projection's group has an explicit ID and skips position or rotation interpolation, or the projection could not be
matched at all, or the previous frame was not matched (the motion vector tracker's reset), or the previous displayed frame
did not run DLSS (pause menu, no 3D content, first frame), or the output texture or mode changed. A frame frozen with the
debugger's pause does *not* reset: it re-renders the same image with zero motion, which is exactly what a temporal filter
wants, and it makes frozen-frame comparisons meaningful. The frame map is indexed by the workload's queue index, not by its
position in the frame (a mistake that made every frame look like a cut until fixed).

### 2026-10-29 — Streamline counts frames itself and the inputs are tagged "valid until evaluate"
Passing RT64's own displayed-frame counter to `slGetNewFrameToken` and tagging the inputs as "only valid now" made
Streamline's common plugin clone all four textures into a ring of frames every frame (and log that it cannot size 16-bit
UNORM and D32 resources). The inputs are consumed by the evaluate call that immediately follows and reused by the renderer
afterwards, so `eValidUntilEvaluate` is the truthful lifecycle; with it and Streamline's own present-based frame counting
there are no copies. `slAllocateResources` is called when the options are set so the feature is created up front, and
`slFreeResources` runs as soon as DLSS is turned off (the workload thread waits for the GPU every frame, so this is safe).

### 2026-10-29 — The DLSS setting is generic; applications decide how it interacts with MSAA
`UserConfiguration::upscaler` is a plain enum with JSON, default `None`; `WorkloadConfiguration::upscalerActive` is true only
when the build and the device support DLSS and MSAA is off. RT64 does not force MSAA off itself because changing the sample
count rebuilds the ubershaders through `Application::updateMultisampling`, which the embedding application drives; the
inspector shows a note instead and Zelda64Recomp's menu turns MSAA off when DLSS is chosen. The F12 developer shortcut toggles
DLAA through the same configuration update a menu uses, for testing without one.

### 2026-10-27 — Add a GPU preference setting
plume picks the D3D12 adapter with the most VRAM, which on a two-GPU machine can be the slower
card or one not driving the monitor. `UserConfiguration::deviceName` (empty = unchanged behaviour)
is forwarded to plume's existing `createDevice(preferredDeviceName)`.
