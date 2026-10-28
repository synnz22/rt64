//
// RT64
//

// Computes the N64 screen-space position each vertex had in the previously displayed frame, using the
// previous view-projection and world transforms. Used to build motion vectors for the high-res renderer.

#include "shared/rt64_rsp_viewport.h"

#define GROUP_SIZE 64

struct RSPPrevPosCB {
    uint vertexStart;
    uint vertexCount;
    float prevFrameWeight;
    float curFrameWeight;
};

[[vk::push_constant]] ConstantBuffer<RSPPrevPosCB> gConstants : register(b0);
Buffer<float> srcPos : register(t1);
Buffer<float> srcVel : register(t2);
Buffer<uint> srcViewProjIndices : register(t3);
Buffer<uint> srcWorldIndices : register(t4);
StructuredBuffer<RSPViewport> rspViewportVector : register(t5);
StructuredBuffer<float4x4> prevViewProjTransforms : register(t6);
StructuredBuffer<float4x4> prevWorldTransforms : register(t7);
RWStructuredBuffer<float4> dstPrevPos : register(u8);

[numthreads(GROUP_SIZE, 1, 1)]
void CSMain(uint vertexIndex : SV_DispatchThreadID) {
    if (vertexIndex >= gConstants.vertexCount) {
        return;
    }

    const uint vertexOffsetIndex = gConstants.vertexStart + vertexIndex;
    const uint viewProjIndex = srcViewProjIndices[vertexOffsetIndex];
    const uint transformIndex = srcWorldIndices[vertexOffsetIndex];
    const float4x4 prevViewProjMat = prevViewProjTransforms[viewProjIndex];
    const float4x4 prevWorldMat = prevWorldTransforms[transformIndex];
    const uint posIndex = vertexOffsetIndex * 3;

    // Same vertex interpolation as RSPProcessCS, evaluated at the previous frame's weight.
    const float3 pos = float3(srcPos[posIndex + 0], srcPos[posIndex + 1], srcPos[posIndex + 2]);
    const float3 vel = float3(srcVel[posIndex + 0], srcVel[posIndex + 1], srcVel[posIndex + 2]);
    float4 tfPos = mul(mul(prevViewProjMat, prevWorldMat), float4(pos - vel * (1.0f - gConstants.prevFrameWeight), 1.0f));
    if (tfPos.w == 0.0f) {
        tfPos.w = 1e-6f;
    }

    // Convert to N64 screen position with the current viewport (viewports are not interpolated).
    const RSPViewport rspViewport = rspViewportVector[viewProjIndex];
    const float3 ndcPos = tfPos.xyz / float3(tfPos.w, -tfPos.w, tfPos.w);
    dstPrevPos[vertexOffsetIndex] = float4(ndcPos * rspViewport.scale + rspViewport.translate, tfPos.w);
}
