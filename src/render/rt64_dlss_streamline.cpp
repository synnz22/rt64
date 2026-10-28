//
// RT64
//

#include "rt64_dlss_streamline.h"

#include <cassert>
#include <cstdio>

#include "common/rt64_dynamic_libraries.h"
#include "common/rt64_math.h"
#include "shared/rt64_hlsl.h"

#if RT64_DLSS_ENABLED
// plume_d3d12.h must come before any other D3D12 header so the same d3d12.h flavor is used everywhere.
#   include "plume_d3d12.h"
#   include <sl.h>
#   include <sl_consts.h>
#   include <sl_dlss.h>
#   include <sl_helpers.h>
#   include <sl_security.h>
#endif

namespace RT64 {
    // DLSSStreamline::OutputTarget

    bool DLSSStreamline::OutputTarget::ensure(RenderDevice *device, uint32_t requiredWidth, uint32_t requiredHeight, RenderFormat requiredFormat) {
        assert(device != nullptr);
        assert((requiredWidth > 0) && (requiredHeight > 0));

        if ((texture != nullptr) && (width == requiredWidth) && (height == requiredHeight) && (format == requiredFormat)) {
            return false;
        }

        width = requiredWidth;
        height = requiredHeight;
        format = requiredFormat;
        texture = device->createTexture(RenderTextureDesc::Texture2D(width, height, 1, format, RenderTextureFlag::STORAGE | RenderTextureFlag::UNORDERED_ACCESS));
        texture->setName("DLSS Output");
        return true;
    }

    DLSSStreamline::Mode DLSSStreamline::modeFromConfiguration(UserConfiguration::Upscaler upscaler) {
        switch (upscaler) {
        case UserConfiguration::Upscaler::DLAA:
            return Mode::DLAA;
        case UserConfiguration::Upscaler::DLSSQuality:
            return Mode::Quality;
        case UserConfiguration::Upscaler::DLSSBalanced:
            return Mode::Balanced;
        case UserConfiguration::Upscaler::DLSSPerformance:
            return Mode::Performance;
        case UserConfiguration::Upscaler::None:
        default:
            return Mode::Off;
        }
    }

#if RT64_DLSS_ENABLED
    // Everything resolved from sl.interposer.dll with GetProcAddress. Nothing from the SDK is linked.
    struct DLSSStreamline::Impl {
        HMODULE module = nullptr;
        std::wstring logDirectory;
        sl::Feature features[1] = { sl::kFeatureDLSS };
        sl::ViewportHandle viewport = sl::ViewportHandle(0u);
        sl::DLSSOptions options;
        bool optionsSet = false;
        bool resourcesAllocated = false;
        bool historyInvalid = false;
        sl::DLSSMode queriedMode = sl::DLSSMode::eOff;
        uint32_t queriedOutputWidth = 0;
        uint32_t queriedOutputHeight = 0;
        uint32_t queriedRenderWidth = 0;
        uint32_t queriedRenderHeight = 0;
        PFun_slInit *slInit = nullptr;
        PFun_slShutdown *slShutdown = nullptr;
        PFun_slIsFeatureSupported *slIsFeatureSupported = nullptr;
        PFun_slSetD3DDevice *slSetD3DDevice = nullptr;
        PFun_slUpgradeInterface *slUpgradeInterface = nullptr;
        PFun_slGetNativeInterface *slGetNativeInterface = nullptr;
        PFun_slGetFeatureFunction *slGetFeatureFunction = nullptr;
        PFun_slGetFeatureRequirements *slGetFeatureRequirements = nullptr;
        PFun_slGetNewFrameToken *slGetNewFrameToken = nullptr;
        PFun_slSetConstants *slSetConstants = nullptr;
        PFun_slSetTagForFrame *slSetTagForFrame = nullptr;
        PFun_slEvaluateFeature *slEvaluateFeature = nullptr;
        PFun_slAllocateResources *slAllocateResources = nullptr;
        PFun_slFreeResources *slFreeResources = nullptr;
        PFun_slDLSSGetOptimalSettings *slDLSSGetOptimalSettings = nullptr;
        PFun_slDLSSSetOptions *slDLSSSetOptions = nullptr;
        PFun_slDLSSGetState *slDLSSGetState = nullptr;
    };

