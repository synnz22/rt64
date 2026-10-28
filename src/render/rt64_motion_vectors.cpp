//
// RT64
//

#include "rt64_motion_vectors.h"

#include <cassert>

namespace RT64 {
    // MotionVectorTarget

    const RenderFormat MotionVectorTarget::Format = RenderFormat::R16G16_FLOAT;

    MotionVectorTarget::MotionVectorTarget() { }

    MotionVectorTarget::~MotionVectorTarget() { }

    void MotionVectorTarget::ensure(RenderWorker *worker, uint32_t requiredWidth, uint32_t requiredHeight, const RenderTarget *depthTarget) {
        assert(worker != nullptr);
        assert(depthTarget != nullptr);
        assert((requiredWidth > 0) && (requiredHeight > 0));

        // The texture must never be bigger than the depth target it is paired with, so it matches the color target exactly.
        const bool recreateTexture = (texture == nullptr) || (requiredWidth != width) || (requiredHeight != height);
        if (recreateTexture) {
            width = requiredWidth;
            height = requiredHeight;

            RenderClearValue clearValue = RenderClearValue::Color(RenderColor(), Format);
            texture = worker->device->createTexture(RenderTextureDesc::ColorTarget(width, height, Format, RenderMultisampling(), &clearValue));
            textureView = texture->createTextureView(RenderTextureViewDesc::Texture2D(Format));
            texture->setName("Motion Vectors");
            framebuffer.reset();
            debugDescSetDirty = true;
        }

        const RenderTexture *newDepthTexture = depthTarget->texture.get();
        const bool recreateFramebuffer = (framebuffer == nullptr) || (depthTexture != newDepthTexture) || (depthTextureRevision != depthTarget->textureRevision);
        if (recreateFramebuffer) {
            const RenderTexture *colorAttachment = texture.get();
            framebuffer = worker->device->createFramebuffer(RenderFramebufferDesc(&colorAttachment, 1, newDepthTexture, true));
            depthTexture = newDepthTexture;
            depthTextureRevision = depthTarget->textureRevision;
        }

        if (debugDescSet == nullptr) {
            debugDescSet = std::make_unique<MotionVectorDebugDescriptorSet>(worker->device);
            debugDescSetDirty = true;
        }

        if (debugDescSetDirty) {
            debugDescSet->setTexture(debugDescSet->gMotionVectors, texture.get(), RenderTextureLayout::SHADER_READ, textureView.get());
            debugDescSetDirty = false;
        }
    }
};
