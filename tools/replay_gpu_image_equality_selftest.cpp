#include "deterministic/ReplayGpuImageEquality.hpp"
#include "deterministic/ReplayGpuPackedImage.hpp"
#include <cassert>
#include <cstdio>
#include <thread>
#include <vector>
using namespace Horse::Deterministic;
using Microsoft::WRL::ComPtr;
void Run(D3D_DRIVER_TYPE driver,bool predicated=false) {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL level{};
    assert(SUCCEEDED(D3D11CreateDevice(nullptr,driver,nullptr,0,nullptr,0,
        D3D11_SDK_VERSION,&device,&level,&context)));
    ComPtr<ID3D11Predicate> predicate;
    if(predicated) {
        D3D11_QUERY_DESC descriptor{D3D11_QUERY_OCCLUSION_PREDICATE,0};
        assert(SUCCEEDED(device->CreatePredicate(&descriptor,&predicate)));
        context->Begin(predicate.Get());context->End(predicate.Get());context->Flush();
        BOOL result=TRUE;HRESULT hr=S_FALSE;
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while((hr=context->GetData(predicate.Get(),&result,sizeof(result),0))==S_FALSE
            && std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
        assert(hr==S_OK && result==FALSE);
    }
    const auto enable_predicate=[&] {if(predicated)context->SetPredication(predicate.Get(),FALSE);};
    const auto check_predicate=[&] {
        if(!predicated)return;
        ComPtr<ID3D11Predicate> observed;BOOL value=TRUE;
        context->GetPredication(&observed,&value);
        assert(observed.Get()==predicate.Get() && value==FALSE);
        context->SetPredication(nullptr,FALSE);
    };
    using Images=ReplayGpuImageEquality::Images;
    Images a,b;
    std::array<std::vector<unsigned char>,4> original;
    for(unsigned i=0;i<4;++i) {
        const unsigned stride=(i%2?8:16);
        std::vector<unsigned char> bytes(1024*1024*stride);
        // Include signed zero, NaN payloads, infinities and all 16-bit words.
        for(std::size_t j=0;j<bytes.size();++j)bytes[j]=static_cast<unsigned char>((j/256)^j);
        if(i==0){std::fill(bytes.begin(),bytes.end(),0);bytes[16*123*stride]=1;}
        if(i==1 || i==3) {
            std::fill(bytes.begin(),bytes.end(),0);
            // Half-float NaN payload and negative zero, kept as integer bits.
            bytes[17*16*stride]=1;bytes[17*16*stride+1]=0x7e;
            bytes[17*16*stride+3]=0x80;
        }
        original[i]=bytes;
        D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=1024;d.MipLevels=d.ArraySize=1;
        d.Format=ReplayGpuImageEquality::StorageFormat(i);d.SampleDesc.Count=1;
        d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA initial{bytes.data(),1024*stride,0};
        assert(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&a[i])));
        // The production source is FLOAT. Cross-family copying must preserve
        // every bit; no typed floating-point load is part of verification.
        auto native_desc=d;native_desc.Format=i%2?DXGI_FORMAT_R16G16B16A16_FLOAT:DXGI_FORMAT_R32G32B32A32_FLOAT;
        ComPtr<ID3D11Texture2D> native;
        assert(SUCCEEDED(device->CreateTexture2D(&native_desc,&initial,&native)));
        enable_predicate();
        ReplayGpuCopyResource(context.Get(),a[i].Get(),native.Get());
        check_predicate();
        if(i==1)bytes.back()^=1; // Last texel must be visited.
        if(i==2)bytes[0]^=0x80; // Sign bit / payload changes cannot compare as floats.
        assert(SUCCEEDED(device->CreateTexture2D(&d,&initial,&b[i])));
    }
    ReplayGpuImageEquality equality;
    assert(FAILED(equality.Prepare(device.Get(),a,b,ReplayGpuImageEquality::reservation_bytes-1)));
    assert(SUCCEEDED(equality.Prepare(device.Get(),a,b,ReplayGpuImageEquality::reservation_bytes)));
    ReplayGpuCompletion completion;
    assert(SUCCEEDED(completion.Prepare(device.Get())));
    std::array<bool,4> equal{true,true,true,true};
    assert(!equality.Read(context.Get(),completion,equal));
    assert((equal==std::array<bool,4>{true,true,true,true}));
    enable_predicate();
    assert(SUCCEEDED(equality.Dispatch(context.Get(),completion)));
    check_predicate();
    assert(!equality.Read(context.Get(),completion,equal));
    assert(SUCCEEDED(completion.Submit(context.Get(),ReplayGpuCompletion::Clock::now()+std::chrono::seconds(10))));
    while(!completion.retired()) {completion.Poll(context.Get(),ReplayGpuCompletion::Clock::now());std::this_thread::yield();}
    assert(equality.Read(context.Get(),completion,equal));
    assert((equal==std::array<bool,4>{true,false,false,true}));
    ReplayGpuImageEquality::TileResult tiles;
    assert(equality.ReadTiles(context.Get(),completion,tiles));
    assert(tiles.equal==equal);
    for(unsigned i=0;i<4;++i) {
        const unsigned stride=i%2?8:16;
        for(unsigned tile=0;tile<4096;++tile) {
            bool changed=false;
            for(unsigned y=0;y<16;++y) {
                const auto* row=original[i].data()+((tile/64*16+y)*1024+tile%64*16)*stride;
                const auto* baseline=original[i].data()+((1008+y)*1024+1008)*stride;
                changed|=std::memcmp(row,baseline,16*stride)!=0;
            }
            assert(tiles.nonuniform[i][tile]==unsigned(changed));
        }
    }
    // A different query cannot authorize this comparison's results.
    ReplayGpuCompletion unrelated;
    assert(!equality.Read(context.Get(),unrelated,equal));
    assert(completion.Release());
    std::array<ReplayGpuPackedImage::Handle,4> packed;
    ReplayGpuCompletion packed_done;assert(SUCCEEDED(packed_done.Prepare(device.Get())));
    for(unsigned i=0;i<4;++i) {
        const auto bytes=ReplayGpuPackedImage::RequiredBytes(i,tiles.nonuniform[i]);
        auto image=std::make_shared<ReplayGpuPackedImage>();
        if(!bytes) {assert(i==2);continue;} // Dense snapshot keeps full storage.
        assert(FAILED(image->Prepare(device.Get(),a[i].Get(),i,tiles.nonuniform[i],bytes-1)));
        assert(SUCCEEDED(image->Prepare(device.Get(),a[i].Get(),i,tiles.nonuniform[i],bytes)));
        assert(image->owned_bytes()==bytes && bytes<1024*1024*(i%2?8:16));
        assert(!image->Complete(packed_done));
        enable_predicate();
        assert(SUCCEEDED(image->SubmitCopies(context.Get(),packed_done)));
        check_predicate();
        packed[i]=image;
    }
    assert(SUCCEEDED(packed_done.Submit(context.Get(),ReplayGpuCompletion::Clock::now()+std::chrono::seconds(10))));
    for(auto& image:packed)if(image)assert(!image->Complete(packed_done));
    while(!packed_done.retired()){packed_done.Poll(context.Get(),ReplayGpuCompletion::Clock::now());std::this_thread::yield();}
    for(auto& image:packed)if(image)assert(image->Complete(packed_done));
    std::array<ComPtr<ID3D11Texture2D>,4> expanded,readback;
    for(unsigned i=0;i<4;++i)if(packed[i]) {
        D3D11_TEXTURE2D_DESC d{};a[i]->GetDesc(&d);d.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
        assert(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&expanded[i])));
        d.BindFlags=0;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        assert(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&readback[i])));
        auto overwritten=original[i];std::fill(overwritten.begin(),overwritten.end(),0x5a);
        context->UpdateSubresource(a[i].Get(),0,nullptr,overwritten.data(),1024*(i%2?8:16),0);
    }
    ReplayGpuImageMaterializer materializer;
    assert(SUCCEEDED(materializer.Prepare(device.Get(),packed,expanded,ReplayGpuImageMaterializer::reservation_bytes)));
    ReplayGpuCompletion expanded_done;assert(SUCCEEDED(expanded_done.Prepare(device.Get())));
    enable_predicate();
    assert(SUCCEEDED(materializer.Dispatch(context.Get())));
    check_predicate();
    for(unsigned i=0;i<4;++i)if(packed[i])context->CopyResource(readback[i].Get(),expanded[i].Get());
    assert(SUCCEEDED(expanded_done.Submit(context.Get(),ReplayGpuCompletion::Clock::now()+std::chrono::seconds(10))));
    while(!expanded_done.retired()){expanded_done.Poll(context.Get(),ReplayGpuCompletion::Clock::now());std::this_thread::yield();}
    assert(expanded_done.result()==ReplayGpuCompletion::Result::Complete);
    for(unsigned i=0;i<4;++i)if(packed[i]) {
        D3D11_MAPPED_SUBRESOURCE mapped{};assert(SUCCEEDED(context->Map(readback[i].Get(),0,D3D11_MAP_READ,0,&mapped)));
        const unsigned stride=1024*(i%2?8:16);
        for(unsigned y=0;y<1024;++y)
            assert(std::memcmp(static_cast<const unsigned char*>(mapped.pData)+y*mapped.RowPitch,original[i].data()+y*stride,stride)==0);
        context->Unmap(readback[i].Get(),0);
    }
}
int main() {
    Run(D3D_DRIVER_TYPE_WARP);
    Run(D3D_DRIVER_TYPE_HARDWARE);
    Run(D3D_DRIVER_TYPE_WARP,true);
    Run(D3D_DRIVER_TYPE_HARDWARE,true);
    std::puts("Full GPU image equality, exact bits, completion identity and capacity passed");
}
