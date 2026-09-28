// Native 141EF9EA0 owns UWorld+1D0/+220 under +270. Entries are weak
// object pairs plus hash links (16 bytes); allocation/header layout matches
// the independently audited scheduler sets. No UObject destructor is involved.
static bool CreationRenderFunctionsMatch(std::uintptr_t base) noexcept
{
        constexpr unsigned char visibility_signature[]{0x48,0x89,0x5c,0x24,0x18,0x55,0x56,0x57,0x48,0x8d,0x6c,0x24,0xb9,0x48,0x81,0xec};
        constexpr unsigned char update_signature[]{0x40,0x55,0x57,0x48,0x8d,0x6c,0x24,0xb1,0x48,0x81,0xec,0xb8,0,0,0,0x8b};
        constexpr unsigned char dirty_signature[]{0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0x8b,0x89,0x88,1,0,0,0x8b};
        constexpr unsigned char weapon_getter[]{0x48,0x8b,0x81,0x90,3,0,0,0xc3};
        return !(std::memcmp(reinterpret_cast<void*>(base+0x1d1dc80),weapon_getter,sizeof(weapon_getter))
            || std::memcmp(reinterpret_cast<void*>(base+0x1dad440),visibility_signature,sizeof(visibility_signature))
            || std::memcmp(reinterpret_cast<void*>(base+0x1efeba0),update_signature,sizeof(update_signature))
            || std::memcmp(reinterpret_cast<void*>(base+0x1d4e910),dirty_signature,sizeof(dirty_signature)));
}

