//
// RT64
//

#pragma once

#include "rt64_descriptor_sets.h"
#include "rt64_render_target.h"
#include "rt64_render_worker.h"

namespace RT64 {
    // Render resolution motion vectors (previous frame position minus current position, in pixels) for the
    // framebuffer that gets presented. Owned by the high resolution renderer only; the native renderer never uses it.
    struct MotionVectorTarget {
        static const RenderFormat Format;

        std::unique_ptr<RenderTexture> texture;
        std::unique_ptr<RenderTextureView> textureView;
        std::unique_ptr<RenderFramebuffer> framebuffer;
        std::unique_ptr<MotionVectorDebugDescriptorSet> debugDescSet;
        const RenderTexture *depthTexture = nullptr;
        uint64_t depthTextureRevision = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        bool debugDescSetDirty = true;

        MotionVectorTarget();
        ~MotionVectorTarget();

        // Makes the texture match the color target size exactly and attaches the depth target as read-only depth.
        void ensure(RenderWorker *worker, uint32_t requiredWidth, uint32_t requiredHeight, const RenderTarget *depthTarget);
    };
};
