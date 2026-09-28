#pragma once
#include "ReplayGpuImageEquality.hpp"
#include "ReplayGpuImageExpandShader.hpp"
#include <memory>
#include <algorithm>

namespace Horse::Deterministic {
// Independent immutable snapshot: atlas tile zero is the original final tile;
// every differing tile has its own exact copy. No earlier checkpoint is needed.
// The enclosing RHI owner retains preparation and source leases through its
// completion query, including cancellation, partial preparation and failure.
class ReplayGpuPackedImage final {
    template<class T>using Com=Microsoft::WRL::ComPtr<T>;
public:
    using Handle=std::shared_ptr<ReplayGpuPackedImage>;
    using Flags=std::array<UINT,4096>;
    static std::size_t RequiredBytes(unsigned plane,const Flags& flags) noexcept {
        if(plane>=4 || flags.back())return 0;
        unsigned count=1;for(auto flag:flags){if(flag>1)return 0;count+=flag;}
        const auto columns=(std::min)(64u,count),rows=(count+63)/64;
        const auto pixels=std::size_t(columns)*rows*256*(plane%2?8:16);
        const auto bytes=pixels+sizeof(ReplayGpuPackedImage)+sizeof(Flags)+4096+64;
        return bytes<std::size_t(1024)*1024*(plane%2?8:16)?bytes:0;
    }
    HRESULT Prepare(ID3D11Device* device,ID3D11Texture2D* source,unsigned plane,const Flags& flags,std::size_t budget) noexcept {
        const auto required=RequiredBytes(plane,flags);
        if(!device || !source || device_ || !required)return E_INVALIDARG;
        if(required>budget)return E_OUTOFMEMORY;
        D3D11_TEXTURE2D_DESC d{};source->GetDesc(&d);
        Com<ID3D11Device> source_device;source->GetDevice(&source_device);
        if(source_device.Get()!=device || d.Width!=1024 || d.Height!=1024 || d.ArraySize!=1 || d.MipLevels!=1
            || d.SampleDesc.Count!=1 || d.SampleDesc.Quality || d.Format!=ReplayGpuImageEquality::StorageFormat(plane))return E_INVALIDARG;
        bytes_=required;device_=device;source_=source;
        unsigned count=1;
        for(unsigned i=0;i<4096;++i)mapping_[i]=flags[i]?count++:0;
        columns_=(std::min)(64u,count);
        d.Width=columns_*16;d.Height=((count+63)/64)*16;
        d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;d.CPUAccessFlags=d.MiscFlags=0;
        auto hr=device->CreateTexture2D(&d,nullptr,&atlas_);if(FAILED(hr))return hr;
        if(FAILED(hr=device->CreateShaderResourceView(atlas_.Get(),nullptr,&atlas_view_)))return hr;
        D3D11_BUFFER_DESC buffer{};buffer.ByteWidth=sizeof(mapping_);buffer.Usage=D3D11_USAGE_IMMUTABLE;
        buffer.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA initial{mapping_.data(),0,0};
        if(FAILED(hr=device->CreateBuffer(&buffer,&initial,&indices_)))return hr;
        D3D11_SHADER_RESOURCE_VIEW_DESC view{};view.Format=DXGI_FORMAT_R32_UINT;
        view.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;view.Buffer.NumElements=4096;
        if(FAILED(hr=device->CreateShaderResourceView(indices_.Get(),&view,&index_view_)))return hr;
        prepared_=true;return S_OK;
    }
    bool CanSubmitCopies(ID3D11DeviceContext* context,const ReplayGpuCompletion& completion) const noexcept {
        return prepared_ && !completion_ && ContextMatches(context) && source_
            && completion.result()==ReplayGpuCompletion::Result::Prepared && completion.submitted_serial()!=UINT64_MAX;
    }
    HRESULT SubmitCopies(ID3D11DeviceContext* context,const ReplayGpuCompletion& completion) noexcept {
        if(!CanSubmitCopies(context,completion))return E_INVALIDARG;
        ReplayGpuUnconditionalCommands commands(context);
        D3D11_BOX baseline{1008,1008,0,1024,1024,1};
        context->CopySubresourceRegion(atlas_.Get(),0,0,0,0,source_.Get(),0,&baseline);
        for(unsigned i=0;i<4096;++i)if(const auto target=mapping_[i]) {
            D3D11_BOX tile{(i%64)*16,(i/64)*16,0,(i%64)*16+16,(i/64)*16+16,1};
            context->CopySubresourceRegion(atlas_.Get(),0,(target%columns_)*16,(target/columns_)*16,0,source_.Get(),0,&tile);
        }
        completion_=&completion;serial_=completion.submitted_serial()+1;return S_OK;
    }
    bool Complete(const ReplayGpuCompletion& completion) noexcept {
        if(completion_!=&completion || completion.submitted_serial()!=serial_ || !completion.retired()
            || completion.result()!=ReplayGpuCompletion::Result::Complete)return false;
        ready_=true;source_.Reset();return true;
    }
    bool ready() const noexcept{return ready_;}
    std::size_t owned_bytes() const noexcept{return bytes_;}
    ID3D11ShaderResourceView* atlas_view() const noexcept{return atlas_view_.Get();}
    ID3D11ShaderResourceView* index_view() const noexcept{return index_view_.Get();}
    ID3D11Device* device() const noexcept{return device_.Get();}
    DXGI_FORMAT format() const noexcept{D3D11_TEXTURE2D_DESC d{};if(atlas_)atlas_->GetDesc(&d);return d.Format;}
private:
    bool ContextMatches(ID3D11DeviceContext* context) const noexcept {
        if(!context || context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return false;
        Com<ID3D11Device> owner;context->GetDevice(&owner);return owner.Get()==device_.Get();
    }
    Com<ID3D11Device> device_;
    Com<ID3D11Texture2D> source_,atlas_;
    Com<ID3D11Buffer> indices_;
    Com<ID3D11ShaderResourceView> atlas_view_,index_view_;
    Flags mapping_{};
    const ReplayGpuCompletion* completion_{};
    std::uint64_t serial_{};
    std::size_t bytes_{};
    unsigned columns_{};
    bool prepared_{},ready_{};
};

// Operation-private views/shader live until the existing publication completion
// guard permits Finish. Preparing views never writes a target texture.
class ReplayGpuImageMaterializer final {
    template<class T>using Com=Microsoft::WRL::ComPtr<T>;
public:
    using Images=std::array<Com<ID3D11Texture2D>,4>;
    using Packed=std::array<ReplayGpuPackedImage::Handle,4>;
    static constexpr std::size_t reservation_bytes=64*1024;
    HRESULT Prepare(ID3D11Device* device,const Packed& packed,const Images& targets,std::size_t budget) noexcept {
        if(!device || device_ || budget<reservation_bytes)return E_INVALIDARG;
        bool any=false;
        for(unsigned i=0;i<4;++i)if(packed[i]) {
            if(!packed[i]->ready() || packed[i]->device()!=device || !targets[i])return E_INVALIDARG;
            D3D11_TEXTURE2D_DESC d{};targets[i]->GetDesc(&d);
            Com<ID3D11Device> owner;targets[i]->GetDevice(&owner);
            if(owner.Get()!=device || d.Width!=1024 || d.Height!=1024 || d.ArraySize!=1 || d.MipLevels!=1
                || d.SampleDesc.Count!=1 || d.SampleDesc.Quality || d.Format!=packed[i]->format()
                || !(d.BindFlags&D3D11_BIND_UNORDERED_ACCESS))return E_INVALIDARG;
            any=true;
        }
        if(!any)return E_INVALIDARG;
        device_=device;packed_=packed;
        auto hr=device->CreateComputeShader(replay_gpu_image_expand_shader,sizeof(replay_gpu_image_expand_shader),nullptr,&shader_);
        if(FAILED(hr))return hr;
        for(unsigned i=0;i<4;++i)if(packed[i]) {
            if(FAILED(hr=device->CreateUnorderedAccessView(targets[i].Get(),nullptr,&targets_[i])))return hr;
        }
        ready_=true;return S_OK;
    }
    HRESULT Dispatch(ID3D11DeviceContext* context) noexcept {
        if(!ready_ || !context || context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return E_INVALIDARG;
        Com<ID3D11Device> device;context->GetDevice(&device);if(device.Get()!=device_.Get())return E_INVALIDARG;
        ReplayGpuUnconditionalCommands commands(context);
        Com<ID3D11ComputeShader> shader;
        std::array<ID3D11ClassInstance*,256> instances{};UINT count=256;
        context->CSGetShader(&shader,instances.data(),&count);
        std::array<ID3D11ShaderResourceView*,2> resources{};context->CSGetShaderResources(0,2,resources.data());
        Com<ID3D11UnorderedAccessView> output;context->CSGetUnorderedAccessViews(0,1,&output);
        const UINT preserve=UINT(-1);
        context->CSSetShader(shader_.Get(),nullptr,0);
        for(unsigned i=0;i<4;++i)if(packed_[i]) {
            ID3D11ShaderResourceView* inputs[]{packed_[i]->atlas_view(),packed_[i]->index_view()};
            auto* target=targets_[i].Get();
            context->CSSetShaderResources(0,2,inputs);context->CSSetUnorderedAccessViews(0,1,&target,&preserve);
            context->Dispatch(64,64,1);
        }
        auto* previous=output.Get();context->CSSetUnorderedAccessViews(0,1,&previous,&preserve);
        context->CSSetShaderResources(0,2,resources.data());context->CSSetShader(shader.Get(),instances.data(),count);
        for(auto* resource:resources)if(resource)resource->Release();
        for(UINT i=0;i<count;++i)if(instances[i])instances[i]->Release();
        return S_OK;
    }
private:
    Com<ID3D11Device> device_;
    Com<ID3D11ComputeShader> shader_;
    Packed packed_;
    std::array<Com<ID3D11UnorderedAccessView>,4> targets_;
    bool ready_{};
};
}
