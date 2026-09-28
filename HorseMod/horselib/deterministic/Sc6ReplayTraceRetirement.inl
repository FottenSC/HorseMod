// Included inside Sc6ReplayTraceState. This extends the existing participant;
// no historical object header or reference count is installed for C children.
private:
    static bool StorageOverlaps(const void* a,std::size_t n,const void* b,std::size_t m) {
        const auto x=reinterpret_cast<std::uintptr_t>(a),y=reinterpret_cast<std::uintptr_t>(b);
        return n && m && (n>UINTPTR_MAX-x || m>UINTPTR_MAX-y || (x<y+m && y<x+n));
    }
    bool Contains(const Ref& ref) const {
        return std::any_of(states_.begin(),states_.end(),[&](const auto& s) {
            return s.ref.state==ref.state && s.ref.controller==ref.controller;
        });
    }
    bool OwnsNativeChild(const Ref& ref) const {
        if(!Contains(ref) || !ref.controller || ref.state!=ref.controller+16
            || At<std::uintptr_t>(ref.controller,0)!=base_+0x3362590)return false;
        int matches{};
        for(const auto& root:roots_) {
            if(!Live(root.component))return false;
            const auto array=At<Array>(root.component.object,0x428);
            if(array.count<0 || array.count>256 || array.capacity<array.count || (array.count&&!array.data))return false;
            for(int i=0;i<array.count;++i) {
                const auto row=At<Ref>(array.data,std::size_t(i)*16);
                if(row.state==ref.state || row.controller==ref.controller) {
                    if(row.state!=ref.state || row.controller!=ref.controller)return false;
                    ++matches;
                }
            }
        }
        return matches>0 && At<int>(ref.controller,8)==matches;
    }
    static bool EmptyEvent(Object* object,const wchar_t* name,const wchar_t* expected,unsigned flags=0x08020800) {
        auto* event=object->GetFunctionByNameInChain(name);
        return event && event->GetFullName()==expected && event->GetFunctionFlags()==flags
            && event->GetScript().Num()==0;
    }
    template<class Accept>
    static bool VisitOwnedComponents(Object* actor,Accept accept) {
        const auto array=At<Array>(actor,0x2c0);
        const auto nbits=At<int>(actor,0x2e8),free=At<int>(actor,0x2f4);
        auto* bits=At<unsigned*>(actor,0x2e0);
        if(array.count<0 || array.count>4096 || array.capacity<array.count || nbits!=array.count
            || free<0 || free>array.count || (!bits && nbits>128) || (array.count&&!array.data))return false;
        if(!bits)bits=reinterpret_cast<unsigned*>(reinterpret_cast<std::byte*>(actor)+0x2d0);
        int live{};
        for(int i=0;i<array.count;++i)if(bits[i/32]&(1u<<(i%32))) {
            ++live;if(!accept(At<Object*>(array.data,std::size_t(i)*16)))return false;
        }
        return live==array.count-free;
    }
    Status CheckRetirementWorld() const {
        // 141EECDF0/1421C2940 enumerate all named drivers, not just DemoNetDriver.
        auto* engine=At<void*>(reinterpret_cast<void*>(base_),0x43b3068);
        if(!engine || At<void*>(world_,0xb8) || At<int>(world_,0x8a8)!=1
            || At<void*>(reinterpret_cast<void*>(base_),0x418b108)
            || At<unsigned char>(reinterpret_cast<void*>(base_),0x418b12f))return Fail("child_world_side_effect_domain");
        const auto contexts=At<Array>(engine,0xbe8);unsigned matches{};
        if(contexts.count<1 || contexts.count>16 || contexts.capacity<contexts.count || !contexts.data)return Fail("child_world_context");
        for(int i=0;i<contexts.count;++i) {
            auto* context=At<void*>(contexts.data,std::size_t(i)*8);
            if(context && At<void*>(context,0x298)==world_) {
                ++matches;if(At<int>(context,0x250))return Fail("child_network_drivers");
            }
        }
        if(matches!=1)return Fail("child_world_context");
        // Current replay has only the audio streaming manager's single-RET
        // removal. Other streaming implementations need a separate contract.
        auto* streaming=At<void*>(reinterpret_cast<void*>(base_),0x43931d8);
        if(!streaming || At<std::uintptr_t>(streaming,0)!=base_+0x388e168)return Fail("child_streaming_owner");
        const auto managers=At<Array>(streaming,0x10);
        if(managers.count!=1 || managers.capacity<1 || !managers.data)return Fail("child_streaming_domain");
        auto* audio=At<void*>(managers.data,0);
        if(!audio || At<std::uintptr_t>(audio,0)!=base_+0x384c1c0
            || At<std::uintptr_t>(At<void*>(audio,0),0x60)!=base_+0x2d2bc0
            || At<unsigned char>(reinterpret_cast<void*>(base_+0x2d2bc0),0)!=0xc2
            || At<unsigned short>(reinterpret_cast<void*>(base_+0x2d2bc0),1)!=0)return Fail("child_streaming_callback");
        return Status::success();
    }
    Status CheckChild(const State& state,const Sc6ReplayTraceState& a,const Sc6ReplayTraceState& b) const {
        if(a.Contains(state.ref)||b.Contains(state.ref)||!OwnsNativeChild(state.ref))return Fail("child_strong_ownership");
        return CheckChildStorageAndLifecycle(state,a,b);
    }
    Status CheckChildStorageAndLifecycle(const State& state,const Sc6ReplayTraceState& a,const Sc6ReplayTraceState& b) const {
        // Native destruction frees these exact private payload allocations.
        // A shared nested allocation would make retirement destructive to B.
        for(const auto offset:{0x10u,0x70u,0x88u}) {
            const auto array=At<Array>(state.ref.state,offset);
            if(!array.data)continue;
            const auto bytes=std::size_t(array.capacity)*(offset==0x10?0x70:0x2c);
            if(!bytes)return Fail("child_empty_allocation");
            for(const auto& other:states_) {
                if(StorageOverlaps(array.data,bytes,other.ref.controller,0x108))return Fail("child_control_alias");
                for(const auto other_offset:{0x10u,0x70u,0x88u})if(other.ref.state!=state.ref.state || other_offset!=offset) {
                    const auto other_array=At<Array>(other.ref.state,other_offset);
                    if(StorageOverlaps(array.data,bytes,other_array.data,std::size_t(other_array.capacity)*(other_offset==0x10?0x70:0x2c)))return Fail("child_payload_alias");
                }
            }
            for(const auto* image:{&a,&b})for(const auto* fields:{&image->values_,&image->bindings_})
                for(const auto& field:*fields)if(StorageOverlaps(array.data,bytes,field.address,field.bytes.size()))return Fail("child_B_payload_alias");
            for(std::size_t i=0;i<b.dynamic_count_;++i)
                if(StorageOverlaps(array.data,bytes,At<void*>(const_cast<std::byte*>(b.dynamic_[i].header.data()),0),b.dynamic_[i].bytes.size()))return Fail("child_private_B_alias");
        }
        const auto cached=At<Ref>(state.ref.state,0xa0);
        if(cached.controller && (!a.Contains(cached)||!b.Contains(cached)
            || cached.state!=cached.controller+16 || At<int>(cached.controller,8)<=0
            || At<int>(cached.controller,12)<2))return Fail("child_cached_owner");
        auto* actor=state.actor.object;auto* mesh=state.mesh.object;
        for(const auto* image:{&a,&b})for(const auto& retained:image->states_)
            for(auto* old:{retained.actor.object,retained.mesh.object,retained.animation.object,retained.attachment.object})
                if(old && (old==actor || old==mesh || old==state.animation.object || old==state.attachment.object))return Fail("child_retained_alias");
        if(At<std::uintptr_t>(actor,0)!=base_+0x3361660 || At<std::uintptr_t>(mesh,0)!=base_+0x38829c0
            || At<unsigned char>(actor,0x118)!=3 || At<void*>(actor,0x98) || At<std::uint64_t>(actor,0x188)
            || (At<unsigned char>(actor,0x148)&4) || (At<unsigned char>(actor,0x34)&0x40) || At<void*>(actor,0x40)
            || At<int>(actor,0x2a8) || At<int>(actor,0x2b8)
            || !EmptyEvent(actor,L"ReceiveDestroyed",L"Function /Script/Engine.Actor:ReceiveDestroyed")
            || !EmptyEvent(actor,L"ReceiveEndPlay",L"Function /Script/Engine.Actor:ReceiveEndPlay"))return Fail("child_actor_lifecycle");
        // MatineeAnimInterface is not NavRelevantInterface. Check every actual
        // reflected interface rather than infer absence from a C++ vtable.
        auto* klass=actor->GetClassPrivate();unsigned depth{};
        for(;klass && depth<16;++depth,klass=static_cast<RC::Unreal::UClass*>(klass->GetSuperStruct())) {
            const auto& interfaces=klass->GetInterfaces();if(interfaces.Num()<0 || interfaces.Num()>1)return Fail("child_interface_domain");
            for(const auto& entry:interfaces)if(!entry.Class || entry.bImplementedByK2 || entry.PointerOffset!=0x388
                || entry.Class->GetFullName()!=L"Class /Script/Engine.MatineeAnimInterface")return Fail("child_interface_domain");
        }
        if(klass)return Fail("child_interface_depth");
        unsigned components{};
        if(!VisitOwnedComponents(actor,[&](Object* object){++components;return object==mesh;}) || components!=1)return Fail("child_actor_components");
        if((At<unsigned>(mesh,0x188)&0x00200004) || At<int>(mesh,0x6a8) || At<void*>(mesh,0xec8)
            || At<int>(mesh,0x3d8) || At<int>(mesh,0x3dc)<0
            || At<void*>(mesh,0x1d0) || At<int>(mesh,0x1e0) || At<void*>(mesh,0x128)
            || At<int>(mesh,0x1b0) || At<int>(mesh,0x1c0)
            || !EmptyEvent(mesh,L"ReceiveEndPlay",L"Function /Script/Engine.ActorComponent:ReceiveEndPlay"))return Fail("child_mesh_lifecycle");
        if(state.attachment.object) {
            auto* attachment=state.attachment.object;auto* owner=At<Object*>(attachment,0x190);
            if(At<std::uintptr_t>(attachment,0)!=base_+0x3360370 || At<unsigned char>(attachment,0x18d)
                || (At<unsigned>(attachment,0x188)&0x3c200007) || At<void*>(attachment,0x1d0) || At<int>(attachment,0x1e0)
                || At<void*>(attachment,0x128) || (At<unsigned char>(attachment,0x11c)&0x40)
                || At<int>(attachment,0x3d8) || At<int>(attachment,0x3dc)<0
                || At<int>(attachment,0x1b0) || At<int>(attachment,0x1c0)
                || !EmptyEvent(attachment,L"ReceiveEndPlay",L"Function /Script/Engine.ActorComponent:ReceiveEndPlay")
                || !owner || At<Object*>(owner,0x168)==attachment
                || std::none_of(roots_.begin(),roots_.end(),[&](const auto& root){return root.manager.object==owner;})
                || At<std::uintptr_t>(owner,0)!=base_+0x3361f98)return Fail("child_attachment_lifecycle");
            unsigned occurrences{};
            if(!VisitOwnedComponents(owner,[&](Object* object) {
                if(object==attachment)++occurrences;
                // 141C27A20/1421C2030 remove only the matching pointer;
                // siblings receive no lifecycle callback. Their classes need
                // not belong to this image, but their owner/lifetime must.
                return object && Identify(object).object==object && At<Object*>(object,0x190)==owner;
            }) || occurrences!=1)return Fail("child_attachment_owner_coverage");
        }
        return Status::success();
    }