    // Streamline calls this from its own threads. Warnings and errors go to the console; info only goes to sl.log.
    static void StreamlineLogCallback(sl::LogType type, const char *msg) {
        if (type == sl::LogType::eError) {
            fprintf(stderr, "[Streamline] %s", msg);
        }
        else if (type == sl::LogType::eWarn) {
            fprintf(stdout, "[Streamline] %s", msg);
        }
    }

    static std::string DescribeResult(sl::Result result) {
        switch (result) {
        case sl::Result::eErrorOSOutOfDate:
            return "Windows is too old for DLSS";
        case sl::Result::eErrorDriverOutOfDate:
            return "the NVIDIA driver is too old for DLSS";
        case sl::Result::eErrorNoSupportedAdapterFound:
            return "no GPU in this system supports DLSS";
        case sl::Result::eErrorAdapterNotSupported:
            return "the selected GPU does not support DLSS";
        case sl::Result::eErrorNoPlugins:
        case sl::Result::eErrorFeatureMissing:
        case sl::Result::eErrorFeatureFailedToLoad:
            return "the DLSS plugin (sl.common.dll, sl.dlss.dll, nvngx_dlss.dll) is missing or failed to load";
        default:
            return std::string("Streamline reported ") + sl::getResultAsStr(result);
        }
    }

    static sl::DLSSMode ToStreamlineMode(DLSSStreamline::Mode mode) {
        switch (mode) {
        case DLSSStreamline::Mode::DLAA:
            return sl::DLSSMode::eDLAA;
        case DLSSStreamline::Mode::Quality:
            return sl::DLSSMode::eMaxQuality;
        case DLSSStreamline::Mode::Balanced:
            return sl::DLSSMode::eBalanced;
        case DLSSStreamline::Mode::Performance:
            return sl::DLSSMode::eMaxPerformance;
        case DLSSStreamline::Mode::UltraPerformance:
            return sl::DLSSMode::eUltraPerformance;
        case DLSSStreamline::Mode::Off:
        default:
            return sl::DLSSMode::eOff;
        }
    }

    // RT64 stores matrices row-major in the row vector convention (v * M), which is also what Streamline documents.
    static sl::float4x4 ToStreamlineMatrix(const hlslpp::float4x4 &m) {
        const interop::float4x4 t(m);
        sl::float4x4 r;
        for (uint32_t i = 0; i < 4; i++) {
            r.setRow(i, sl::float4(t.m[i][0], t.m[i][1], t.m[i][2], t.m[i][3]));
        }

        return r;
    }

    // DLSSStreamline

    DLSSStreamline::DLSSStreamline() {
        impl = std::make_unique<Impl>();
        status.compiledIn = true;
        status.message = "not initialized";
    }

    DLSSStreamline::~DLSSStreamline() {
        shutdown();
    }

    bool DLSSStreamline::isCompiledIn() {
        return true;
    }