bool Sc6ReplayWorldState::PreparedRestore::FingerprintRenderSet(const RenderSet& set, std::uint64_t& hash) noexcept
{
    __try {
        const auto slots = ReadAt<int>(set.data(), 8), capacity = ReadAt<int>(set.data(), 12);
        const auto bits = ReadAt<int>(set.data(), 0x28), max_bits = ReadAt<int>(set.data(), 0x2c);
        const auto free = ReadAt<int>(set.data(), 0x34), hashes = ReadAt<int>(set.data(), 0x48);
        if (slots < 0 || capacity < slots || capacity > 65536 || bits != slots || max_bits < bits
            || max_bits > 65536 || free < 0 || free > slots || hashes < 0 || hashes > 65536
            || (hashes && (hashes & (hashes - 1)))) return false;
        hash = 14695981039346656037ull;
        const auto add = [&](const void* data, std::size_t bytes) {
            const auto* p = static_cast<const unsigned char*>(data);
            for (std::size_t i = 0; i < bytes; ++i) { hash ^= p[i]; hash *= 1099511628211ull; }
        };
        add(set.data(), set.size());
        if (slots) add(ReadAt<void*>(set.data(), 0), static_cast<std::size_t>(slots) * 16);
        auto* flags = ReadAt<const std::uint32_t*>(set.data(), 0x20);
        if (!flags && max_bits > 128) return false;
        if (!flags) flags = reinterpret_cast<const std::uint32_t*>(set.data() + 0x10);
        add(flags, ((static_cast<std::size_t>(bits) + 31) / 32) * 4);
        auto* buckets = ReadAt<const void*>(set.data(), 0x40);
        if (!buckets && hashes > 2) return false;
        if (hashes) add(buckets ? buckets : set.data() + 0x38, static_cast<std::size_t>(hashes) * 4);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void Sc6ReplayWorldState::PreparedRestore::ClearRenderWork() noexcept
{
    if (render_published_ || !base_) return;
    for (auto& set : render_owned_) {
        for (unsigned offset : {0u, 0x20u, 0x40u})
            if (auto* p = ReadAt<void*>(set.data(), offset))
                reinterpret_cast<void (*)(void*)>(base_ + 0xd46a00)(p);
        set = {};
    }
    render_ready_ = false;
    render_binding_count_ = 0;
    render_particle_admission_ = {};
    render_creation_owner_count_ = 0;
    render_executing_=render_execution_settled_=false;render_execution_budget_=0;
    render_publication_work_=PublicationWork::Unavailable;render_publication_backing_={};render_publication_fingerprint_={};
}

Status Sc6ReplayWorldState::PreparedRestore::PrepareRenderWork(const PhysicsBoundary& target,
    const PhysicsBoundary& original, std::size_t budget) noexcept
{
    if (!ready_ || published_ || render_ready_ || target.render_work_counts != std::array<std::int32_t, 2>{}
        || original.render_work_counts != std::array<std::int32_t, 2>{})
        return Status::failure(FailureCode::IllegalTransition);
    __try {
        if(!CreationRenderFunctionsMatch(base_)) return Status::failure(FailureCode::GenerationMismatch);
        if (!Writable(static_cast<std::byte*>(world_) + 0x1d0, 0xa0))
            return Status::failure(FailureCode::RestorePreflightFailed);
        if (target.creation_owner_count != original.creation_owner_count
            || !std::equal(target.creation_owners.begin(),target.creation_owners.end(),original.creation_owners.begin(),
                [](const auto& a,const auto& b){return a.SameBinding(b);}))
            return Status::failure(FailureCode::GenerationMismatch);
        render_creation_owners_ = original.creation_owners;
        render_creation_owner_count_ = original.creation_owner_count;
        for (std::size_t i = 0; i < render_creation_owner_count_; ++i) {
            const auto& entry = render_creation_owners_[i];
            if (render_binding_count_ >= render_bindings_.size()-1 || (entry.flags & 0xc0000060u))
                return Status::failure(FailureCode::UnsupportedContent);
            if (!Writable(reinterpret_cast<void*>(entry.component + 0x188), 4))
                return Status::failure(FailureCode::RestorePreflightFailed);
            render_bindings_[render_binding_count_++] = {entry.component, entry.weak, entry.flags};
        }
        for (unsigned scene = 0; scene < original.scenes.size(); ++scene)
            for (unsigned i = 0; i < original.observed_actors[scene]; ++i) {
                const auto& actor = original.actors[scene][i];
                if (!actor.component) continue;
                bool seen{};
                for (std::size_t n = 0; n < render_binding_count_; ++n) seen |= render_bindings_[n].component == actor.component;
                if (seen) continue;
                if (render_binding_count_ >= render_bindings_.size()-1 || (actor.component_flags & 0xc0000060u))
                    return Status::failure(FailureCode::UnsupportedContent);
                if (!Writable(reinterpret_cast<void*>(actor.component + 0x188), 4))
                    return Status::failure(FailureCode::RestorePreflightFailed);
                render_bindings_[render_binding_count_++] = {actor.component, actor.component_weak, actor.component_flags};
            }
        for(const auto& actor:previous_stage_)for(const auto& c:actor.components)if(c.object) {
            bool seen{};for(std::size_t i=0;i<render_binding_count_;++i)seen|=render_bindings_[i].component==c.object;
            if(seen)continue;
            if(render_binding_count_>=render_bindings_.size()-1 || (c.flags&0xc0000060u)
                || !Writable(reinterpret_cast<void*>(c.object+0x188),4))return Status::failure(FailureCode::RestorePreflightFailed);
            render_bindings_[render_binding_count_++]={c.object,c.weak,c.flags};
        }
        // Native mode1 visits descendants. Every direct child must be another
        // retained stage member; checking every member also bounds the closure.
        for(const auto& actor:previous_stage_)for(const auto& c:actor.components)if(c.object)
            for(int child=0;child<c.child_count;++child)if(c.child_objects[child]) {
                bool found{};
                for(const auto& a:previous_stage_)for(const auto& x:a.components)found|=x.object==c.child_objects[child];
                if(!found)return Status::failure(FailureCode::UnsupportedContent);
            }
        auto* lock = reinterpret_cast<CRITICAL_SECTION*>(static_cast<std::byte*>(world_) + 0x270);
        EnterCriticalSection(lock);
        __try { std::memcpy(render_previous_.data(), static_cast<std::byte*>(world_) + 0x1d0, 0xa0); }
        __finally { LeaveCriticalSection(lock); }
        std::size_t charged{};
        std::array<void*, 6> retained_allocations{};
        unsigned retained_count{};
        const auto quantize = reinterpret_cast<std::size_t (*)(std::size_t, unsigned)>(base_ + 0xd50dc0);
        for (unsigned i = 0; i < 2; ++i) {
            const auto& prior = render_previous_[i];
            if (!FingerprintRenderSet(prior, render_previous_fingerprint_[i])
                || ReadAt<int>(prior.data(), 8) != ReadAt<int>(prior.data(), 0x34))
                return Status::failure(FailureCode::GenerationMismatch);
            for (unsigned offset : {0u, 0x20u, 0x40u}) if (ReadAt<void*>(prior.data(), offset)) {
                auto* pointer = ReadAt<void*>(prior.data(), offset);
                for (unsigned n = 0; n < retained_count; ++n)
                    if (retained_allocations[n] == pointer) return Status::failure(FailureCode::GenerationMismatch);
                retained_allocations[retained_count++] = pointer;
                const auto requested = offset == 0 ? static_cast<std::size_t>(ReadAt<int>(prior.data(), 12)) * 16
                    : offset == 0x20 ? ((static_cast<std::size_t>(ReadAt<int>(prior.data(), 0x2c)) + 31) / 32) * 4
                    : static_cast<std::size_t>(ReadAt<int>(prior.data(), 0x48)) * 4;
                charged += quantize(requested, 0);
            }
            charged += quantize(RenderCapacity * 16, 0) + quantize(RenderCapacity * 4, 0) + quantize(RenderCapacity/8,0);
        }
        if (charged > budget) return Status::failure(FailureCode::CapacityExceeded);
        for (auto& set : render_owned_) {
            auto* slots = reinterpret_cast<void* (*)(std::size_t)>(base_ + 0x4a61c0)(RenderCapacity * 16);
            std::memcpy(set.data(), &slots, 8);
            auto* hash = reinterpret_cast<void* (*)(std::size_t)>(base_ + 0x4a61c0)(RenderCapacity * 4);
            std::memcpy(set.data() + 0x40, &hash, 8);
            auto* flags=reinterpret_cast<void* (*)(std::size_t)>(base_+0x4a61c0)(RenderCapacity/8);
            std::memcpy(set.data()+0x20,&flags,8);
            if (!slots || !hash || !flags) { ClearRenderWork(); return Status::failure(FailureCode::CapacityExceeded); }
            std::memset(slots, 0, RenderCapacity * 16);
            std::memset(hash, 0xff, RenderCapacity * 4);
            std::memset(flags,0,RenderCapacity/8);
            const int capacity = RenderCapacity, invalid = -1;
            for (unsigned offset : {12u, 0x2cu, 0x48u}) std::memcpy(set.data() + offset, &capacity, 4);
            std::memcpy(set.data() + 0x30, &invalid, 4);
        }
        owned_bytes_ += charged;
        render_ready_ = true;
        return Status::success();
    } __except (EXCEPTION_EXECUTE_HANDLER) { return Status::failure(FailureCode::ContextUnavailable); }
}

Status Sc6ReplayWorldState::PreparedRestore::PublishRenderWork() noexcept
{
    if (!render_ready_ || render_published_ || !ValidateBinding().ok()) return Status::failure(FailureCode::IllegalTransition);
    __try {
        auto* live = static_cast<std::byte*>(world_) + 0x1d0;
        auto* lock = reinterpret_cast<CRITICAL_SECTION*>(static_cast<std::byte*>(world_) + 0x270);
        EnterCriticalSection(lock);
        __try {
            if (std::memcmp(live, render_previous_.data(), 0xa0)) return Status::failure(FailureCode::GenerationMismatch);
            for (unsigned i = 0; i < 2; ++i) {
                std::uint64_t hash{};
                if (!FingerprintRenderSet(render_previous_[i], hash) || hash != render_previous_fingerprint_[i])
                    return Status::failure(FailureCode::GenerationMismatch);
            }
            render_published_ = true;
            std::memcpy(live, render_owned_.data(), 0xa0);
            render_write_complete_ = true;
        } __finally { LeaveCriticalSection(lock); }
        return Status::success();
    } __except (EXCEPTION_EXECUTE_HANDLER) { return Status::failure(FailureCode::RestoreWriteFailed); }
}

Status Sc6ReplayWorldState::PreparedRestore::PublishCreationVisibility(const PhysicsBoundary& image,bool recovery) noexcept
{
    if(!render_published_ || !render_write_complete_ || render_undo_started_
        || image.creation_owner_count!=render_creation_owner_count_ || !ValidateBinding().ok())
        return Status::failure(FailureCode::IllegalTransition);
    __try {
        if(!CreationRenderFunctionsMatch(base_)) return Status::failure(FailureCode::GenerationMismatch);
        for(std::size_t i=0;i<render_creation_owner_count_;++i) {
            const auto& row=image.creation_owners[i];
            const auto* p=reinterpret_cast<void*>(row.component);
            if(!row.SameBinding(render_creation_owners_[i])
                || reinterpret_cast<std::uintptr_t>(reinterpret_cast<void*(*)(const void*)>(base_+0xf823f0)(row.weak.data()))!=row.component
                || ReadAt<std::uintptr_t>(p,0)!=base_+0x38829c0
                || ReadAt<std::uintptr_t>(p,0x910)!=row.mesh || ReadAt<std::uintptr_t>(p,0xab0)!=row.animation
                || (ReadAt<unsigned>(p,0x240)&~0x10u)!=(row.visibility&~0x10u)
                || !Writable(reinterpret_cast<void*>(row.component+0x240),4))
                return Status::failure(FailureCode::GenerationMismatch);
        }
        auto status=ValidateRenderWork();if(!status.ok()) return status;
        for(std::size_t i=0;i<render_creation_owner_count_;++i) {
            const auto& row=image.creation_owners[i];auto* p=reinterpret_cast<void*>(row.component);
            reinterpret_cast<void(*)(void*,unsigned char,unsigned char)>(base_+0x1dad440)(p,(row.visibility>>4)&1,0);
            // C may have destroyed the hidden variant's MeshObject. Restoring
            // the scalar alone cannot recover native B rendering. A render
            // recreate uses native mesh ownership; it never revives old pointers.
            if(recovery && render_executing_ && !(ReadAt<unsigned>(p,0x3fc)&2)
                && bool(row.visibility&0x10)!=bool(ReadAt<std::uintptr_t>(p,0xa18)))
                reinterpret_cast<void(*)(void*)>(base_+0x1d4e910)(p);
            if(ReadAt<unsigned>(p,0x240)!=row.visibility) return Status::failure(FailureCode::RestoreWriteFailed);
        }
        status=PublishStageVisibility(recovery);if(!status.ok())return status;
        status=ValidateRenderWork();if(!status.ok()) return status;
        if(recovery && render_executing_) {
            // Reuse native's already allocated, idle weak scratch array. Do
            // not admit an unaccounted retained allocation during recovery.
            int pending_components{};
            for(unsigned set=0;set<2;++set) {
                const auto* h=static_cast<std::byte*>(world_)+0x1d0+set*0x50;
                pending_components+=ReadAt<int>(h,8)-ReadAt<int>(h,0x34);
            }
            if(ReadAt<int>(reinterpret_cast<void*>(base_+0x439ba00),8)!=0
                || ReadAt<int>(reinterpret_cast<void*>(base_+0x439ba00),12)<pending_components)
                return Status::failure(FailureCode::CapacityExceeded);
            // 141EFEBA0 drains only owned end-frame component updates and joins
            // its parallel jobs; 141EE5FB0 clears both weak sets. It does not
            // execute a world/gameplay tick. Queued render commands complete at
            // the host's following admitted Finish command before recovery exits.
            reinterpret_cast<void(*)(void*)>(base_+0x1efeba0)(world_);
            // End-frame jobs have joined. Restore the exact admission value
            // retained before native CreateRenderState, not an expected image.
            // Leave witnesses retained on any failure; no simulation may resume.
            for(std::size_t i=0;i<render_binding_count_;++i)if(render_particle_admission_[i].retained) {
                const auto p=render_bindings_[i].component;
                const auto flags=ReadAt<unsigned>(reinterpret_cast<void*>(p),0x830);
                if(!render_particle_admission_[i].Accepts(flags)) {
                    render_diagnostic_={"stage_particle_recreation_side_effect",p,flags,render_particle_admission_[i].original|0x200u};
                    return Status::failure(FailureCode::UndoFailed);
                }
            }
            for(std::size_t i=0;i<render_binding_count_;++i)if(render_particle_admission_[i].retained) {
                auto* flags=reinterpret_cast<unsigned*>(render_bindings_[i].component+0x830);
                *flags=render_particle_admission_[i].original;
                if(*flags!=render_particle_admission_[i].original)return Status::failure(FailureCode::UndoFailed);
                render_particle_admission_[i]={};
            }
            status=ValidateRenderWork();if(!status.ok()) return status;
            for(unsigned i=0;i<2;++i) {
                const auto* h=static_cast<std::byte*>(world_)+0x1d0+i*0x50;
                if(ReadAt<int>(h,8)!=ReadAt<int>(h,0x34)) return Status::failure(FailureCode::UndoFailed);
            }
        }
        return ValidateBinding();
    } __except(EXCEPTION_EXECUTE_HANDLER) {return Status::failure(FailureCode::RestoreWriteFailed);}
}

Status Sc6ReplayWorldState::PreparedRestore::PublishStageVisibility(bool recovery) noexcept
{
    render_diagnostic_ = {};
    const auto reject = [&](const char* check, std::uintptr_t component, std::uint64_t observed, std::uint64_t expected) {
        render_diagnostic_ = {check, component, observed, expected};
        return Status::failure(FailureCode::UnsupportedContent);
    };
    const auto& image=recovery?previous_stage_:stage_;
    auto status=ValidateStage(base_,image,true);if(!status.ok())return status;
    __try {
        constexpr unsigned char name_signature[]{0x40,0x53,0x48,0x83,0xec,0x30,0x48,0x8b,0xd9,0x48,0x85,0xd2};
        constexpr unsigned char alpha_signature[]{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x54,0x24,0x10,0x57};
        if(std::memcmp(reinterpret_cast<void*>(base_+0xdf5d00),name_signature,sizeof(name_signature))
            || std::memcmp(reinterpret_cast<void*>(base_+0x55cf00),alpha_signature,sizeof(alpha_signature))
            || std::memcmp(reinterpret_cast<void*>(base_+0x55cf90),alpha_signature,sizeof(alpha_signature)))
            return Status::failure(FailureCode::GenerationMismatch);
        std::array<bool,RenderCapacity> dirty{};
        const auto index_of=[&](std::uintptr_t p){std::size_t i{};while(i<render_binding_count_ && render_bindings_[i].component!=p)++i;return i;};
        for(const auto& row:image)for(const auto& c:row.components)if(c.object) {
            const auto index=index_of(c.object);if(index==render_binding_count_)return Status::failure(FailureCode::GenerationMismatch);
            const auto* p=reinterpret_cast<void*>(c.object);
            dirty[index]=bool((ReadAt<unsigned>(p,0x240)^c.visibility)&0x10)
                || ((c.visibility&0x10) && !ReadAt<std::uintptr_t>(p,0x790));
            if(!Writable(reinterpret_cast<void*>(c.object+0x240),4))return Status::failure(FailureCode::RestorePreflightFailed);
        }
        // Mode1's descendant invalidation, expressed as a bounded retained
        // closure. Set each saved visibility separately, never copy a proxy.
        for(std::size_t pass=0;pass<render_binding_count_;++pass) {
            bool changed{};
            for(const auto& row:image)for(const auto& c:row.components)if(c.object && dirty[index_of(c.object)])
                for(int n=0;n<c.child_count;++n)if(c.child_objects[n]) {
                    const auto index=index_of(c.child_objects[n]);if(index==render_binding_count_)return Status::failure(FailureCode::UnsupportedContent);
                    if(!dirty[index]){dirty[index]=true;changed=true;}
                }
            if(!changed)break;
        }
        for(const auto& row:image)for(const auto& c:row.components)if(c.object && dirty[index_of(c.object)]) {
            const auto* p=reinterpret_cast<void*>(c.object);
            if(ReadAt<std::uintptr_t>(reinterpret_cast<void*>(c.type),0x418)!=base_+0x1da6a50)
                return reject("stage_visibility_callback",c.object,ReadAt<std::uintptr_t>(reinterpret_cast<void*>(c.type),0x418),base_+0x1da6a50);
            if(row.kind==ReplayStageVisibility::Kind::Emitter) {
                // 141F711A0/141F71FD0 must not complete simulation work,
                // clear a template, disable ticks, or finalize an emitter.
                const auto fence=ReadAt<std::uintptr_t>(p,0xa70);
                if(c.type!=base_+0x335db28 || !c.asset)
                    return reject("stage_particle_type_asset",c.object,c.type,base_+0x335db28);
                if(ReadAt<unsigned char>(p,0xa81))return reject("stage_particle_async_busy",c.object,ReadAt<unsigned char>(p,0xa81),0);
                if(fence && !(ReadAt<std::uint64_t>(reinterpret_cast<void*>(fence),8)&(1ull<<26)))
                    return reject("stage_particle_fence",c.object,ReadAt<std::uint64_t>(reinterpret_cast<void*>(fence),8),1ull<<26);
                if(!ReadAt<unsigned char>(reinterpret_cast<void*>(base_+0x40956b8),0))
                    return reject("stage_particle_template_enabled",c.object,0,1);
                if(ReadAt<unsigned char>(reinterpret_cast<void*>(c.asset),0xbc)&2)
                    return reject("stage_particle_template_tick_write",c.object,ReadAt<unsigned char>(reinterpret_cast<void*>(c.asset),0xbc),0);
                if((ReadAt<unsigned>(p,0x830)&0x80)
                    || (!(recovery && render_executing_) && !(ReadAt<unsigned>(p,0x830)&0x200)))
                    return reject("stage_particle_finalize_or_create",c.object,ReadAt<unsigned>(p,0x830),0x200);
                if(ReadAt<std::uintptr_t>(reinterpret_cast<void*>(c.type),0x298)!=base_+0x1f711a0)
                    return reject("stage_particle_create_callback",c.object,ReadAt<std::uintptr_t>(reinterpret_cast<void*>(c.type),0x298),base_+0x1f711a0);
                if(ReadAt<std::uintptr_t>(reinterpret_cast<void*>(c.type),0x2b0)!=base_+0x1f71fd0)
                    return reject("stage_particle_destroy_callback",c.object,ReadAt<std::uintptr_t>(reinterpret_cast<void*>(c.type),0x2b0),base_+0x1f71fd0);
                if(recovery && render_executing_
                    && !render_particle_admission_[index_of(c.object)].Retain(ReadAt<unsigned>(p,0x830)))
                    return reject("stage_particle_recreation_retry",c.object,ReadAt<unsigned>(p,0x830),0);
            }
        }
        std::uint64_t alpha_name{};
        reinterpret_cast<void*(*)(void*,const wchar_t*,unsigned)>(base_+0xdf5d00)(&alpha_name,L"Alpha",0);
        if(!alpha_name && !image.empty())return Status::failure(FailureCode::GenerationMismatch);
        for(const auto& row:image) {
            for(unsigned i=0;i<row.component_count;++i) {
                const auto& c=row.components[i];if(!c.object)continue;
                auto* p=reinterpret_cast<void*>(c.object);
                if(c.material_count)reinterpret_cast<void(*)(void*,std::uint64_t,float)>(
                    base_+(row.kind==ReplayStageVisibility::Kind::Wall && i==5?0x55cf90:0x55cf00))(p,alpha_name,row.values.alpha);
                reinterpret_cast<void(*)(void*,unsigned char,unsigned char)>(base_+0x1dad440)(p,(c.visibility>>4)&1,0);
                if(dirty[index_of(c.object)])reinterpret_cast<void(*)(void*)>(base_+0x1d4e910)(p);
                if(ReadAt<unsigned>(p,0x240)!=c.visibility)return Status::failure(FailureCode::RestoreWriteFailed);
            }
        }
        return ValidateStage(base_,image,true);
    } __except(EXCEPTION_EXECUTE_HANDLER) {return Status::failure(FailureCode::RestoreWriteFailed);}
}

Status Sc6ReplayWorldState::PreparedRestore::ValidateRenderWork() const noexcept
{
    if(!render_published_ || !render_write_complete_ || render_undo_started_
        || (render_executing_ && !render_execution_settled_)) {
        render_diagnostic_={"world_binding_or_write_phase",0,render_published_,1};
        return Status::failure(FailureCode::GenerationMismatch);
    }
    return ValidateRenderWorkStorage(epoch_,render_owned_,render_execution_settled_);
}

Status Sc6ReplayWorldState::PreparedRestore::ValidateRenderWorkStorage(std::uint64_t epoch,
    const std::array<RenderSet,2>& owned,bool native_capacity) const noexcept
{
    render_diagnostic_ = {};
    const auto reject = [&](const char* check, std::uintptr_t component, std::uint64_t observed, std::uint64_t expected) {
        render_diagnostic_ = {check, component, observed, expected};
        return Status::failure(FailureCode::GenerationMismatch);
    };
    if(!ValidateBinding(epoch).ok()) return reject("world_binding",0,epoch,0);
    __try {
        auto* lock = reinterpret_cast<CRITICAL_SECTION*>(static_cast<std::byte*>(world_) + 0x270);
        EnterCriticalSection(lock);
        __try {
            std::array<unsigned, RenderCapacity> membership{};
            for (std::size_t i = 0; i < render_creation_owner_count_; ++i) {
                const auto& entry = render_creation_owners_[i];
                if (reinterpret_cast<std::uintptr_t>(reinterpret_cast<void* (*)(const void*)>(base_ + 0xf823f0)(entry.owner_weak.data())) != entry.owner)
                    return reject("creation_owner_lifetime", entry.component, 0, entry.owner);
                const auto* owner = reinterpret_cast<void*>(entry.owner);
                if (!entry.LiveMembership([](std::uintptr_t p,auto& value){std::memcpy(&value,reinterpret_cast<void*>(p),sizeof(value));return true;})
                    || ReadAt<std::uintptr_t>(reinterpret_cast<void*>(entry.component), 0x190) != entry.owner
                    || ReadAt<std::uintptr_t>(reinterpret_cast<void*>(entry.component), 0x1d0) != entry.parent
                    || ReadAt<unsigned>(reinterpret_cast<void*>(entry.component),0x420)!=entry.primitive_id
                    || ReadAt<std::uintptr_t>(reinterpret_cast<void*>(entry.component),0x910)!=entry.mesh
                    || ReadAt<std::uintptr_t>(reinterpret_cast<void*>(entry.component),0xab0)!=entry.animation
                    || (ReadAt<unsigned>(reinterpret_cast<void*>(entry.component),0x240)&~0x10u)!=(entry.visibility&~0x10u))
                    return reject("creation_membership", entry.component, 0, entry.owner);
            }
            for (unsigned set = 0; set < 2; ++set) {
                const auto* h = static_cast<std::byte*>(world_) + 0x1d0 + set * 0x50;
                for (unsigned offset : {0u, 0x20u, 0x40u})
                    if (ReadAt<std::uintptr_t>(h, offset) != ReadAt<std::uintptr_t>(owned[set].data(), offset))
                        return reject(offset == 0 ? "slots_binding" : offset == 0x20 ? "bits_binding" : "hash_binding", set,
                            ReadAt<std::uintptr_t>(h, offset), ReadAt<std::uintptr_t>(owned[set].data(), offset));
                for (unsigned offset : {12u, 0x2cu, 0x48u}) {
                    const auto expected=native_capacity?ReadAt<int>(owned[set].data(),offset):RenderCapacity;
                    if (ReadAt<int>(h, offset) != expected)
                        return reject(offset == 12 ? "slot_capacity" : offset == 0x2c ? "bit_capacity" : "hash_size", set, ReadAt<unsigned>(h, offset), expected);
                }
                const auto slots = ReadAt<int>(h, 8);
                if (slots < 0 || slots > ReadAt<int>(h,12) || slots>65536 || ReadAt<int>(h, 0x28) != slots) return reject("slot_bit_count", set, slots, ReadAt<unsigned>(h, 0x28));
                const auto* flags = ReadAt<const unsigned*>(h,0x20);
                if(!flags) {if(slots>128) return reject("inline_flags",set,slots,128);flags=reinterpret_cast<const unsigned*>(h + 0x10);}
                unsigned occupied{};
                for (int slot = 0; slot < slots; ++slot) if ((flags[slot / 32] >> (slot % 32)) & 1) {
                    ++occupied;
                    const auto* weak = ReadAt<const std::byte*>(h, 0) + slot * 16;
                    const auto object = reinterpret_cast<std::uintptr_t>(reinterpret_cast<void* (*)(const void*)>(base_ + 0xf823f0)(weak));
                    std::size_t index{};
                    while (index < render_binding_count_ && render_bindings_[index].component != object) ++index;
                    if (index == render_binding_count_) return reject("unretained_component", object, ReadAt<std::uint64_t>(weak, 0), set);
                    if (std::memcmp(weak, render_bindings_[index].weak.data(), 8) || membership[index])
                        return reject("weak_or_duplicate", object, ReadAt<std::uint64_t>(weak, 0), ReadAt<std::uint64_t>(render_bindings_[index].weak.data(), 0));
                    membership[index] = set + 1;
                }
                if (occupied != static_cast<unsigned>(slots - ReadAt<int>(h, 0x34))) return reject("occupancy", set, occupied, slots - ReadAt<int>(h, 0x34));
            }
            for (std::size_t i = 0; i < render_binding_count_; ++i) {
                const auto& binding = render_bindings_[i];
                if (reinterpret_cast<std::uintptr_t>(reinterpret_cast<void* (*)(const void*)>(base_ + 0xf823f0)(binding.weak.data())) != binding.component)
                    return reject("component_lifetime", binding.component, 0, 1);
                const auto flags = ReadAt<unsigned>(reinterpret_cast<void*>(binding.component), 0x188);
                const auto logical=StageTickFlagMask(binding.component);
                if ((flags & ~(0xc0000060u|logical)) != (binding.flags&~logical) || (flags >> 30) != membership[i]
                    || (bool(flags & 0x60) != bool(membership[i])))
                    return reject("component_flags", binding.component, flags, binding.flags | (membership[i] << 30));
            }
            return Status::success();
        } __finally { LeaveCriticalSection(lock); }
    } __except (EXCEPTION_EXECUTE_HANDLER) { return Status::failure(FailureCode::ContextUnavailable); }
}

Status Sc6ReplayWorldState::PreparedRestore::UndoRenderWork() noexcept
{
    if (!render_published_) return Status::success();
    if(render_executing_ && !render_execution_settled_) return Status::failure(FailureCode::IllegalTransition);
    // A failed memcpy can leave mixed headers. Never traverse those headers
    // to recover B. A completed first publication still requires the full A
    // membership check; retries use the retained destinations and B backing.
    auto status = render_write_complete_ && !render_undo_started_
        ? ValidateRenderWork() : ValidateBinding();
    if (!status.ok()) return status;
    __try {
        auto* lock = reinterpret_cast<CRITICAL_SECTION*>(static_cast<std::byte*>(world_) + 0x270);
        EnterCriticalSection(lock);
        __try {
            for (std::size_t i = 0; i < render_creation_owner_count_; ++i) {
                const auto& entry = render_creation_owners_[i];
                if (reinterpret_cast<std::uintptr_t>(reinterpret_cast<void* (*)(const void*)>(base_ + 0xf823f0)(entry.owner_weak.data())) != entry.owner
                    || !entry.LiveMembership([](std::uintptr_t p,auto& value){std::memcpy(&value,reinterpret_cast<void*>(p),sizeof(value));return true;})
                    || ReadAt<std::uintptr_t>(reinterpret_cast<void*>(entry.component), 0x190) != entry.owner
                    || ReadAt<std::uintptr_t>(reinterpret_cast<void*>(entry.component), 0x1d0) != entry.parent)
                    return Status::failure(FailureCode::GenerationMismatch);
            }
            for (unsigned i = 0; i < 2; ++i) {
                std::uint64_t hash{};
                if (!FingerprintRenderSet(render_previous_[i], hash) || hash != render_previous_fingerprint_[i])
                    return Status::failure(FailureCode::GenerationMismatch);
            }
            for (std::size_t i = 0; i < render_binding_count_; ++i) {
                const auto& binding = render_bindings_[i];
                if (reinterpret_cast<std::uintptr_t>(reinterpret_cast<void* (*)(const void*)>(base_ + 0xf823f0)(binding.weak.data())) != binding.component
                    || !Writable(reinterpret_cast<void*>(binding.component + 0x188), 4)
                    || (ReadAt<unsigned>(reinterpret_cast<void*>(binding.component), 0x188) & ~0xc0000060u) != binding.flags)
                    return Status::failure(FailureCode::GenerationMismatch);
            }
            if (!Writable(static_cast<std::byte*>(world_) + 0x1d0, 0xa0))
                return Status::failure(FailureCode::RestorePreflightFailed);
            render_undo_started_ = true;
            for (std::size_t i = 0; i < render_binding_count_; ++i) {
                const auto& binding = render_bindings_[i];
                std::memcpy(reinterpret_cast<void*>(binding.component + 0x188), &binding.flags, 4);
            }
            std::memcpy(static_cast<std::byte*>(world_) + 0x1d0, render_previous_.data(), 0xa0);
            if (std::memcmp(static_cast<std::byte*>(world_) + 0x1d0, render_previous_.data(), 0xa0))
                return Status::failure(FailureCode::UndoFailed);
            for (unsigned i = 0; i < 2; ++i) {
                std::uint64_t hash{};
                if (!FingerprintRenderSet(render_previous_[i], hash) || hash != render_previous_fingerprint_[i])
                    return Status::failure(FailureCode::UndoFailed);
            }
            for (std::size_t i = 0; i < render_binding_count_; ++i)
                if (ReadAt<unsigned>(reinterpret_cast<void*>(render_bindings_[i].component), 0x188) != render_bindings_[i].flags)
                    return Status::failure(FailureCode::UndoFailed);
            render_published_ = false;
            render_write_complete_ = render_undo_started_ = false;
            return Status::success();
        } __finally { LeaveCriticalSection(lock); }
    } __except (EXCEPTION_EXECUTE_HANDLER) { return Status::failure(FailureCode::RestoreWriteFailed); }
}

Status Sc6ReplayWorldState::PreparedRestore::CommitRenderWork() noexcept
{
    if (!render_ready_) return Status::success();
    const auto status = ValidateRenderWork();
    if (!status.ok()) return status;
    for (unsigned i = 0; i < 2; ++i) {
        std::uint64_t hash{};
        if (!FingerprintRenderSet(render_previous_[i], hash) || hash != render_previous_fingerprint_[i])
            return Status::failure(FailureCode::GenerationMismatch);
    }
    render_owned_ = render_previous_; // Native world keeps the private pending queues.
    render_published_ = false;
    render_write_complete_ = render_undo_started_ = false;
    ClearRenderWork();
    return Status::success();
}

#include "Sc6ReplayWorldState.RenderHandoff.inl"

Status Sc6ReplayWorldState::PreparedRestore::SettleRenderWorkExecution(const PhysicsBoundary& observed) noexcept
{
    render_diagnostic_={};
    const auto binding=render_executing_ && !render_execution_settled_ && render_published_ && !render_undo_started_
        ? ValidateBinding() : Status::failure(FailureCode::IllegalTransition);
    if(!render_executing_ || render_execution_settled_ || !render_published_ || render_undo_started_
        || observed.render_work_counts!=std::array<std::int32_t,2>{} || !binding.ok()) {
        render_diagnostic_={"settlement_admission",0,
            (std::uint64_t(static_cast<std::uint32_t>(observed.render_work_counts[0]))<<32)
                |static_cast<std::uint32_t>(observed.render_work_counts[1]),
            std::uint64_t(render_executing_) | (std::uint64_t(render_execution_settled_)<<1)
                | (std::uint64_t(render_published_)<<2) | (std::uint64_t(render_undo_started_)<<3)
                | (std::uint64_t(binding.code)<<8)};
        return Status::failure(FailureCode::IllegalTransition);
    }
    // Timer/world settlement must already have bound the actual physical C
    // epoch. The host owns completed application/task/render admission.
    __try {
        std::array<RenderSet,2> live{};
        auto* lock=reinterpret_cast<CRITICAL_SECTION*>(static_cast<std::byte*>(world_)+0x270);
        if(!TryEnterCriticalSection(lock)) return Status::failure(FailureCode::RestorePreflightFailed);
        __try {
            std::memcpy(live.data(),static_cast<std::byte*>(world_)+0x1d0,0xa0);
            struct Range {std::uintptr_t address;std::size_t bytes;};
            std::array<Range,6> current{},previous{};std::size_t charge{};
            const auto overlap=[](Range a,Range b) {
                if(!a.bytes || !b.bytes) return false;
                if(!a.address || !b.address || a.bytes>UINTPTR_MAX-a.address || b.bytes>UINTPTR_MAX-b.address) return true;
                return a.address<b.address+b.bytes && b.address<a.address+a.bytes;
            };
            for(unsigned set=0;set<2;++set) {
                std::uint64_t hash{};
                if(!FingerprintRenderSet(render_previous_[set],hash) || hash!=render_previous_fingerprint_[set]
                    || !FingerprintRenderSet(live[set],hash)
                    || ReadAt<int>(live[set].data(),8)!=ReadAt<int>(live[set].data(),0x34))
                    return Status::failure(FailureCode::GenerationMismatch);
                const auto* flags=ReadAt<const unsigned*>(live[set].data(),0x20);
                if(!flags) flags=reinterpret_cast<const unsigned*>(live[set].data()+0x10);
                for(int bit=0;bit<ReadAt<int>(live[set].data(),0x28);++bit)
                    if(flags[bit/32]&(1u<<(bit%32))) return Status::failure(FailureCode::GenerationMismatch);
                for(unsigned i=0;i<3;++i) {
                    const auto offset=i==0?0:i==1?0x20:0x40;
                    const auto range=[&](const RenderSet& h) -> Range {
                        const auto pointer=ReadAt<std::uintptr_t>(h.data(),offset);
                        return {pointer,pointer?(i==0?std::size_t(ReadAt<int>(h.data(),12))*16:
                            i==1?((std::size_t(ReadAt<int>(h.data(),0x2c))+31)/32)*4:std::size_t(ReadAt<int>(h.data(),0x48))*4):0};
                    };
                    current[set*3+i]=range(live[set]);previous[set*3+i]=range(render_previous_[set]);
                    const auto bytes=current[set*3+i].bytes;
                    const auto actual=bytes?reinterpret_cast<std::size_t(*)(std::size_t,unsigned)>(base_+0xd50dc0)(bytes,0):0;
                    if(actual<bytes || charge>render_execution_budget_ || actual>render_execution_budget_-charge)
                        return Status::failure(FailureCode::CapacityExceeded);
                    charge+=actual;
                }
            }
            for(std::size_t i=0;i<current.size();++i) {
                if(overlap(current[i],{reinterpret_cast<std::uintptr_t>(world_)+0x1d0,0xa0}))
                    return Status::failure(FailureCode::GenerationMismatch);
                for(std::size_t j=0;j<current.size();++j)
                    if(overlap(current[i],previous[j]) || (i!=j && overlap(current[i],current[j])))
                        return Status::failure(FailureCode::GenerationMismatch);
            }
            render_owned_=live;render_execution_settled_=true;
        } __finally {LeaveCriticalSection(lock);}
        return ValidateRenderWork();
    } __except(EXCEPTION_EXECUTE_HANDLER) {return Status::failure(FailureCode::ContextUnavailable);}
}

Status Sc6ReplayWorldState::PreparedRestore::ReopenRenderWorkForUndo() noexcept
{
    if(!render_executing_ || !render_published_ || render_undo_started_)
        return Status::failure(FailureCode::IllegalTransition);
    if(!render_execution_settled_) return Status::success();
    auto status=ValidateRenderWork();if(!status.ok()) return status;
    for(unsigned i=0;i<2;++i) {
        std::uint64_t hash{};
        if(!FingerprintRenderSet(render_previous_[i],hash) || hash!=render_previous_fingerprint_[i])
            return Status::failure(FailureCode::RestoreVerificationFailed);
    }
    render_owned_={};render_execution_settled_=false;
    return Status::success();
}

void Sc6ReplayWorldState::PreparedRestore::InvokeNativePublicationRenderTail() const noexcept
{
    reinterpret_cast<void(*)(void*)>(base_+0x1efeba0)(world_);
}
#include "Sc6ReplayWorldState.PublicationTail.inl"
