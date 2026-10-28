//
// RT64
//

// Vertex shader for the motion vector pass. Applies the exact same N64 screen space to clip space
// mapping as RasterVS to the current and the previous position so the depth test can use EQUAL.

#include "shared/rt64_motion_vector_params.h"

[[vk::push_constant]] ConstantBuffer<MotionVectorParams> gConstants : register(b0);

float4 screenToClip(float4 screenPos) {
    float4 clipPos = screenPos;
    clipPos.xy -= float2(gConstants.resolution / 2);
    clipPos.xy /= float2(gConstants.resolution.x / 2.0f, gConstants.resolution.y / -2.0f);
    clipPos.xyz *= clipPos.w;
    clipPos.xy = (clipPos.xy * gConstants.screenScale) + gConstants.screenOffset * clipPos.w;
    return clipPos;
}

void VSMain(
    [[vk::location(0)]] in float4 iPosition : POSITION,
    [[vk::location(1)]] in float4 iPrevPosition : PREVPOSITION,
    out float4 oPosition : SV_POSITION,
    out float4 oCurClip : TEXCOORD0,
    out float4 oPrevClip : TEXCOORD1)
{
    const float4 curClip = screenToClip(iPosition);
    oPosition = curClip;
    oCurClip = curClip;
    oPrevClip = screenToClip(iPrevPosition);
}