    bool DLSSStreamline::initialize(const std::filesystem::path &logDirectory) {
        std::filesystem::path moduleDir;
        if (!DynamicLibraries::moduleDirectory(moduleDir)) {
            status.message = "the executable's directory could not be determined";
            return false;
        }

        const std::filesystem::path interposerPath = moduleDir / L"sl.interposer.dll";
        std::error_code ec;
        if (!std::filesystem::exists(interposerPath, ec)) {
            status.message = "sl.interposer.dll is not next to the executable";
            return false;
        }

        // Only run NVIDIA's own binary. This is NVIDIA's requirement for shipping Streamline and it also protects against a
        // tampered DLL sitting next to the game.
        if (!sl::security::verifyEmbeddedSignature(interposerPath.c_str())) {
            status.message = "sl.interposer.dll is not signed by NVIDIA";
            return false;
        }

        if (impl->module == nullptr) {
            impl->module = LoadLibraryW(interposerPath.c_str());
            if (impl->module == nullptr) {
                status.message = "sl.interposer.dll failed to load (error 0x" + std::to_string(GetLastError()) + ")";
                return false;
            }
        }

#   define RT64_SL_RESOLVE(name) \
        impl->name = reinterpret_cast<PFun_##name *>(GetProcAddress(impl->module, #name)); \
        if (impl->name == nullptr) { \
            status.message = "sl.interposer.dll does not export " #name; \
            return false; \
        }

        RT64_SL_RESOLVE(slInit);
        RT64_SL_RESOLVE(slShutdown);
        RT64_SL_RESOLVE(slIsFeatureSupported);
        RT64_SL_RESOLVE(slSetD3DDevice);
        RT64_SL_RESOLVE(slUpgradeInterface);
        RT64_SL_RESOLVE(slGetNativeInterface);
        RT64_SL_RESOLVE(slGetFeatureFunction);
        RT64_SL_RESOLVE(slGetFeatureRequirements);
        RT64_SL_RESOLVE(slGetNewFrameToken);
        RT64_SL_RESOLVE(slSetConstants);
        RT64_SL_RESOLVE(slSetTagForFrame);
        RT64_SL_RESOLVE(slEvaluateFeature);
        RT64_SL_RESOLVE(slAllocateResources);
        RT64_SL_RESOLVE(slFreeResources);
#   undef RT64_SL_RESOLVE

        status.libraryLoaded = true;

        impl->logDirectory = logDirectory.wstring();

        sl::Preferences pref{};
        pref.showConsole = false;
        pref.logLevel = sl::LogLevel::eDefault;
        pref.pathToLogsAndData = impl->logDirectory.empty() ? nullptr : impl->logDirectory.c_str();
        pref.logMessageCallback = &StreamlineLogCallback;

        // Manual hooking: Streamline intercepts nothing on its own; the renderer hands it the device and the DXGI factory
        // explicitly (see attachDevice). Frame based tagging matches how the inputs are tagged every frame. Over the air
        // updates are deliberately not enabled: the DLSS that runs is the one that ships next to the executable (unless the
        // NVIDIA driver's own global DLSS override is on, which replaces it system wide).
        pref.flags = sl::PreferenceFlags::eUseManualHooking | sl::PreferenceFlags::eDisableCLStateTracking | sl::PreferenceFlags::eUseFrameBasedResourceTagging;
        pref.featuresToLoad = impl->features;
        pref.numFeaturesToLoad = 1;
        pref.engine = sl::EngineType::eCustom;
        pref.engineVersion = "RT64";
        pref.projectId = "a4d0a6d6-1b3d-4b4b-9a2e-5c1b7f8e2d64";
        pref.renderAPI = sl::RenderAPI::eD3D12;

        const sl::Result result = impl->slInit(pref, sl::kSDKVersion);
        if (result != sl::Result::eOk) {
            status.message = "slInit failed, " + DescribeResult(result);
            return false;
        }

        status.initialized = true;
        status.message = "initialized, no device attached";
        return true;
    }

