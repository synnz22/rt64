//
// RT64
//

#pragma once

#include "shared/rt64_hlsl.h"

#ifdef HLSL_CPU
namespace interop {
#endif
    // Push constants shared by the motion vector pass and its debug overlay.
    struct MotionVectorParams {
        float2 resolution;
        float2 screenScale;
        float2 screenOffset;
        float2 viewportSize;
    };
#ifdef HLSL_CPU
};
#endif
