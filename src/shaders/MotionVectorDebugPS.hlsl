//
// RT64
//

// Debug overlay for the motion vector texture: hue encodes the direction, brightness the magnitude,
// and a white line is drawn from the center of every 32x32 block to where that pixel was in the previous frame.
// Line drawing adapted from https://www.shadertoy.com/view/3dlGDM

#include "shared/rt64_motion_vector_params.h"

[[vk::push_constant]] ConstantBuffer<MotionVectorParams> gConstants : register(b0);

Texture2D<float2> gMotionVectors : register(t1);

float3 hsvToRgb(float3 c) {
    const float4 K = float4(1.0f, 2.0f / 3.0f, 1.0f / 3.0f, 3.0f);
    const float3 p = abs(frac(c.xxx + K.xyz) * 6.0f - K.www);
    return c.z * lerp(K.xxx, saturate(p - K.xxx), c.y);
}

float distanceFromLineSegment(float2 p, float2 start, float2 end) {
    const float len = length(start - end);
    const float l2 = len * len;
    if (l2 == 0.0f) {
        return length(p - start);
    }

    const float t = max(0.0f, min(1.0f, dot(p - start, end - start) / l2));
    const float2 projection = start + t * (end - start);
    return length(p - projection);
}

float4 PSMain(in float4 pos : SV_Position, in float2 uv : TEXCOORD0) : SV_TARGET {
    const float2 pixel = uv * gConstants.viewportSize;
    const float2 mv = gMotionVectors.Load(uint3(uint2(pixel), 0));
    const float magnitude = length(mv);
    const float MaxMagnitude = 8.0f;
    const float hue = (atan2(-mv.y, mv.x) / (2.0f * 3.14159265f)) + 0.5f;
    float3 rgb = hsvToRgb(float3(hue, 1.0f, saturate(magnitude / MaxMagnitude)));
    float alpha = (magnitude > 0.01f) ? 0.65f : 0.0f;

    // Sparse arrows: one line per block, from the block center to the previous position of that pixel.
    const float BlockSize = 32.0f;
    const float2 blockCenter = floor(pixel / BlockSize) * BlockSize + (BlockSize * 0.5f);
    const float2 blockMv = gMotionVectors.Load(uint3(uint2(blockCenter), 0));
    const float lineDistance = distanceFromLineSegment(pixel, blockCenter, blockCenter + blockMv);
    if ((length(blockMv) > 0.5f) && (lineDistance < 1.0f)) {
        rgb = float3(1.0f, 1.0f, 1.0f);
        alpha = 1.0f;
    }

    return float4(rgb, alpha);
}
