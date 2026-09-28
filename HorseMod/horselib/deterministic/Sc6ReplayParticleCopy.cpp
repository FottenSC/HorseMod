#include "Sc6ReplayParticleCopy.hpp"
#include "Sc6ReplayGroundDebrisState.hpp"
#include "Sc6ReplayTraceState.hpp"
#include "ReplaySparseRegistry.hpp"
#include "ReplayCaptureAccounting.hpp"
#include <algorithm>
#include <cstring>
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UObject.hpp>

namespace Horse::Deterministic
{
namespace
{
template<class T> T& Field(std::uintptr_t address, std::size_t offset = 0)
{ return *reinterpret_cast<T*>(address + offset); }
constexpr std::array<unsigned, 6> pixel_bytes{16, 8, 16, 8, 4, 4};
constexpr std::uint64_t image_bytes = Sc6ReplayParticleCopy::minimum_private_capture_bytes;
bool ReadBytes(void* output, const void* input, std::size_t bytes) noexcept
{
    __try { if (bytes) std::memcpy(output, input, bytes); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
}

#include "Sc6ReplayParticleCopy.Visibility.inl"
#include "Sc6ReplayParticleCopy.Lighting.inl"
#include "Sc6ReplayParticleCopy.Reopen.inl"
#include "Sc6ReplayParticleCopy.Capture.inl"

bool Sc6ReplayParticleCopy::BirthOwnersBinding() const noexcept
{
    __try {
        for (std::size_t i = 0; i < birth_count_; ++i) {
            const auto& birth = births_[i];
            if (!birth.component || birth.base != base_ || birth.system != system_ || birth.pool != pool_
                || Field<std::uint64_t>(base_, 0x4197170) != (execution_started_ ? execution_boundary_epoch_ : birth.epoch)
                || Field<std::uintptr_t>(birth.emitter) != base_ + 0x394c100
                || Field<std::uintptr_t>(birth.emitter, 0x18) != birth.component
                || Field<std::uintptr_t>(birth.emitter, 0x1d0) != system_
                || Field<std::uintptr_t>(birth.emitter, 0x1e0) != birth.render
                || Field<std::uintptr_t>(birth.render) != base_ + 0x394bfc0
                || Field<std::uintptr_t>(birth.render, 0x1f0) != base_ + 0x394c000
                || Field<std::uint8_t>(birth.render, 0x252)) return false;
            if (birth_fields_retained_) {
                if (std::memcmp(reinterpret_cast<void*>(birth.render + 0xe0), birth_fields_[i].data(), 0x110)) return false;
                const auto resource = Field<std::uintptr_t>(birth.render, 0xe0);
                if (resource && Field<std::uintptr_t>(resource) != birth_field_types_[i]) return false;
            }
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool Sc6ReplayParticleCopy::BirthBinding() const noexcept
{
    if (!BirthOwnersBinding()) return false;
    __try {
        const auto saved = reinterpret_cast<std::uintptr_t>(birth_registry_published_
            ? birth_excluded_registry_.data() : birth_registry_.data());
        const auto live = system_ + 0x40;
        for (const auto offset : {0u, 8u, 0x20u, 0x28u})
            if (Field<std::uint64_t>(live, offset) != Field<std::uint64_t>(saved, offset)) return false;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool Sc6ReplayParticleCopy::BirthRegistryMatchesExcluded() const noexcept
{
    if (!BirthBinding()) return false;
    __try {
        const auto registry = system_ + 0x40;
        const auto flags = Field<std::uintptr_t>(registry, 0x20);
        for (std::size_t i = 0; i < birth_count_; ++i)
            if (Field<std::int32_t>(births_[i].render, 0x240) != -1) return false;
        return !std::memcmp(reinterpret_cast<void*>(registry), birth_excluded_registry_.data(), birth_excluded_registry_.size())
            && (birth_excluded_slots_.empty() || !std::memcmp(Field<void*>(registry), birth_excluded_slots_.data(), birth_excluded_slots_.size()*8))
            && (birth_excluded_flags_.empty() || !std::memcmp(reinterpret_cast<void*>(flags ? flags : registry+0x10), birth_excluded_flags_.data(), birth_excluded_flags_.size()*4));
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool Sc6ReplayParticleCopy::BirthRegistryMatchesB() const noexcept
{
    if (!BirthBinding()) return false;
    __try {
        const auto registry = system_ + 0x40;
        const auto flags = Field<std::uintptr_t>(registry, 0x20);
        for (std::size_t i = 0; i < birth_count_; ++i)
            if (Field<std::int32_t>(births_[i].render, 0x240) != birth_indices_[i]) return false;
        return !std::memcmp(reinterpret_cast<void*>(registry), birth_registry_.data(), birth_registry_.size())
            && (birth_slots_.empty() || !std::memcmp(Field<void*>(registry), birth_slots_.data(), birth_slots_.size()*8))
            && (birth_flags_.empty() || !std::memcmp(reinterpret_cast<void*>(flags ? flags : registry+0x10), birth_flags_.data(), birth_flags_.size()*4));
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool Sc6ReplayParticleCopy::PrepareBirthRegistration(
    std::span<const Sc6ReplayVfxState::ParticleBirth::RenderBinding> bindings, std::size_t budget) noexcept
{
    if (witness_.phase != Phase::UndoReady || !completion_.retired() || witness_.birth_prepared
        || birth_dirty_ || !birth_slots_.empty() || !birth_flags_.empty()
        || !birth_excluded_slots_.empty() || !birth_excluded_flags_.empty()
        || bindings.size() > births_.size() || !Bindings(reinterpret_cast<void*>(world_), false)) return false;
    birth_count_ = bindings.size();
    std::copy(bindings.begin(), bindings.end(), births_.begin());
    if (!ReadBytes(birth_registry_.data(), reinterpret_cast<void*>(system_+0x40), birth_registry_.size())) return false;
    for (std::size_t i = 0; i < birth_count_; ++i)
        if (!ReadBytes(&birth_indices_[i], reinterpret_cast<void*>(births_[i].render+0x240), sizeof(birth_indices_[i]))) return false;
    if (!BirthBinding()) return false;
    for (std::size_t i = 0; i < birth_count_; ++i) {
        if (!ReadBytes(birth_fields_[i].data(), reinterpret_cast<void*>(births_[i].render+0xe0), 0x110)) return false;
        std::uintptr_t resource{};
        std::memcpy(&resource, birth_fields_[i].data(), sizeof(resource));
        if (resource && !ReadBytes(&birth_field_types_[i], reinterpret_cast<void*>(resource), sizeof(std::uintptr_t))) return false;
    }
    birth_fields_retained_ = true;
    const auto header = reinterpret_cast<std::uintptr_t>(birth_registry_.data());
    const auto count = Field<std::int32_t>(header, 8), capacity = Field<std::int32_t>(header, 0xc);
    const auto bits = Field<std::int32_t>(header, 0x28), max_bits = Field<std::int32_t>(header, 0x2c);
    const auto flags = Field<std::uintptr_t>(header, 0x20);
    if (count < 0 || count > 65536 || capacity < count || bits != count || max_bits < bits
        || (!flags && max_bits > 128) || (count && !Field<void*>(header))) return false;
    const auto words = (static_cast<std::size_t>(bits)+31)/32;
    const auto bytes = 2*(static_cast<std::size_t>(count)*8+words*4);
    if (witness_.bytes > budget || bytes > budget-witness_.bytes) return false;
    const auto allocated = [&] { return (birth_slots_.capacity()+birth_excluded_slots_.capacity())*8
        + (birth_flags_.capacity()+birth_excluded_flags_.capacity())*4; };
    try {
        birth_slots_.resize(count); birth_excluded_slots_.resize(count);
        birth_flags_.resize(words); birth_excluded_flags_.resize(words);
    } catch (...) { witness_.bytes += allocated(); return false; }
    witness_.bytes += allocated();
    if (witness_.bytes > budget
        || !ReadBytes(birth_slots_.data(), Field<void*>(header), birth_slots_.size()*8)
        || !ReadBytes(birth_flags_.data(), reinterpret_cast<void*>(flags ? flags : system_+0x50), words*4)) return false;
    for (std::size_t i = 0; i < birth_count_; ++i) {
        const auto index = birth_indices_[i];
        if (index < 0 || index >= count || birth_slots_[index] != births_[i].render) return false;
        for (std::size_t j = 0; j < i; ++j)
            if (births_[j].render == births_[i].render || births_[j].emitter == births_[i].emitter) return false;
    }
    birth_excluded_registry_ = birth_registry_;
    std::copy(birth_slots_.begin(), birth_slots_.end(), birth_excluded_slots_.begin());
    std::copy(birth_flags_.begin(), birth_flags_.end(), birth_excluded_flags_.begin());
    const auto excluded = reinterpret_cast<std::uintptr_t>(birth_excluded_registry_.data());
    if (!PlanReplaySparseRemoval(birth_excluded_slots_, birth_excluded_flags_,
        Field<std::int32_t>(excluded, 0x30), Field<std::int32_t>(excluded, 0x34), {birth_indices_.data(), birth_count_})) return false;
    if (!flags && words) std::memcpy(birth_excluded_registry_.data()+0x10, birth_excluded_flags_.data(), words*4);
    if (!BirthRegistryMatchesB()) return false;
    // Recheck on the admitted render thread after the host proves the exact
    // B-only owner set. A fields are still unsupported: the exception applies
    // only to storage that stays untouched and is excluded from A's registry.
    witness_.reconstruction_b = AdmitRenderReconstruction(true);
    if (!witness_.reconstruction_a || !witness_.reconstruction_b) return false;
    if (!PrepareRegistryStorage(budget)) return false;
    witness_.birth_prepared = true;
    return true;
}

bool Sc6ReplayParticleCopy::PrepareRegistryStorage(std::size_t budget) noexcept
{
    if (registry_prepared_) return true;
    const ReplaySparseRegistryStorage::Heap heap{
        this,
        [](void* owner, std::size_t bytes) -> void* {
            __try { return reinterpret_cast<void*(*)(std::size_t)>(static_cast<Sc6ReplayParticleCopy*>(owner)->base_+0x4a61c0)(bytes); }
            __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
        },
        [](void* owner, void* memory) {
            reinterpret_cast<void(*)(void*)>(static_cast<Sc6ReplayParticleCopy*>(owner)->base_+0xd46a00)(memory);
        },
        [](void* owner, std::size_t bytes) -> std::size_t {
            __try { return reinterpret_cast<std::size_t(*)(std::size_t,unsigned)>(static_cast<Sc6ReplayParticleCopy*>(owner)->base_+0xd50dc0)(bytes,0); }
            __except(EXCEPTION_EXECUTE_HANDLER) { return SIZE_MAX; }
        }};
    if (witness_.bytes > budget || !birth_registry_storage_.Prepare(reinterpret_cast<void*>(system_+0x40),
        heap, budget-witness_.bytes)) return false;
    witness_.bytes += birth_registry_storage_.owned_bytes();
    registry_prepared_=true;
    return true;
}

bool Sc6ReplayParticleCopy::ExcludeBirthRegistration() noexcept
{
    if (!witness_.birth_prepared || birth_dirty_ || !BirthRegistryMatchesB()) return false;
    birth_dirty_ = true;
    if (!birth_registry_storage_.Publish()) return false;
    // The removal plan retains its independently computed values. Only its
    // two backing bindings change to the newly installed native A allocation.
    if (!ReadBytes(birth_excluded_registry_.data(), reinterpret_cast<void*>(system_+0x40), 8)
        || !ReadBytes(birth_excluded_registry_.data()+0x20, reinterpret_cast<void*>(system_+0x60), 8)) return false;
    birth_registry_published_ = true;
    if (!birth_registry_storage_.BeginPublicationWrite()) return false;
    __try {
        // Every removal uses the native free-chain operation. Complete B stays
        // retained if any removal or render-index publication fails midway.
        for (std::size_t i = 0; i < birth_count_; ++i) {
            reinterpret_cast<void (*)(void*, int, int)>(base_+0x14f5500)(reinterpret_cast<void*>(system_+0x40), birth_indices_[i], 1);
            Field<std::int32_t>(births_[i].render, 0x240) = -1;
        }
        birth_write_complete_ = true;
        if (!BirthRegistryMatchesExcluded()) return false;
        if (!birth_registry_storage_.SealPublicationWrite()) return false;
        witness_.birth_excluded = true;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool Sc6ReplayParticleCopy::UndoBirthRegistration() noexcept
{
    if (!birth_dirty_) {
        if (birth_registry_storage_.pending() && !birth_registry_storage_.Undo()) return false;
        return !witness_.birth_prepared || BirthRegistryMatchesB();
    }
    if (!BirthOwnersBinding()) return false;
    if (birth_write_complete_ && witness_.birth_excluded && !birth_undo_started_ && !BirthRegistryMatchesExcluded()) return false;
    birth_undo_started_ = true;
    if (birth_registry_storage_.pending() && !birth_registry_storage_.Undo()) return false;
    birth_registry_published_ = false;
    if (!BirthBinding()) return false;
    __try {
        const auto registry = system_+0x40;
        const auto flags = Field<std::uintptr_t>(registry, 0x20);
        if (!birth_slots_.empty()) std::memcpy(Field<void*>(registry), birth_slots_.data(), birth_slots_.size()*8);
        if (!birth_flags_.empty()) std::memcpy(reinterpret_cast<void*>(flags ? flags : registry+0x10), birth_flags_.data(), birth_flags_.size()*4);
        std::memcpy(reinterpret_cast<void*>(registry), birth_registry_.data(), birth_registry_.size());
        for (std::size_t i = 0; i < birth_count_; ++i) Field<std::int32_t>(births_[i].render, 0x240) = birth_indices_[i];
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    if (!BirthRegistryMatchesB()) return false;
    birth_dirty_ = birth_write_complete_ = birth_undo_started_ = false;
    witness_.birth_recovered = true;
    return true;
}

bool Sc6ReplayParticleCopy::Bindings(void* world, bool retain) noexcept
{
    if(capture_owner_fault_)return false;
    __try
    {
        const auto address = reinterpret_cast<std::uintptr_t>(world);
        if (!address || address != world_) return false;
        const auto scene = Field<std::uintptr_t>(address, 0x168);
        const auto system = Field<std::uintptr_t>(address, 0x770);
        if (!scene || !system || Field<std::uintptr_t>(scene) != base_ + 0x3657a70
            || Field<std::uintptr_t>(scene, 0x10) != system
            || Field<std::uintptr_t>(system) != base_ + 0x3941830) return false;
        const auto pool = Field<std::uintptr_t>(system, 0x78);
        if (!pool || Field<std::uintptr_t>(pool) != base_ + 0x394bd48
            || Field<std::uintptr_t>(pool, 0x58) != base_ + 0x394bd48
            || Field<std::uintptr_t>(pool, 0xb0) != base_ + 0x394bde8
            || Field<std::uintptr_t>(pool, 0xf0) != base_ + 0x394bde8) return false;
        if (!retain && (scene != scene_ || system != system_ || pool != pool_)) return false;
        // Inspect the entire binding set before acquiring any reference.
        std::array<void*, 6> wrappers{};
        for (std::size_t i = 0; i < wrappers.size(); ++i)
        {
            const auto wrapper = Field<std::uintptr_t>(pool, offsets_[i]);
            if (!wrapper || Field<std::uintptr_t>(pool, offsets_[i] + 8) != wrapper
                || Field<std::uintptr_t>(wrapper) != base_ + 0x35d3ea0
                || Field<LONG>(wrapper, 8) <= 0 || Field<LONG>(wrapper, 0xc) != 0
                || !Field<void*>(wrapper, 0xa0) || !Field<void*>(wrapper, 0x80)) return false;
            wrappers[i] = reinterpret_cast<void*>(wrapper);
            if (!retain && wrappers[i] != wrappers_[i]) return false;
            if (!retain && Field<ID3D11Texture2D*>(wrapper, 0xa0) != sources_[i].Get()) return false;
            for (std::size_t j = 0; j < i; ++j)
                if (wrappers[j] == wrappers[i]) return false;
        }
        if (retain)
        {
            scene_ = scene; system_ = system; pool_ = pool;
            witness_.system = system; witness_.pool = pool;
            const auto assign = reinterpret_cast<void** (*)(void**, void*)>(base_ + 0x15b83f0);
            for (std::size_t i = 0; i < wrappers.size(); ++i) assign(&wrappers_[i], wrappers[i]);
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// SSR history is a retained native pooled target, not a seventh copied
// particle image. 141451C50 reuses only non-transient targets at refcount one;
// 1414BEF10 reads +B60 as input and 1413B4C50 replaces it with a newly allocated
// pass output. Retaining A excludes it from subsequent output allocation.
bool Sc6ReplayParticleCopy::ReflectionBinding(std::uintptr_t target, ID3D11Texture2D* texture, int minimum_refs) const noexcept
{
    __try {
        if(!view_state_ || Field<std::uintptr_t>(view_state_)!=base_+0x3657810 || !target
            || Field<std::uintptr_t>(target)!=base_+0x364c2a8 || Field<int>(target,0x48)<(Field<std::uintptr_t>(target,0xa8) ? minimum_refs : 1)
            || Field<unsigned char>(target,0xa0)!=0) return false;
        const auto wrapper=Field<std::uintptr_t>(target,0x10);
        return wrapper && wrapper==Field<std::uintptr_t>(target,8)
            && Field<std::uintptr_t>(wrapper)==base_+0x35d3ea0
            && Field<LONG>(wrapper,8)>0 && Field<ID3D11Texture2D*>(wrapper,0xa0)==texture;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

bool Sc6ReplayParticleCopy::RetainReflection(bool original,std::size_t budget) noexcept
{
    if(!view_state_) return true; // Texture-only diagnostic; historical gate requires the history witness.
    __try {
        if(Field<std::uintptr_t>(view_state_)!=base_+0x3657810) return false;
        // Histogram adaptation14138B8D0 writes current AE0 in place;
        // histogram reduction141384D40 reads it on the next publication.
        // Admit the observed mature single-target path, not a guessed image
        // for an uninitialized or double-buffered exposure controller.
        if(Field<unsigned>(view_state_,0xad8)!=0 || Field<std::uintptr_t>(view_state_,0xae8)
            || Field<unsigned char>(view_state_,0xaf0)!=1) return false;
        for(auto& h : histories_) {
        const auto target=Field<std::uintptr_t>(view_state_,h.offset);
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] reflection source admission original={} scene_color_history={:x}\n"),original,Field<std::uintptr_t>(view_state_,0xb40));
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] reflection admission original={} target={:x} captured={:x} refs={} transient={}\n"), original,target,h.a,target ? Field<int>(target,0x48) : -1,target ? Field<unsigned char>(target,0xa0) : 255);
        if(!target || Field<std::uintptr_t>(target)!=base_+0x364c2a8) return false;
        const auto wrapper=Field<std::uintptr_t>(target,0x10);
        if(!wrapper || Field<std::uintptr_t>(wrapper)!=base_+0x35d3ea0) return false;
        auto* source=Field<ID3D11Texture2D*>(wrapper,0xa0);
        if(!source || !ReflectionBinding(target,source,1)) return false;
        D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);
        ID3D11Device* device{};source->GetDevice(&device);
        const bool same_device=device==device_.Get();if(device) device->Release();
        if(!same_device || !desc.Width || !desc.Height || desc.Width>4096 || desc.Height>4096
            || desc.MipLevels!=1 || desc.ArraySize!=1 || desc.SampleDesc.Count!=1) return false;
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] reflection descriptor original={} width={} height={} format={} mips={} array={} samples={} same_device={}\n"), original,desc.Width,desc.Height,static_cast<unsigned>(desc.Format),desc.MipLevels,desc.ArraySize,desc.SampleDesc.Count,same_device);
        const unsigned bpp=(desc.Format==DXGI_FORMAT_R16G16B16A16_FLOAT || desc.Format==DXGI_FORMAT_R32G32_FLOAT) ? 8 : (desc.Format==DXGI_FORMAT_R11G11B10_FLOAT || desc.Format==DXGI_FORMAT_R10G10B10A2_UNORM) ? 4 : 0;
        if(!bpp) return false;
        if(h.offset==0xae0 && (desc.Width!=1 || desc.Height!=1 || desc.Format!=DXGI_FORMAT_R32G32_FLOAT)) return false;
        const bool external=Field<std::uintptr_t>(target,0xa8)==0;
        const bool copy_pixels=external || h.offset==0xae0;
        if(original) {
            h.external=external;h.copy_pixels=copy_pixels;
            const auto bytes=std::uint64_t(desc.Width)*desc.Height*bpp;
            const auto required=bytes*((copy_pixels ? 4 : 2)+(witness_.diagnostic_readbacks ? 1 : 0))+2*0xb8;
            if(witness_.bytes>budget || required>budget-witness_.bytes) {
                RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] checkpoint capacity participant=reflection_history native_frame={} offset={:x} owned={} envelope={} available={} budget={} external={} copy_pixels={}\n"),
                    Field<std::uint64_t>(base_,0x4197170),h.offset,witness_.bytes,required,
                    witness_.bytes<budget?budget-witness_.bytes:0,budget,external,copy_pixels);
                return false;
            }
            witness_.bytes+=bytes*((copy_pixels ? 4 : 2)+(witness_.diagnostic_readbacks ? 1 : 0))+2*0xb8; // Native A/B, readback, and private A/B for targets written in place.
            h.descriptor=desc;h.pixel_bytes=bpp;
            desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=desc.MiscFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            if(witness_.diagnostic_readbacks && FAILED(device_->CreateTexture2D(&desc,nullptr,h.staging.GetAddressOf()))) return false;
            if(copy_pixels) {
                desc.Usage=D3D11_USAGE_DEFAULT;desc.CPUAccessFlags=0;
                if(FAILED(device_->CreateTexture2D(&desc,nullptr,h.image_a.GetAddressOf()))
                    || FAILED(device_->CreateTexture2D(&desc,nullptr,h.image_b.GetAddressOf()))) return false;
            }
        } else if(!h.a || h.b
            || desc.Width!=h.descriptor.Width || desc.Height!=h.descriptor.Height
            || desc.Format!=h.descriptor.Format || external!=h.external || copy_pixels!=h.copy_pixels
            // One shared external destination: the B image is complete undo
            // for every native alias touched by publishing A. Other bindings
            // need a separately audited destination undo and are not admitted.
            || (copy_pixels && source!=h.source_a.Get())) return false;
        // A dormant history may retain the same target at B. Keep separate
        // leases; ReadRetainedA still rejects any intervening content mutation.
        const auto refs=Field<int>(target,0x48);
        const auto retained=reinterpret_cast<int (*)(void*)>(base_+0x144ac80)(reinterpret_cast<void*>(target));
        if(original) {h.a=target;h.source_a=source;}
        else {h.b=target;h.source_b=source;}
        // One live native owner suffices before acquisition. Every subsequent
        // binding requires the extra replay lease (at least two references),
        // excluding pool reuse at native count one.
        if(retained!=refs+1 || retained<2 || !ReflectionBinding(target,source)) return false;
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] reflection history retained offset={:x} original={} width={} height={} format={} refs={} bytes={} external={} source={:x}\n"),
            h.offset,original,h.descriptor.Width,h.descriptor.Height,static_cast<unsigned>(h.descriptor.Format),retained,witness_.bytes,external,reinterpret_cast<std::uintptr_t>(source));
        }
        witness_.reflection_history=true;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

bool Sc6ReplayParticleCopy::RetainExecutionReflection(std::size_t budget) noexcept
{
    if(!view_state_) return true;
    __try {
        for(auto& h:histories_) {
            const auto target=Field<std::uintptr_t>(view_state_,h.offset);
            if(h.c) {
                if(target!=h.c || !ReflectionBinding(h.c,h.source_c.Get())) return false;
                continue;
            }
            if(!target || Field<std::uintptr_t>(target)!=base_+0x364c2a8) return false;
            const auto wrapper=Field<std::uintptr_t>(target,0x10);
            if(!wrapper || Field<std::uintptr_t>(wrapper)!=base_+0x35d3ea0) return false;
            auto* source=Field<ID3D11Texture2D*>(wrapper,0xa0);
            if(!source || !ReflectionBinding(target,source,1)) return false;
            D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);
            ID3D11Device* device{};source->GetDevice(&device);
            const bool same_device=device==device_.Get();if(device) device->Release();
            if(!same_device || desc.Width!=h.descriptor.Width || desc.Height!=h.descriptor.Height
                || desc.Format!=h.descriptor.Format || desc.MipLevels!=1 || desc.ArraySize!=1
                || desc.SampleDesc.Count!=1 || (Field<std::uintptr_t>(target,0xa8)==0)!=h.external
                || (h.copy_pixels && source!=h.source_a.Get())) return false;
            const auto charge=source==h.source_a.Get() || source==h.source_b.Get() ? 0
                : std::uint64_t(desc.Width)*desc.Height*h.pixel_bytes+0xb8;
            if(witness_.bytes>budget || charge>budget-witness_.bytes) return false;
            const auto refs=Field<int>(target,0x48);
            const auto retained=reinterpret_cast<int(*)(void*)>(base_+0x144ac80)(reinterpret_cast<void*>(target));
            h.c=target;h.source_c=source;witness_.bytes+=charge;
            if(retained!=refs+1 || !ReflectionBinding(h.c,h.source_c.Get())) return false;
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {capture_owner_fault_=true;return false;}
}

bool Sc6ReplayParticleCopy::PublishReflection(bool original) noexcept
{
    if(!view_state_) return true;
    __try {
        if(view_state_ && (Field<unsigned>(view_state_,0xad8)!=0 || Field<std::uintptr_t>(view_state_,0xae8)
            || Field<unsigned char>(view_state_,0xaf0)!=1)) return false;
        // Validate every slot before the first write. Undo admits an untouched
        // B slot after partial publication; each completed write has its own bit.
        for(const auto& h : histories_) {
            if(!ReflectionBinding(h.a,h.source_a.Get()) || !ReflectionBinding(h.b,h.source_b.Get())) return false;
            const auto expected=original ? h.b : (h.dirty ? (execution_started_ ? h.c : h.a) : h.b);
            if(!original && execution_started_ && h.dirty && !ReflectionBinding(h.c,h.source_c.Get())) return false;
            if(Field<std::uintptr_t>(view_state_,h.offset)!=expected) return false;
        }
        for(auto& h : histories_) {
            if(!original && !h.dirty && !h.image_dirty) continue;
            const auto desired=original ? h.a : h.b;
            auto& slot=Field<std::uintptr_t>(view_state_,h.offset);
            const auto previous=slot;
            if(h.copy_pixels) {
                // The caller marked the enclosing transaction uncommitted
                // before any submission. Timeout retains both images/owners.
                // Pixel submission and native slot publication are distinct.
                // A failure before the slot write must still admit B's slot
                // for undo while retaining both in-flight image owners.
                h.image_dirty=true;
                ReplayGpuCopyResource(context_.Get(),h.source_a.Get(),original ? h.image_a.Get() : h.image_b.Get());
            }
            reinterpret_cast<int (*)(void*)>(base_+0x144ac80)(reinterpret_cast<void*>(desired));
            slot=desired;h.dirty=original;
            reinterpret_cast<int (*)(void*)>(base_+0x146b380)(reinterpret_cast<void*>(previous));
            if(slot!=desired) return false;
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

bool Sc6ReplayParticleCopy::ReadReflectionHash() noexcept
{
    if(!view_state_) return true;
    for(auto& h : histories_) {
        const bool use_b=witness_.phase==Phase::ReadB || witness_.phase==Phase::ReadUndo || witness_.phase==Phase::ReadRecovered;
        if(!ReflectionBinding(use_b?h.b:h.a,use_b?h.source_b.Get():h.source_a.Get())
            || (witness_.phase==Phase::ReadInstalled && Field<std::uintptr_t>(view_state_,h.offset)!=h.a)
            || (witness_.phase==Phase::ReadRecovered && Field<std::uintptr_t>(view_state_,h.offset)!=h.b)) {Fail(E_INVALIDARG);return false;}
        if(!witness_.diagnostic_readbacks) continue;
        D3D11_MAPPED_SUBRESOURCE mapped{};
        ++witness_.readback_maps;
        const auto hr=context_->Map(h.staging.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&mapped);
        if(FAILED(hr)) {Fail(hr);return false;}
        std::uint64_t hash=14695981039346656037ull;
        for(unsigned y=0;y<h.descriptor.Height;++y)
            for(unsigned x=0;x<h.descriptor.Width*h.pixel_bytes;++x)
                hash=(hash^static_cast<const unsigned char*>(mapped.pData)[std::size_t(y)*mapped.RowPitch+x])*1099511628211ull;
        if(h.offset==0xae0) {
            std::uint32_t words[2]{};std::memcpy(words,mapped.pData,sizeof(words));
            RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] exposure history observation phase={} words={:08x},{:08x}\n"),
                static_cast<unsigned>(witness_.phase),words[0],words[1]);
        }
        context_->Unmap(h.staging.Get(),0);
        bool match=true;
        switch(witness_.phase) {
        case Phase::ReadA: h.hash_a=hash;break;
        case Phase::ReadB: h.hash_b=hash;break;
        case Phase::ReadRetainedA: match=hash==h.hash_a;break;
        case Phase::ReadUndo: match=hash==h.hash_b;break;
        case Phase::ReadInstalled:
            match=hash==h.hash_a && Field<std::uintptr_t>(view_state_,h.offset)==h.a;break;
        case Phase::ReadRecovered:
            match=hash==h.hash_b && Field<std::uintptr_t>(view_state_,h.offset)==h.b;break;
        default: match=false;break;
        }
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] reflection history observation offset={:x} phase={} hash={:016x} match={}\n"),h.offset,static_cast<unsigned>(witness_.phase),hash,match);
        if(!match) {Fail(E_FAIL);return false;}
    }
    witness_.reflection_a=histories_[0].hash_a;witness_.reflection_b=histories_[0].hash_b;
    // Aggregate witnesses require every admitted native history.
    if(witness_.phase==Phase::ReadRetainedA) witness_.reflection_retained=true;
    if(witness_.phase==Phase::ReadInstalled) witness_.reflection_installed=true;
    if(witness_.phase==Phase::ReadRecovered) witness_.reflection_recovered=true;
    return true;
}

bool Sc6ReplayParticleCopy::Begin(std::uintptr_t base, void* world, std::size_t budget, void* view_state, bool diagnostic_readbacks) noexcept
{
    if (witness_.phase != Phase::Empty || !base || !world) return false;
    request_deadline_ = ReplayGpuCompletion::Clock::now() + std::chrono::milliseconds(500);
    base_ = base; world_ = reinterpret_cast<std::uintptr_t>(world);view_state_=reinterpret_cast<std::uintptr_t>(view_state);
    // Private A image, reusable readback, and retained native source payload.
    // This reservation excludes other host owners, supplied via remaining budget.
    witness_.diagnostic_readbacks = diagnostic_readbacks;
    witness_.bytes = (diagnostic_readbacks ? 3 : 2) * image_bytes + sizeof(*this);
    if (budget < witness_.bytes) { Fail(E_OUTOFMEMORY); return false; }
    if (!Bindings(world, true) || !ReadSelection(witness_.captured_selection)) { Fail(E_INVALIDARG); return false; }
    if(!CaptureLocalFields(local_fields_a_)) {Fail(E_INVALIDARG);return false;}
    witness_.reconstruction_a = AdmitRenderReconstruction();
    try { return BeginUnchecked(budget); }
    catch (...) { Fail(E_FAIL); return false; }
}

bool Sc6ReplayParticleCopy::ReadSelection(std::int32_t& selection) const noexcept
{
    // 141FA5DB0 reads both sides using +170 and xor 1; 141F99B90
    // independently selects the pair used to draw. This is native render
    // state, not the global scheduling epoch. The enclosing CPU/render
    // transaction owns writing it at an admitted render boundary.
    __try
    {
        if (!pool_) return false;
        selection = Field<std::int32_t>(pool_, 0x170);
        return selection == 0 || selection == 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool Sc6ReplayParticleCopy::CaptureLocalFields(LocalFields& image) noexcept
{
    if(image.captured)return false;
    image.count=0;
    __try {
        const auto registry=system_+0x40;
        const auto count=Field<int>(registry,8),bits=Field<int>(registry,0x28);
        const auto storage=Field<std::uintptr_t>(registry);
        const auto heap=Field<std::uintptr_t>(registry,0x20),flags=heap?heap:registry+0x10;
        if(count<0 || count>65536 || bits!=count || Field<int>(registry,0xc)<count
            || (!heap && bits>128) || (count && !storage))return false;
        for(int i=0;i<count;++i) {
            if(!(Field<unsigned>(flags,(i/32)*4)&(1u<<(i%32))))continue;
            const auto render=Field<std::uintptr_t>(storage,i*8);
            if(!render || Field<std::uintptr_t>(render)!=base_+0x394bfc0
                || Field<int>(render,0x240)!=i || Field<unsigned char>(render,0x252))return false;
            const auto resource=Field<std::uintptr_t>(render,0xe0);
            // Retain render parameters even without a local vector field.
            // 141FA5DB0 leaves them intact when the tile count is zero.
            // Unknown/owned resources still fail reconstruction admission.
            // B-only fields retain their separate exclusion/lifetime protocol.
            if((resource && Field<std::uintptr_t>(resource)!=base_+0x39e5a90) || Field<unsigned char>(render,0x1e0))continue;
            if(image.count==image.rows.size() || !image.rows[image.count].Capture(base_,render))return false;
            ++image.count;
        }
        image.captured=true;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

bool Sc6ReplayParticleCopy::LocalFieldOwnersBound() const noexcept
{
    if(!local_fields_a_.captured || !local_fields_b_.captured)return false;
    __try {
        for(std::size_t i=0;i<local_fields_a_.count;++i) {
            const auto& field=local_fields_a_.rows[i];
            const Sc6ReplayVfxState::CoordinateRebuild* owner{};
            for(const auto& row:coordinates_)if(row.render==field.render) {
                if(owner)return false;owner=&row;
            }
            if(!owner || Field<std::uintptr_t>(owner->descriptor,0x30)!=owner->vector_field_asset)return false;
            if(owner->fresh) {
                if(!field.FreshDestinationBound(base_,owner->vector_field_asset))return false;
                continue; // Exact B has no alias to this private fresh render owner.
            }
            if(!field.AssetBound(base_,owner->vector_field_asset))return false;
            bool undo=false;
            for(std::size_t j=0;j<local_fields_b_.count;++j)
                undo|=local_fields_b_.rows[j].render==field.render && local_fields_b_.rows[j].resource==field.resource;
            if(!undo)return false;
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

bool Sc6ReplayParticleCopy::PublishLocalFields(const LocalFields& image) noexcept
{
    if(!image.captured || !completion_.retired() || !witness_.native_write_uncommitted)return false;
    // Validate every destination before the first write. The enclosing B undo
    // remains retained even if a subsequent native access fails part way.
    if(image.count>image.rows.size())return false;
    if(&image==&local_fields_a_) {
        if(!LocalFieldOwnersBound())return false;
        // All bindings were checked before entering the first native leaf.
        // A partial failure keeps complete B and every fresh native owner.
        for(std::size_t i=0;i<image.count;++i)for(const auto& owner:coordinates_)
            if(owner.fresh && owner.render==image.rows[i].render
                && !image.rows[i].InitializeFresh(base_,owner.vector_field_asset))return false;
    }
    return ReplayStaticVectorField::PublishSet(base_,{image.rows.data(),image.count},completion_.retired(),
        witness_.native_write_uncommitted && witness_.undo_ready);
}

bool Sc6ReplayParticleCopy::AdmitRenderReconstruction(bool allow_displaced_fields) noexcept
{
    __try
    {
        witness_.reconstruction_owner = system_;
        // 141FA5DB0 consumes the separate scene-field sparse registry at +8.
        // Local emitter +E0 rejection does not cover it. Admit only verified
        // empty occupancy; capacity alone is not an emptiness witness.
        witness_.scene_fields_empty = false;
        witness_.reconstruction_issue = 8;
        const auto fields = system_ + 8;
        const auto slots = Field<std::int32_t>(fields, 8);
        const auto capacity = Field<std::int32_t>(fields, 0xc);
        const auto field_bits = Field<std::int32_t>(fields, 0x28);
        const auto max_bits = Field<std::int32_t>(fields, 0x2c);
        const auto heap = Field<std::uintptr_t>(fields, 0x20);
        const auto words = heap ? heap : fields + 0x10;
        const auto storage = Field<std::uintptr_t>(fields);
        if (slots < 0 || slots > 65536 || capacity < slots || field_bits != slots || max_bits < field_bits
            || (!heap && max_bits > 128) || (slots && !storage)) return false;
        witness_.scene_field_slots = slots;
        witness_.reconstruction_issue = 9;
        for (int i = 0; i < slots; ++i)
            if (Field<std::uint32_t>(words, (i / 32) * 4) & (1u << (i % 32))) return false;
        // Also prove the sparse free chain agrees with the empty occupancy.
        witness_.reconstruction_issue = 8;
        if (Field<std::int32_t>(fields, 0x34) != slots) return false;
        int previous = -1, current = Field<std::int32_t>(fields, 0x30);
        for (int i = 0; i < slots; ++i) {
            if (current < 0 || current >= slots || Field<std::int32_t>(storage, current * 8) != previous) return false;
            previous = current;
            current = Field<std::int32_t>(storage, current * 8 + 4);
        }
        if (current != -1) return false;
        witness_.scene_fields_empty = true;
        ++witness_.scene_field_checks;
        witness_.reconstruction_issue = 1;
        if (Field<std::int32_t>(system_, 0x90)) return false;
        witness_.reconstruction_issue = 2;
        if (!Field<std::uint8_t>(pool_, 0x50) || !Field<std::uint8_t>(pool_, 0xa8)) return false;
        const auto registry = system_ + 0x40;
        const auto count = Field<std::int32_t>(registry, 8);
        const auto bits = Field<std::int32_t>(registry, 0x28);
        const auto flags_heap = Field<std::uintptr_t>(registry, 0x20);
        const auto flags = flags_heap ? flags_heap : registry + 0x10;
        witness_.reconstruction_issue = 3;
        if (count < 0 || count > 65536 || count != bits || Field<std::int32_t>(registry, 0xc) < count
            || (!flags_heap && bits > 128) || (count && !Field<std::uintptr_t>(registry))) return false;
        for (int i = 0; i < count; ++i)
        {
            if (!(Field<std::uint32_t>(flags, (i / 32) * 4) & (1u << (i % 32)))) continue;
            const auto render = Field<std::uintptr_t>(Field<std::uintptr_t>(registry), i * 8);
            witness_.reconstruction_owner = render;
            witness_.reconstruction_issue = 4;
            if (!render || Field<std::uintptr_t>(render) != base_ + 0x394bfc0
                || Field<std::int32_t>(render, 0x240) != i || Field<std::uint8_t>(render, 0x252)) return false;
            witness_.reconstruction_issue = 5;
            bool retained_static=false;
            bool displaced = false;
            if (allow_displaced_fields && birth_fields_retained_ && BirthRegistryMatchesB())
                for (std::size_t n=0; n<birth_count_; ++n) displaced |= births_[n].render == render;
            if(local_fields_a_.captured)
                for(std::size_t n=0;n<local_fields_a_.count;++n)
                    retained_static|=local_fields_a_.rows[n].render==render && local_fields_a_.rows[n].Bound(base_);
            if (Field<std::uintptr_t>(render, 0xe0)) {
                // 141FA5DB0 visits occupied render-registry entries before
                // invoking the local field update. Excluded B entries cannot
                // update their field. 142190FE0 retires owned fields through
                // native render teardown after the cancellation window.
                if (!displaced && !retained_static) {
                    const auto resource=Field<std::uintptr_t>(render,0xe0);
                    const auto type=Field<std::uintptr_t>(resource);
                    RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] local particle field rejected phase={} render={:x} resource={:x} type_rva={:x} update_rva={:x} owned={} intensity={:x} strength={:x} texture={:x} dimensions={}/{}/{} pending_delta={:x} read_only=true guard_unchanged=true\n"),
                        static_cast<unsigned>(witness_.phase),render,resource,type-base_,Field<std::uintptr_t>(type,0x40)-base_,
                        Field<std::uint8_t>(render,0x1e0),Field<std::uint32_t>(render,0x1d4),Field<std::uint32_t>(resource,0x44),
                        Field<std::uintptr_t>(resource,0x30),Field<std::int32_t>(resource,0x38),Field<std::int32_t>(resource,0x3c),Field<std::int32_t>(resource,0x40),Field<std::uint32_t>(render,0x98));
                    return false;
                }
            }
            witness_.reconstruction_issue = 6;
            if (Field<std::int32_t>(render, 0xc8) || Field<std::int32_t>(render, 0xd8)
                || (!retained_static && !displaced && (Field<std::uint64_t>(render, 0x88) || Field<std::uint64_t>(render, 0x90)
                || Field<std::uint32_t>(render, 0x98)))) {
                // Report the whole bounded producer/consumer admission row,
                // rather than guessing which of the independent guards fired.
                RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] GPU pending work rejected phase={} render={:x} registry_slot={} tiles={}/{} pass={} active={} spawn_rows={}/{} tile_rows={}/{} parameters88={:x} parameters90={:x} delta98={:x} field={:x} read_only=true guard_unchanged=true\n"),
                    static_cast<unsigned>(witness_.phase),render,i,Field<unsigned>(render,0x40),Field<unsigned>(render,0x44),
                    Field<int>(render,0x244),Field<unsigned char>(render,0x253),Field<int>(render,0xc8),Field<int>(render,0xcc),
                    Field<int>(render,0xd8),Field<int>(render,0xdc),Field<std::uint64_t>(render,0x88),Field<std::uint64_t>(render,0x90),
                    Field<unsigned>(render,0x98),Field<std::uintptr_t>(render,0xe0));
                return false;
            }
        }
        // 141F91320 resets sort request count/offset before view collection;
        // 141F91250 appends this frame's inputs and 141FA8210 rebuilds the
        // shared sorted output from the selected position texture. No old
        // sort output is used by held draws (which use the retained image).
        witness_.reconstruction_owner = 0;
        witness_.reconstruction_issue = 0;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { witness_.reconstruction_issue = 7; return false; }
}

bool Sc6ReplayParticleCopy::SelectionMatches(std::int32_t expected) const noexcept
{
    std::int32_t observed{-1};
    return ReadSelection(observed) && observed == expected;
}

bool Sc6ReplayParticleCopy::BindTexturesUnchecked()
{
    for (std::size_t i = 0; i < sources_.size(); ++i)
    {
        sources_[i] = Field<ID3D11Texture2D*>(reinterpret_cast<std::uintptr_t>(wrappers_[i]), 0xa0);
        Microsoft::WRL::ComPtr<ID3D11Device> device;
        sources_[i]->GetDevice(device.GetAddressOf());
        if (!device || (device_ && device.Get() != device_.Get())) { Fail(E_INVALIDARG); return false; }
        device_ = device;
        D3D11_TEXTURE2D_DESC descriptor{};
        sources_[i]->GetDesc(&descriptor);
        witness_.native_formats[i] = descriptor.Format;
        // The engine's PF_B8G8R8A8 describes the view family, not necessarily
        // a typed allocation: the actual tick-205 attribute allocation is 90
        // (B8G8R8A8_TYPELESS). Copies below preserve the native descriptor.
        const bool format_matches = descriptor.Format == formats_[i]
            || (i >= 4 && descriptor.Format == DXGI_FORMAT_B8G8R8A8_TYPELESS);
        if (descriptor.Width != 1024 || descriptor.Height != 1024 || descriptor.ArraySize != 1
            || descriptor.MipLevels != 1 || descriptor.SampleDesc.Count != 1 || descriptor.SampleDesc.Quality
            || !format_matches) { witness_.failed_resource = static_cast<std::int32_t>(i); Fail(E_INVALIDARG); return false; }
        for (std::size_t j = 0; j < i; ++j)
            if (sources_[j].Get() == sources_[i].Get()) { Fail(E_INVALIDARG); return false; }
    }
    device_->GetImmediateContext(context_.GetAddressOf());
    if (!context_ || context_->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) { Fail(E_INVALIDARG); return false; }
    return true;
}

bool Sc6ReplayParticleCopy::BeginUnchecked(std::size_t budget)
{
    if (!BindTexturesUnchecked()) return false;
    for (std::size_t i = 0; i < images_.size(); ++i)
    {
        D3D11_TEXTURE2D_DESC descriptor{};
        sources_[i]->GetDesc(&descriptor);
        descriptor.Usage = D3D11_USAGE_DEFAULT;
        descriptor.BindFlags = descriptor.CPUAccessFlags = descriptor.MiscFlags = 0;
        if(i<4) {
            // CopyResource preserves bits across the matching DXGI type group.
            // Integer storage enables exact comparison, without float loads.
            descriptor.Format=ReplayGpuImageEquality::StorageFormat(static_cast<unsigned>(i));
            descriptor.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        }
        auto hr = device_->CreateTexture2D(&descriptor, nullptr, images_[i].GetAddressOf());
        descriptor.Usage = D3D11_USAGE_STAGING;
        descriptor.BindFlags=0;
        descriptor.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (SUCCEEDED(hr) && witness_.diagnostic_readbacks) hr = device_->CreateTexture2D(&descriptor, nullptr, staging_[i].GetAddressOf());
        if (FAILED(hr)) { Fail(hr); return false; }
    }
    if(!PrepareImageEquality(budget) || !RetainReflection(true,budget) || !CaptureVisibility(visibility_a_,budget) || !CaptureLighting(lighting_a_,budget)) {Fail(E_INVALIDARG);return false;}
    const auto hr = completion_.Prepare(device_.Get());
    if (FAILED(hr)) { Fail(hr); return false; }
    for (std::size_t i = 0; i < images_.size(); ++i) ReplayGpuCopyResource(context_.Get(),images_[i].Get(), sources_[i].Get());
    witness_.phase = Phase::ReadA;
    return SubmitRead(images_);
}

bool Sc6ReplayParticleCopy::PrepareTransfer() noexcept
{
    if (completion_.result() != ReplayGpuCompletion::Result::Prepared)
    {
        if (!completion_.Release()) { Fail(E_UNEXPECTED); return false; }
        const auto hr = completion_.Prepare(device_.Get());
        if (FAILED(hr)) { Fail(hr); return false; }
    }
    return true;
}

bool Sc6ReplayParticleCopy::SubmitRead(const std::array<Texture, 6>& source) noexcept
{
    if (!Bindings(reinterpret_cast<void*>(world_), false)) {Fail(E_INVALIDARG);return false;}
    if (!PrepareTransfer()) return false;
    if(view_state_) {
        const bool use_b=witness_.phase==Phase::ReadB || witness_.phase==Phase::ReadUndo || witness_.phase==Phase::ReadRecovered;
        for(const auto& h : histories_) {
            auto* texture=use_b ? h.source_b.Get() : h.source_a.Get();
            if(!ReflectionBinding(use_b ? h.b : h.a,texture)) {Fail(E_INVALIDARG);return false;}
            // Originals are captured once, never overwritten by observations.
            // ReadInstalled/Recovered inspect the actual native destination.
            if(h.copy_pixels) {
                if(witness_.phase==Phase::ReadA) ReplayGpuCopyResource(context_.Get(),h.image_a.Get(),texture);
                if(witness_.phase==Phase::ReadB) ReplayGpuCopyResource(context_.Get(),h.image_b.Get(),texture);
                if(witness_.phase==Phase::ReadA || witness_.phase==Phase::ReadRetainedA) texture=h.image_a.Get();
                if(witness_.phase==Phase::ReadB) texture=h.image_b.Get();
            }
            if(witness_.diagnostic_readbacks) ReplayGpuCopyResource(context_.Get(),h.staging.Get(),texture);
        }
    }
    if(witness_.diagnostic_readbacks) for (std::size_t i = 0; i < source.size(); ++i) ReplayGpuCopyResource(context_.Get(),staging_[i].Get(), source[i].Get());
    if(witness_.phase==Phase::ReadA && image_equality_) {
        const auto equality_hr=image_equality_->Dispatch(context_.Get(),completion_);
        if(FAILED(equality_hr)){Fail(equality_hr);return false;}
    }
    const auto hr = completion_.Submit(context_.Get(), request_deadline_);
    if (FAILED(hr)) { Fail(hr); return false; }
    return true;
}

bool Sc6ReplayParticleCopy::ReadHashes(std::array<std::uint64_t, 6>& output) noexcept
{
    if(!Bindings(reinterpret_cast<void*>(world_), false)) {Fail(E_INVALIDARG);return false;}
    if(!ReadReflectionHash()) return false;
    if(!witness_.diagnostic_readbacks) return true;
    for (std::size_t i = 0; i < staging_.size(); ++i)
    {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        ++witness_.readback_maps;
        const auto hr = context_->Map(staging_[i].Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
        if (FAILED(hr)) { Fail(hr); return false; }
        std::uint64_t hash = 14695981039346656037ull;
        for (unsigned row = 0; row < 1024; ++row)
            for (unsigned col = 0; col < 1024 * pixel_bytes[i]; ++col)
            {
                hash ^= static_cast<const unsigned char*>(mapped.pData)[static_cast<std::size_t>(row) * mapped.RowPitch + col];
                hash *= 1099511628211ull;
            }
        context_->Unmap(staging_[i].Get(), 0);
        output[i] = hash;
    }
    return true;
}

bool Sc6ReplayParticleCopy::VerifyAtB(void* world, bool retirement_probe) noexcept
{
    if (witness_.phase != Phase::ReadyA || !Bindings(world, false)
        || !ReadSelection(witness_.subsequent_selection)) { Fail(E_INVALIDARG); return false; }
    if(!RetainReflection(false,witness_.bytes)) {Fail(E_INVALIDARG);return false;}
    if(!CaptureLocalFields(local_fields_b_)) {Fail(E_INVALIDARG);return false;}
    witness_.reconstruction_b = AdmitRenderReconstruction();
    retirement_probe_ = retirement_probe;
    request_deadline_ = ReplayGpuCompletion::Clock::now() + std::chrono::milliseconds(500);
    witness_.phase = Phase::ReadRetainedA;
    return SubmitRead(images_);
}

bool Sc6ReplayParticleCopy::PrepareUndoAtB(void* world, std::size_t budget, ReplayGpuCompletion::Clock::time_point deadline, bool defer_target) noexcept
{
    if (witness_.phase != Phase::ReadyB || !completion_.retired() || !Bindings(world, false)
        || !SelectionMatches(witness_.subsequent_selection)) return false;
    request_deadline_ = deadline;
    if (ReplayGpuCompletion::Clock::now() >= request_deadline_)
    { witness_.deadline_expired = true; Fail(DXGI_ERROR_WAIT_TIMEOUT); return false; }
    if(!CaptureVisibility(visibility_b_,budget) || !CaptureLighting(lighting_b_,budget)) {Fail(E_INVALIDARG);return false;}
    // Validate A/B storage and membership before the enclosing owner starts
    // publishing CPU state; repeat the check at actual render publication.
    if(!defer_target) {
        if(!PrepareLighting(budget) || !PrepareVisibilityStorage(budget) || !PublishVisibility(true,true) || !PublishMaterial(true,true) || !PublishLighting(true,true)) {Fail(E_INVALIDARG);return false;}
        witness_.target_prepared=true;
    }
    if (witness_.bytes > budget || image_bytes > budget - witness_.bytes) {
        RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] render undo capacity owned={} requested={} budget={}\n"),witness_.bytes,image_bytes,budget);
        Fail(E_OUTOFMEMORY); return false;
    }
    // Reserve the entire B image before allocating; retain partial allocations
    // on failure until Finish. No native destination is touched in preparation.
    witness_.bytes += image_bytes;
    for (std::size_t i = 0; i < undo_.size(); ++i)
    {
        D3D11_TEXTURE2D_DESC descriptor{};
        images_[i]->GetDesc(&descriptor);
        const auto hr = device_->CreateTexture2D(&descriptor, nullptr, undo_[i].GetAddressOf());
        if (FAILED(hr)) { Fail(hr); return false; }
    }
    if (!PrepareTransfer()) return false;
    for (std::size_t i = 0; i < undo_.size(); ++i) ReplayGpuCopyResource(context_.Get(),undo_[i].Get(), sources_[i].Get());
    witness_.phase = Phase::ReadUndo;
    return SubmitRead(undo_);
}

bool Sc6ReplayParticleCopy::PrepareTargetAtB(void* world,std::size_t budget) noexcept
{
    if(witness_.phase!=Phase::UndoReady || !witness_.undo_ready || witness_.target_prepared
        || witness_.native_write_uncommitted || !completion_.retired() || !Bindings(world,false)) return false;
    if(ReplayGpuCompletion::Clock::now()>=request_deadline_)
    {witness_.deadline_expired=true;Fail(DXGI_ERROR_WAIT_TIMEOUT);return false;}
    const auto prepare=[&](bool result,const wchar_t* participant) {
        if(!result) RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] render target preparation rejected participant={} A_published=false B_retained=true\n"),participant);
        return result;
    };
    if(!prepare(PrepareLighting(budget),L"lighting") || !prepare(PruneOrphanVisibility(budget),L"visibility_orphan_reconstruction")
        || !prepare(PrepareVisibilityStorage(budget),L"visibility_storage")
        || !prepare(PublishVisibility(true,true),L"visibility") || !prepare(PublishMaterial(true,true),L"material")
        || !prepare(PublishLighting(true,true),L"lighting_publication")) {Fail(E_INVALIDARG);return false;}
    witness_.target_prepared=true;
    return true;
}

bool Sc6ReplayParticleCopy::InstallCaptured(void* world, bool restore_selection) noexcept
{
    if (restore_selection && (!witness_.reconstruction_a || !witness_.reconstruction_b || !AdmitRenderReconstruction(witness_.birth_prepared))) return false;
    if (witness_.phase != Phase::UndoReady || !witness_.undo_ready || !witness_.target_prepared || !completion_.retired()
        || !Bindings(world, false)) return false;
    if (ReplayGpuCompletion::Clock::now() >= request_deadline_)
    { witness_.deadline_expired = true; Fail(DXGI_ERROR_WAIT_TIMEOUT); return false; }
    if (!LocalFieldOwnersBound() || !PrepareTransfer()) return false;
    // This remains set even if submission or readback fails. Timeout cannot
    // permit Finish or a native resume with partially installed state.
    witness_.native_write_uncommitted = true;
    if (witness_.birth_prepared && !ExcludeBirthRegistration()) { Fail(E_FAIL); return false; }
    if (restore_selection)
    {
        if (!SelectionMatches(witness_.subsequent_selection)) { Fail(E_FAIL); return false; }
        selection_dirty_ = true;
        __try { Field<std::int32_t>(pool_, 0x170) = witness_.captured_selection; }
        __except (EXCEPTION_EXECUTE_HANDLER) { Fail(E_FAIL); return false; }
        if (!SelectionMatches(witness_.captured_selection)) { Fail(E_FAIL); return false; }
    }
    if(!PublishLocalFields(local_fields_a_) || !PublishVisibility(true) || !PublishReflection(true) || !PublishMaterial(true) || !PublishLighting(true)) {Fail(E_FAIL);return false;}
    if(image_materializer_ && FAILED(image_materializer_->Dispatch(context_.Get()))){Fail(E_FAIL);return false;}
    for (std::size_t i = 0; i < images_.size(); ++i) ReplayGpuCopyResource(context_.Get(),sources_[i].Get(), images_[i].Get());
    witness_.phase = Phase::ReadInstalled;
    return SubmitRead(sources_);
}

bool Sc6ReplayParticleCopy::RestoreUndo(void* world) noexcept
{
    if ((execution_started_ && (!execution_settled_ || execution_settlement_!=RestoreSettlement::RecoverOriginal)) || coordinate_commit_started_ || !witness_.native_write_uncommitted || !witness_.undo_ready || !completion_.retired()
        || !Bindings(world, false) || FAILED(device_->GetDeviceRemovedReason())) return false;
    if (coordinate_execution_started_ && !coordinate_undo_complete_)
        return RebuildCoordinates(false, true);
    // Recovery has its own deadline. The original failure remains in error;
    // recovering B does not turn an expired/cancelled seek into success.
    request_deadline_ = ReplayGpuCompletion::Clock::now() + std::chrono::milliseconds(500);
    if (!UndoBirthRegistration() || !PublishLocalFields(local_fields_b_) || !PublishReflection(false))return false;
    if(execution_started_ && (!stage_render_owners_.empty() || trace_render_owner_)) {
        if(native_recovery_!=NativeRecovery::None || !BeginLightingExecution() || !BeginVisibilityExecution())return false;
        // Native B component reconstruction follows CPU B installation. C
        // proxy/cache addresses are relinquished before that native work;
        // detached B allocations and references remain privately owned.
        native_recovery_=NativeRecovery::CpuPending;
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] stage render recovery state=CpuPending B_retained=true native_destinations_deferred=true\n"));
    } else if(!PublishVisibility(false) || !PublishMaterial(false) || !PublishLighting(false))return false;
    if (selection_dirty_)
    {
        __try { Field<std::int32_t>(pool_, 0x170) = witness_.subsequent_selection; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        if (!SelectionMatches(witness_.subsequent_selection)) return false;
        selection_dirty_ = false;
    }
    if (!PrepareTransfer()) return false;
    for (std::size_t i = 0; i < undo_.size(); ++i) ReplayGpuCopyResource(context_.Get(),sources_[i].Get(), undo_[i].Get());
    witness_.phase = Phase::ReadRecovered;
    return SubmitRead(sources_);
}

bool Sc6ReplayParticleCopy::CompleteNativeReconstruction(std::size_t budget) noexcept
{
    if(native_recovery_!=NativeRecovery::CpuReady || !completion_.retired()
        || !witness_.native_write_uncommitted || !execution_started_ || !execution_settled_
        || execution_settlement_!=RestoreSettlement::RecoverOriginal || !Bindings(reinterpret_cast<void*>(world_),false))return false;
    // This ordered RHI command follows native GT end-frame reconstruction and
    // its joined tasks. Capture actual destinations/backing after those writes;
    // never reuse the relinquished C proxy or allocation addresses.
    if((!lighting_execution_settled_ && !SettleLightingExecution(budget,RestoreSettlement::RecoverOriginal))
        || (view_state_ && !visibility_execution_settled_ && !SettleVisibilityExecution(budget)) || !PublishVisibility(false)
        || !PublishMaterial(false) || !PublishLighting(false))return false;
    if(!PublishLocalFields(local_fields_b_))return false;
    native_recovery_=NativeRecovery::PublicationPending;
    RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] stage render recovery state=PublicationPending B_retained=true native_destinations_rebound=true\n"));
    request_deadline_=ReplayGpuCompletion::Clock::now()+std::chrono::milliseconds(500);
    witness_.phase=Phase::ReadRecovered;
    return SubmitRead(sources_);
}

bool Sc6ReplayParticleCopy::CoordinatesBound(const Sc6ReplayVfxState::CoordinateRebuild& row, bool installed) noexcept
{
    if (row.native_retired) return row.fresh && !installed;
    __try {
        const auto count = installed ? row.tiles.size() : row.previous_count;
        if (!(row.system == system_ && row.pool == pool_ && count <= 65536)) { witness_.reconstruction_owner = row.component; witness_.reconstruction_issue = 101; return false; }
        if (!(Field<std::uintptr_t>(row.emitter) == base_ + 0x394c100)) { witness_.reconstruction_owner = row.component; witness_.reconstruction_issue = 102; return false; }
        if (!(Field<std::uintptr_t>(row.emitter, 0x18) == row.component
            || (row.fresh && !installed && !Field<std::uintptr_t>(row.emitter, 0x18)))) { witness_.reconstruction_owner = row.component; witness_.reconstruction_issue = 103; return false; }
        if (!(Field<std::uintptr_t>(row.emitter, 0x1d0) == row.system)) { witness_.reconstruction_owner = row.component; witness_.reconstruction_issue = 104; return false; }
        if (!(Field<std::uintptr_t>(row.emitter, 0x1d8) == row.descriptor)) { witness_.reconstruction_owner = row.component; witness_.reconstruction_issue = 105; return false; }
        if (!(Field<std::uintptr_t>(row.emitter, 0x1e0) == row.render)) { witness_.reconstruction_owner = row.component; witness_.reconstruction_issue = 106; return false; }
        if (!(Field<std::uintptr_t>(row.descriptor, 0x2b0) == row.resource)) { witness_.reconstruction_owner = row.component; witness_.reconstruction_issue = 107; return false; }
        if (!(Field<std::uintptr_t>(row.render) == base_ + 0x394bfc0)) { witness_.reconstruction_owner = row.component; witness_.reconstruction_issue = 108; return false; }
        if (!(Field<std::uintptr_t>(row.render, 0x1f0) == base_ + 0x394c000)) { witness_.reconstruction_owner = row.component; witness_.reconstruction_issue = 109; return false; }
        if (row.fresh && !installed) {
            // A constructor has no B buffers. Preparation runs before the CPU
            // publication; rebuilding runs after it, with root+18 rebound.
            // Both admit only the original empty native render resource.
            const bool empty = !row.previous_count && row.previous_tiles.empty()
                && Field<int>(row.render, 0x240) == -1
                && !Field<std::uintptr_t>(row.render, 0x48)
                && !Field<std::uintptr_t>(row.render, 0x30)
                && !Field<std::uintptr_t>(row.render, 0x38)
                && !Field<std::uintptr_t>(row.render, 0x220)
                && !Field<unsigned>(row.render, 0x40) && !Field<unsigned>(row.render, 0x44)
                && !Field<unsigned>(row.render, 0x230)
                && !Field<unsigned char>(row.render, 0x28) && !Field<unsigned char>(row.render, 0x218)
                // Native141F8DE20 initializes bytes250/251/253 to1 and252 to0.
                && Field<unsigned>(row.render, 0x250)==0x01000101
                && Field<LONG>(row.resource, 0x230) > 0;
            if (!empty) { witness_.reconstruction_owner = row.component; witness_.reconstruction_issue = 118; }
            return empty;
        }
        if (!(Field<std::uintptr_t>(row.render, 0x48) == row.resource)) { witness_.reconstruction_owner = row.component; witness_.reconstruction_issue = 110; return false; }
        if (!(Field<LONG>(row.resource, 0x230) > 0)) { witness_.reconstruction_owner = row.component; witness_.reconstruction_issue = 111; return false; }
        if (!(Field<unsigned>(row.render, 0x40) == count)) { witness_.reconstruction_owner = row.component; witness_.reconstruction_issue = 112; return false; }
        if (!(Field<unsigned>(row.render, 0x44) == ((count + 7) & ~std::size_t{7}))) { witness_.reconstruction_owner = row.component; witness_.reconstruction_issue = 113; return false; }
        if (!(Field<unsigned>(row.render, 0x230) == count * 16)) { witness_.reconstruction_owner = row.component; witness_.reconstruction_issue = 114; return false; }
        if (!(Field<unsigned char>(row.render, 0x28) == 1 && Field<unsigned char>(row.render, 0x218) == 1)) { witness_.reconstruction_owner = row.component; witness_.reconstruction_issue = 115; return false; }
        if (!(!Field<unsigned short>(row.render, 0x250) && !Field<unsigned char>(row.render, 0x252))) { witness_.reconstruction_owner = row.component; witness_.reconstruction_issue = 116; return false; }
        if (!((!count || (Field<std::uintptr_t>(row.render, 0x30)
                && Field<std::uintptr_t>(row.render, 0x38) && Field<std::uintptr_t>(row.render, 0x220))))) { witness_.reconstruction_owner = row.component; witness_.reconstruction_issue = 117; return false; }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { witness_.reconstruction_owner = row.component; witness_.reconstruction_issue = 199; return false; }
}

bool Sc6ReplayParticleCopy::PrepareCoordinates(std::vector<Sc6ReplayVfxState::CoordinateRebuild>&& rows, std::size_t budget) noexcept
{
    if (!coordinates_.empty() || coordinate_refs_ || witness_.phase != Phase::UndoReady || !completion_.retired()) return false;
    std::size_t bytes = rows.capacity() * sizeof(Sc6ReplayVfxState::CoordinateRebuild);
    for (const auto& row : rows) {
        const auto reject=[&](unsigned issue,std::uint32_t tile) {
            witness_.reconstruction_owner=row.component;witness_.reconstruction_issue=issue;
            RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] coordinate inventory rejected issue={} component={:x} fresh={} tiles={} previous_tiles={} previous_count={} tile={:x} budget={} used={}\n"),
                issue,row.component,row.fresh,row.tiles.size(),row.previous_tiles.size(),row.previous_count,tile,budget,bytes);
            return false;
        };
        if (row.previous_tiles.size() != row.previous_count) return reject(120,0);
        for (const auto tile : row.previous_tiles) if (tile >= 65536) return reject(121,tile);
        for (const auto tile : row.tiles) if (tile >= 65536) return reject(122,tile);
        if (!CoordinatesBound(row, false)) return false;
        // Both old/new generated vertex and coordinate buffers plus native
        // wrapper overhead are charged through hardware retirement.
        const auto vertices = (row.tiles.size() + row.previous_count) * 64;
        const auto coordinates = (((row.tiles.size() + 7) & ~std::size_t{7})
            + ((row.previous_count + 7u) & ~7u)) * 8;
        if (bytes > budget || vertices + coordinates + 4096 > budget - bytes) return reject(123,0);
        bytes += vertices + coordinates + 4096;
    }
    if (bytes > budget) return false;
    coordinates_ = std::move(rows);
    // Translate only the captured field's typed render edge. Its resource and
    // value inputs remain A's; the retained checkpoint and complete B are immutable.
    for(std::size_t i=0;i<local_fields_a_.count;++i) {
        auto& field=local_fields_a_.rows[i];
        const Sc6ReplayVfxState::CoordinateRebuild* mapped{};
        for(const auto& row:coordinates_)if(row.fresh && row.source_render==field.render) {
            if(mapped)return false;mapped=&row;
        }
        if(mapped) {
            field.render=mapped->render;
            // Native141F6A360 clears only low four flag bits;141FB0F50
            // publishes those bits and141FA5050 consumes tiling bits0..2.
            // Preserve the fresh allocation's unused upper bits, not A padding.
            field.flags=(field.flags&0x0f)|(Field<unsigned char>(mapped->render,0x1dc)&0xf0);
        }
    }
    // The host prepares render-target storage before preparing emitter rows.
    // Bind field assets here, once those rows exist, still before any A write.
    // InstallCaptured repeats this check at the actual render publication.
    if(!LocalFieldOwnersBound()) {
        RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] static field owner preparation rejected fields={} rows={} B_retained=true A_published=false\n"),local_fields_a_.count,coordinates_.size());
        for(std::size_t i=0;i<local_fields_a_.count;++i) {
            const auto& field=local_fields_a_.rows[i];
            const Sc6ReplayVfxState::CoordinateRebuild* owner{};
            for(const auto& row:coordinates_)if(row.render==field.render)owner=&row;
            // Read only coordinate destinations already validated above, never
            // the old captured render pointer when mapping failed.
            RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] static field binding detail mapped={} fresh={} render={:x} resource={:x} asset={:x} resource_bound={} expected_enabled={} actual_enabled={} expected_pass={} actual_pass={} expected_flags={:x} actual_flags={:x} actual_resource={:x} owned={}\n"),
                owner!=nullptr,owner&&owner->fresh,field.render,field.resource,owner?owner->vector_field_asset:0,
                owner&&field.ResourceBound(base_,owner->vector_field_asset),field.enabled,owner?Field<unsigned char>(owner->render,0x253):0,
                field.simulation_pass,owner?Field<int>(owner->render,0x244):0,field.flags,owner?Field<unsigned char>(owner->render,0x1dc):0,
                owner?Field<std::uintptr_t>(owner->render,0xe0):0,owner?Field<unsigned char>(owner->render,0x1e0):0);
        }
        return false;
    }
    __try {
        for (const auto& row : coordinates_) {
            InterlockedIncrement(&Field<LONG>(row.resource, 0x230));
            ++coordinate_refs_;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    witness_.bytes += bytes;
    return true;
}

bool Sc6ReplayParticleCopy::SealFreshCoordinateRetirement(const Sc6ReplayVfxState::PreparedEmitterSet& gpu) noexcept
{
    if (!completion_.retired() || witness_.phase != Phase::Installed) return false;
    // Preparation may fail after A publication but before coordinate creation.
    // RestoreUndo then restores the original copy without RebuildCoordinates;
    // unpublished fresh buffers still belong to preparation cleanup. Do not
    // mark a live owner retired or require a destructor that has not run.
    if (!coordinate_execution_started_) return true;
    // Validate every receipt before changing the journal. No render/component
    // address is dereferenced here, and a live fresh owner cannot be skipped.
    for (const auto& row : coordinates_)
        if (row.fresh && !row.native_retired && !gpu.FreshComponentRetired(row.component, row.component_weak)) return false;
    for (auto& row : coordinates_) if (row.fresh) row.native_retired = true;
    return true;
}

bool Sc6ReplayParticleCopy::PrepareExecutionCoordinates() noexcept
{
    if (coordinate_execution_started_ || coordinate_commit_started_ || witness_.phase != Phase::Installed
        || !completion_.retired() || !witness_.installed_copy_complete || !witness_.native_write_uncommitted
        || !Bindings(reinterpret_cast<void*>(world_), false)) return false;
    if (coordinates_.empty()) return true;
    return RebuildCoordinates(true, true);
}

bool Sc6ReplayParticleCopy::ReadQuarantinedPrimitiveIds(
    const Sc6ReplayVfxState::ParticleBirthSet& births,std::span<unsigned> output,std::size_t& count) const noexcept
{
    count=0;
    if(!lighting_b_.captured || !completion_.retired() || !witness_.native_write_uncommitted
        || births.members().size()>output.size()) return false;
    for(const auto& birth:births.members()) {
        if(!birth.ValidateQuarantined().ok()) return false;
        const LightingPrimitive* found=nullptr;
        for(const auto& primitive:lighting_b_.primitives) if(primitive.component==birth.component()) {
            if(found) return false;
            found=&primitive;
        }
        if(!found || !found->id) return false;
        for(std::size_t i=0;i<count;++i) if(output[i]==found->id) return false;
        output[count++]=found->id;
    }
    return true;
}

bool Sc6ReplayParticleCopy::AppendQuarantinedTracePrimitive(std::uintptr_t component,
    std::span<unsigned> output,std::size_t& count) const noexcept
{
    if(!lighting_b_.captured || !completion_.retired() || !witness_.native_write_uncommitted
        || !trace_render_owner_ || count>=output.size())return false;
    const LightingPrimitive* found=nullptr;
    for(const auto& row:lighting_b_.primitives)if(row.component==component) {
        if(found)return false;found=&row;
    }
    if(!found && trace_render_owner_->RetainedDormantRenderBinding(component))return true;
    const bool binding=found && found->id && found->proxy_type==base_+0x39af350 && found->slot_count==1
        && trace_render_owner_->RetainedRenderBinding(component,found->weak,found->id,found->proxy_inputs[0],false);
    if(!binding) {
        RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] trace quarantine rejected component={:x} captured_primitive={} id={} proxy_rva={:x} slots={} retained_binding=false B_retained=true\n"),
            component,found!=nullptr,found?found->id:0,found?found->proxy_type-base_:0,found?found->slot_count:0);
        return false;
    }
    for(std::size_t i=0;i<count;++i)if(output[i]==found->id)return false;
    output[count++]=found->id;return true;
}
bool Sc6ReplayParticleCopy::PrepareGroundRenderOwner(const Sc6ReplayGroundDebrisState& original) noexcept
{
    if(lighting_dirty_ || witness_.target_prepared || (ground_render_owner_ && ground_render_owner_!=&original)
        || !original.ValidateFrozen().ok())return false;
    ground_render_owner_=&original;return true;
}
bool Sc6ReplayParticleCopy::AppendQuarantinedGroundPrimitives(std::span<unsigned> output,std::size_t& count) const noexcept
{
    if(!ground_render_owner_ || ground_render_owner_->empty())return true;
    if(!lighting_b_.captured || !completion_.retired() || !witness_.native_write_uncommitted)return false;
    for(const auto& mesh:ground_render_owner_->meshes()) {
        if(count>=output.size())return false;
        const LightingPrimitive* found{};
        for(const auto& row:lighting_b_.primitives)if(row.component==mesh.component) {
            if(found)return false;found=&row;
        }
        std::uintptr_t render_data{};
        if(!found || !found->id || found->proxy_type!=base_+0x36bd1a8 || found->lci_binding.mesh!=mesh.asset
            || !ground_render_owner_->RetainedRenderBinding(mesh.component,found->weak,found->id,mesh.asset,false)
            || !ReadBytes(&render_data,reinterpret_cast<void*>(mesh.asset+0x38),sizeof(render_data))
            || render_data!=found->lci_binding.render_data)return false;
        for(std::size_t i=0;i<count;++i)if(output[i]==found->id)return false;
        output[count++]=found->id;
    }
    return true;
}

bool Sc6ReplayParticleCopy::CaptureExecutionRegistry() noexcept
{
    // Only validation metadata is refreshed. B's saved registry and its
    // detached native allocations remain untouched.
    __try {
        const auto root=system_+0x40;
        const auto count=Field<int>(root,8),bits=Field<int>(root,0x28);
        if(count<0 || count>65536 || bits!=count
            || std::size_t(count)>birth_excluded_slots_.capacity()
            || std::size_t((bits+31)/32)>birth_excluded_flags_.capacity()) return false;
        birth_excluded_slots_.resize(count);
        birth_excluded_flags_.resize((bits+31)/32);
        std::memcpy(birth_excluded_registry_.data(),reinterpret_cast<void*>(root),0x38);
        if(count) std::memcpy(birth_excluded_slots_.data(),Field<void*>(root),std::size_t(count)*8);
        const auto flags=Field<std::uintptr_t>(root,0x20);
        if(bits) std::memcpy(birth_excluded_flags_.data(),reinterpret_cast<void*>(flags?flags:root+0x10),
            birth_excluded_flags_.size()*4);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

bool Sc6ReplayParticleCopy::BeginExecution(std::size_t budget,std::size_t registry_ceiling) noexcept
{
    if(execution_started_ || witness_.phase!=Phase::Installed || !completion_.retired()
        || !witness_.native_write_uncommitted || !witness_.installed_copy_complete
        || (!coordinates_.empty() && !coordinate_execution_started_)
        || !Bindings(reinterpret_cast<void*>(world_),false)
        || !SelectionMatches(witness_.captured_selection)
        || (birth_dirty_ && !BirthRegistryMatchesExcluded()) || witness_.bytes>budget) return false;
    // Reserve validation scratch before native ownership changes. The sparse
    // registry's independently validated native limit is 65536 entries.
    const auto old_scratch=birth_excluded_slots_.capacity()*8+birth_excluded_flags_.capacity()*4;
    constexpr std::size_t scratch=65536*8+2048*4;
    if(old_scratch>scratch || scratch-old_scratch>budget-witness_.bytes) return false;
    try {
        birth_excluded_slots_.reserve(65536);
        birth_excluded_flags_.reserve(2048);
    } catch(...) {
        witness_.bytes+=birth_excluded_slots_.capacity()*8+birth_excluded_flags_.capacity()*4-old_scratch;
        return false;
    }
    witness_.bytes+=birth_excluded_slots_.capacity()*8+birth_excluded_flags_.capacity()*4-old_scratch;
    if(witness_.bytes>budget || !PrepareRegistryStorage(budget)) return false;
    const auto old_registry=birth_registry_storage_.owned_bytes();
    // Conservatively retain the previous charge as well as the C ceiling.
    // This may overcount A storage retired by native code, never undercount B.
    if(registry_ceiling>budget-witness_.bytes) return false;
    if(!birth_registry_storage_.pending() && !birth_registry_storage_.Publish()) return false;
    if(!ReadBytes(&execution_boundary_epoch_,reinterpret_cast<void*>(base_+0x4197170),8)) return false;
    execution_started_=true;
    // A partial handoff remains an uncommitted transaction. The host must
    // settle it without advancing if any following operation rejects.
    if(!BeginLightingExecution() || !BeginVisibilityExecution()) return false;
    if(!birth_registry_storage_.BeginExecution(registry_ceiling)) return false;
    registry_executing_=true;
    const auto current_registry=birth_registry_storage_.owned_bytes();
    if(current_registry<old_registry || current_registry-old_registry>budget-witness_.bytes) return false;
    witness_.bytes+=current_registry-old_registry;
    return true;
}

bool Sc6ReplayParticleCopy::SettleExecution(std::size_t budget, RestoreSettlement purpose) noexcept
{
    if(!execution_started_ || execution_settled_ || !completion_.retired() || !witness_.execution_work_complete
        || (purpose!=RestoreSettlement::RecoverOriginal && purpose!=RestoreSettlement::CommitCurrent)
        || !witness_.native_write_uncommitted || witness_.bytes>budget
        || !Bindings(reinterpret_cast<void*>(world_),false)) return false;
    // The physical epoch is a binding for this completed boundary, not the
    // historical simulation coordinate. Never write the global engine clock.
    if(!ReadBytes(&execution_boundary_epoch_,reinterpret_cast<void*>(base_+0x4197170),8)
        || !BirthOwnersBinding()) return false;
    if(registry_executing_ && !birth_registry_storage_.settled()
        && !birth_registry_storage_.SettleExecution()) return false;
    if(birth_dirty_ && (!CaptureExecutionRegistry() || !BirthRegistryMatchesExcluded())) return false;
    if(lighting_executing_ && !lighting_execution_settled_ && !SettleLightingExecution(budget,purpose)) return false;
    if(visibility_executing_ && !visibility_execution_settled_ && !SettleVisibilityExecution(budget)) return false;
    if(!RetainExecutionReflection(budget)) return false;
    if(!ReadSelection(execution_selection_)) return false;
    execution_settlement_=purpose;execution_settled_=true;
    return true;
}

bool Sc6ReplayParticleCopy::DrainExecutionWork() noexcept
{
    if(!execution_started_ || execution_settled_ || !witness_.native_write_uncommitted
        || !completion_.retired() || !Bindings(reinterpret_cast<void*>(world_),false)
        || !PrepareTransfer()) return false;
    // The host submits this on the ordered native render route after C-only
    // teardown. A GPU timeout retains the original transaction and resources;
    // it does not cancel the queued native releases or authorize B publication.
    witness_.execution_work_complete=false;
    request_deadline_=ReplayGpuCompletion::Clock::now()+std::chrono::milliseconds(500);
    const auto hr=completion_.Submit(context_.Get(),request_deadline_);
    if(FAILED(hr)) {Fail(hr);return false;}
    witness_.phase=Phase::ReadExecutionDrain;
    return true;
}

bool Sc6ReplayParticleCopy::DrainPrivateOwnerRetirement() noexcept
{
    if(execution_started_ || witness_.native_write_uncommitted || !completion_.retired()
        || (witness_.phase!=Phase::ReadyA && witness_.phase!=Phase::UndoReady
            && witness_.phase!=Phase::Recovered
            && !(witness_.phase==Phase::Failed && witness_.undo_ready))
        || witness_.private_owner_retirements==UINT64_MAX
        || !Bindings(reinterpret_cast<void*>(world_),false) || !PrepareTransfer())return false;
    private_retirement_return_=witness_.phase;
    request_deadline_=ReplayGpuCompletion::Clock::now()+std::chrono::milliseconds(500);
    const auto hr=completion_.Submit(context_.Get(),request_deadline_);
    if(FAILED(hr)){Fail(hr);return false;}
    witness_.phase=Phase::ReadPrivateOwnerRetirement;
    return true;
}



bool Sc6ReplayParticleCopy::CanRetireAfterExecutionDrain() const noexcept
{
    // C is still native-owned here. No participant may have adopted its
    // allocation metadata; partial render settlement needs its own reopening.
    return execution_started_ && !execution_settled_ && completion_.retired()
        && witness_.native_write_uncommitted && witness_.execution_work_complete
        && !witness_.pending && registry_executing_ && !birth_registry_storage_.settled()
        && lighting_executing_ && !lighting_execution_settled_ && !lighting_c_.captured
        && lighting_c_.uniforms.empty()
        && (!view_state_ || (visibility_executing_ && !visibility_execution_settled_
            && !visibility_c_.captured && !visibility_c_.leases
            && !visibility_c_.material_uniform && !visibility_c_.material_scene_uniform));
}

bool Sc6ReplayParticleCopy::RebuildCoordinates(bool target, bool reversible) noexcept
{
    if (coordinates_.empty()) return true;
    // 141F95B10 owns FRenderResource list mutation: this bounded port requires
    // the verified render-thread/immediate-RHI combination, not an RHI worker.
    if (Field<bool>(base_, 0x434459e) || coordinate_refs_ != coordinates_.size()) return false;
    if (!target && (!reversible || !coordinate_execution_started_ || coordinate_undo_complete_)) return false;
    for (const auto& row : coordinates_) {
        if (!target && row.fresh) {
            if (!row.native_retired) return false;
            continue; // B owns no corresponding buffers; native retirement was sealed before CPU undo.
        }
        auto observed = row;
        if (!target) {
            // C may have generated another number of coordinates. Validate
            // the actual current native buffers; never compare to stale A's
            // count or use this observation as the B reconstruction input.
            if (!ReadBytes(&observed.previous_count, reinterpret_cast<void*>(row.render + 0x40), sizeof(observed.previous_count))) return false;
        }
        if (!CoordinatesBound(observed, false) || row.previous_tiles.size() != row.previous_count) return false;
    }
    if (!PrepareTransfer()) return false;
    if (reversible) coordinate_execution_started_ = true;
    else coordinate_commit_started_ = true;
    __try {
        for (const auto& row : coordinates_) {
            if (!target && row.fresh) continue;
            if(target && !row.rebuild_tiles)continue; // Ownership-only row; do not rebuild unchanged buffers.
            struct Packet { std::uintptr_t render; const std::uint32_t* tiles; int count, capacity; std::uintptr_t resource; };
            const auto tiles = target ? row.tiles : row.previous_tiles;
            const Packet packet{row.render, tiles.data(), static_cast<int>(tiles.size()), static_cast<int>(tiles.size()), row.resource};
            // Exact native resource rebuild, not the consuming dynamic-packet
            // builder. The retained compiled resource cannot die in this call.
            reinterpret_cast<void (*)(const Packet*)>(base_ + 0x1f95b10)(&packet);
            if(target && row.fresh) {
                // Native publisher141F9CFB0 consumes these two request flags
                // after invoking this rebuild (or queuing its owned packet).
                // We already own the ordered RT call and immutable tile input;
                // do not queue a second task or consume dynamic spawn data.
                if(Field<unsigned>(row.render,0x250)!=0x01000101) {
                    witness_.reconstruction_owner=row.component;witness_.reconstruction_issue=119;return false;
                }
                Field<unsigned short>(row.render,0x250)=0;
            }
            if (!CoordinatesBound(row, target)) return false;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    request_deadline_ = ReplayGpuCompletion::Clock::now() + std::chrono::milliseconds(500);
    const auto hr = completion_.Submit(context_.Get(), request_deadline_);
    if (FAILED(hr)) { Fail(hr); return false; }
    witness_.phase = reversible ? (target ? Phase::ReadExecutionCoordinates : Phase::ReadUndoCoordinates) : Phase::ReadCoordinates;
    return true;
}

bool Sc6ReplayParticleCopy::CommitInstalled(bool transfer_birth) noexcept
{
    // Execution retirement is distinct from recovery settlement: C-only
    // births must survive success. Until that path is admitted, retain B.
    if (execution_started_ && (!execution_settled_ || execution_settlement_!=RestoreSettlement::CommitCurrent)) return false;
    // Only the enclosing owner can decide whether CPU state, memberships,
    // presentation and cancellation checks also permit commit.
    // Transferring a quarantined component to session ownership and final
    // tile-safe retirement belong to the enclosing lifecycle participant.
    if (transfer_birth ? (!witness_.birth_prepared || !birth_dirty_ || !BirthRegistryMatchesExcluded())
        : (witness_.birth_prepared || birth_dirty_)) return false;
    if (witness_.phase != Phase::Installed || !completion_.retired() || !witness_.installed_copy_complete
        || !Bindings(reinterpret_cast<void*>(world_), false) || FAILED(device_->GetDeviceRemovedReason())
        || !SelectionMatches(execution_started_?execution_selection_:witness_.captured_selection)) return false;
    // ReadInstalled already enforced the request-to-ready deadline. A user
    // may hold the verified target before deciding whether to resume/commit.
    if (!execution_started_ && !coordinates_.empty()) {
        if (!RebuildCoordinates()) { Fail(E_FAIL); return false; }
        return true; // The event query, not native submission, finishes commit.
    }
    if(!FinishLighting(true) || !FinishVisibility(true)
        || (birth_registry_storage_.pending() && !birth_registry_storage_.Commit())) {Fail(E_FAIL);return false;}
    witness_.native_write_uncommitted = false;
    birth_dirty_ = false;
    selection_dirty_ = false;
    for(auto& h : histories_) h.dirty=h.image_dirty=false;
    witness_.phase = Phase::Committed;
    return true;
}

bool Sc6ReplayParticleCopy::CompleteNativeRetirement() noexcept
{
    // Submitted after native component teardown on the ordered render/RHI
    // route. Keep all resource leases until the GPU completion is observed.
    if (witness_.phase != Phase::Committed || !completion_.retired()
        || !Bindings(reinterpret_cast<void*>(world_), false) || !PrepareTransfer()) return false;
    request_deadline_ = ReplayGpuCompletion::Clock::now() + std::chrono::milliseconds(500);
    witness_.phase = Phase::RetiringCommit;
    const auto result = completion_.Submit(context_.Get(), request_deadline_);
    if (FAILED(result)) { Fail(result); return false; }
    return true;
}

void Sc6ReplayParticleCopy::RetireInFlight() noexcept
{
    completion_.Cancel();
    if (!completion_.retired()) completion_.Poll(context_.Get(), ReplayGpuCompletion::Clock::now());
    Fail(completion_.error() == S_OK ? E_ABORT : completion_.error());
}

void Sc6ReplayParticleCopy::Poll() noexcept
{
    if (completion_.retired()) return;
    const auto result = completion_.Poll(context_.Get(), ReplayGpuCompletion::Clock::now());
    if (result == ReplayGpuCompletion::Result::Pending) return;
    if (witness_.phase == Phase::CancelRetirement || witness_.phase == Phase::TimeoutRetirement)
    {
        const bool timeout = witness_.phase == Phase::TimeoutRetirement;
        const auto expected = timeout ? ReplayGpuCompletion::Result::TimedOut : ReplayGpuCompletion::Result::Cancelled;
        if (result != expected) { Fail(E_UNEXPECTED); return; }
        if (!completion_.retired()) return; // Expiration/cancellation never cancels submitted work.
        if (timeout)
        {
            witness_.timeout_retired = true;
            witness_.phase = Phase::ReadyB;
        }
        else
        {
            witness_.cancel_retired = true;
            StartRetirementProbe(true);
        }
        return;
    }
    if (result != ReplayGpuCompletion::Result::Complete)
    {
        witness_.deadline_expired |= result == ReplayGpuCompletion::Result::TimedOut;
        Fail(completion_.error() == S_OK ? E_ABORT : completion_.error());
        return;
    }
    if (witness_.phase == Phase::RetiringCommit)
    { witness_.phase = Phase::RetiredCommit; return; }
    if (witness_.phase == Phase::ReadExecutionDrain) {
        witness_.execution_work_complete=true;
        witness_.phase=Phase::Installed;
        return;
    }
    if(witness_.phase==Phase::ReadPrivateOwnerRetirement) {
        ++witness_.private_owner_retirements;
        witness_.phase=private_retirement_return_;
        private_retirement_return_=Phase::Empty;
        return;
    }
    if (witness_.phase == Phase::ReadExecutionCoordinates || witness_.phase == Phase::ReadUndoCoordinates) {
        const bool target = witness_.phase == Phase::ReadExecutionCoordinates;
        for (const auto& row : coordinates_) if (!CoordinatesBound(row, target)) { Fail(E_FAIL); return; }
        if (!target) coordinate_undo_complete_ = true;
        // Coordinate completion never releases B or publishes seek success.
        // Recovery re-enters RestoreUndo to restore the other GPU owners.
        witness_.phase = Phase::Installed;
        return;
    }
    if (witness_.phase == Phase::ReadCoordinates) {
        for (const auto& row : coordinates_) if (!CoordinatesBound(row, true)) { Fail(E_FAIL); return; }
        if(!FinishLighting(true) || !FinishVisibility(true)
            || (birth_registry_storage_.pending() && !birth_registry_storage_.Commit())) {Fail(E_FAIL);return;}
        witness_.native_write_uncommitted = false;
        birth_dirty_ = selection_dirty_ = false;
    for(auto& h : histories_) h.dirty=h.image_dirty=false;
        witness_.phase = Phase::Committed;
        return;
    }
    if(witness_.phase==Phase::ReadPackedA) {
        for(const auto& image:packed_images_)if(image && !image->Complete(completion_)){Fail(E_FAIL);return;}
        if(!SelectionMatches(witness_.captured_selection)){Fail(E_FAIL);return;}
        if(ReadHashes(witness_.original))witness_.phase=Phase::ReadyA;
        return;
    }
    if (witness_.phase == Phase::ReadUndo || witness_.phase == Phase::ReadInstalled || witness_.phase == Phase::ReadRecovered)
    {
        const auto phase = witness_.phase;
        std::array<std::uint64_t, 6> observed{};
        if (!ReadHashes(observed)) return;
        const auto& expected = phase == Phase::ReadInstalled ? witness_.original : witness_.subsequent;
        if (witness_.diagnostic_readbacks && observed != expected) { Fail(E_FAIL); return; }
        if (phase == Phase::ReadUndo) { witness_.undo_ready = true; witness_.phase = Phase::UndoReady; }
        else if (phase == Phase::ReadInstalled) { witness_.installed_copy_complete = true; witness_.installed_matches = witness_.diagnostic_readbacks; witness_.phase = Phase::Installed; }
        else
        {
            // Undo is not ready for release while the pool still selects A.
            // Do not clear the dirty flag merely because all six texture images
            // match B. The enclosing transaction must restore B's selection.
            if (!SelectionMatches(witness_.subsequent_selection)
                || (witness_.birth_prepared && (!witness_.birth_recovered || !BirthRegistryMatchesB())))
            { Fail(E_FAIL); return; }
            witness_.recovered_copy_complete = true;
            witness_.recovered_matches = witness_.diagnostic_readbacks;
            for(auto& h : histories_) h.image_dirty=false;
            if(native_recovery_==NativeRecovery::CpuPending)native_recovery_=NativeRecovery::CpuReady;
            else {
                if(native_recovery_==NativeRecovery::PublicationPending) {
                    native_recovery_=NativeRecovery::Complete;
                    RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] stage render recovery state=Complete B_retained=true GPU_completed=true\n"));
                }
                witness_.native_write_uncommitted = false;
            }
            witness_.phase = Phase::Recovered;
        }
    }
    else if (witness_.phase == Phase::ReadA)
    {
        if (!SelectionMatches(witness_.captured_selection)) { Fail(E_FAIL); return; }
        if(!FinishImageEquality()){Fail(E_FAIL);return;}
        if(witness_.phase==Phase::ReadPackedA)return;
        if (ReadHashes(witness_.original)) witness_.phase = Phase::ReadyA;
    }
    else if (witness_.phase == Phase::ReadRetainedA)
    {
        if (!ReadHashes(witness_.retained)) return;
        witness_.retained_matches = witness_.diagnostic_readbacks && witness_.original == witness_.retained;
        if (witness_.diagnostic_readbacks && !witness_.retained_matches) { Fail(E_FAIL); return; }
        witness_.retained_copy_complete = true;
        witness_.phase = Phase::ReadB;
        SubmitRead(sources_);
    }
    else if (witness_.phase == Phase::ReadB)
    {
        if (!SelectionMatches(witness_.subsequent_selection)) { Fail(E_FAIL); return; }
        if (!ReadHashes(witness_.subsequent)) return;
        for (std::size_t i = 0; i < sources_.size(); ++i)
            witness_.source_changes += witness_.original[i] != witness_.subsequent[i];
        // Reuse the existing private image and staging allocation to exercise
        // failure retirement. No native state is written and no new image is captured.
        if (retirement_probe_) StartRetirementProbe(false);
        else witness_.phase = Phase::ReadyB;
    }
}

bool Sc6ReplayParticleCopy::StartRetirementProbe(bool timeout) noexcept
{
    request_deadline_ = ReplayGpuCompletion::Clock::now()
        + (timeout ? std::chrono::milliseconds(0) : std::chrono::milliseconds(500));
    if (!SubmitRead(images_)) return false;
    if (!timeout) completion_.Cancel();
    // Even if the GPU has already finished, completion has not been observed:
    // the owner must reject release and retain every lease until Poll retires it.
    const bool blocked = !completion_.Release();
    if (!blocked) { Fail(E_UNEXPECTED); return false; }
    if (timeout) witness_.timeout_release_blocked = true;
    else witness_.cancel_release_blocked = true;
    witness_.phase = timeout ? Phase::TimeoutRetirement : Phase::CancelRetirement;
    return true;
}

void Sc6ReplayParticleCopy::Fail(HRESULT error) noexcept
{
    if (witness_.phase != Phase::Failed && witness_.phase != Phase::RetiringFailure)
        RC::Output::send<RC::LogLevel::Error>(STR(
            "[HorseMod] particle transaction failure phase={} error={:08x} gpu_result={} retired={} deadline_expired={} deadline_remaining_us={} submitted_serial={}\n"),
            static_cast<unsigned>(witness_.phase), static_cast<unsigned>(error),
            static_cast<unsigned>(completion_.result()), completion_.retired(), witness_.deadline_expired,
            std::chrono::duration_cast<std::chrono::microseconds>(request_deadline_-ReplayGpuCompletion::Clock::now()).count(),
            completion_.submitted_serial());
    witness_.error = error;
    witness_.phase = completion_.retired() ? Phase::Failed : Phase::RetiringFailure;
}
void Sc6ReplayParticleCopy::Cancel() noexcept
{
    completion_.Cancel();
    Fail(E_ABORT);
}
bool Sc6ReplayParticleCopy::Finish() noexcept
{
    if (capture_owner_fault_) return false;
    if (birth_registry_storage_.pending()) return false;
    if (witness_.native_write_uncommitted || birth_dirty_ || HistoriesDirty() || visibility_dirty_ || visibility_native_a_refs_ || material_dirty_ || lighting_dirty_) return false;
    if (!completion_.Release()) return false;
    image_equality_.reset();
    image_equality_basis_.reset();
    image_materializer_.reset();
    packed_images_={};
    if(!FinishLighting(false) || !FinishVisibility(false)) return false;
    visibility_a_={};visibility_b_={};visibility_c_={};
    visibility_storage_ready_=visibility_storage_published_=visibility_storage_transferred_=false;
    visibility_executing_=visibility_execution_settled_=false;
    lighting_a_={};lighting_b_={};lighting_c_={};lighting_transferred_=false;
    lighting_executing_=lighting_execution_settled_=false;
    for (std::size_t i = 0; i < coordinate_refs_; ++i)
        reinterpret_cast<int (*)(void*)>(base_ + 0x1fa0e50)(reinterpret_cast<void*>(coordinates_[i].resource));
    coordinate_refs_ = 0;
    coordinate_execution_started_=coordinate_undo_complete_=false;
    std::vector<Sc6ReplayVfxState::CoordinateRebuild>().swap(coordinates_);
    for (auto& resource : undo_) resource.Reset();
    for (auto& resource : staging_) resource.Reset();
    for (auto& resource : images_) resource.Reset();
    for (auto& resource : sources_) resource.Reset();
    const auto assign = reinterpret_cast<void** (*)(void**, void*)>(base_ + 0x15b83f0);
    for (auto& wrapper : wrappers_) if (wrapper) assign(&wrapper, nullptr);
    for(auto& h : histories_) {
        h.staging.Reset();h.image_a.Reset();h.image_b.Reset();h.source_a.Reset();h.source_b.Reset();h.source_c.Reset();
        for(auto* target : {&h.a,&h.b,&h.c}) if(*target) {
            reinterpret_cast<int (*)(void*)>(base_+0x146b380)(reinterpret_cast<void*>(*target));*target=0;
        }
    }
    context_.Reset(); device_.Reset();
    captured_.reset();
    std::vector<std::uint64_t>().swap(birth_slots_);
    std::vector<std::uint32_t>().swap(birth_flags_);
    std::vector<std::uint64_t>().swap(birth_excluded_slots_);
    std::vector<std::uint32_t>().swap(birth_excluded_flags_);
    births_ = {}; birth_count_ = 0; birth_indices_ = {};
    birth_fields_ = {}; birth_field_types_ = {}; birth_fields_retained_ = false;
    local_fields_a_.count=local_fields_b_.count=0;local_fields_a_.captured=local_fields_b_.captured=false;
    if (!birth_registry_storage_.DiscardPreparation()) return false;
    birth_registry_published_ = false;
    registry_prepared_=registry_executing_=execution_started_=execution_settled_=false;
    execution_boundary_epoch_=0;execution_selection_=-1;execution_settlement_=RestoreSettlement::RecoverOriginal;
    std::vector<ReplayStageRenderOwner>().swap(stage_render_owners_);
    native_recovery_=NativeRecovery::None;
    witness_.phase = Phase::Released;
    return true;
}
Sc6ReplayParticleCopy::Witness Sc6ReplayParticleCopy::witness() const noexcept
{
    auto result = witness_;
    result.pending = !completion_.retired();
    return result;
}
}
