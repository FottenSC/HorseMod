#pragma once
#include "ReplayGpuCompletion.hpp"
#include "ReplayGpuCommandState.hpp"
#include "ReplayGpuImageEqualityShader.hpp"
#include <array>
#include <cstddef>
#include <cstring>

namespace Horse::Deterministic {
// Enclosed by the existing RHI transaction and its completion owner. All
// texture owners outlive completion, including failed/cancelled operations.
// Only four private integer-view images are read; native bindings are untouched.
class ReplayGpuImageEquality final {
    template<class T> using Com=Microsoft::WRL::ComPtr<T>;
public:
    using Images=std::array<Com<ID3D11Texture2D>,4>;
    struct TileResult {std::array<bool,4> equal{};std::array<std::array<UINT,4096>,4> nonuniform{};};
    static constexpr UINT result_words=4*4097;
    static constexpr UINT readback_bytes=result_words*sizeof(UINT);
    // Bounded CPU/COM bookkeeping, bytecode, shader, result buffers and the
    // caller's bounded tile census. No full-image CPU readback is required.
    // Driver-internal allocation overhead still belongs in the native audit.
    static constexpr std::size_t reservation_bytes=512*1024;
    static constexpr DXGI_FORMAT StorageFormat(unsigned i) noexcept {
        return i%2?DXGI_FORMAT_R16G16B16A16_UINT:DXGI_FORMAT_R32G32B32A32_UINT;
    }
    HRESULT Prepare(ID3D11Device* device,const Images& candidate,const Images& retained,std::size_t budget) noexcept {
        if(!device || device_ || budget<reservation_bytes)return E_INVALIDARG;
        // Validate every image before acquiring any operation storage.
        for(unsigned i=0;i<4;++i)for(const auto* images:{&candidate,&retained}) {
            if(!(*images)[i])return E_INVALIDARG;
            D3D11_TEXTURE2D_DESC d{};(*images)[i]->GetDesc(&d);
            Com<ID3D11Device> owner;(*images)[i]->GetDevice(&owner);
            if(owner.Get()!=device || d.Width!=1024 || d.Height!=1024 || d.MipLevels!=1 || d.ArraySize!=1
                || d.SampleDesc.Count!=1 || d.SampleDesc.Quality || d.Format!=StorageFormat(i)
                || !(d.BindFlags&D3D11_BIND_SHADER_RESOURCE))return E_INVALIDARG;
        }
        device_=device;
        auto hr=device->CreateComputeShader(replay_gpu_image_equality_shader,sizeof(replay_gpu_image_equality_shader),nullptr,&shader_);
        if(FAILED(hr))return hr;
        for(unsigned i=0;i<4;++i) {
            if(FAILED(hr=device->CreateShaderResourceView(candidate[i].Get(),nullptr,&candidate_[i])))return hr;
            if(FAILED(hr=device->CreateShaderResourceView(retained[i].Get(),nullptr,&retained_[i])))return hr;
        }
        D3D11_BUFFER_DESC d{};d.ByteWidth=readback_bytes;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
        if(FAILED(hr=device->CreateBuffer(&d,nullptr,&result_)))return hr;
        d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        if(FAILED(hr=device->CreateBuffer(&d,nullptr,&readback_)))return hr;
        D3D11_UNORDERED_ACCESS_VIEW_DESC view{};view.Format=DXGI_FORMAT_R32_UINT;view.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;
        view.Buffer.NumElements=result_words;
        if(FAILED(hr=device->CreateUnorderedAccessView(result_.Get(),&view,&clear_)))return hr;
        view.Buffer.NumElements=4097;
        for(unsigned i=0;i<4;++i) {
            view.Buffer.FirstElement=i*4097;
            if(FAILED(hr=device->CreateUnorderedAccessView(result_.Get(),&view,&output_[i])))return hr;
        }
        ready_=true;return S_OK;
    }
    HRESULT Dispatch(ID3D11DeviceContext* context,const ReplayGpuCompletion& completion) noexcept {
        if(!ready_ || completion_ || !ContextMatches(context)
            || completion.result()!=ReplayGpuCompletion::Result::Prepared
            || completion.submitted_serial()==UINT64_MAX)return E_INVALIDARG;
        ReplayGpuUnconditionalCommands commands(context);
        // Save precisely the touched native slots, including dynamic linkage.
        Com<ID3D11ComputeShader> previous_shader;
        std::array<ID3D11ClassInstance*,256> instances{};UINT instance_count=256;
        context->CSGetShader(&previous_shader,instances.data(),&instance_count);
        std::array<ID3D11ShaderResourceView*,2> previous_resources{};
        context->CSGetShaderResources(0,2,previous_resources.data());
        Com<ID3D11UnorderedAccessView> previous_output;
        context->CSGetUnorderedAccessViews(0,1,&previous_output);
        const UINT zero[4]{};context->ClearUnorderedAccessViewUint(clear_.Get(),zero);
        context->CSSetShader(shader_.Get(),nullptr,0);
        const UINT preserve=UINT(-1);
        for(unsigned i=0;i<4;++i) {
            ID3D11ShaderResourceView* resources[]{candidate_[i].Get(),retained_[i].Get()};
            auto* output=output_[i].Get();
            context->CSSetShaderResources(0,2,resources);
            context->CSSetUnorderedAccessViews(0,1,&output,&preserve);
            context->Dispatch(64,64,1);
        }
        auto* old_output=previous_output.Get();
        context->CSSetUnorderedAccessViews(0,1,&old_output,&preserve);
        context->CSSetShaderResources(0,2,previous_resources.data());
        context->CSSetShader(previous_shader.Get(),instances.data(),instance_count);
        for(auto* resource:previous_resources)if(resource)resource->Release();
        for(UINT i=0;i<instance_count;++i)if(instances[i])instances[i]->Release();
        context->CopyResource(readback_.Get(),result_.Get());
        completion_=&completion;serial_=completion.submitted_serial()+1;
        return S_OK;
    }
    bool Read(ID3D11DeviceContext* context,const ReplayGpuCompletion& completion,std::array<bool,4>& output) noexcept {
        TileResult tiles;
        if(!ReadTiles(context,completion,tiles))return false;
        output=tiles.equal;return true;
    }
    bool ReadTiles(ID3D11DeviceContext* context,const ReplayGpuCompletion& completion,TileResult& output) noexcept {
        if(!ContextMatches(context) || completion_!=&completion || completion.submitted_serial()!=serial_
            || !completion.retired() || completion.result()!=ReplayGpuCompletion::Result::Complete)return false;
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if(FAILED(context->Map(readback_.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&mapped)))return false;
        const auto* values=static_cast<const UINT*>(mapped.pData);
        for(unsigned i=0;i<result_words;++i)if(values[i]>1){context->Unmap(readback_.Get(),0);return false;}
        for(unsigned i=0;i<4;++i) {
            output.equal[i]=values[i*4097]==0;
            std::memcpy(output.nonuniform[i].data(),values+i*4097+1,sizeof(output.nonuniform[i]));
        }
        context->Unmap(readback_.Get(),0);
        return true;
    }
private:
    bool ContextMatches(ID3D11DeviceContext* context) const noexcept {
        if(!context || context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return false;
        Com<ID3D11Device> owner;context->GetDevice(&owner);return owner && owner.Get()==device_.Get();
    }
    Com<ID3D11Device> device_;
    Com<ID3D11ComputeShader> shader_;
    std::array<Com<ID3D11ShaderResourceView>,4> candidate_,retained_;
    std::array<Com<ID3D11UnorderedAccessView>,4> output_;
    Com<ID3D11UnorderedAccessView> clear_;
    Com<ID3D11Buffer> result_,readback_;
    const ReplayGpuCompletion* completion_{};
    std::uint64_t serial_{};
    bool ready_{};
};
}
