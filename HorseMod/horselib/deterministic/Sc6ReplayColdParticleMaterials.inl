// Included in Sc6ReplayColdParticleOwner; exact Lux MID recipe only.
static bool MaterialSlotDefaults(std::uintptr_t base,const ReplayParticleMaterialGraph::Slot& slot,Witness& w) {
    using namespace RC::Unreal;
        if(!slot.object)return true;
        auto* source=reinterpret_cast<UObject*>(slot.object);
        if(!Live(source) || Read<std::uintptr_t>(slot.object)!=base+0x391ee70
            || !FUObjectArray::IndexToObject(source->GetInternalIndex())->IsRootSet()
            || !Live(reinterpret_cast<void*>(slot.parent))) {w.check="material_source_identity";return false;}
        auto* defaults=source->GetClassPrivate()->GetClassDefaultObject().Get();
        if(!Live(defaults)) {w.check="material_class_defaults";return false;}
        unsigned count{};
        for(auto* property:source->GetClassPrivate()->ForEachPropertyInChain()) {
            if(++count>512)return false;
            if(property->GetPropertyFlags()&(CPF_Transient|CPF_DuplicateTransient))continue;
            const auto offset=property->GetOffset_Internal();
            if(offset==0x78)continue; // independently leased authored parent
            if(offset==0x88 || offset==0x98 || offset==0xa8 || offset==0xb8) {
                if(property->GetClass().GetFName().ToString()!=L"ArrayProperty" || property->GetArrayDim()!=1)return false;
                continue; // bounded values/GUID policy proved by CaptureSlot
            }
            for(int j=0;j<property->GetArrayDim();++j)
                if(!EquivalentProperty(property,slot.object,reinterpret_cast<std::uintptr_t>(defaults),j)) {
                    w.visited_property=property;w.check="material_nondefault_property";return false;
                }
        }
    return true;
}
static bool MaterialDefaults(std::uintptr_t base,const ReplayParticleMaterialGraph& recipe,Witness& w) {
    for(unsigned side=0;side<2;++side)for(int i=0;i<recipe.arrays[side].count;++i)
        if(!MaterialSlotDefaults(base,recipe.arrays[side].slots[i],w))return false;
    return true;
}
static bool ConstructConfiguration(std::uintptr_t base,void* original,Sc6ReplayObjectLease& lease,
    Witness& w,const ReplayParticleMaterialGraph& recipe) {
    return recipe.ready && ConstructDetachedImpl(base,original,lease,w,false,&recipe);
}
struct MaterialNativeCalls {
    std::uintptr_t base{},component{};
    struct Array {const std::uintptr_t* data{};int count{},capacity{};};
    bool Initialize(unsigned side,const std::uintptr_t* parents,int count,float fade) noexcept {
        Array input{parents,count,count};
        __try {
            if(side)reinterpret_cast<void(*)(void*,float,const Array*)>(base+0x8d6620)
                (reinterpret_cast<void*>(component),fade,&input);
            else reinterpret_cast<void(*)(void*,const Array*)>(base+0x8d64e0)
                (reinterpret_cast<void*>(component),&input);
            return true;
        } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    bool Scalar(std::uintptr_t object,std::uint64_t name,float value) noexcept {
        __try {reinterpret_cast<void(*)(void*,std::uint64_t,float)>(base+0x1f45660)
            (reinterpret_cast<void*>(object),name,value);return true;}
        __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    bool Vector(std::uintptr_t object,std::uint64_t name,const float* value) noexcept {
        __try {reinterpret_cast<void(*)(void*,std::uint64_t,const float*)>(base+0x1f45940)
            (reinterpret_cast<void*>(object),name,value);return true;}
        __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    bool Override(int slot,std::uintptr_t material) noexcept {
        __try {reinterpret_cast<void(*)(void*,int,void*)>(base+0x1f82010)
            (reinterpret_cast<void*>(component),slot,reinterpret_cast<void*>(material));return true;}
        __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
};
static bool MaterialNativeSignatures(std::uintptr_t base) noexcept {
    __try {
        constexpr unsigned char signature0[]{0x40,0x56,0x41,0x54,0x48,0x83,0xec,0x28,0x83,0xb9,0xa0,0xa,0x0,0x0,0x0,0x4c,0x8b,0xe2,0x48,0x8b};
        if(std::memcmp(reinterpret_cast<void*>(base+0x8d64e0),signature0,sizeof(signature0)))return false;
        constexpr unsigned char signature1[]{0x40,0x55,0x41,0x54,0x48,0x83,0xec,0x28,0x83,0xb9,0xb0,0xa,0x0,0x0,0x0,0x4d,0x8b,0xe0,0x48,0x8b};
        if(std::memcmp(reinterpret_cast<void*>(base+0x8d6620),signature1,sizeof(signature1)))return false;
        constexpr unsigned char signature2[]{0x48,0x83,0xec,0x28,0xe8,0x97,0x8b,0xfd,0xff,0x48,0x83,0xc4,0x28,0xc3,0xcc,0xcc,0x48,0x89,0x5c,0x24};
        if(std::memcmp(reinterpret_cast<void*>(base+0x1f45660),signature2,sizeof(signature2)))return false;
        constexpr unsigned char signature3[]{0x48,0x83,0xec,0x38,0x41,0xf,0x10,0x0,0x4c,0x8d,0x44,0x24,0x20,0xf,0x29,0x44,0x24,0x20,0xe8,0xc9};
        if(std::memcmp(reinterpret_cast<void*>(base+0x1f45940),signature3,sizeof(signature3)))return false;
        constexpr unsigned char signature4[]{0x48,0x89,0x5c,0x24,0x18,0x55,0x56,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0x48,0x63,0xfa,0x48,0x8b};
        if(std::memcmp(reinterpret_cast<void*>(base+0x1f82010),signature4,sizeof(signature4)))return false;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
static bool RealizeMaterials(std::uintptr_t base,const ReplayParticleMaterialGraph& recipe,
    Sc6ReplayObjectLease& lease,Witness& w) {
    if(!recipe.ready || !w.created || !w.detached || !w.copy || w.destroyed || w.material_construction_uncertain
        || !lease.ValidateObject(reinterpret_cast<void*>(w.copy)).ok() || !Cold(base,w.copy,w.outer)
        || !MaterialNativeSignatures(base)) {w.check="material_realization_preflight";return false;}
    if(!recipe.materials && !recipe.arrays[0].count && !recipe.arrays[1].count && !recipe.arrays[2].count)return true;
    // The enclosing fresh-particle owner retains its creation/render journal
    // through registration and ordered GPU retirement. Failure is a held,
    // uncertain native acquisition, never permission to free partial resources.
    w.material_construction_uncertain=true;w.check="material_realization_entered";
    MaterialNativeCalls native{base,w.copy};
    if(!recipe.Realize(base,w.copy,native) || !Cold(base,w.copy,w.outer)) {
        w.check="material_realization_hold_required";return false;
    }
    w.material_construction_uncertain=false;w.check="cold_copy_ready";return true;
}

static Status CaptureMaterials(std::uintptr_t base,void* source,std::size_t budget,
    Sc6ReplayObjectLease& lease,ReplayParticleMaterialGraph& recipe,Witness& w) {
            if(!recipe.Capture(base,reinterpret_cast<std::uintptr_t>(source))) {
                w.check=recipe.check;return Status::failure(FailureCode::CapturePreflightFailed);
            }
            std::vector<void*> references;
            const auto retain=[&](std::uintptr_t address) {
                if(!address)return true;
                auto* object=reinterpret_cast<RC::Unreal::UObject*>(address);
                if(!Sc6ReplayColdParticleOwner::Live(object))return false;
                const auto type=object->GetClassPrivate()->GetFName().ToString();
                if(type!=L"MaterialInstanceConstant" && type!=L"Material")return false;
                if(std::find(references.begin(),references.end(),object)==references.end())references.push_back(object);
                return true;
            };
            for(unsigned side=0;side<2;++side)for(int i=0;i<recipe.arrays[side].count;++i)
                if(!retain(recipe.arrays[side].slots[i].parent)) {
                    w.check="material_parent_asset";return Status::failure(FailureCode::UnsupportedContent);
                }
            for(int i=0;i<recipe.arrays[2].count;++i) {
                const auto address=recipe.arrays[2].slots[i].object;
                bool owned{};
                for(unsigned side=0;side<2;++side)for(int j=0;j<recipe.arrays[side].count;++j)
                    owned=owned || (address && recipe.arrays[side].slots[j].object==address);
                if(!owned && !retain(address)) {
                    w.check="material_override_asset";return Status::failure(FailureCode::UnsupportedContent);
                }
            }
            if(!references.empty()) {
                const auto held=lease.Acquire(base,references,budget);
                if(!held.ok())return held;
            }
    return Status::success();
}
