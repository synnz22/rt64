//
// RT64
//

// Writes the motion vector of the pixel: where it was in the previous displayed frame minus where it is
// now, in render target pixels (x right, y down).

#include "shared/rt64_motion_vector_params.h"

[[vk::push_constant]] ConstantBuffer<MotionVectorParams> gConstants : register(b0);

float2 PSMain(in float4 pos : SV_Position, in float4 curClip : TEXCOORD0, in float4 prevClip : TEXCOORD1) : SV_TARGET {
    const float2 curNdc = curClip.xy / curClip.w;
    const float2 prevNdc = prevClip.xy / prevClip.w;
    return (prevNdc - curNdc) * float2(0.5f * gConstants.viewportSize.x, -0.5f * gConstants.viewportSize.y);
}
