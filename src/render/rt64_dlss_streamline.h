//
// RT64
//

#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "common/rt64_common.h"
#include "common/rt64_plume.h"
#include "common/rt64_user_configuration.h"

namespace RT64 {
    // NVIDIA DLSS Super Resolution through the Streamline SDK.
    //
    // This is the only part of RT64 that talks to Streamline, and this header never includes a Streamline header, so
    // the rest of the renderer (and anything that links RT64) sees the same declarations whether or not the build was
    // configured with RT64_ENABLE_DLSS. Without that option every method is a stub that reports "not compiled in".
    //
    // sl.interposer.dll is loaded at runtime from the executable's directory after its NVIDIA signature is verified;
    // nothing is linked. A build with DLSS enabled therefore still starts on machines without the DLLs or without an
    // RTX GPU: the option is reported as unavailable and rendering is exactly what it is without DLSS.
    //
    // Threading: initialize(), attachDevice() and shutdown() run on the thread that sets up the application. Everything
    // else is only called from the workload thread while it records the high resolution frame.
    struct DLSSStreamline {
        enum class Mode : int {
            Off = 0,
            DLAA,
            Quality,
            Balanced,
            Performance,
            UltraPerformance
        };

        struct Status {
            bool compiledIn = false;       // Built with RT64_ENABLE_DLSS.
            bool libraryLoaded = false;    // sl.interposer.dll found, signed by NVIDIA and loaded.
            bool initialized = false;      // slInit succeeded.
            bool deviceAttached = false;   // The D3D12 device was handed to Streamline.
            bool supported = false;        // DLSS is supported on the device's adapter.
            std::string message;           // Plain-language reason for the first false above, or "supported".
        };

        // Output of the upscaler: a texture of the color target's size and format that DLSS can write (unordered access).
        struct OutputTarget {
            std::unique_ptr<RenderTexture> texture;
            uint32_t width = 0;
            uint32_t height = 0;
            RenderFormat format = RenderFormat::UNKNOWN;

            // Recreates the texture when the size or format differs. Returns true when it was (re)created.
            bool ensure(RenderDevice *device, uint32_t requiredWidth, uint32_t requiredHeight, RenderFormat requiredFormat);
        };

        // Everything one DLSS pass needs. The textures are in the layouts noted and the pass leaves them there.
        struct EvaluateParams {
            RenderCommandList *commandList = nullptr;
            const RenderTexture *inputColor = nullptr;     // SHADER_READ. The jittered 3D frame.
            const RenderTexture *depth = nullptr;          // DEPTH_READ. Single sampled, 1.0 is far.
            const RenderTexture *motionVectors = nullptr;  // SHADER_READ. Pixels, previous minus current, y down, unjittered.
            RenderTexture *outputColor = nullptr;          // GENERAL (unordered access).
            Mode mode = Mode::DLAA;
            uint32_t renderWidth = 0;                      // Region of the inputs holding the frame, from the top left corner.
            uint32_t renderHeight = 0;
            uint32_t outputWidth = 0;                      // Region of the output that receives the frame.
            uint32_t outputHeight = 0;
            hlslpp::float4x4 viewToClip;                   // Unjittered projection, row vector convention as RT64 stores it.
            hlslpp::float4x4 worldToView;
            hlslpp::float4x4 clipToPrevClip;               // Current clip space to the previous displayed frame's clip space.
            hlslpp::float2 jitterPixels = 0.0f;            // The sub-pixel offset the 3D content was rendered with.
            uint32_t frameIndex = 0;                       // Goes up by one for every displayed frame.
            bool reset = false;                            // The history is invalid: cut, pause, first frame, mode change.
        };

        DLSSStreamline();
        ~DLSSStreamline();

        // True when RT64 was built with RT64_ENABLE_DLSS. Everything else returns false without it.
        static bool isCompiledIn();

        static Mode modeFromConfiguration(UserConfiguration::Upscaler upscaler);

        // Loads and initializes Streamline. logDirectory is where Streamline writes sl.log (empty disables the file).
        bool initialize(const std::filesystem::path &logDirectory);

        // Hands the device to Streamline and checks DLSS support on its adapter. Only D3D12 is accepted for now; the
        // swap chain must not exist yet, because Streamline takes over the DXGI factory that will create it.
        bool attachDevice(RenderInterface *renderInterface, RenderDevice *device, UserConfiguration::GraphicsAPI graphicsAPI);

        // True once attachDevice() reported DLSS support.
        bool isSupported() const;
        const Status &getStatus() const;

        // Records DLSS into the command list. False (output untouched) when Streamline refused; the reason goes to the console.
        bool evaluate(const EvaluateParams &p);

        // Frees the DLSS resources of the viewport. Only call with the GPU idle. Done automatically at shutdown.
        void releaseResources();

        // Releases Streamline. Must run before the device is destroyed. The interposer DLL stays loaded on purpose: the
        // renderer still holds the DXGI factory proxy Streamline handed out and releases it after this.
        void shutdown();

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
        Status status;
    };
};
