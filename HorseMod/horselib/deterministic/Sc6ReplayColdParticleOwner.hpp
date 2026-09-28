#pragma once
#include <Windows.h>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectArray.hpp>
#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>
#include "Sc6ReplayObjectLease.hpp"
#include "ReplayParticleMaterialGraph.hpp"
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstring>

namespace Horse::Deterministic {
// Native cold configuration boundary for component-version construction.
// Construct/Retire require a completed application/render hold and an owned
// GC lease. No registration, activation or emitter/render publication occurs.
// A failed partial construction requires the caller to preserve the hold.
// The existing diagnostic invokes this same construction/retirement boundary.
struct Sc6ReplayColdParticleOwner {
    struct Witness {
        const char* check{"not_started"};std::uintptr_t source{},copy{},outer{};
        unsigned properties{},arrays{},outer_members{},property_offset{},copy_flags{};
        std::size_t property_bytes{};
        std::int32_t source_index{-1},source_serial{};
        RC::Unreal::FProperty* visited_property{};
        RC::Unreal::FProperty* differing_property{};
        std::array<std::uint64_t,2> source_value{},copy_value{};
        bool differing_alias{};
        std::uintptr_t modify_rva{},transaction_buffer{};
        unsigned dirty_gate{};
        bool created{},detached{},destroyed{},lease_released{},source_unchanged{};
        bool native_construction_entered{};
        bool material_construction_uncertain{};
    };
    template<class T> static T Read(std::uintptr_t p,std::size_t offset=0) {
        T value;std::memcpy(&value,reinterpret_cast<const void*>(p+offset),sizeof(value));return value;
    }
    static bool NativeDuplicate(std::uintptr_t base,void* source,void* outer,void*& result) noexcept {
        __try {
            // Both flag masks are zero: no PendingKill, load or root flags
            // are copied. Native duplicate mode0; same class and fresh name.
            result=reinterpret_cast<void*(*)(void*,void*,std::uint64_t,unsigned,void*,int,unsigned)>(base+0xf8d310)
                (source,outer,0,0,nullptr,0,0);
            return result!=nullptr;
        } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static bool NativeRemove(std::uintptr_t base,void* outer,void* component) noexcept {
        __try {reinterpret_cast<void(*)(void*,void*)>(base+0x1c27a20)(outer,component);return true;}
        __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static bool NativeDestroy(std::uintptr_t base,void* component) noexcept {
        __try {reinterpret_cast<void(*)(void*,bool)>(base+0x8cecf0)(component,false);return true;}
        __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static bool Live(void* object) {
        if(!object)return false;
        const auto* item=RC::Unreal::FUObjectArray::IndexToObject(static_cast<RC::Unreal::UObject*>(object)->GetInternalIndex());
        return item && item->GetUObject()==object && item->IsValid(false);
    }
    using Members=std::array<std::uintptr_t,1024>;
    static bool ReadMembers(std::uintptr_t owner,Members& out,unsigned& count) {
        count=0;out={};const auto root=owner+0x2c0;
        const auto n=Read<int>(root,8),cap=Read<int>(root,12),bits=Read<int>(root,0x28),maxbits=Read<int>(root,0x2c),free=Read<int>(root,0x34);
        const auto data=Read<std::uintptr_t>(root),allocated=Read<std::uintptr_t>(root,0x20);
        if(n<0 || cap<n || cap>1024 || bits!=n || maxbits<bits || free<0 || free>n
            || (!allocated && maxbits>128) || (n && !data))return false;
        const auto flags=allocated?allocated:root+0x10;
        for(int i=0;i<n;++i)if(Read<unsigned>(flags,std::size_t(i/32)*4)&(1u<<(i%32))) {
            const auto object=Read<std::uintptr_t>(data,std::size_t(i)*16);
            if(!object || count==out.size() || std::find(out.begin(),out.begin()+count,object)!=out.begin()+count)return false;
            out[count++]=object;
        }
        return count==unsigned(n-free);
    }
    static bool EmptyArray(std::uintptr_t p,std::size_t offset) {return Read<int>(p,offset+8)==0;}
    static bool BoundedProperty(std::uintptr_t base,RC::Unreal::FProperty* property,std::uintptr_t container,
        std::size_t extent,unsigned depth,std::size_t& bytes,RC::Unreal::FProperty** visited=nullptr) {
        using namespace RC::Unreal;
        if(visited)*visited=property;
        if(depth>8)return false;
        const auto offset=property->GetOffset_Internal(), size=property->GetElementSize(),dim=property->GetArrayDim();
        if(offset<0 || size<=0 || dim<=0 || dim>64 || std::size_t(offset)>extent
            || std::size_t(size)*dim>extent-std::size_t(offset))return false;
        if(property->GetPropertyFlags()&(CPF_Transient|CPF_DuplicateTransient))return true;
        const auto kind=property->GetClass().GetFName().ToString();
        if(property->GetPropertyFlags()&(CPF_InstancedReference|CPF_ContainsInstancedReference)) {
            // UE marks weak/delegate references as instanced too; they do not
            // own nested UObject construction. Nonempty strong instanced
            // graphs remain outside this one-component experiment.
            const bool weak=kind==L"WeakObjectProperty" || kind==L"DelegateProperty"
                || kind==L"MulticastDelegateProperty";
            const bool absent_object=kind==L"ObjectProperty" && dim==1 && !Read<std::uintptr_t>(container,offset);
            const bool empty_array=kind==L"ArrayProperty" && dim==1 && !Read<int>(container,offset+8);
            if(!weak && !absent_object && !empty_array)return false;
        }
        for(int i=0;i<dim;++i) {
            const auto value=container+offset+std::size_t(i)*size;
            if(kind==L"ArrayProperty") {
                auto* inner=static_cast<FArrayProperty*>(property)->GetInner();
                const auto n=Read<int>(value,8),cap=Read<int>(value,12);
                const auto data=Read<std::uintptr_t>(value);
                if(!inner || n<0 || cap<n || cap>64 || (cap && !data) || inner->GetElementSize()<=0)return false;
                const auto stride=std::size_t(inner->GetElementSize());
                if(stride>65536 || bytes+stride*cap>1024*1024)return false;
                bytes+=stride*cap;
                for(int j=0;j<n;++j)if(!BoundedProperty(base,inner,data+std::size_t(j)*stride,stride,depth+1,bytes,visited))return false;
            } else if(kind==L"StructProperty") {
                auto type=static_cast<FStructProperty*>(property)->GetStruct();
                // Custom serializers can allocate beyond the reflected graph.
                // Reject them until their concrete allocation/lifetime path is audited.
                if(!type)return false;
                if(type->HasAnyStructFlags(EStructFlags(STRUCT_SerializeNative|STRUCT_PostSerializeNative))) {
                    const auto ops=reinterpret_cast<std::uintptr_t>(type->GetCppStructOps());
                    if(!ops)return false;
                    const auto table=Read<std::uintptr_t>(ops);
                    // Vector/Rotator F468A0 -> 1AC4D60 serializes three floats;
                    // Color F465E0 -> F48A60 serializes one uint32. Neither
                    // consumer constructs nested owners. Keep exact metadata
                    // and dispatch checks; size alone does not admit a serializer.
                    const bool vector=size==12 && type->GetStructFlags()==0xec38
                        && (table==base+0x354cc68 || table==base+0x354bf10)
                        && Read<std::uintptr_t>(table,0x38)==base+0xf468a0;
                    const bool color=size==4 && type->GetStructFlags()==0xe838
                        && table==base+0x354c750
                        && Read<std::uintptr_t>(table,0x38)==base+0xf465e0;
                    if(!vector && !color)return false;
                    continue;
                }
                unsigned count{};
                for(auto* field:type->ForEachPropertyInChain())
                    if(++count>512 || !BoundedProperty(base,field,value,size,depth+1,bytes,visited))return false;
            } else if(kind==L"UInt64Property") {
                // BodyInstance exposes native rigid-actor IDs as UInt64.
                // 141FE7A50 initializes them to zero. Do not duplicate any
                // opaque nonzero handle into a supposedly independent owner.
                if(size!=8 || Read<std::uint64_t>(value))return false;
            } else if(kind==L"MulticastDelegateProperty") {
                const auto n=Read<int>(value,8),cap=Read<int>(value,12);
                if(size!=16 || n<0 || cap<n || cap>64 || (cap && !Read<std::uintptr_t>(value))
                    || bytes+std::size_t(cap)*16>1024*1024)return false;
                bytes+=std::size_t(cap)*16;
            } else if(kind!=L"ObjectProperty" && kind!=L"WeakObjectProperty" && kind!=L"ClassProperty"
                && kind!=L"BoolProperty" && kind!=L"ByteProperty" && kind!=L"IntProperty"
                && kind!=L"FloatProperty" && kind!=L"NameProperty" && kind!=L"EnumProperty"
                && kind!=L"DelegateProperty")return false;
        }
        return true;
    }
    static bool EquivalentProperty(RC::Unreal::FProperty* property,std::uintptr_t source,std::uintptr_t copy,int index) {
        if(property->GetClass().GetFName().ToString()==L"MulticastDelegateProperty") {
            // Native F59120 deliberately treats every nonempty delegate as
            // nonidentical with port1000. Compare the serialized ordered weak
            // index/serial + FName index/number tuples, never install them.
            if(index!=0 || property->GetArrayDim()!=1 || property->GetElementSize()!=16)return false;
            const auto offset=property->GetOffset_Internal();if(offset<0)return false;
            const auto a=Read<std::uintptr_t>(source,offset),b=Read<std::uintptr_t>(copy,offset);
            const auto an=Read<int>(source,offset+8),ac=Read<int>(source,offset+12);
            const auto bn=Read<int>(copy,offset+8),bc=Read<int>(copy,offset+12);
            if(an<0 || an!=bn || ac<an || bc<bn || ac>64 || bc>64
                || (ac && !a) || (bc && !b) || (a && a==b))return false;
            return !an || std::memcmp(reinterpret_cast<const void*>(a),reinterpret_cast<const void*>(b),std::size_t(an)*16)==0;
        }
        return property->Identical_InContainer(reinterpret_cast<const void*>(source),reinterpret_cast<const void*>(copy),index,0x1000);
    }
    static bool Cold(std::uintptr_t base,std::uintptr_t component,std::uintptr_t outer) {
        const auto flags=Read<unsigned>(component,0x188);
        return Read<std::uintptr_t>(component)==base+0x335db28 && Read<std::uintptr_t>(component,0x20)==outer
            && Read<std::uintptr_t>(component,0x190)==outer
            && !(flags&(1u|2u|4u|0x40000u|0x200000u|0x1c000000u|0x20000000u))
            && !(Read<unsigned char>(component,0x11c)&0x40) && !(Read<unsigned char>(component,0x7bc)&0x40)
            && !Read<std::uintptr_t>(component,0x1c8) && !Read<std::uintptr_t>(component,0x1d0)
            && EmptyArray(component,0x1d8) && EmptyArray(component,0x6a0)
            && !(Read<unsigned char>(component,0x3f8)&2)
            && !Read<std::uintptr_t>(component,0x790)
            && EmptyArray(component,0xa50) && !Read<std::uintptr_t>(component,0xa60)
            && !Read<std::uintptr_t>(component,0xa70) && !Read<unsigned char>(component,0xa80)
            && !Read<unsigned char>(component,0xa81);
    }
    static bool PassiveOuterModify(std::uintptr_t base,std::uintptr_t outer) {
        if(Read<std::uintptr_t>(base+0x418b108) || Read<unsigned char>(base+0x418b12f))return false;
        const auto table=Read<std::uintptr_t>(outer),modify=Read<std::uintptr_t>(table,0x70);
        if(modify==base+0xf5f2d0)return true;
        // LuxWorldSettings ctor CF4E90 installs3499188. Its Actor::Modify
        // override1C23160 calls UObject::Modify, optionally visiting reflected
        // components under the transaction buffer and recursing into root168.
        // Require both paths absent; invoke native Modify normally, never skip it.
        return table==base+0x3499188 && modify==base+0x1c23160
            && !Read<std::uintptr_t>(outer,0x168);
    }
    static bool SingleGpuSource(std::uintptr_t base,std::uintptr_t component) {
        const auto emitters=Read<std::uintptr_t>(component,0xa50);
        const auto count=Read<int>(component,0xa58),capacity=Read<int>(component,0xa5c);
        if(count<1 || capacity<count || capacity>128 || !emitters)return false;
        unsigned live{};
        for(int i=0;i<count;++i) {
            const auto emitter=Read<std::uintptr_t>(emitters,std::size_t(i)*8);
            if(!emitter)continue;
            if(++live!=1 || Read<std::uintptr_t>(emitter)!=base+0x394c100)return false;
        }
        return live==1;
    }
    static bool ConstructDetached(std::uintptr_t base,void* original,
        Sc6ReplayObjectLease& lease,Witness& w) {
        return ConstructDetachedImpl(base,original,lease,w,false);
    }
    static bool CloneDetached(std::uintptr_t base,const Witness& source,
        const Sc6ReplayObjectLease& source_lease,Sc6ReplayObjectLease& lease,Witness& w) {
        if(&source==&w || &source_lease==&lease)return false;
        if(!source.created || !source.detached
            || source.destroyed || source.lease_released || !source.copy
            || !source_lease.ValidateObject(reinterpret_cast<void*>(source.copy)).ok()
            || !Cold(base,source.copy,source.outer)) {
            w.check="cold_source_lease";return false;
        }
        return ConstructDetachedImpl(base,reinterpret_cast<void*>(source.copy),lease,w,true);
    }
    static bool EquivalentDetached(std::uintptr_t base,const Witness& a,const Sc6ReplayObjectLease& a_lease,
        const Witness& b,const Sc6ReplayObjectLease& b_lease) {
        using namespace RC::Unreal;
        // Compare two independently constructed, validated cold copies. This
        // never substitutes a logical owner or skips native source admission.
        if(!a.created || !b.created || !a.detached || !b.detached || a.destroyed || b.destroyed
            || a.lease_released || b.lease_released || !a.copy || !b.copy || !a.source
            || a.source!=b.source || a.outer!=b.outer || a.source_index<0 || !a.source_serial
            || a.source_index!=b.source_index || a.source_serial!=b.source_serial
            || !a_lease.ValidateObject(reinterpret_cast<void*>(a.copy)).ok()
            || !b_lease.ValidateObject(reinterpret_cast<void*>(b.copy)).ok()
            || !Cold(base,a.copy,a.outer) || !Cold(base,b.copy,b.outer))return false;
        auto* a_class=reinterpret_cast<UObject*>(a.copy)->GetClassPrivate();
        if(!a_class || a_class!=reinterpret_cast<UObject*>(b.copy)->GetClassPrivate())return false;
        std::size_t a_bytes{},b_bytes{};unsigned count{},visited{};
        for(auto* property:a_class->ForEachPropertyInChain()) {
            if(++visited>512)return false;
            if(property->GetPropertyFlags()&(CPF_Transient|CPF_DuplicateTransient))continue;
            if(property->GetArrayDim()!=1
                || !BoundedProperty(base,property,a.copy,0xad0,0,a_bytes)
                || !BoundedProperty(base,property,b.copy,0xad0,0,b_bytes)
                || !EquivalentProperty(property,a.copy,b.copy,0))return false;
            ++count;
        }
        return count==a.properties && count==b.properties;
    }
#include "Sc6ReplayColdParticleMaterials.inl"
    static bool RegistrationCallbacksEmpty(std::uintptr_t base,std::uintptr_t component) noexcept {
        __try {
            const auto name=Read<std::uint64_t>(base+0x43b5530);
            const auto function=reinterpret_cast<std::uintptr_t(*)(const void*,std::uint64_t)>(base+0xf6e0e0)
                (reinterpret_cast<void*>(component),name);
            // Same native ProcessEvent empty-script rule as the retained
            // EndPlay admission. Registration still invokes BeginPlay normally.
            return function && !(Read<unsigned>(function,0x88)&0x400) && !Read<unsigned>(function,0x50);
        } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    static bool WorldMatches(std::uintptr_t component,void* world) noexcept {
        __try {return world && reinterpret_cast<RC::Unreal::UObject*>(component)->GetWorld()==world;}
        __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    // Non-render admission only. Registration of an active component reaches
    // CreateParticleSystemSceneProxy141FBD100 -> dynamic data141F70740 -> GPU
    // builder141F98C80, which consumes pending arrays/timing. Passing this
    // check does not authorize registration against the restored live graph;
    // the enclosing owner must supply a separately verified render handoff.
    static bool RegistrationPreflight(std::uintptr_t base,void* world,
        const Sc6ReplayObjectLease& lease,const Witness& w) {
        if(!w.created || !w.detached || w.destroyed || w.lease_released || !w.copy
            || !lease.ValidateObject(reinterpret_cast<void*>(w.copy)).ok() || !Cold(base,w.copy,w.outer)
            || !WorldMatches(w.copy,world) || !PassiveOuterModify(base,w.outer))return false;
        const auto table=Read<std::uintptr_t>(w.copy);
        if(Read<std::uintptr_t>(table,0x1f8)!=base+0xf71540
            || Read<std::uintptr_t>(table,0x280)!=base+0x1f7ae20
            || Read<std::uintptr_t>(table,0x2c8)!=base+0x1dae440
            || Read<std::uintptr_t>(table,0x2e0)!=base+0x1d4c640
            || Read<std::uintptr_t>(table,0x2e8)!=base+0x1d94480
            || Read<std::uintptr_t>(table,0x368)!=base+0x1d4fd40
            // Primitive BeginPlay141D94480 queries BodySetup independently
            // of ShouldCreatePhysics. The exact Lux slot is a null leaf;
            // reject an override rather than admit possible detach work.
            || Read<std::uintptr_t>(table,0x630)!=base+0x2d9bf0)return false;
        // OnRegister141F7AE20 may activate or attach; these are simulation
        // operations, not reconstruction. Never clear their flags to pass.
        if(!Read<std::uintptr_t>(w.copy,0x808) || (Read<unsigned>(w.copy,0x830)&0x1020))return false;
        // ShouldCreatePhysics141DAE440 -> collision query141FF5120 returns
        // zero when BodyInstance+22E is zero, regardless of its weak owner.
        if((Read<unsigned char>(w.copy,0x3f8)&8) || Read<unsigned char>(w.copy,0x430+0x22e))return false;
        return RegistrationCallbacksEmpty(base,w.copy);
    }
private:
    static bool ConstructDetachedImpl(std::uintptr_t base,void* original,
        Sc6ReplayObjectLease& lease,Witness& w,bool detached_source,
        const ReplayParticleMaterialGraph* recipe=nullptr) {
        using namespace RC::Unreal;
        const auto fail=[&](const char* check){w.check=check;return false;};
        if(!original || w.created || w.copy || lease.registered())return fail("copy_owner_already_used");
        w.source=reinterpret_cast<std::uintptr_t>(original);
        auto* source=reinterpret_cast<UObject*>(w.source);
        if(!Live(source) || Read<std::uintptr_t>(w.source)!=base+0x335db28)return fail("source_native_class");
        w.source_index=source->GetInternalIndex();
        w.source_serial=FUObjectArray::IndexToObject(w.source_index)->GetSerialNumber();
        w.outer=Read<std::uintptr_t>(w.source,0x190);
        if(!Live(source) || !Live(reinterpret_cast<void*>(w.outer)) || source->GetOuterPrivate()!=reinterpret_cast<UObject*>(w.outer)
            || Read<std::uintptr_t>(w.source,0x1d0) || !EmptyArray(w.source,0x1d8))return fail("source_outer_attachment");
        ReplayParticleMaterialGraph materials;
        if(!materials.Capture(base,w.source))return fail(materials.check);
        if(materials.materials) {
            if(!recipe || !recipe->ValuesEqual(materials))return fail("source_shared_material");
            if(!MaterialDefaults(base,materials,w))return false;
        }
        for(const auto offset:{0x850u,0x860u,0x870u,0x880u,0x1a8u,0x1b8u})
            if(!EmptyArray(w.source,offset))return fail("source_extra_delegates");
        if((Read<unsigned>(w.source,0x188)&4) || Read<int>(w.source,0x6a8))return fail("source_physics_or_overlap");
        unsigned property_count{};
        for(auto* property:source->GetClassPrivate()->ForEachPropertyInChain()) {
            if(++property_count>512)return fail("property_capacity");
            const auto flags=property->GetPropertyFlags();
            if(flags&(CPF_Transient|CPF_DuplicateTransient))continue;
            w.property_offset=property->GetOffset_Internal();
            if(!BoundedProperty(base,property,w.source,0xad0,0,w.property_bytes,&w.visited_property))return fail("property_recursive_ownership_unproven");
            const auto kind=property->GetClass().GetFName().ToString();
            const auto offset=property->GetOffset_Internal();
            if(offset<0 || offset>=0xad0 || property->GetArrayDim()!=1)return fail("property_extent");
            if(kind==L"ArrayProperty") {
                const auto n=Read<int>(w.source,offset+8),cap=Read<int>(w.source,offset+12);
                if(n<0 || cap<n || cap>64 || (cap && !Read<std::uintptr_t>(w.source,offset)))return fail("property_array_capacity");
            }
            if(kind==L"MapProperty" || kind==L"SetProperty")return fail("property_container_unsupported");
        }
        w.modify_rva=Read<std::uintptr_t>(Read<std::uintptr_t>(w.outer),0x70)-base;
        w.transaction_buffer=Read<std::uintptr_t>(base+0x418b108);
        w.dirty_gate=Read<unsigned char>(base+0x418b12f);
        if(!PassiveOuterModify(base,w.outer))return fail("outer_modify_domain");
        if(!Read<bool>(base+0x419718c) || Read<DWORD>(base+0x419716c)!=GetCurrentThreadId())return fail("owner_thread");
        auto* lock=reinterpret_cast<LPCRITICAL_SECTION>(base+0x429f678);
        if(!TryEnterCriticalSection(lock))return fail("gc_busy");
        struct Unlock {LPCRITICAL_SECTION p;~Unlock(){LeaveCriticalSection(p);}} unlock{lock};
        if(Read<int>(base+0x429f674) || Read<int>(base+0x429fa54) || Read<unsigned char>(base+0x429f648)
            || Read<unsigned char>(base+0x429fa0c))return fail("gc_pending");
        Members before{},after{};unsigned count{},current{};
        if(!ReadMembers(w.outer,before,count))return fail("source_membership");
        const bool member=std::find(before.begin(),before.begin()+count,w.source)!=before.begin()+count;
        if(member==detached_source || (detached_source && !Cold(base,w.source,w.outer)))return fail("source_membership");
        w.outer_members=count;
        void* copy{};
        // MID lists are ordinary copied properties, not transient. For an
        // owned-material recipe construct from the exact CDO, then copy the
        // already-audited ordinary properties with those two lists omitted.
        // Their captured values belong to the explicit material participant.
        auto* construction_source=materials.materials?source->GetClassPrivate()->GetClassDefaultObject().Get():source;
        if(!Live(construction_source) || construction_source->GetClassPrivate()!=source->GetClassPrivate())
            return fail("material_constructor_class");
        w.native_construction_entered=true;
        if(!NativeDuplicate(base,construction_source,reinterpret_cast<void*>(w.outer),copy))return fail("duplicate_native_failure_hold_required");
        w.created=true;w.copy=reinterpret_cast<std::uintptr_t>(copy);
        // A failed preflight below leaves the application held for native exit.
        // It is never permission to destroy an unknown/active partial object.
        if(!Live(copy) || copy==source)return fail("copy_identity_hold_required");
        void* owners[]{copy};
        if(!lease.Acquire(base,owners,1024*1024).ok())return fail("copy_lease_hold_required");
        w.copy_flags=Read<unsigned>(w.copy,0x188);
        if(!Cold(base,w.copy,w.outer))return fail("copy_not_cold_hold_required");
        if(!NativeRemove(base,reinterpret_cast<void*>(w.outer),copy) || !ReadMembers(w.outer,after,current)
            || current!=count || before!=after)return fail("copy_detachment_hold_required");
        w.detached=true;
        if(materials.materials) {
            w.material_construction_uncertain=true;
            for(unsigned offset:{0xa98u,0xaa8u,0x810u})
                if(!EmptyArray(w.copy,offset))return fail("cold_material_alias_hold_required");
            for(auto* property:source->GetClassPrivate()->ForEachPropertyInChain()) {
                const auto flags=property->GetPropertyFlags();const auto offset=property->GetOffset_Internal();
                if((flags&(CPF_Transient|CPF_DuplicateTransient)) || offset==0xa98 || offset==0xaa8)continue;
                property->CopyCompleteValue_InContainer(copy,source);
            }
            if(!Cold(base,w.copy,w.outer))return fail("material_configuration_copy_hold_required");
            w.material_construction_uncertain=false;
        }
        bool properties=true;
        for(auto* property:source->GetClassPrivate()->ForEachPropertyInChain()) {
            const auto flags=property->GetPropertyFlags();
            if(flags&(CPF_Transient|CPF_DuplicateTransient))continue;
            ++w.properties;
            const auto material_offset=property->GetOffset_Internal();
            if(materials.materials && (material_offset==0xa98 || material_offset==0xaa8)) {
                ++w.arrays;
                if(!EmptyArray(w.copy,material_offset))return fail("cold_material_alias_hold_required");
                continue;
            }
            for(int i=0;i<property->GetArrayDim();++i)
                properties=EquivalentProperty(property,w.source,w.copy,i) && properties;
            // TArray backing must never alias its source, even for all-null
            // logical values. Other container ownership is not inferred here.
            if(property->GetClass().GetFName().ToString()==L"ArrayProperty") {
                const auto offset=property->GetOffset_Internal();++w.arrays;
                const auto a=Read<std::uintptr_t>(w.source,offset),b=Read<std::uintptr_t>(w.copy,offset);
                if(a && a==b) {properties=false;w.differing_alias=true;}
            }
            if(!properties) {
                w.differing_property=property;
                const auto offset=property->GetOffset_Internal();
                const auto bytes=(std::min)(std::size_t(property->GetElementSize()),sizeof(w.source_value));
                std::memcpy(w.source_value.data(),reinterpret_cast<const void*>(w.source+offset),bytes);
                std::memcpy(w.copy_value.data(),reinterpret_cast<const void*>(w.copy+offset),bytes);
                break; // Finish safe retirement, but do not inspect later properties after failure.
            }
        }
        if(!properties) {
            if(!Retire(base,lease,w))return false;
            return fail("copy_properties_differ");
        }
        w.check="cold_copy_ready";return true;
    }
public:
    static bool Retire(std::uintptr_t base,Sc6ReplayObjectLease& lease,Witness& w) {
        const auto fail=[&](const char* check){w.check=check;return false;};
        if(w.destroyed && w.lease_released)return true;
        // A failed registry retirement must retry only that retirement. The
        // completed native destruction is irreversible and must not run twice.
        if(w.destroyed) {
            w.lease_released=lease.Release().ok();
            if(!w.lease_released)return fail("copy_retirement_recovery_failed");
            w.check="cold_copy_retired";return true;
        }
        if(w.material_construction_uncertain)return fail("copy_material_hold_required");
        if(!w.created || !w.detached || !w.copy || !Cold(base,w.copy,w.outer)
            || !lease.Validate().ok())return fail("copy_retirement_preflight_hold_required");
        auto* copy=reinterpret_cast<void*>(w.copy);
        if(!NativeDestroy(base,copy))return fail("copy_retirement_native_failure_hold_required");
        w.destroyed=!Live(copy);
        if(!w.destroyed)return fail("copy_retirement_recovery_failed");
        w.lease_released=lease.Release().ok();
        if(!w.destroyed || !w.lease_released)return fail("copy_retirement_recovery_failed");
        w.check="cold_copy_retired";return true;
    }
};
}