public:
    struct Retirement {
        enum class Step : std::uint8_t { Empty, Ready, WeakRemoved, ActorDestroyed, AttachmentDestroyed, Finished, Failed };
        enum class Storage : std::uint8_t { Published, Private };
        std::array<State,16> children{};
        std::array<Object*,16> roots{};
        std::size_t count{},completed{};
        Step step{Step::Empty};
        // Private headers belong to the enclosing displaced-B transaction.
        // They point at its retained native backing, never at C's live headers.
        // This metadata does not grant historical publication admission.
        Storage storage{Storage::Published};
        std::array<void*,16> private_strong_headers{};
        std::array<void*,6> private_weak_headers{};
        std::size_t private_weak_count{};
    };
    Status PrepareAddedRetirement(const Sc6ReplayTraceState& a,const Sc6ReplayTraceState& b,
        const Sc6ReplayVfxState::ParticleBirthSet& particles,Retirement& output,const Retirement* private_b=nullptr) const {
        if(output.step!=Retirement::Step::Empty)return Fail("child_retirement_reuse");
        auto status=b.PrepareExecution(*this,a,private_b);if(!status.ok())return status;
        return PrepareRetirementEntries(b,particles,output);
    }
    Status PrepareHistoricalRetirement(const Sc6ReplayTraceState& target,
        const Sc6ReplayVfxState::ParticleBirthSet& particles,Retirement& output) const {
        if(output.step!=Retirement::Step::Empty)return Fail("child_retirement_reuse");
        auto status=target.PrepareHistoricalChildren(*this);if(!status.ok())return status;
        return PrepareRetirementEntries(target,particles,output);
    }
    Status PrepareRetirementEntries(const Sc6ReplayTraceState& b,
        const Sc6ReplayVfxState::ParticleBirthSet& particles,Retirement& output) const {
        auto status=Status::success();
        Retirement candidate;
        for(const auto& state:states_)if(!b.Contains(state.ref)) {
            if(candidate.count==candidate.children.size())return Fail("child_retirement_capacity");
            status=particles.ValidateTraceAttachment(At<int>(state.ref.state,0xd8),reinterpret_cast<std::uintptr_t>(state.attachment.object));
            if(!status.ok())return Fail("child_vfx_retirement_coverage");
            Object* owner{};unsigned occurrences{};
            for(const auto& root:roots_) {
                const auto refs=At<Array>(root.component.object,0x428);
                for(int i=0;i<refs.count;++i)if(At<Ref>(refs.data,std::size_t(i)*16).state==state.ref.state) {
                    ++occurrences;owner=root.component.object;
                }
            }
            // The observed domain has one native strong entry per child.
            // Multiplicity beyond this is explicitly unsupported, never lost.
            if(occurrences!=1 || At<int>(state.ref.controller,8)!=1)return Fail("child_strong_multiplicity");
            candidate.children[candidate.count]=state;candidate.roots[candidate.count++]=owner;
        }
        if(candidate.count) {
            status=particles.ValidateRetirement();if(!status.ok())return status;
            constexpr unsigned char actor[]{0x48,0x89,0x5c,0x24,8,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20};
            constexpr unsigned char attachment[]{0x48,0x8b,0xc4,0x88,0x50,0x10,0x55,0x56,0x41,0x55,0x48,0x8b,0xec,0x48,0x81,0xec,0x80,0,0,0};
            constexpr unsigned char weak[]{0x40,0x55,0x41,0x55,0x41,0x56,0x48,0x83,0xec,0x30,0x8b,0x69,8,0x4c,0x8b,0xea};
            constexpr unsigned char strong[]{0x48,0x89,0x54,0x24,0x10,0x48,0x89,0x4c,0x24,8,0x53,0x48,0x83,0xec,0x50};
            if(std::memcmp(reinterpret_cast<void*>(base_+0x1c11c60),actor,sizeof(actor))
                || std::memcmp(reinterpret_cast<void*>(base_+0x1d99730),attachment,sizeof(attachment))
                || std::memcmp(reinterpret_cast<void*>(base_+0x213cc20),weak,sizeof(weak))
                || std::memcmp(reinterpret_cast<void*>(base_+0x1d0a460),strong,sizeof(strong)))return Fail("child_native_signature");
        }
        candidate.step=Retirement::Step::Ready;output=candidate;return Status::success();
    }
    Status RetireAddedForUndo(const Sc6ReplayTraceState& protected_b,Retirement& progress) const {
        if(progress.storage!=Retirement::Storage::Published)return Fail("child_undo_storage_domain");
        return RetireOwnedChildren(protected_b,progress);
    }
    Status RetirePrivateForCommit(const Sc6ReplayTraceState& protected_c,Retirement& progress) const {
        if(progress.storage!=Retirement::Storage::Private)return Fail("child_commit_storage_domain");
        return RetireOwnedChildren(protected_c,progress);
    }
    Status RetireOwnedChildren(const Sc6ReplayTraceState& protected_b,Retirement& progress) const {
        using Step=Retirement::Step;
        if(progress.step==Step::Finished)return protected_b.ValidateBindings();
        if(progress.step==Step::Empty || progress.step==Step::Failed || progress.completed>progress.count)return Fail("child_retirement_phase");
        auto status=protected_b.ValidateBindings();if(!status.ok())return status;
        if(progress.count==0) {progress.step=Step::Finished;return Status::success();}
        if(progress.completed==0 && progress.step==Step::Ready) {
            // Published C values must match their capture. Private B's dynamic
            // headers are displaced; its enclosing transaction validates their
            // private bytes and frozen child values before irreversible commit.
            status=progress.storage==Retirement::Storage::Private?ValidateBindings():ValidateValues();
            if(!status.ok())return status;
        }
        // Operation-local witnesses, not historical component-set snapshots.
        // Native set removal may change backing/free-list layout; require the
        // exact surviving UObject identities and owners instead of old bytes.
        struct Member { Id owner{},object{}; };
        std::array<Member,256> members{};std::size_t count{};
        for(const auto& root:roots_) {
            if(!Live(root.manager))return Fail("child_manager_lifetime");
            if(!VisitOwnedComponents(root.manager.object,[&](Object* object) {
                if(count==members.size() || !object || At<Object*>(object,0x190)!=root.manager.object)return false;
                const auto id=Identify(object);if(id.object!=object)return false;
                for(std::size_t i=0;i<count;++i)if(members[i].object.object==object)return false;
                members[count++]={root.manager,id};return true;
            }))return Fail("child_manager_members_before");
        }
        if(!RetireChildrenNative(progress))return Fail("child_native_retirement");
        const auto retired=[&](Object* object) {
            for(std::size_t i=0;i<progress.count;++i)if(progress.children[i].attachment.object==object)return true;
            return false;
        };
        std::array<bool,256> seen{};
        for(const auto& root:roots_) {
            if(!Live(root.manager) || !VisitOwnedComponents(root.manager.object,[&](Object* object) {
                for(std::size_t i=0;i<count;++i)if(members[i].object.object==object) {
                    if(seen[i] || retired(object) || members[i].owner!=root.manager
                        || !Live(members[i].object) || At<Object*>(object,0x190)!=root.manager.object)return false;
                    seen[i]=true;return true;
                }
                return false;
            })) {progress.step=Step::Failed;return Fail("child_manager_members_after");}
        }
        for(std::size_t i=0;i<count;++i)if(seen[i]==retired(members[i].object.object)) {
            progress.step=Step::Failed;return Fail("child_manager_member_loss");
        }
        status=protected_b.ValidateBindings();if(!status.ok())return status;
        if(progress.storage==Retirement::Storage::Private)
            RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] trace private B retirement owners={} completed={} C_retained=true fresh_drain_required=true\n"),progress.count,progress.completed);
        else RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] trace C-only retirement owners={} completed={} B_retained=true fresh_drain_required=true\n"),progress.count,progress.completed);
        return Status::success();
    }