    bool DLSSStreamline::attachDevice(RenderInterface *renderInterface, RenderDevice *device, UserConfiguration::GraphicsAPI graphicsAPI) {
        if (!status.initialized) {
            return false;
        }

        if (graphicsAPI != UserConfiguration::GraphicsAPI::D3D12) {
            status.message = "DLSS is only available with the D3D12 graphics API for now";
            return false;
        }

        D3D12Interface *d3d12Interface = static_cast<D3D12Interface *>(renderInterface);
        D3D12Device *d3d12Device = static_cast<D3D12Device *>(device);
        sl::Result result = impl->slSetD3DDevice(d3d12Device->d3d);
        if (result != sl::Result::eOk) {
            status.message = "slSetD3DDevice failed, " + DescribeResult(result);
            return false;
        }

        status.deviceAttached = true;

        // Ask about the adapter the device was actually created on, not just the system in general.
        DXGI_ADAPTER_DESC1 adapterDesc = {};
        sl::AdapterInfo adapterInfo{};
        if ((d3d12Device->adapter != nullptr) && SUCCEEDED(d3d12Device->adapter->GetDesc1(&adapterDesc))) {
            adapterInfo.deviceLUID = reinterpret_cast<uint8_t *>(&adapterDesc.AdapterLuid);
            adapterInfo.deviceLUIDSizeInBytes = sizeof(LUID);
        }

        result = impl->slIsFeatureSupported(sl::kFeatureDLSS, adapterInfo);
        if (result != sl::Result::eOk) {
            status.message = DescribeResult(result);
            return false;
        }

        // The DLSS entry points only resolve once a device is set.
        result = impl->slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSGetOptimalSettings", reinterpret_cast<void *&>(impl->slDLSSGetOptimalSettings));
        if (result == sl::Result::eOk) {
            result = impl->slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSSetOptions", reinterpret_cast<void *&>(impl->slDLSSSetOptions));
        }

        if (result == sl::Result::eOk) {
            result = impl->slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSGetState", reinterpret_cast<void *&>(impl->slDLSSGetState));
        }

        if (result != sl::Result::eOk) {
            status.message = "the DLSS plugin does not provide its functions, " + DescribeResult(result);
            return false;
        }

        // Manual hooking still requires Streamline to see every Present: it does its per-frame bookkeeping there (the frame
        // based resource tags are released on present). The documented way is to create the swap chain through a factory
        // proxy from Streamline, so the factory plume will use for the swap chain is swapped for the proxy here. The proxy
        // forwards everything else untouched and plume releases it at shutdown like the original.
        void *factory = d3d12Interface->dxgiFactory;
        result = impl->slUpgradeInterface(&factory);
        if (result != sl::Result::eOk) {
            status.message = "slUpgradeInterface failed for the DXGI factory, " + DescribeResult(result);
            return false;
        }

        d3d12Interface->dxgiFactory = static_cast<IDXGIFactory4 *>(factory);

        status.supported = true;
        status.message = "supported";
        return true;
    }

    bool DLSSStreamline::isSupported() const {
        return status.supported;
    }

    const DLSSStreamline::Status &DLSSStreamline::getStatus() const {
        return status;
    }

    bool DLSSStreamline::queryRenderSize(Mode mode, uint32_t outputWidth, uint32_t outputHeight, uint32_t &renderWidth, uint32_t &renderHeight) {
        if (!status.supported || (mode == Mode::Off) || (outputWidth == 0) || (outputHeight == 0)) {
            return false;
        }

        const sl::DLSSMode slMode = ToStreamlineMode(mode);
        if ((impl->queriedMode != slMode) || (impl->queriedOutputWidth != outputWidth) || (impl->queriedOutputHeight != outputHeight)) {
            sl::DLSSOptions options;
            options.mode = slMode;
            options.outputWidth = outputWidth;
            options.outputHeight = outputHeight;
            sl::DLSSOptimalSettings optimal;
            const sl::Result result = impl->slDLSSGetOptimalSettings(options, optimal);
            if (result != sl::Result::eOk) {
                fprintf(stderr, "slDLSSGetOptimalSettings failed, %s.\n", DescribeResult(result).c_str());
                return false;
            }

            // DLAA reports the output size. Never go above the output or below what DLSS allows.
            impl->queriedMode = slMode;
            impl->queriedOutputWidth = outputWidth;
            impl->queriedOutputHeight = outputHeight;
            impl->queriedRenderWidth = std::min(std::max(optimal.optimalRenderWidth, optimal.renderWidthMin), outputWidth);
            impl->queriedRenderHeight = std::min(std::max(optimal.optimalRenderHeight, optimal.renderHeightMin), outputHeight);
            if ((impl->queriedRenderWidth == 0) || (impl->queriedRenderHeight == 0)) {
                impl->queriedRenderWidth = outputWidth;
                impl->queriedRenderHeight = outputHeight;
            }
        }

        renderWidth = impl->queriedRenderWidth;
        renderHeight = impl->queriedRenderHeight;
        return true;
    }

