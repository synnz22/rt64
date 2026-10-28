# DLAA / DLSS Super Resolution for RT64 (Zelda64Recomp, Majora's Mask)

Status: **Phases 0-3 (DLAA) complete (2026-10-27/29).** Phase 3's scaling modes checkpoint and Phase 4 not started. The SDK is unpacked at `K:\Git\MM\sdk\streamline-sdk-v2.14.16\` (configure with `-DRT64_ENABLE_DLSS=ON -DRT64_STREAMLINE_SDK_DIR=K:/Git/MM/sdk/streamline-sdk-v2.14.16`). Each phase ends at a
CHECKPOINT; nothing past a checkpoint starts without sign-off.

Branch: `synnz22/rt64` → `dlss-integration`, based on upstream rt64 `main` d9817c6 (2026-10-23).
Zelda64Recomp (`K:\Git\MM\Zelda64Recomp`, local branch `dlss-integration`, never pushed) points its
`lib/rt64` submodule at this branch.

## 1. Goal and rules

Add NVIDIA DLSS Super Resolution with a DLAA mode (native-resolution input, temporal anti-aliasing
quality) plus Quality / Balanced / Performance scaling modes, so the game looks like a clean native
4K title. Constraints, all from RT64's own philosophy:

- no game-specific hacks: every change is generic and off by default;
- the native-resolution renderer stays byte-identical;
- with the feature off, the high-res output is pixel-identical to upstream;
- DLSS 5 (Neural Rendering) is out of scope (see section 9).

## 2. Decisions (2026-10-27)

| Decision | Choice | Why |
|---|---|---|
| Base commit | rt64 `main` d9817c6, not the recomp's pinned 23cab603 (2025-08-25) | Closer to upstream. 23cab603 is not an ancestor of main (it sits on a side branch); the real delta is every commit since `facf03e` "Switch to using Plume instead of the RHI". Cost: a small port of the recomp (section 3.2). |
| Forks | Only `rt64/rt64` is forked. Recomp + N64ModernRuntime changes stay local. | User's choice. |
| Upscaler SDK | NVIDIA Streamline (DLSS plugin), loaded at runtime, manual-hooking mode, DLSS SR only, D3D12 first | Windows-only by nature; silent fallback elsewhere. Vulkan needs a plume change, so it gets its own later checkpoint. |
| Order | DLAA first, then scaling modes | DLAA needs no resolution plumbing and proves inputs, constants and resets. |
| GPU | New `UserConfiguration::deviceName` forwarded to plume `createDevice(preferredDeviceName)`; the recomp sets it to the RTX 4080 | plume picks the largest-VRAM adapter (the RTX 3090 Ti on this machine). |
| ROM / binaries | Never committed; `.gitignore` covers `sdk/`, `*.z64`, `*.n64`, `*.v64`, `*.elf`, `nvngx_*.dll`, `sl.*.dll` | As requested. |

## 3. Phase 0 — what was done

### 3.1 Toolchain findings (Windows 11, RTX 4080 + RTX 3090 Ti, driver 621.01)

- **Compiler:** VS 2022 BuildTools `clang-cl` 19.1.5 + Ninja + MSVC runtime (`vcvars64.bat` of the BuildTools install).
- **MIPS patches:** the VS-bundled clang has **no MIPS backend** (`--print-targets` lists none; it rejects
  `-mno-check-zero-division`). As the recomp's CI does, portable **LLVM 19.1.3** from the llvm-project GitHub
  release is used for `patches/`: only `clang.exe` and `ld.lld.exe` were extracted, to
  `K:\Git\MM\tools\llvm-19.1.3\LLVM-19.1.3-Windows-X64\bin`.
- **make:** not installed; `K:\Git\MM\tools\bin\make.exe` is GNU Make 4.4.1 copied from the WinLibs package already
  on the machine (with its `libintl-8.dll` and `libiconv-2.dll`). The recomp's `PatchesBin` target calls `make` by name.