private:
    bool RetireChildrenNative(Retirement& progress) const noexcept {
        // No C image dereference after releasing the last strong reference.
        // Native faults may have performed work: preserve the entire owner and
        // stop retrying ambiguous destruction, just like particle retirement.
        __try {
            using Step=Retirement::Step;
            if(progress.step==Step::Empty || progress.step==Step::Failed || progress.count>progress.children.size()
                || progress.completed>progress.count)return false;
            const bool private_storage=progress.storage==Retirement::Storage::Private;
            if(private_storage && (progress.private_weak_count!=roots_.size()*3
                || progress.private_weak_count>progress.private_weak_headers.size()))return false;
            const auto valid_array=[](const Array& a) {
                return a.count>=0 && a.count<=256 && a.capacity>=a.count && a.capacity<=256
                    && (!a.capacity || a.data);
            };
            // Reject aliases with C before any native call. Partial overlap is
            // just as unsafe as sharing the first address of an allocation.
            const auto overlaps=[](const Array& a,const Array& b) {
                const auto x=reinterpret_cast<std::uintptr_t>(a.data),y=reinterpret_cast<std::uintptr_t>(b.data);
                const auto n=std::size_t(a.capacity)*16,m=std::size_t(b.capacity)*16;
                return n && m && (n>UINTPTR_MAX-x || m>UINTPTR_MAX-y || (x<y+m && y<x+n));
            };
            if(private_storage) {
                std::array<void*,22> headers{};std::size_t count{};
                for(std::size_t i=0;i<progress.count;++i) {
                    auto* h=progress.private_strong_headers[i];if(!h)return false;
                    // Several children can share one private strong array.
                    if(std::find(headers.begin(),headers.begin()+count,h)==headers.begin()+count) {
                        const auto a=At<Array>(h,0);if(!valid_array(a))return false;
                        std::size_t expected{};
                        for(std::size_t j=progress.completed;j<progress.count;++j)
                            if(progress.private_strong_headers[j]==h)++expected;
                        if(std::size_t(a.count)!=expected)return false;
                        headers[count++]=h;
                    }
                }
                for(std::size_t i=0;i<progress.private_weak_count;++i) {
                    auto* h=progress.private_weak_headers[i];
                    if(!h || std::find(headers.begin(),headers.begin()+count,h)!=headers.begin()+count)return false;
                    headers[count++]=h;
                }
                for(std::size_t i=0;i<count;++i) {
                    const auto a=At<Array>(headers[i],0);if(!valid_array(a))return false;
                    for(std::size_t j=0;j<i;++j)if(overlaps(a,At<Array>(headers[j],0)))return false;
                    for(const auto& root:roots_)for(const auto offset:{0x428u,0x3e0u,0x3f0u,0x400u}) {
                        auto* live=reinterpret_cast<std::byte*>(root.component.object)+offset;
                        const auto b=At<Array>(live,0);
                        if(headers[i]==live || !valid_array(b) || overlaps(a,b))return false;
                    }
                }
            }
            while(progress.completed<progress.count) {
                const auto& state=progress.children[progress.completed];
                if(progress.step==Step::Ready) {
                    if(!Live(state.actor)||!Live(state.mesh)||!Live(state.attachment))return false;
                    if(private_storage) {
                        if(!state.ref.controller || state.ref.state!=state.ref.controller+16
                            || At<std::uintptr_t>(state.ref.controller,0)!=base_+0x3362590)return false;
                        const auto strong=At<Array>(progress.private_strong_headers[progress.completed],0);
                        unsigned matches{};
                        for(int i=0;i<strong.count;++i) {
                            const auto ref=At<Ref>(strong.data,std::size_t(i)*16);
                            if(ref.state==state.ref.state || ref.controller==state.ref.controller) {
                                if(ref.state!=state.ref.state || ref.controller!=state.ref.controller)return false;
                                ++matches;
                            }
                        }
                        if(matches!=1 || At<int>(state.ref.controller,8)!=1)return false;
                        // A C weak reference is a consumer too: destroying its
                        // target would change C even without a C strong owner.
                        for(const auto& root:roots_)for(const auto offset:{0x428u,0x3e0u,0x3f0u,0x400u}) {
                            const auto live=At<Array>(root.component.object,offset);
                            for(int i=0;i<live.count;++i) {
                                const auto ref=At<Ref>(live.data,std::size_t(i)*16);
                                if(ref.state==state.ref.state || ref.controller==state.ref.controller)return false;
                            }
                        }
                    } else if(!OwnsNativeChild(state.ref))return false;
                    auto reference=state.ref;auto* needle=&reference;
                    for(std::size_t entry=0;entry<roots_.size()*3;++entry) {
                        constexpr std::size_t offsets[]{0x3e0,0x3f0,0x400};
                        auto* header=private_storage?progress.private_weak_headers[entry]:
                            reinterpret_cast<std::byte*>(roots_[entry/3].component.object)+offsets[entry%3];
                        const auto refs=At<Array>(header,0);int matches{};
                        if(refs.count<0 || refs.count>256 || refs.capacity<refs.count || (refs.count&&!refs.data))return false;
                        for(int i=0;i<refs.count;++i)matches+=At<Ref>(refs.data,std::size_t(i)*16).state==state.ref.state;
                        // Mark uncertainty before a mutating native call. Its
                        // count/result must prove completion before continuing.
                        progress.step=Step::Failed;
                        const auto removed=reinterpret_cast<int(*)(void*,Ref**)>(base_+0x213cc20)(header,&needle);
                        if(removed!=matches || At<int>(header,8)!=refs.count-matches)return false;
                    }
                    progress.step=Step::WeakRemoved;
                }
                if(progress.step==Step::WeakRemoved) {
                    progress.step=Step::Failed;
                    if(!reinterpret_cast<bool(*)(void*,bool,bool)>(base_+0x1c11c60)(state.actor.object,false,true)
                        || Live(state.actor) || (At<unsigned>(state.mesh.object,0x188)&7))return false;
                    progress.step=Step::ActorDestroyed;
                }
                if(progress.step==Step::ActorDestroyed) {
                    progress.step=Step::Failed;
                    if(state.attachment.object) {
                        if(!Live(state.attachment))return false;
                        reinterpret_cast<void(*)(void*,bool)>(base_+0x1d99730)(state.attachment.object,false);
                        if(Live(state.attachment) || (At<unsigned>(state.attachment.object,0x188)&7))return false;
                    }
                    progress.step=Step::AttachmentDestroyed;
                }
                if(progress.step==Step::AttachmentDestroyed) {
                    auto* header=private_storage?progress.private_strong_headers[progress.completed]:
                        reinterpret_cast<std::byte*>(progress.roots[progress.completed])+0x428;
                    auto* value=state.ref.state;auto* needle=&value;
                    const auto before=At<int>(header,8);
                    progress.step=Step::Failed;
                    const auto removed=reinterpret_cast<int(*)(void*,std::byte***)>(base_+0x1d0a460)(header,&needle);
                    if(removed!=1 || At<int>(header,8)!=before-1)return false;
                    ++progress.completed;progress.step=Step::Ready;
                }
            }
            progress.step=Step::Finished;return true;
        } __except(EXCEPTION_EXECUTE_HANDLER) {progress.step=Retirement::Step::Failed;return false;}
    }