    bool DLSSStreamline::evaluate(const EvaluateParams &p) {
        if (!status.supported || (p.mode == Mode::Off)) {
            return false;
        }

        assert(p.commandList != nullptr);
        assert(p.inputColor != nullptr);
        assert(p.depth != nullptr);
        assert(p.motionVectors != nullptr);
        assert(p.outputColor != nullptr);
        assert((p.renderWidth > 0) && (p.renderHeight > 0) && (p.outputWidth > 0) && (p.outputHeight > 0));

        D3D12CommandList *commandList = static_cast<D3D12CommandList *>(p.commandList);

        // Options are only re-sent when the mode or the output size changes. DLSS keeps its own copy.
        const sl::DLSSMode mode = ToStreamlineMode(p.mode);
        if (!impl->optionsSet || (impl->options.mode != mode) || (impl->options.outputWidth != p.outputWidth) || (impl->options.outputHeight != p.outputHeight)) {
            impl->options = sl::DLSSOptions();
            impl->options.mode = mode;
            impl->options.outputWidth = p.outputWidth;
            impl->options.outputHeight = p.outputHeight;
            impl->options.colorBuffersHDR = sl::Boolean::eFalse;
            impl->options.useAutoExposure = sl::Boolean::eFalse;
            const sl::Result result = impl->slDLSSSetOptions(impl->viewport, impl->options);
            if (result != sl::Result::eOk) {
                fprintf(stderr, "slDLSSSetOptions failed, %s.\n", DescribeResult(result).c_str());
                return false;
            }

            sl::DLSSOptimalSettings optimal;
            if (impl->slDLSSGetOptimalSettings(impl->options, optimal) == sl::Result::eOk) {
                fprintf(stdout, "DLSS: %s, output %ux%u, optimal render size %ux%u (min %ux%u, max %ux%u).\n", sl::getDLSSModeAsStr(mode), p.outputWidth, p.outputHeight,
                    optimal.optimalRenderWidth, optimal.optimalRenderHeight, optimal.renderWidthMin, optimal.renderHeightMin, optimal.renderWidthMax, optimal.renderHeightMax);
                fflush(stdout);
            }

            impl->optionsSet = true;
            impl->historyInvalid = true;
        }

        // Frame token. Streamline counts frames itself from the presents it sees through the swap chain proxy; handing it the
        // renderer's own count instead made its common plugin clone every tagged texture (it saw tags for "other" frames).
        sl::FrameToken *frame = nullptr;
        sl::Result result = impl->slGetNewFrameToken(frame, nullptr);
        if (result != sl::Result::eOk) {
            fprintf(stderr, "slGetNewFrameToken failed, %s.\n", DescribeResult(result).c_str());
            return false;
        }

        // Camera constants. Matrices never contain the jitter; it is passed separately in pixels.
        const hlslpp::float4x4 clipToView = hlslpp::inverse(p.viewToClip);
        const hlslpp::float4x4 viewToWorld = hlslpp::inverse(p.worldToView);
        const interop::float4x4 viewToWorldRows(viewToWorld);
        sl::Constants consts{};
        consts.cameraViewToClip = ToStreamlineMatrix(p.viewToClip);
        consts.clipToCameraView = ToStreamlineMatrix(clipToView);
        consts.clipToLensClip = ToStreamlineMatrix(hlslpp::float4x4::identity());
        consts.clipToPrevClip = ToStreamlineMatrix(p.clipToPrevClip);
        consts.prevClipToClip = ToStreamlineMatrix(hlslpp::inverse(p.clipToPrevClip));
        consts.jitterOffset = sl::float2(float(p.jitterPixels.x), float(p.jitterPixels.y));
        consts.mvecScale = sl::float2(1.0f / float(p.renderWidth), 1.0f / float(p.renderHeight));
        consts.cameraPinholeOffset = sl::float2(0.0f, 0.0f);
        consts.cameraPos = sl::float3(viewToWorldRows.m[3][0], viewToWorldRows.m[3][1], viewToWorldRows.m[3][2]);
        consts.cameraRight = sl::float3(viewToWorldRows.m[0][0], viewToWorldRows.m[0][1], viewToWorldRows.m[0][2]);
        consts.cameraUp = sl::float3(viewToWorldRows.m[1][0], viewToWorldRows.m[1][1], viewToWorldRows.m[1][2]);
        consts.cameraFwd = sl::float3(-viewToWorldRows.m[2][0], -viewToWorldRows.m[2][1], -viewToWorldRows.m[2][2]);
        consts.cameraNear = nearPlaneFromProj(p.viewToClip);
        consts.cameraFar = farPlaneFromProj(p.viewToClip);
        consts.cameraFOV = fovFromProj(p.viewToClip);
        consts.cameraAspectRatio = float(p.renderWidth) / float(p.renderHeight);
        consts.depthInverted = sl::Boolean::eFalse;
        consts.cameraMotionIncluded = sl::Boolean::eTrue;
        consts.motionVectors3D = sl::Boolean::eFalse;
        consts.reset = (p.reset || impl->historyInvalid) ? sl::Boolean::eTrue : sl::Boolean::eFalse;
        impl->historyInvalid = false;
        consts.orthographicProjection = sl::Boolean::eFalse;
        consts.motionVectorsDilated = sl::Boolean::eFalse;
        consts.motionVectorsJittered = sl::Boolean::eFalse;
        result = impl->slSetConstants(consts, *frame, impl->viewport);
        if (result != sl::Result::eOk) {
            fprintf(stderr, "slSetConstants failed, %s.\n", DescribeResult(result).c_str());
            return false;
        }

        // Inputs and output, with the D3D12 state plume currently tracks for each so Streamline can transition them and back.
        const D3D12Texture *inputColor = static_cast<const D3D12Texture *>(p.inputColor);
        const D3D12Texture *depth = static_cast<const D3D12Texture *>(p.depth);
        const D3D12Texture *motionVectors = static_cast<const D3D12Texture *>(p.motionVectors);
        const D3D12Texture *outputColor = static_cast<const D3D12Texture *>(p.outputColor);
        sl::Extent renderExtent;
        renderExtent.top = 0;
        renderExtent.left = 0;
        renderExtent.width = p.renderWidth;
        renderExtent.height = p.renderHeight;

        sl::Extent outputExtent;
        outputExtent.top = 0;
        outputExtent.left = 0;
        outputExtent.width = p.outputWidth;
        outputExtent.height = p.outputHeight;

        sl::Resource colorInResource(sl::ResourceType::eTex2d, inputColor->d3d, nullptr, nullptr, uint32_t(inputColor->resourceStates));
        sl::Resource colorOutResource(sl::ResourceType::eTex2d, outputColor->d3d, nullptr, nullptr, uint32_t(outputColor->resourceStates));
        sl::Resource depthResource(sl::ResourceType::eTex2d, depth->d3d, nullptr, nullptr, uint32_t(depth->resourceStates));
        sl::Resource mvecResource(sl::ResourceType::eTex2d, motionVectors->d3d, nullptr, nullptr, uint32_t(motionVectors->resourceStates));
        // The textures are consumed by the evaluate call that follows and reused by the renderer afterwards. "Only valid now"
        // would make Streamline's common plugin copy all four into a ring of frames every frame; "valid until evaluate" says
        // exactly what is true and avoids the copies.
        sl::ResourceTag tags[] = {
            sl::ResourceTag(&colorInResource, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eValidUntilEvaluate, &renderExtent),
            sl::ResourceTag(&colorOutResource, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eValidUntilEvaluate, &outputExtent),
            sl::ResourceTag(&depthResource, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilEvaluate, &renderExtent),
            sl::ResourceTag(&mvecResource, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilEvaluate, &renderExtent)
        };

        result = impl->slSetTagForFrame(*frame, impl->viewport, tags, uint32_t(std::size(tags)), commandList->d3d);
        if (result != sl::Result::eOk) {
            fprintf(stderr, "slSetTagForFrame failed, %s.\n", DescribeResult(result).c_str());
            return false;
        }

        const sl::BaseStructure *inputs[] = { &impl->viewport };
        result = impl->slEvaluateFeature(sl::kFeatureDLSS, *frame, inputs, uint32_t(std::size(inputs)), commandList->d3d);
        impl->resourcesAllocated = true;

        // Streamline changed the descriptor heaps, root signatures and pipeline state. Drop what plume remembers about
        // them so its next calls set them again; the renderer re-binds the rest itself after this pass.
        commandList->notifyDescriptorHeapWasChangedExternally();
        commandList->activeTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
        commandList->activeStencilRef = UINT32_MAX;

        if (result != sl::Result::eOk) {
            fprintf(stderr, "slEvaluateFeature failed, %s.\n", DescribeResult(result).c_str());
            return false;
        }

        return true;
    }

