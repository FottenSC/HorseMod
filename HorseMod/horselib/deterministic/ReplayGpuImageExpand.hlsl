Texture2D<uint4> atlas : register(t0);
Buffer<uint> tiles : register(t1);
RWTexture2D<uint4> destination : register(u0);
[numthreads(16,16,1)]
void main(uint3 pixel : SV_DispatchThreadID) {
    uint width,height;atlas.GetDimensions(width,height);
    uint columns=width/16;
    uint tile=tiles[(pixel.y/16)*64+pixel.x/16];
    destination[pixel.xy]=atlas.Load(int3((tile%columns)*16+pixel.x%16,(tile/columns)*16+pixel.y%16,0));
}