public:
    bool HasAddedStates(const Sc6ReplayTraceState& previous) const {
        return std::any_of(states_.begin(),states_.end(),[&](const auto& s){return !previous.Contains(s.ref);});
    }
    // Read-only initial A/B ownership proof. Historical header pointers are not
    // private allocations: unlike execution settlement, A may describe backing
    // that native B still uses. Publication must clone A before displacing B.
    // This proof alone does not enable historical child publication.
    Status PrepareHistoricalChildren(const Sc6ReplayTraceState& current) const {
        if(!current.HasAddedStates(*this))return Prepare(current);
        auto status=ValidateBindings();if(status.ok())status=current.ValidateValues();
        if(!status.ok())return status;
        if(base_!=current.base_ || world_!=current.world_ || roots_.size()!=current.roots_.size()
            || dynamic_count_!=current.dynamic_count_ || current.states_.size()<states_.size()
            || current.states_.size()>states_.size()+16)
            return Fail("historical_child_domain");
        for(std::size_t i=0;i<roots_.size();++i)
            if(roots_[i].component!=current.roots_[i].component || roots_[i].chara!=current.roots_[i].chara
                || roots_[i].manager!=current.roots_[i].manager || roots_[i].scene!=current.roots_[i].scene)return Fail("historical_child_root");
        for(const auto& old:states_) {
            const auto found=std::find_if(current.states_.begin(),current.states_.end(),[&](const auto& row) {
                return row.ref.state==old.ref.state && row.ref.controller==old.ref.controller;
            });
            if(found==current.states_.end() || found->actor!=old.actor || found->mesh!=old.mesh
                || found->attachment!=old.attachment || found->animation!=old.animation)
                return Fail("historical_child_retained_identity");
        }
        for(const auto& value:values_) {
            const auto found=std::find_if(current.values_.begin(),current.values_.end(),[&](const auto& row) {
                return row.address==value.address;
            });
            if(found==current.values_.end() || found->bytes.size()!=value.bytes.size() || !Writable(value))
                return Fail("historical_child_retained_storage");
            for(const auto& other:current.values_)if(other.address!=value.address
                && StorageOverlaps(value.address,value.bytes.size(),other.address,other.bytes.size()))
                return Fail("historical_child_storage_alias");
        }
        std::size_t strong_entries{};
        for(std::size_t i=0;i<dynamic_count_;++i) {
            const auto& a=dynamic_[i];const auto& b=current.dynamic_[i];
            if(a.address!=b.address || a.kind!=b.kind)return Fail("historical_child_dynamic_owner");
            if(a.kind==DynamicImage::Kind::ChildStrongReferences) {
                const auto aa=At<Array>(const_cast<std::byte*>(a.header.data()),0);
                const auto bb=At<Array>(const_cast<std::byte*>(b.header.data()),0);
                if(aa.count || bb.count<0 || bb.count>16)return Fail("historical_child_strong_domain");
                strong_entries+=std::size_t(bb.count);
            }
        }
        if(strong_entries!=current.states_.size()-states_.size())return Fail("historical_child_strong_coverage");
        status=current.CheckRetirementWorld();if(!status.ok())return status;
        for(const auto& state:current.states_)if(!Contains(state.ref)) {
            status=current.CheckChild(state,*this,*this);if(!status.ok())return status;
        }
        return Status::success();
    }
    Status PrepareExecution(const Sc6ReplayTraceState& current,const Sc6ReplayTraceState& target,const Retirement* private_b=nullptr) const {
        if(!private_b && !current.HasAddedStates(*this))return Prepare(current);
        const auto& baseline=private_b?target:*this;
        if(private_b) {
            if(private_b->storage!=Retirement::Storage::Private || private_b->step!=Retirement::Step::Ready
                || !private_b->count || private_b->count>private_b->children.size()
                || states_.size()!=target.states_.size()+private_b->count)return Fail("private_child_execution_proof");
            std::size_t matched{};
            for(const auto& state:states_)if(!target.Contains(state.ref)) {
                const auto found=std::find_if(private_b->children.begin(),private_b->children.begin()+private_b->count,[&](const auto& child) {
                    return child.ref.state==state.ref.state && child.ref.controller==state.ref.controller
                        && child.actor==state.actor && child.mesh==state.mesh && child.attachment==state.attachment && child.animation==state.animation;
                });
                if(found==private_b->children.begin()+private_b->count || current.Contains(state.ref))return Fail("private_child_execution_membership");
                const auto i=std::size_t(found-private_b->children.begin());
                if(!Live(state.actor) || !Live(state.mesh) || !Live(state.attachment) || !Live(state.animation)
                    || At<int>(state.ref.controller,8)!=1 || !private_b->private_strong_headers[i])return Fail("private_child_execution_lifetime");
                const auto array=At<Array>(private_b->private_strong_headers[i],0);
                if(array.count<1 || array.count>16 || array.capacity<array.count || !array.data)return Fail("private_child_execution_strong");
                unsigned refs{};
                for(int n=0;n<array.count;++n) {
                    const auto ref=At<Ref>(array.data,std::size_t(n)*16);
                    if(ref.state==state.ref.state && ref.controller==state.ref.controller)++refs;
                }
                if(refs!=1)return Fail("private_child_execution_strong");
                for(std::size_t j=0;j<current.dynamic_count_;++j)if(current.dynamic_[j].kind!=DynamicImage::Kind::Map) {
                    const auto& d=current.dynamic_[j];const auto count=At<int>(const_cast<std::byte*>(d.header.data()),8);
                    if(count<0 || std::size_t(count)>d.bytes.size()/16)return Fail("private_child_C_array");
                    for(int n=0;n<count;++n) {
                        const auto ref=At<Ref>(const_cast<std::byte*>(d.bytes.data()),std::size_t(n)*16);
                        if(ref.state==state.ref.state || ref.controller==state.ref.controller)return Fail("private_child_C_consumer");
                    }
                }
                ++matched;
            }
            if(matched!=private_b->count)return Fail("private_child_execution_coverage");
            for(const auto& value:values_)if(std::none_of(target.values_.begin(),target.values_.end(),[&](const auto& v) {
                return v.address==value.address && v.bytes.size()==value.bytes.size();
            }) && !Equal(value))return Fail("private_child_execution_changed");
        }
        auto status=ValidateBindings();if(status.ok())status=target.ValidateBindings();
        if(status.ok())status=current.ValidateValues();if(!status.ok())return status;
        if(base_!=current.base_ || world_!=current.world_ || dynamic_count_!=current.dynamic_count_
            || current.states_.size()>baseline.states_.size()+16)return Fail("child_execution_domain");
        for(const auto& old:baseline.states_) {
            const auto found=std::find_if(current.states_.begin(),current.states_.end(),[&](const auto& row){return row.ref.state==old.ref.state && row.ref.controller==old.ref.controller;});
            if(found==current.states_.end() || found->actor!=old.actor || found->mesh!=old.mesh
                || found->attachment!=old.attachment || found->animation!=old.animation)return Fail("child_retained_identity");
        }
        for(const auto& value:baseline.values_) {
            const auto found=std::find_if(current.values_.begin(),current.values_.end(),[&](const auto& row){return row.address==value.address;});
            if(found==current.values_.end() || found->bytes.size()!=value.bytes.size() || !Writable(value))return Fail("child_retained_storage");
        }
        for(std::size_t i=0;i<dynamic_count_;++i) {
            if(dynamic_[i].address!=current.dynamic_[i].address || dynamic_[i].kind!=current.dynamic_[i].kind
                || (dynamic_[i].kind==DynamicImage::Kind::ChildStrongReferences
                    && !private_b && At<int>(const_cast<std::byte*>(dynamic_[i].header.data()),8)))return Fail("child_dynamic_ownership");
            for(std::size_t j=0;j<dynamic_count_;++j)
                if(StorageOverlaps(At<void*>(const_cast<std::byte*>(current.dynamic_[i].header.data()),0),current.dynamic_[i].bytes.size(),
                    At<void*>(const_cast<std::byte*>(dynamic_[j].header.data()),0),dynamic_[j].bytes.size()))return Fail("child_C_B_storage_alias");
        }
        status=current.CheckRetirementWorld();if(!status.ok())return status;
        for(const auto& state:current.states_)if(!Contains(state.ref)) {
            status=current.CheckChild(state,target,*this);if(!status.ok())return status;
        }
        return Status::success();
    }
