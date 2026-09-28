// Integer views preserve every position/velocity bit, including NaN payloads
// and signed zero. This is equality of the entire allocation, not a hash.
Texture2D<uint4> candidate : register(t0);
Texture2D<uint4> retained : register(t1);
RWBuffer<uint> difference : register(u0);
groupshared uint changed;
groupshared uint nonuniform;
[numthreads(16,16,1)]
void main(uint3 pixel : SV_DispatchThreadID, uint index : SV_GroupIndex) {
    if(index==0){changed=0;nonuniform=0;}
    GroupMemoryBarrierWithGroupSync();
    if(any(candidate.Load(int3(pixel.xy,0))!=retained.Load(int3(pixel.xy,0))))
        InterlockedOr(changed,1);
    // A lossless independent baseline candidate: the last 16x16 tile.
    // No inactive-region assumption is used; compare every pixel bit.
    if(any(candidate.Load(int3(pixel.xy,0))!=candidate.Load(int3(1008+pixel.xy%16,0))))
        InterlockedOr(nonuniform,1);
    GroupMemoryBarrierWithGroupSync();
    if(index==0) {
        if(changed)InterlockedOr(difference[0],1);
        difference[1+(pixel.y/16)*64+pixel.x/16]=nonuniform;
    }
}