- **Recompiler:** `Mr-Wiseguy/N64Recomp` at `a13e5cff` (the commit the recomp's CI pins), built with MSVC + Ninja
  into `K:\Git\MM\N64Recomp\cmake-build`; `N64Recomp.exe` and `RSPRecomp.exe` copied to the recomp root (git-ignored).
- **ROM:** the decompressed US ROM is produced by the pinned decomp's `lib/mm-decomp/tools/fixbaserom.py`
  (needs the `libyaz0` package: `uv run --with libyaz0 python -I fixbaserom.py` in a folder holding the ROM as
  `baserom.mm.us.rev1.z64`), then placed as `mm.us.rev1.rom_uncompressed.z64` at the recomp root (git-ignored).
  Original ROM SHA-1 `d6133ace5afaa0882cf214cf88daba39e266c078`. The `decompress_baserom.py` named in the build
  guide does not exist at the pinned decomp commit.
- **Configure and build** (from a BuildTools `vcvars64` shell with the two tool folders prepended to `PATH`):

  ```
  cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_C_COMPILER=clang-cl
        -DCMAKE_MAKE_PROGRAM=ninja -G Ninja -S . -B cmake-build
        -DPATCHES_C_COMPILER=K:/Git/MM/tools/llvm-19.1.3/LLVM-19.1.3-Windows-X64/bin/clang.exe
        -DPATCHES_LD=K:/Git/MM/tools/llvm-19.1.3/LLVM-19.1.3-Windows-X64/bin/ld.lld.exe
        -DCMAKE_CXX_FLAGS="-Xclang -fexceptions -Xclang -fcxx-exceptions"
  cmake --build cmake-build --config RelWithDebInfo --target Zelda64Recompiled
  ```

  SDL2 2.30.3 is fetched by the recomp's CMake. The private CI shader cache (`mm_shader_cache.bin`) is not
  available; RT64's ubershaders make that a performance-only difference.

### 3.3 GPU preference (`deviceName`)

`UserConfiguration::deviceName` (`src/common/rt64_user_configuration.*`, JSON key `deviceName`, default empty) is forwarded to
both `createDevice()` calls in `src/hle/rt64_application.cpp`. plume compares it exactly against the adapter description and
stops enumerating on a match, so enumeration order does not matter. The recomp reads/writes a `device_name` key in `graphics.json`
(`src/game/config.cpp`, accessors in `include/zelda_config.h`) and applies it in `RT64Context` right before `app->setup()`
(`src/main/rt64_render_context.cpp`). On this machine it is set to `NVIDIA GeForce RTX 4080`, the GPU driving the primary display.

### 3.2 Porting Zelda64Recomp to rt64 main (local branch `dlss-integration`)

- `CMakeLists.txt`: keep `lib/rt64/src/rhi` (still holds `rt64_render_hooks.*`), **add** `lib/rt64/src/contrib/plume`.
- `src/ui/ui_renderer.h`: forward declarations moved to `namespace plume`.
- `src/ui/ui_renderer.cpp`: drop `#include "rt64_render_interface_builders.h"` (comes via plume);
  `RT64::Render*` to `plume::Render*` (RT64's `using namespace plume` is at global scope, so `RT64::`-qualified
  names no longer resolve); `createCommandList(RT64::RenderCommandListType::COPY)` to `createCommandList()`.
- `src/ui/ui_state.cpp`, `src/main/rt64_render_context.cpp`: the same namespace change.
- `patches/patches.h` and 20 direct `gEXMatrixGroupSimple` calls: upstream widened `gEXMatrixGroup`
  (+`aspect, tc, lookat`), `gEXMatrixGroupSimple` and `gEXMatrixGroupDecomposed` (+`tc, lookat`). Appended
  `G_EX_ASPECT_AUTO` / `G_EX_COMPONENT_SKIP` as applicable: all encode zero bits, i.e. the old behaviour.
- Unchanged: `Application`'s public API, `include/zelda_render.h`, `src/game/config.cpp`, N64ModernRuntime,
  `PresentWait` (defaults to Automatic).
- Expect small visual differences vs the shipped recomp: upstream changed `removeBlackBorders` defaults and aspect
  detection after the pin. The "stock" baseline for every pixel-exact comparison in later phases is **this** build.

## 4. Code map (verified on rt64 main d9817c6)

### 4.1 Two renderers, one class
- Native: `State::framebufferRenderer` (`src/hle/rt64_state.cpp`), `resolutionScale (1,1)`, runs only with `renderToRDRAM`.
- High-res: `WorkloadQueue::framebufferRenderer` (`src/hle/rt64_workload_queue.cpp`). Both use
  `FramebufferRenderer` (`src/render/rt64_framebuffer_renderer.*`), whose `DrawParams` has no "high-res" flag today.
  Every DLSS/MV/jitter change is gated by a new flag that only the workload queue sets. New `DrawParams` fields
  need default member initializers (`State` declares a `DrawParams` without zeroing, `rt64_state.cpp:1286`).

### 4.2 High-res frame (`WorkloadQueue::threadRenderFrame`)
Projection/transform/tile/look-at processors, then `RSPProcessCS` (+`RSPWorldCS` when matched), then `getTargetsFromPair`
sizing, then per framebuffer pair: `fbManager.recordOperations` (tile copies), RDRAM-to-target syncs,
`FramebufferRenderer::recordFramebuffer` (about L792), colour/depth to `SHADER_READ`, MSAA resolve into the HFR override
target, end operations; one `execute` + blocking `wait` per frame. The presented pair is *guessed* at render time
(`earlyPresentCandidate`, then `interpolationTargetKey` / `interpolationTargetFbPairIndex`, passed as
`overrideTargetFbPairIndex`). Present (`src/hle/rt64_present_queue.cpp`) only sees the colour target: downsample or
resolve, `VIRenderer::render`, RmlUi draw hook, inspector, present.

### 4.3 Scenes, 2D vs 3D, the pre-HUD insertion point
`Projection::type` is Perspective/Orthographic/Rectangle/Triangle (perspective when `proj[3][3]==0`,
`src/hle/rt64_rsp.cpp`); `FramebufferPair::projections` is an ordered run-length list with 3D and 2D interleaved.
In raster mode `addFramebuffer` (about L1319) turns the whole pair into **one `RasterScene`** (`checkRasterScene` about L1396);
`recordFramebuffer` (about L1231) loops `targetDrawCall.sceneIndices` calling `submitDepthAccess` + `submitRasterScene`
(about L468) per scene. The HUD draws into the same colour target and normally the same pair. **Insertion point** = split
the scene after the last Perspective projection with a `zUpd` draw; the `#if RT_ENABLED` "interleaved raster"
block in `recordFramebuffer` (about L1254-1293) is the template for running an external pass between scenes
(barriers, `setFramebuffer`, `submitDepthAccess(readOnly=true)` leaves `depthState` so the next scene re-binds).
`submitRasterScene` re-issues pipeline layout, descriptor sets and viewport itself (`switchToGraphicsPipeline` about L477-513).

### 4.4 Depth
`RenderTarget` of type Depth keyed by depth-image address; **`D32_FLOAT`, MSAA when MSAA is on, no resolved copy**
(`src/render/rt64_render_target.cpp`), clear 1.0, compare LESS (not reversed), shared across colour targets, grow-only.
Read in-pair by decals (`gBackgroundDepth` in `RasterPS.hlsl`), `RSPVertexTestZCS`, and depth-to-colour copies
(`RtCopyDepthToColorPS`). Main also clips the far plane in the pixel shader (`MaxDepth = 1022/1024`).
**MSAA must be off while DLSS is on** (DLSS needs single-sample depth).

### 4.5 Vertex pipeline and the jitter point
`RSPProcessCS.hlsl` writes `screenPos = ndc*viewport.scale + viewport.translate, clip.w` (N64 screen space) with
per-vertex interpolation `pos - vel*(1-curFrameWeight)` and texcoord velocity; `RSPModifyCS.hlsl` applies
`G_MWO_POINT_*` overrides. `RasterVS.hlsl:19-25` maps screen space to NDC and then applies the per-draw push
constants `RasterParams {renderIndex, padding, screenScale, screenOffset}` (`src/shared/rt64_raster_params.h`):
`ndc.xy = ndc.xy*screenScale + screenOffset*w`, so `screenOffset` is a pure NDC translation, API-independent.
`addFramebuffer` fills `triangles.screenOffset` per projection (about L1481-1503) and per rect (about L1657). A pixel jitter
`(jx, jy)` (x right, y down, render pixels) becomes `jitterNDC = (2*jx/vpW, -2*jy/vpH)` added to `screenOffset`
for Perspective `IndexedTriangles` only. `frameParams.frameCount` increments once per displayed frame including
HFR frames (`FramebufferRenderer::advanceFrame` about L1835). `HaltonSequence` / `HaltonJitter` exist in `src/common/rt64_common.h`.

### 4.6 Matching, interpolation, and the motion-vector source
- `DrawData` (`src/hle/rt64_workload.h`): model matrices `worldTransforms` (+ `prevWorldTransforms`, `lerpWorldTransforms`),
  per-projection `view/proj/viewProjTransforms` (+ interpolated `mod*` and previous-slot `prev*`), `rspViewports`,
  per-vertex `posFloats`, `velFloats`, `tcVelFloats`, `worldIndices`, `viewProjIndices`.
- **Uploaded every displayed frame:** `worldTransformsBuffer` (interpolated), `prevWorldTransformsBuffer` (at the previous
  slot's weight, `src/render/rt64_transform_processor.cpp`), `viewProjTransformsBuffer` (= `modViewProjTransforms`,
  `src/render/rt64_projection_processor.cpp` about L150-162). **Computed but never uploaded:** `prevViewProjTransforms`
  (about L145). **Computed but unused:** `OutputBuffers::worldVelBuffer` (`RSPWorldCS.hlsl`).
- Matching (`GameFrame::match` / `matchScene`, `src/hle/rt64_game_frame.cpp`) is per *world transform* and per
  *projection slot*: `GameFrameMap::WorkloadMap{viewProjections[], transforms[] ({rigidBody, prevTransformIndex, mapped}), tiles[]}`.
  Unmatched = `mapped == false`. Cameras are `TransformGroup`s from `gEXMatrixGroup(... proj ...)`; `G_EX_ID_AUTO`
  camera groups are **always** mapped (hard cuts produce bogus motion), explicit IDs map only on equal ID.
  Matching runs only when `targetRate > 0 || raytracingEnabled` (`requiresFrameMatching`, `rt64_workload_queue.cpp:976`).
- Weights `prevFrameWeight` / `curFrameWeight` (about L1083-1085) are **tick-derived**: wrong after `frameReduction`
  (about L1007), in-pair skips (about L1069), `skipWorkloadNow` (about L1135), meaningless when paused (about L1104). Within a pair,
  weight 0 of a new pair equals weight 1 of the old pair exactly unless the old pair ended early.
- **Fallback is free:** for an unmatched transform `prevWorldTransforms == lerpWorldTransforms`, so
  `prevViewProj * prevWorld * pos` yields camera-only motion; for an unmatched camera `prevViewProj == viewProj`.
- The ubershader uses **dual-source blending** (`SV_TARGET0/1`, `SRC1_ALPHA`), so a motion-vector output cannot be a second
  render target of the main pass; it needs its own small pass.
- Cut-detection levers: camera unmapped / ID changed, `gEXMatrixGroupNoInterpolate`, `RigidBody::lerpTranslation == false`,
  unmatched fraction, scene count change, `prevFrame.matched == false`, pair incompatibility, config change, paused.

### 4.7 plume handles (public fields; `static_cast` pattern as in `src/gui/rt64_inspector.cpp`)
D3D12: `D3D12Device::d3d` (ID3D12Device8*), `::adapter` (IDXGIAdapter1*, LUID via `GetDesc1`), `D3D12Interface::dxgiFactory`,
`D3D12CommandQueue::d3d`, `D3D12CommandList::d3d` (+ cached `descriptorHeapsSet` / `activeTopology` / `activeStencilRef` and
`notifyDescriptorHeapWasChangedExternally()`), `D3D12Texture::d3d` + tracked `resourceStates` / `layout` / `desc`, `D3D12SwapChain::d3d`.
Vulkan: `VulkanInterface::instance` (API 1.2), `VulkanDevice::vk` / `physicalDevice` / `queueFamilyIndices`,
`VulkanCommandQueue::queue->vk` / `mutex` + `familyIndex` / `queueIndex`, `VulkanCommandList::vk` + `endActiveRenderPass()`,
`VulkanTexture::vk` / `imageView` / `imageFormat` / `allocationInfo` / `textureLayout` / `barrierStages`. `toTextureState` in `plume_d3d12.cpp`
maps `RenderTextureLayout` to D3D12 states (GENERAL maps to UAV only with the UNORDERED_ACCESS flag); `barriers()` diffs tracked
state and writes it back. `copyTexture` is a raw `CopyResource` (same size/format; the caller barriers to COPY_SOURCE/DEST).
Adapter selection: D3D12 largest `DedicatedVideoMemory` unless `preferredDeviceName` matches (`plume_d3d12.cpp` about L3741-3782);
Vulkan first discrete GPU. `RenderInterface::createDevice(const std::string &preferredDeviceName = "")` exists.
Hooks: `RT64::SetRenderHooks(init, draw, deinit)` are single global slots the recomp owns (RmlUi); init fires *after* the
device exists (`rt64_application.cpp` about L306), so Streamline needs its own pre-device step in `Application::setup`.
Dead `DLSS_ENABLED` vestiges exist in `plume_vulkan.cpp` and `rt64_dynamic_libraries.cpp`; **never define that macro**.

### 4.8 Build, config, debugger, recomp plumbing
- `CMakeLists.txt`: shaders via `build_pixel_shader` / `build_vertex_shader` / `build_compute_shader(rt64 "src/shaders/X.hlsl")`,
  dxc, then `file_to_c` into `X.hlsl.dxil.h` / `.spirv.h`, consumed in `src/render/rt64_shader_library.cpp`. Definitions and include
  dirs are directory-scoped (they do not propagate to the recomp exe). No `-Werror` on main.
- Dormant scaffolding to reuse: `Upscaler` interface (`src/render/rt64_upscaler.h`: `QualityMode`, `UpscaleParameters{inColor,
  inFlow, inDepth, outColor, jitterX/Y, resetAccumulation...}`), `UpscaleMode{Bilinear,FSR,DLSS,XeSS}` (`rt64_common.h`),
  the RT-era "Visualization Mode: Motion vectors" combo and `DebugPS.hlsl` arrows, `DynamicLibraries::load()` (loads DLLs from the
  exe's directory; failures silently ignored and `Application::setup` still returns Success).
- Config apply path: `Application::updateUserConfig`, `SharedQueueResources::setUserConfig`,
  `WorkloadQueue::threadConfigurationUpdate` (destroys targets on framebuffer-config change); MSAA change goes through
  `Application::updateMultisampling()`. Graphics API, HDR, hardware resolve and buffer count apply only at `setup()`.
- Debugger: F1 with `developerMode` (recomp: `"developer_mode": true` in `graphics.json`); views in
  `src/gui/rt64_debugger_inspector.cpp`; `DebuggerRenderer` in `src/hle/rt64_workload.h`.
- Recomp glue: `src/main/rt64_render_context.cpp` (`set_application_user_config`, `RT64Context::update_config`, statics
  `RT64MaxMSAA` etc. feeding the menu's "supported" flags); `GraphicsConfig` + enums + JSON maps live in the
  **N64ModernRuntime submodule** (`ultramodern/include/ultramodern/config.hpp`, change detection via defaulted `operator<=>`);
  persisted by `src/game/config.cpp`; menu bindings in `src/ui/ui_config.cpp`; markup `assets/config_menu/graphics.rml`.
  The recomp's CMake copies `SDL2.dll`, `dxil.dll`, `dxcompiler.dll` next to the exe (the model for the four Streamline DLLs).
- `ultramodern::get_resolution_scale()` reaches game patches only for dither-noise strength (`patches/effect_patches.c`).

## 5. Streamline / DLSS facts (v2.14.16, Oct 2026; re-verify against the downloaded headers before Phase 3)

- Download: https://github.com/NVIDIA-RTX/Streamline/releases (zip with `include/`, `lib/x64/`, `bin/x64/`). Put it at
  `K:\Git\MM\sdk\streamline\` (outside the repos). Runtime DLLs for SR: `sl.interposer.dll`, `sl.common.dll`, `sl.dlss.dll`,
  `nvngx_dlss.dll`, next to `Zelda64Recompiled.exe`.
- Load: `sl::security::verifyEmbeddedSignature`, `LoadLibrary(sl.interposer.dll)`, `GetProcAddress` for every `sl*` entry,
  `slInit(Preferences{PreferenceFlag::eUseManualHooking})` **before** plume creates the DXGI factory/device,
  `slSetD3DDevice(ID3D12Device*)`, `slIsFeatureSupported(kFeatureDLSS, AdapterInfo{LUID})`. Vulkan: `slGetFeatureRequirements`
  + `slSetVulkanInfo` (needs plume changes). `slShutdown` before destroying the device.
- Modes `sl::DLSSMode::{eDLAA, eMaxQuality, eBalanced, eMaxPerformance, eUltraPerformance}`; `slDLSSGetOptimalSettings` gives the render
  size; presets left `eDefault` (K for DLAA/Quality/Balanced); `sharpness` is deprecated; `colorBuffersHDR = eFalse` for SDR targets.
- Per frame: `slGetNewFrameToken`, `slSetConstants` (row-major matrices **without** jitter, `jitterOffset` in pixels,
  `mvecScale`, `depthInverted = eFalse`, `cameraMotionIncluded = eTrue`, `motionVectorsJittered = eFalse`, `reset`),
  `slSetTagForFrame` (`kBufferTypeScalingInputColor`, `kBufferTypeScalingOutputColor`, `kBufferTypeDepth`, `kBufferTypeMvec`,
  each `sl::Resource` with native pointer + current D3D12 state + `sl::Extent`), `slEvaluateFeature(kFeatureDLSS, token,
  {&viewport}, cmdList)` on the render thread, then restore descriptor heaps / topology / stencil ref.
- Motion vectors may be render-resolution, in pixels with `mvecScale = {1/renderW, 1/renderH}`, pointing from the current
  pixel to its previous-frame position. Sign conventions are confirmed empirically with the Streamline debug overlay.
- `interop::float4x4` is row-major with the N64 row-vector convention, exactly what `sl::Constants` wants:
  `cameraViewToClip = drawData.modProjTransforms[idx]` as stored, `clipToCameraView = inverse`; FOV/near/far via
  `fovFromProj` / `nearPlaneFromProj` / `farPlaneFromProj` (`src/common/rt64_math.cpp`).

## 6. Design in one paragraph

Everything new is gated by one flag only the high-res renderer sets. Three new per-frame products, all off by default: a
previous-position vertex stream (compute), a motion-vector texture (one extra small raster pass), and a jitter offset folded
into the existing per-draw `screenOffset`. DLSS runs inside the presented framebuffer pair, between its last 3D scene and its
first HUD scene, using the scene-splitting mechanism that already exists; HUD/2D draws then continue into the DLSS output.
DLAA first, scaling modes second. Streamline is loaded at runtime in manual-hooking mode for DLSS Super Resolution only; any
failure means the option reports "unsupported" and rendering is exactly stock.

## 7. Phase plan

### Phase 1 — Motion vectors (render-res, previous *displayed* frame to current). CHECKPOINT: debug-view screenshots
1. Flags: `DebuggerRenderer::viewMotionVectors` + inspector checkbox; `WorkloadConfiguration::motionVectors`;
   `requiresFrameMatching |= motionVectors` (works at "Original" refresh rate); `processProjections |= motionVectors`;
   `DrawParams::highRes=false, motionVectors=nullptr, jitterPixels={0,0}`.
2. `motionPrevWeight` tracker in the display loop: first rendered frame of a pair gives 0; later frames give the `curFrameWeight`
   of the last frame actually rendered; paused gives zero motion + reset; no HFR gives 0/1; `!prevFrame.matched` gives zero motion + reset.
3. Upload `prevViewProjTransforms` (`DrawBuffers::prevViewProjTransformsBuffer`, second `Upload` in `ProjectionProcessor::upload`).
4. Sibling compute shader `RSPPrevPosCS.hlsl` (+ `RSPPrevPosDescriptorSet`, `ShaderRecord rspPrevPos`,
   `OutputBuffers::prevScreenPosBuffer`, `RSPProcessor::ProcessParams::prevScreenPos` default off, second dispatch of the existing
   `RSPModifyCS` on the prev buffer). Formula `prevViewProj[vpIdx] * prevWorld[tfIdx] * (pos - vel*(1-prevFrameWeight))` through the
   *current* RSP viewport. When unmatched, bind the raw `worldTransforms` / `viewProjTransforms` buffers as "prev" (zero motion + reset).
5. Scene marker + MV pass: `perspective` / `depthWrite` bits in `InstanceDrawCall`; split after the last Perspective projection with a
   `zUpd` draw (`RenderTargetDrawCall::motionVectorSceneIndex`, -1 = skip); `src/render/rt64_motion_vectors.*` owned by
   `WorkloadQueue` (R16G16_FLOAT texture + framebuffer with the depth target read-only, keyed on `textureRevision`);
   `MotionVectorVS/PS.hlsl` with a 2-slot input layout (POSITION + PREVPOSITION), push constants `{screenScale,
   screenOffsetUnjittered, viewportSize}`, PS writes `(prevNdc.xy - curNdc.xy) * (0.5*vpW, -0.5*vpH)` (previous minus current,
   render pixels), depth test EQUAL, no depth write, no blend; only Perspective + `zUpd` + non-decal draws; presented pair only;
   skipped under MSAA. Fallback if EQUAL leaves holes: `LESS_EQUAL` + `depthBias = -1`.
6. Debug overlay at render time (`MotionVectorDebugPS.hlsl`: hue = direction, brightness = magnitude, sparse arrows) drawn after
   the MV pass, before the HUD scenes.
7. Validation: 60/120 Hz vectors about 1/3 and 1/6 of per-game-frame motion and continuous across interpolated frames; frame-skip
   stress; pause/unpause; scene transitions; `developer_mode` off means no change. Pixel-exact diff vs this Phase 0 build with the
   feature off after every step.

### Phase 2 — Jitter (perspective draws only). CHECKPOINT
1. `DebuggerRenderer::forceJitter`; per displayed frame `jitterPixels = HaltonJitter(frameCount, phaseCount)` stored on
   `WorkloadQueue` and passed in `DrawParams`.
2. In `addFramebuffer`: `jitterNDC = {2*jx/viewport.width, -2*jy/viewport.height}` added to `triangles.screenOffset` for
   `proj.type == Perspective` only; `screenOffsetUnjittered` kept for the MV pass. Rects, raw triangles and ortho untouched.
3. `phaseCount = 8*(D/R)^2` (DLAA gives 8). Off means pixel-identical output (verified by screenshot diff).

### Phase 3 — Streamline / DLSS (D3D12). CHECKPOINT (DLAA), then CHECKPOINT (scaling modes)
1. CMake: `option(RT64_ENABLE_DLSS OFF)` + `RT64_STREAMLINE_SDK_DIR`; PRIVATE sources/include/definition `RT64_DLSS_ENABLED`;
   `src/render/rt64_dlss_streamline.h/.cpp` is the only file including `sl*.h` (the header has inline stubs when the macro is absent);
   recomp POST_BUILD copy of the four DLLs. Test: both configurations build; DLLs renamed gives "Streamline unavailable" and stock rendering.
2. Init/shutdown in `src/hle/rt64_application.cpp`: `slInit` before `CreateD3D12Interface()` (about L162); `slSetD3DDevice` +
   `slIsFeatureSupported` after the driver-workaround block (about L269-289) that may recreate the device; `Application::upscalerSupported`
   getter; `slFreeResources` after `workloadQueue.reset()`, `slShutdown()` after `swapChain.reset()` and before `device.reset()`.
3. `UserConfiguration::Upscaler {None, DLAA, DLSSQuality, DLSSBalanced, DLSSPerformance}` + `upscaler` field (always present),
   JSON, `validate()`; `WorkloadConfiguration::upscaler` / `upscalerActive` (active = mode not None, supported, MSAA off, output
   format supported); lazy `slDLSSSetOptions` + `slDLSSGetOptimalSettings` on change + history reset; MSAA forced to None while on.
4. Scene split marker: `DrawParams::upscalerActive`, `upscalerFbPairIndex` (from `overrideTargetFbPairIndex`),
   `RenderTargetDrawCall::upscaleAfterSceneIndex`, `upscaleProjIndex` (-1 for pairs without perspective draws, e.g. the pause menu).
5. DLAA evaluate in `recordFramebuffer` via `submitUpscalerPass`: `submitDepthAccess(readOnly=true)`, `setFramebuffer(nullptr)`,
   barriers (colour to SHADER_READ, MV to SHADER_READ, output to GENERAL), `slGetNewFrameToken(frameCount)`, `slSetConstants`,
   `slSetTagForFrame`, `slEvaluateFeature`, invalidate plume's descriptor-heap/topology/stencil caches, barriers
   (output to COPY_SOURCE, colour to COPY_DEST), `copyTexture`, colour to COLOR_WRITE; the next scene re-binds via `submitDepthAccess(false)`.
   Output texture owned by `DLSSStreamline` (`Texture2D(colorTarget->width/height, format, STORAGE|UNORDERED_ACCESS)`, keyed on
   `textureRevision`). SDR `R8G8B8A8_UNORM` directly; the recomp's default high-precision `R16G16B16A16_UNORM` is tried first,
   with an `R16G16B16A16_FLOAT` output + `textureCopy` blit as fallback. Post-blend dither noise skipped while active.
6. Resets: previous rendered frame did not evaluate DLSS; `!prevFrame.matched`; the pair's camera unmatched
   (`viewProjections[idx].mapped == false`; MM tags its camera with an explicit ID and emits `gEXMatrixGroupNoInterpolation`
   on cuts); `workload.paused`.
7. Menu: extend ultramodern's `GraphicsConfig` with `Upscaler upscaler_option` (local submodule commit) so `operator<=>` drives
   change detection; recomp `config.cpp` (JSON, default Off), `rt64_render_context.cpp` (`to_rt64`, force MSAA None,
   `dlss_supported` static, `RT64DLSSSupported()`), `ui_config.cpp` (binding, `dlss_supported`), `graphics.rml` (new block between
   MSAA and HUD Placement, gated, nav chain re-threaded, help index shifted).
8. Scaling modes: per-projection resolution scale inside `addFramebuffer` (pre-marker projections render into the top-left
   R-region of the same grow-only targets: scaled `convertFixedRect` / `convertViewportRect`, a second `framebuffer.viewportRender`,
   a second `FbParams` set), SL input extent `{0,0,Rw,Rh}` / output `{0,0,Dw,Dh}`, MV pass at R, optional depth upsample R to D.
   Must be tested: tile copies at D while 3D samples at R (Lens of Truth, pause pre-render), `lastWriteRect` bookkeeping, the
   360-line scale-halving rule, `VertexTestZ`, `upscale2D` rects before the marker.
9. Vulkan (later, own checkpoint): plume change for extensions/queues, `slSetVulkanInfo`, Vulkan `sl::Resource` descriptors.

### Phase 4 — Verification
DLSS off vs DLAA (then each scaling mode): Clock Town walk + camera spin, Termina Field fog/distance, Lens of Truth,
grayscale cutscenes, Deku bubble, fire/ice arrows, bomb explosions, HUD/text boxes/pause menu/map; HFR at 60 and 120+.
Before/after screenshots into `docs/dlss_comparisons/`, frame times from the inspector's frametime plot; results and
known issues recorded in section 10.

## 8. Risks and known limitations

- Depth-equality in the MV pass (a different shader binary than the ubershader); fallback `LESS_EQUAL` + bias.
- Hard cuts with `G_EX_ID_AUTO` cameras are always "matched", giving one frame of bogus motion in untagged games; MM's camera tagging
  covers it here.
- Motion-vector / jitter sign conventions must be confirmed with the Streamline debug overlay (a wrong sign shows as shimmer).
- Texture scrolling and look-at interpolation change shading without geometric motion (water, skies), so ghosting is possible;
  a reactive mask is a later refinement. Per-frame dither noise is disabled while DLSS is on.
- D3D12 picks the largest-VRAM adapter; the `deviceName` setting fixes it. Vulkan picks the first discrete GPU.
- Three locally modified trees: this fork branch (pushed), the recomp branch and an N64ModernRuntime commit (local only).
- Licensing: NVIDIA's DLLs beside a GPL-3.0 executable is fine for a personal build; redistribution is the user's call.
- Vulkan DLSS needs a plume change; deferred.

## 9. DLSS 5 (Neural Rendering): out of scope, what it would need

Beyond colour, depth, motion and jitter, DLSS 5 / Ray Reconstruction-style features take G-buffer inputs: diffuse albedo,
specular albedo, normals + roughness (optionally specular motion / hit distance), tagged via Streamline's
`kBufferTypeAlbedo`, `kBufferTypeSpecularAlbedo`, `kBufferTypeNormals`, `kBufferTypeRoughness` and the `sl.dlss_nr` /
`nvngx_dlssnr` plugin. RT64's raster path has no such buffers (its old `Upscaler::UpscaleParameters` already names
`inDiffuseAlbedo`, `inSpecularAlbedo`, `inNormalRoughness` slots from the path-tracing era). Producing them would mean
a second ubershader output pass per draw with per-material classification, which RT64's "no game knowledge" rule makes
hard to do generically.

## 10. Results and known issues (filled per phase)

- Phase 1 (2026-10-28): implemented as planned (section 7, Phase 1 steps 1-6) with one deviation: the previous-position compute is
  a sibling shader (`RSPPrevPosCS`) and the overlay is drawn at render time, both so the native renderer is untouched. A second
  deviation is the F6 developer shortcut, added because the inspector could not be driven from scripts. Verified on the title demo
  (window 1600x960, 4x scale, MSAA off, D3D12): camera flying forward through Clock Town gives opposite hues on the left and right
  walls and nothing on the sky; a static camera with an animated character tints only the character; a camera pan tints every
  static surface with one hue while the character differs. At the "Display" refresh rate (60 Hz, three displayed frames per game
  frame) the vectors are about a third of the length seen at "Original" (20 fps), which is the expected previous-displayed-frame
  behaviour. Screenshots: `docs/phase1/`. Not yet covered: Termina Field and in-game walking (needs a loaded save; the title demo
  only shows Clock Town), and a numeric read-back of the texture. EQUAL depth testing showed no visible holes.
- Phase 2 (2026-10-28): implemented as planned (section 7, Phase 2): the jitter is folded into the per-projection `screenOffset`
  for perspective projections only; `screenOffsetUnjittered` feeds the motion vector pass; Halton (2,3) with 8 phases for DLAA;
  toggles: "Force Jitter" in the debugger tab and F9; F10 pauses rendering. Verified on a frame frozen with F10 (title demo,
  1600x960 window, MSAA off, D3D12): two captures with jitter off are bit-identical (0 of 1,536,000 pixels differ), jitter
  off -> on -> off is bit-identical to the first capture, jitter on changes 35% of the pixels by a mean of 2 levels (sub-pixel
  texture resampling) with larger differences only along edges, and two jittered frames differ from each other (38%, different
  phases). The difference mask covers only the 3D content; the letterbox bars are untouched. With both the motion vector view and
  jitter on, the vectors look the same as without jitter. Screenshots and masks: `docs/phase2/`.
- Phase 3, DLAA (2026-10-29): implemented per section 7 with these deviations. (1) `slInit` runs after the D3D12 device exists
  (manual hooking allows it on D3D12), and plume's `IDXGIFactory4*` is swapped for Streamline's proxy before the swap chain is
  created so Streamline sees every Present; nothing else in plume is touched. (2) The output texture (`DLSSStreamline::OutputTarget`,
  same size and format as the colour target, unordered access) is owned by the workload queue and copied back as a region.
  (3) Camera constants come from the *main* perspective projection (most depth-writing draws), not the marker projection.
  (4) The cut signal is the camera transform group asking to skip position/rotation interpolation: Majora's Mask keeps
  `CAMERA_TRANSFORM_ID` constant across cuts, so `mapped` never goes false there (the plan assumed the ID changes). (5) A frame
  frozen by the debugger keeps its history (zero motion, same image). (6) Streamline counts frames itself and the inputs are
  tagged `eValidUntilEvaluate`: handing it RT64's frame counter with `eOnlyValidNow` tags made it clone all four textures into
  a 16-frame ring every frame and log 64 "don't know the size" errors. (7) F12 toggles DLAA and the inspector has a DLSS combo.
  (8) The recomp menu shows Off / DLAA only until the scaling modes exist. Verified on the title demo (1600x960 window, MSAA off,
  D3D12, RTX 4080, driver 621.01, Streamline 2.14.16; the NVIDIA driver's global DLSS override is on and substitutes its own
  copies of the same version): `sl.log` shows DLSS supported, the DLSS context created at 1600x960 (DLAA optimal 1600x960,
  min 1584x950) and no errors; on a frame frozen with F10, DLAA on vs off changes 28.2% of the pixels (mean 2.1 levels, large
  differences only along edges), two DLAA captures of the same frozen frame differ on 7.3% of pixels by at most 12 levels, two
  captures with DLAA off are bit-identical, and the 4x zoomed crops show stair-stepped edges replaced by smooth ones with
  textures unchanged; the difference mask covers only the 3D content. History resets in a 15 s stretch of the demo: two when
  the output texture was created (each enable) and the three displayed frames of the demo's one camera cut, none otherwise
  (an earlier build reset every frame because the frame map was indexed by the frame-local workload number instead of the
  queue index). Clean DLSS context release on disable and clean `slShutdown` at exit. Images: `docs/phase3/`. Not covered:
  in-game play with the HUD (title demo only, needs a save), frame-time measurement, HFR at 120 Hz, Vulkan, the scaling modes.
- Testing notes: the recomp starts straight into the game when a ROM is configured; keys sent with simulated input never reach it
  from a background script (Windows blocks foreground changes), but `PostMessage(WM_KEYDOWN)` to the game window does, and goes
  through the same RT64 filter as real key presses. The game prints nothing to a redirected stdout until a normal exit.
  Posted mouse clicks only hover ImGui and RmlUi widgets, which is why F10 exists. `PrintWindow(PW_RENDERFULLCONTENT)`
  captures the game while the desktop is locked (used for all Phase 3 captures); the recomp ignores `WM_CLOSE` once in-game,
  so a kill ends test runs and the developer-mode DLSS lines flush stdout themselves. The PC rebooted unexpectedly a few minutes after the
  last Phase 2 run (game already closed, no crash dump, no GPU watchdog or hardware error record, an unrelated crash dump from
  six days earlier exists); nothing links it to these changes, but it is noted here in case it recurs.

- Phase 0 (2026-10-27): the ported build launches to the recomp launcher, the user started the game and it runs (reported
  "runs but definitely not 4K": their `graphics.json` has Window Mode = Windowed with Resolution = Auto, so RT64 renders at the
  largest multiple of 240 lines that fits the window; fullscreen on the 4K display gives 9x = 2160p). Display facts: the primary
  monitor is 3840x2160 at 150% DPI scaling and is driven by the RTX 4080; a second portrait display exists; plume picked the
  RTX 3090 Ti by VRAM, so frames crossed GPUs until `deviceName` was added (section 3.3). Verified after the change:
  `nvidia-smi` lists `Zelda64Recompiled.exe` under GPU 0 = NVIDIA GeForce RTX 4080.