    void DLSSStreamline::releaseResources() {
        if (status.initialized && impl->resourcesAllocated) {
            const sl::Result result = impl->slFreeResources(sl::kFeatureDLSS, impl->viewport);
            if (result != sl::Result::eOk) {
                fprintf(stderr, "slFreeResources failed, %s.\n", DescribeResult(result).c_str());
            }
        }

        impl->resourcesAllocated = false;
        impl->optionsSet = false;
    }

    void DLSSStreamline::shutdown() {
        if (status.initialized) {
            releaseResources();

            const sl::Result result = impl->slShutdown();
            if (result != sl::Result::eOk) {
                fprintf(stderr, "slShutdown failed, %s.\n", DescribeResult(result).c_str());
            }
        }

        if (status.supported) {
            status.message = "shut down";
        }

        status.initialized = false;
        status.deviceAttached = false;
        status.supported = false;

        // sl.interposer.dll is intentionally never unloaded: plume still owns the factory proxy that lives in it.
    }
#else
    // Stubs for builds without RT64_ENABLE_DLSS. They keep every caller identical in both configurations.
    struct DLSSStreamline::Impl { };

    DLSSStreamline::DLSSStreamline() {
        status.message = "RT64 was built without DLSS support";
    }

    DLSSStreamline::~DLSSStreamline() { }

    bool DLSSStreamline::isCompiledIn() {
        return false;
    }

    bool DLSSStreamline::initialize(const std::filesystem::path &logDirectory) {
        return false;
    }

    bool DLSSStreamline::attachDevice(RenderInterface *renderInterface, RenderDevice *device, UserConfiguration::GraphicsAPI graphicsAPI) {
        return false;
    }

    bool DLSSStreamline::isSupported() const {
        return false;
    }

    const DLSSStreamline::Status &DLSSStreamline::getStatus() const {
        return status;
    }

    bool DLSSStreamline::queryRenderSize(Mode mode, uint32_t outputWidth, uint32_t outputHeight, uint32_t &renderWidth, uint32_t &renderHeight) {
        return false;
    }

    bool DLSSStreamline::evaluate(const EvaluateParams &p) {
        return false;
    }

    void DLSSStreamline::releaseResources() { }

    void DLSSStreamline::shutdown() { }
#endif
};
