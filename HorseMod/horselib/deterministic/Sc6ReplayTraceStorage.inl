#pragma once

namespace Horse::Deterministic {
// Owns displaced manager/weak/child-array backing. The fixed trace pool and
// UObject leases remain in the enclosing A/B images; B-only child strong entries
// stay in displaced native storage until recovery or explicit commit. Never free a published
// buffer on abandonment; the host must recover or finish its transaction.
class Sc6ReplayTraceState::Prepared final {
    friend class Sc6ReplayTraceState;
    struct Chunk {
        const DynamicImage* a{};const DynamicImage* b{};
        std::array<std::byte,0x50> installed{};
        std::array<std::byte,0x50> displaced{};
        void* storage{};std::size_t charged{},old_charged{};
    };
    struct Control {
        std::byte* address{};int strong{},weak{},added{},removed{},child_weak{};
    };
    std::array<Chunk,10> chunks_{};
    std::array<Control,1536> controls_{};
    std::size_t count_{},control_count_{},execution_original_controls_{};
    const Sc6ReplayTraceState* a_{};const Sc6ReplayTraceState* b_{};
    const Sc6ReplayTraceState* historical_a_{};
    std::array<FreshChild*,16> fresh_children_{};
    std::size_t fresh_count_{};
    Retirement private_children_;
    std::uintptr_t base_{};DWORD thread_{};
    bool ready_{},published_{};
    bool executing_{},execution_settled_{},execution_undo_started_{};
    std::size_t execution_budget_{};
    const ReplayGpuCompletion* fresh_completion_{};
    std::uint64_t fresh_submission_floor_{};

    FreshChild* FreshControl(const std::byte* controller) const {
        for(std::size_t i=0;i<fresh_count_;++i)if(fresh_children_[i]->state.ref.controller==controller)return fresh_children_[i];
        return nullptr;
    }
    bool FreshOwner(const FreshChild& child,bool published) const {
        const auto& ref=child.state.ref;
        if(!child.prepared || child.retired || !child.identity_retained
            || child.factory.phase!=Sc6ReplayTraceChildFactory::Phase::Returned
            || child.history_phase!=FreshChild::HistoryPhase::Ready || child.animation_phase!=FreshChild::AnimationPhase::Ready
            || !child.lease.Validate().ok() || !ref.controller || ref.state!=ref.controller+16
            || child.factory.reference.state!=ref.state || child.factory.reference.controller!=ref.controller
            || At<std::uintptr_t>(ref.controller,0)!=base_+0x3362590 || At<int>(ref.controller,8)!=1 || At<int>(ref.controller,12)<2
            || child.strong.data!=reinterpret_cast<const std::byte*>(&child.factory.reference) || child.strong.capacity!=1
            || child.strong.count!=(published?0:1))return false;
        const auto parent=At<Ref>(ref.state,0xa0);
        if(parent.state!=child.cached_parent.state || parent.controller!=child.cached_parent.controller
            || (parent.controller ? !historical_a_->Contains(parent) || !b_->Contains(parent) : parent.state!=nullptr))return false;
        return published ? child.strong_phase==FreshChild::StrongPhase::Publishing || child.strong_phase==FreshChild::StrongPhase::Published
            : child.strong_phase==FreshChild::StrongPhase::Private;
    }
    Status PrepareInitial() const {
        auto status=(private_children_.count || fresh_count_)?historical_a_->PrepareHistoricalChildren(*b_):a_->Prepare(*b_);
        if(!status.ok())return status;
        // Historical/private-child routes can bypass Prepare. Recheck actual
        // A and B providers immediately before publishing any native headers.
        if(!fresh_count_)return private_children_.count?a_->ValidateMaterialPublication(*b_):status;
        if(!historical_a_->validation_only_ || historical_a_->survivor_source_!=a_
            || a_->states_.size()!=historical_a_->states_.size()+fresh_count_)return Fail("fresh_child_baseline");
        status=a_->ValidateBindings();if(!status.ok())return status;
        std::array<bool,16> strong{};
        for(std::size_t i=0;i<fresh_count_;++i) {
            const auto& child=*fresh_children_[i];
            if(!FreshOwner(child,false) || b_->Contains(child.state.ref) || historical_a_->Contains(child.state.ref))return Fail("fresh_child_private_owner");
            const auto state=std::find_if(a_->states_.begin(),a_->states_.end(),[&](const auto& s) {
                return s.ref.state==child.state.ref.state && s.ref.controller==child.state.ref.controller;
            });
            if(state==a_->states_.end() || state->actor!=child.state.actor || state->mesh!=child.state.mesh
                || state->animation!=child.state.animation || state->attachment!=child.state.attachment)return Fail("fresh_child_projected_identity");
            for(std::size_t j=0;j<i;++j)if(fresh_children_[j]->state.ref.controller==child.state.ref.controller)return Fail("fresh_child_duplicate");
        }
        for(std::size_t i=0;i<a_->dynamic_count_;++i)if(a_->dynamic_[i].kind==DynamicImage::Kind::ChildStrongReferences) {
            const auto& image=a_->dynamic_[i];const auto count=At<int>(const_cast<std::byte*>(image.header.data()),8);
            if(count<0 || std::size_t(count)>image.bytes.size()/16)return Fail("fresh_child_strong_count");
            for(int n=0;n<count;++n) {
                const auto ref=At<Ref>(const_cast<std::byte*>(image.bytes.data()),std::size_t(n)*16);
                auto* child=FreshControl(ref.controller);
                if(!child || ref.state!=child->state.ref.state)return Fail("fresh_child_strong_owner");
                std::size_t j{};while(fresh_children_[j]!=child)++j;
                if(strong[j])return Fail("fresh_child_strong_duplicate");strong[j]=true;
            }
        }
        for(std::size_t i=0;i<fresh_count_;++i)if(!strong[i])return Fail("fresh_child_strong_missing");
        return a_->ValidateMaterialPublication(*b_);
    }

    bool PrivateChildControl(const std::byte* controller) const {
        for(std::size_t i=0;i<private_children_.count;++i)
            if(private_children_.children[i].ref.controller==controller)return true;
        return false;
    }
    bool FrozenPrivateChildren() const {
        if(!private_children_.count)return true;
        if(!historical_a_ || !b_ || private_children_.step!=Retirement::Step::Ready)return false;
        for(const auto& value:b_->values_) {
            const auto shared=std::find_if(historical_a_->values_.begin(),historical_a_->values_.end(),[&](const auto& a) {
                return a.address==value.address && a.bytes.size()==value.bytes.size();
            });
            if(shared==historical_a_->values_.end() && !Equal(value))return false;
        }
        return true;
    }

    static bool Copy(void* destination,const void* source,std::size_t bytes) noexcept {
        __try {if(bytes) std::memcpy(destination,source,bytes);return true;}
        __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static bool WritableRange(void* pointer,std::size_t bytes) noexcept {
        auto p=reinterpret_cast<std::uintptr_t>(pointer);
        if(!p || bytes>UINTPTR_MAX-p) return false;
        const auto end=p+bytes;
        while(p<end) {
            MEMORY_BASIC_INFORMATION r{};
            if(!VirtualQuery(reinterpret_cast<void*>(p),&r,sizeof(r)) || r.State!=MEM_COMMIT
                || (r.Protect&(PAGE_GUARD|PAGE_NOACCESS))
                || !(r.Protect&(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))) return false;
            const auto next=reinterpret_cast<std::uintptr_t>(r.BaseAddress)+r.RegionSize;
            if(next<=p) return false;p=next;
        }
        return true;
    }
    bool Known(const Ref& ref,const Sc6ReplayTraceState* current=nullptr) const {
        const auto contains=[&](const Sc6ReplayTraceState& image) {
            return std::any_of(image.states_.begin(),image.states_.end(),[&](const auto& s) {
                return s.ref.state==ref.state && s.ref.controller==ref.controller;
            });
        };
        if(!ref.controller || ref.state!=ref.controller+16
            || At<std::uintptr_t>(ref.controller,0)!=base_+0x3362590 || At<int>(ref.controller,12)<=0) return false;
        if(PrivateChildControl(ref.controller))
            return contains(*b_) && !contains(*historical_a_) && At<int>(ref.controller,8)==1;
        if(const auto* child=FreshControl(ref.controller)) {
            if(child->state.ref.state!=ref.state || contains(*b_))return false;
            if(executing_ && current && fresh_completion_) {
                bool dead{};
                return historical_a_->ValidateFreshChildExecution(*child,*current,*fresh_completion_,fresh_submission_floor_,dead).ok();
            }
            return contains(*a_) && FreshOwner(*child,published_);
        }
        // Current C may be the only image leasing a controller whose state
        // expired during native execution. Its weak lease is retained through
        // settlement/undo; never inspect the destroyed state or acquire strong.
        const auto expired=a_->OwnsExpired(ref) || b_->OwnsExpired(ref)
            || (current && current->OwnsExpired(ref));
        if(current && At<int>(ref.controller,8)>0 && !contains(*a_) && !contains(*b_)
            && current->OwnsNativeChild(ref))return true; // Execution-only, after complete delta preflight.
        return reinterpret_cast<const ReplayTraceWeakController*>(ref.controller)->MatchesOwnership(
            base_+0x3362590,expired,contains(*a_),contains(*b_));
    }
    Control* Track(const Ref& ref) {
        if(!Known(ref)) return nullptr;
        for(std::size_t i=0;i<control_count_;++i) if(controls_[i].address==ref.controller) return &controls_[i];
        if(control_count_==controls_.size()) return nullptr;
        auto& c=controls_[control_count_++];
        c={ref.controller,At<int>(ref.controller,8),At<int>(ref.controller,12),0,0};
        return &c;
    }
    bool ControlsMatch() const {
        for(std::size_t i=0;i<control_count_;++i) {
            const auto& c=controls_[i];
            if(At<std::uintptr_t>(c.address,0)!=base_+0x3362590
                || At<int>(c.address,8)!=c.strong || At<int>(c.address,12)!=c.weak+c.added) return false;
        }
        return true;
    }
    bool PrivateBMatches() const noexcept {
        __try {
            for(std::size_t i=0;i<count_;++i) {
                const auto& image=*chunks_[i].b;
                const auto* original=At<void*>(const_cast<std::byte*>(image.header.data()),0);
                if(!image.bytes.empty() && std::memcmp(original,image.bytes.data(),image.bytes.size())) return false;
            }
            return true;
        } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    bool ReleasePrivate() noexcept {
        // Every increment below belongs to this transaction. A weak reference
        // retains its controller even if an external teardown ended its state.
        __try {
            for(std::size_t i=0;i<control_count_;++i) {
                auto& c=controls_[i];
                while(c.added) {
                    auto& weak=At<int>(c.address,12);
                    if(weak<=0) return false;
                    --c.added;
                    if(--weak==0)
                        reinterpret_cast<void(*)(void*,unsigned)>(At<std::uintptr_t>(At<void*>(c.address,0),8))(c.address,1);
                }
            }
            for(std::size_t i=0;i<count_;++i) if(chunks_[i].storage) {
                reinterpret_cast<void(*)(void*)>(base_+0xd46a00)(chunks_[i].storage);
                chunks_[i].storage=nullptr;
            }
            count_=control_count_=0;ready_=false;
            executing_=execution_settled_=execution_undo_started_=false;execution_budget_=0;
            return true;
        } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
public:
    Prepared()=default;
    const Retirement* PrivateChildren() const {return private_children_.count?&private_children_:nullptr;}
    Status VisitPrivateMeshOwners(void* context,bool(*accept)(void*,std::uintptr_t)) const {
        if(!private_children_.count)return Status::success();
        if(!accept || !b_ || thread_!=GetCurrentThreadId() || private_children_.step!=Retirement::Step::Ready)
            return Fail("private_child_phase");
        auto status=b_->ValidateBindings();if(!status.ok())return status;
        if((published_ || ready_) && (!PrivateBMatches() || !FrozenPrivateChildren()))return Fail("private_child_changed");
        for(std::size_t i=0;i<private_children_.count;++i) {
            const auto& state=private_children_.children[i];
            if(!Live(state.actor) || !Live(state.mesh) || !Live(state.attachment) || !Live(state.animation))return Fail("private_child_lifetime");
            if(!state.ref.controller || state.ref.state!=state.ref.controller+16 || At<std::uintptr_t>(state.ref.controller,0)!=base_+0x3362590
                || At<int>(state.ref.controller,8)!=1)return Fail("private_child_controller");
            if(!accept(context,reinterpret_cast<std::uintptr_t>(state.mesh.object)))return Fail("private_child_render_accept");
            if(!published_ && !ready_ && !b_->OwnsNativeChild(state.ref))return Fail("private_child_recovered_membership");
        }
        return Status::success();
    }
    Status ValidateRetirementB() const {
        if(!executing_ || !published_ || !ready_ || thread_!=GetCurrentThreadId()
            || !b_ || !PrivateBMatches())return Fail("retirement_private_B");
        return b_->ValidateBindings();
    }
    Prepared(const Prepared&)=delete;
    Prepared& operator=(const Prepared&)=delete;
    ~Prepared() {if(!published_) ReleasePrivate();}
    std::size_t owned_bytes() const {
        std::size_t n=sizeof(*this)+execution_budget_;
        for(std::size_t i=0;i<count_;++i) n+=chunks_[i].charged+chunks_[i].old_charged;
        return n;
    }
    Status ValidatePublished() const {
        if(!ready_ || !published_ || thread_!=GetCurrentThreadId()) return Fail("storage_phase");
        if(executing_ && (!execution_settled_ || execution_undo_started_))return Fail("execution_unsettled");
        if(!ControlsMatch() || !PrivateBMatches() || !FrozenPrivateChildren()) return Fail("weak_or_private_B_ownership");
        const auto status=b_->ValidateBindings();if(!status.ok()) return status;
        for(std::size_t i=0;i<count_;++i) {
            const auto& c=chunks_[i];
            if(std::memcmp(c.a->address,c.installed.data(),c.a->header_size())) return Fail("published_storage");
        }
        return a_->ValidateValues();
    }
    Status BeginExecution(std::size_t retirement_budget,const ReplayGpuCompletion* completion=nullptr) {
        if(fresh_count_ && (!completion || !completion->retired()))return Fail("fresh_child_execution_contract");
        if(executing_ || !ready_ || !a_ || !b_ || count_!=a_->dynamic_count_ || count_!=b_->dynamic_count_)return Fail("execution_storage_shape");
        auto s=ValidatePublished();if(!s.ok())return s;
        std::size_t active{};for(std::size_t i=0;i<count_;++i)active+=chunks_[i].charged;
        if(retirement_budget<active+sizeof(controls_) || retirement_budget>SIZE_MAX-owned_bytes())return Fail("execution_budget");
        // Every dynamic container was cloned, even if A and B were identical.
        // Its weak references and backing now belong to native execution.
        for(std::size_t i=0;i<count_;++i){chunks_[i].storage=nullptr;chunks_[i].charged=0;}
        for(std::size_t i=0;i<control_count_;++i)controls_[i].added=0;
        execution_original_controls_=control_count_;
        if(fresh_count_) {fresh_completion_=completion;fresh_submission_floor_=completion->submitted_serial();}
        executing_=true;execution_budget_=retirement_budget;return Status::success();
    }
    // C is an independently captured current ownership image, retained by the
    // caller until this transaction ends. It is never installed as expected A.
    Status SettleExecution(const Sc6ReplayTraceState& c,const Sc6ReplayVfxState::ParticleBirthSet* particles=nullptr) {
        if(!executing_ || execution_settled_ || !published_ || !ready_ || thread_!=GetCurrentThreadId())return Fail("execution_settle_phase");
        auto s=b_->PrepareExecution(c,*historical_a_,PrivateChildren());if(!s.ok()) {
            RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] trace settlement partition original=B current=C\n"));
            b_->DescribeOwnershipDelta(c);
            if(a_!=b_ && a_!=&c) {
                RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] trace settlement partition original=A current=C\n"));
                a_->DescribeOwnershipDelta(c);
            }
            return s;
        }
        if(c.HasAddedStates(*b_)) {
            if(!particles)return Fail("execution_child_retirement_missing");
            Retirement retirement;
            s=c.PrepareAddedRetirement(*historical_a_,*b_,*particles,retirement,PrivateChildren());if(!s.ok())return s;
        }
        if(!PrivateBMatches())return Fail("execution_private_B");
        const auto overlaps=[](std::uintptr_t a,std::size_t n,std::uintptr_t b,std::size_t m) {
            return n && m && (n>UINTPTR_MAX-a || m>UINTPTR_MAX-b || (a<b+m && b<a+n));
        };
        std::size_t charged{};
        for(std::size_t i=0;i<count_;++i) {
            const auto& image=c.dynamic_[i];
            if(image.address!=chunks_[i].b->address || image.kind!=chunks_[i].b->kind
                || std::memcmp(image.address,image.header.data(),image.header_size()))return Fail("execution_header");
            const auto pointer=At<std::uintptr_t>(const_cast<std::byte*>(image.header.data()),0);
            for(std::size_t j=0;j<count_;++j) {
                const auto& b=*chunks_[j].b;
                if(overlaps(pointer,image.bytes.size(),At<std::uintptr_t>(const_cast<std::byte*>(b.header.data()),0),b.bytes.size()))return Fail("execution_B_alias");
                const auto& other=c.dynamic_[j];
                if(j!=i && overlaps(pointer,image.bytes.size(),At<std::uintptr_t>(const_cast<std::byte*>(other.header.data()),0),other.bytes.size()))return Fail("execution_C_alias");
            }
            for(const auto* images:{&c.bindings_,&c.values_})for(const auto& field:*images)
                if(overlaps(pointer,image.bytes.size(),reinterpret_cast<std::uintptr_t>(field.address),field.bytes.size()))return Fail("execution_inline_alias");
            const auto size=image.bytes.empty()?0:reinterpret_cast<std::size_t(*)(std::size_t,unsigned)>(base_+0xd50dc0)(image.bytes.size(),0);
            if(size<image.bytes.size() || size>execution_budget_-sizeof(controls_)-charged)return Fail("execution_retirement_budget");
            charged+=size;
        }
        // Work on local metadata until every weak-count check has passed.
        auto controls=controls_;auto control_count=control_count_;
        const auto original_controls=control_count_;
        for(std::size_t i=0;i<fresh_count_;++i) {
            bool dead{};
            if(!fresh_completion_)return Fail("fresh_child_execution_completion");
            s=historical_a_->ValidateFreshChildExecution(*fresh_children_[i],c,*fresh_completion_,fresh_submission_floor_,dead);
            if(!s.ok())return s;
            const auto& child=*fresh_children_[i];
            auto owned=std::find_if(controls.begin(),controls.begin()+original_controls,[&](const auto& v) {return v.address==child.state.ref.controller;});
            if(owned==controls.begin()+original_controls || owned->strong<0 || owned->strong>1
                || (!dead && owned->strong!=1))return Fail("fresh_child_execution_control");
            if(dead && owned->strong==1) {
                // The native final strong release consumes its implicit weak
                // reference and the payload's cached-parent weak owner once.
                // This adjusts only our baseline, never native refcounts.
                if(owned->weak<2)return Fail("fresh_child_execution_implicit_weak");
                --owned->weak;owned->strong=0;
                if(child.cached_parent.controller) {
                    auto parent=std::find_if(controls.begin(),controls.begin()+original_controls,[&](const auto& v) {return v.address==child.cached_parent.controller;});
                    if(parent==controls.begin()+original_controls || parent->weak<2)return Fail("fresh_child_execution_parent_weak");
                    --parent->weak;
                }
            }
        }
        // Capturing C acquires its own expired-controller weak lease. Account
        // only that proven owner; do not accept unexplained native count drift.
        if(&c!=a_ && &c!=b_) for(std::size_t i=0;i<original_controls;++i)
            if(c.OwnsExpired({controls[i].address+16,controls[i].address})) {
                if(controls[i].weak==INT_MAX)return Fail("execution_image_weak_overflow");
                ++controls[i].weak;
            }
        for(std::size_t i=0;i<count_;++i)if(c.dynamic_[i].weak()) {
            const auto& image=c.dynamic_[i];const auto n=At<int>(const_cast<std::byte*>(image.header.data()),8);
            for(int j=0;j<n;++j) {
                const auto ref=At<Ref>(const_cast<std::byte*>(image.bytes.data()),std::size_t(j)*16);
                if(!Known(ref,&c))return Fail("execution_weak_owner");
                std::size_t k{};while(k<control_count && controls[k].address!=ref.controller)++k;
                if(k==control_count) {
                    if(control_count==controls.size())return Fail("execution_weak_capacity");
                    controls[control_count++]={ref.controller,At<int>(ref.controller,8),At<int>(ref.controller,12),0,0};
                }
                ++controls[k].added;
            }
        }
        // Each live C-only child's verified cached parent owns one native
        // weak reference in addition to the published weak-array entries.
        // It is never inferred from the observed count difference.
        for(const auto& child:c.states_)if(!b_->Contains(child.ref) && !historical_a_->Contains(child.ref) && !FreshControl(child.ref.controller)) {
            const auto parent=At<Ref>(child.ref.state,0xa0);
            if(!parent.controller)continue;
            std::size_t k{};while(k<original_controls && controls[k].address!=parent.controller)++k;
            if(k==original_controls || controls[k].weak==INT_MAX)return Fail("execution_child_parent_owner");
            ++controls[k].weak;++controls[k].child_weak;
        }
        for(std::size_t i=0;i<control_count;++i) {
            auto& v=controls[i];if(i>=original_controls)v.weak-=v.added;
            if(v.weak<1 || At<std::uintptr_t>(v.address,0)!=base_+0x3362590
                || At<int>(v.address,8)!=v.strong || v.strong<0
                || std::int64_t(At<int>(v.address,12))!=std::int64_t(v.weak)+v.added)return Fail("execution_weak_count");
        }
        a_=&c;controls_=controls;control_count_=control_count;
        for(std::size_t i=0;i<count_;++i) {
            auto& chunk=chunks_[i];chunk.a=&c.dynamic_[i];chunk.installed=chunk.a->header;
            chunk.storage=At<void*>(chunk.installed.data(),0);
            chunk.charged=chunk.a->bytes.empty()?0:reinterpret_cast<std::size_t(*)(std::size_t,unsigned)>(base_+0xd50dc0)(chunk.a->bytes.size(),0);
        }
        execution_budget_-=charged;execution_settled_=true;
        s=ValidatePublished();if(!s.ok())return s;
        for(std::size_t i=0;i<fresh_count_;++i) {
            fresh_children_[i]->native_dead=At<int>(fresh_children_[i]->state.ref.controller,8)==0;
            fresh_children_[i]->execution_settled=true;
        }
        return Status::success();
    }
    Status ReopenExecutionForUndo(const Sc6ReplayTraceState& target) {
        if(!executing_ || !published_ || execution_undo_started_ || target.dynamic_count_!=count_)
            return Fail("execution_reopen_phase");
        if(!execution_settled_) {
            if(fresh_completion_)fresh_submission_floor_=fresh_completion_->submitted_serial();
            for(std::size_t i=0;i<fresh_count_;++i)fresh_children_[i]->execution_settled=false;
            return Status::success();
        }
        auto s=ValidatePublished();if(!s.ok())return s;
        std::size_t released{};
        for(std::size_t i=0;i<count_;++i) {
            if(chunks_[i].charged>SIZE_MAX-released)return Fail("execution_reopen_budget");
            released+=chunks_[i].charged;
        }
        if(released>SIZE_MAX-execution_budget_)return Fail("execution_reopen_budget");
        // Newly tracked controls have no displaced B references. They may be
        // C-expired controllers or new weak entries to retained A/B live owners.
        // Forget their metadata
        // before the host releases C: native removal of its last weak entry can
        // then delete the controller. A later capture discovers it afresh if it
        // survives. Original A/B controls remain image-owned throughout.
        auto controls=controls_;
        for(std::size_t i=0;i<execution_original_controls_;++i) {
            auto& c=controls[i];
            if(c.child_weak<0 || c.weak<=c.child_weak)return Fail("execution_child_parent_retirement");
            c.weak-=c.child_weak;c.child_weak=0;
        }
        if(!ReplayTraceReopenControls(controls,control_count_,execution_original_controls_,
            [&](const auto& control) {
                return a_!=&target && a_!=b_
                    && a_->OwnsExpired({control.address+16,control.address});
            },[&](const auto& control) {
                const auto contains=[&](const Sc6ReplayTraceState& image) {
                    return std::any_of(image.states_.begin(),image.states_.end(),[&](const auto& state) {
                        return state.ref.controller==control.address && state.ref.state==control.address+16;
                    });
                };
                return (contains(target) && contains(*b_))
                    || (!contains(target) && !contains(*b_) && a_->OwnsNativeChild({control.address+16,control.address}));
            }))return Fail("execution_image_weak_retirement");
        controls_=controls;control_count_=execution_original_controls_;
        execution_budget_+=released;a_=&target;
        for(std::size_t i=0;i<count_;++i) {
            chunks_[i].storage=nullptr;chunks_[i].charged=0;chunks_[i].a=&target.dynamic_[i];
        }
        for(std::size_t i=0;i<control_count_;++i)controls_[i].added=0;
        if(fresh_completion_)fresh_submission_floor_=fresh_completion_->submitted_serial();
        for(std::size_t i=0;i<fresh_count_;++i)fresh_children_[i]->execution_settled=false;
        execution_settled_=false;return Status::success();
    }
    Status Publish() {
        if(!ready_ || published_ || thread_!=GetCurrentThreadId()) return Fail("storage_phase");
        auto status=PrepareInitial();if(!status.ok()) return status;
        // Scheduler publication precedes trace-header publication. The private
        // child keeps its native component lifetime/registration bookkeeping;
        // only its audited primary tick is excluded. Secondary prefixes remain
        // guarded by the B image. No trace child may run after this handoff.
        if(private_children_.count) {
            status=VisitPrivateMeshOwners(nullptr,[](void*,std::uintptr_t mesh) {
                return !(At<unsigned char>(reinterpret_cast<void*>(mesh),0x11c)&0x40);
            });
            if(!status.ok())return status;
        }
        if(!ControlsMatch()) return Fail("weak_ownership");
        for(std::size_t i=0;i<count_;++i) {
            const auto& c=chunks_[i];
            if(!WritableRange(c.a->address,c.a->header_size())
                || std::memcmp(c.b->address,c.b->header.data(),c.b->header_size())) return Fail("B_storage_changed");
        }
        published_=true; // Undo owns even a partial first header write.
        for(std::size_t i=0;i<fresh_count_;++i) {
            // Transfer the factory's single strong owner. No increment, extra
            // pin, destructor, Start call or logical lifecycle event occurs.
            fresh_children_[i]->strong_phase=FreshChild::StrongPhase::Publishing;
            fresh_children_[i]->strong.count=0;
        }
        for(std::size_t i=0;i<count_;++i) {
            const auto& c=chunks_[i];
            if(!Copy(c.a->address,c.installed.data(),c.a->header_size())) return Fail("storage_publication");
        }
        status=a_->Install();if(!status.ok()) return status;
        status=ValidatePublished();
        if(status.ok())for(std::size_t i=0;i<fresh_count_;++i)fresh_children_[i]->strong_phase=FreshChild::StrongPhase::Published;
        return status;
    }
    Status Undo() {
        if(!ready_ && !published_ && !count_ && !control_count_) return Status::success();
        if(thread_!=GetCurrentThreadId()) return Fail("storage_thread");
        if(!published_) return ReleasePrivate()?Status::success():Fail("private_retirement");
        auto status=b_->ValidateBindings();if(!status.ok()) return status;
        if(executing_ && !execution_undo_started_ && !ValidatePublished().ok())return Fail("undo_execution_unsettled");
        if(!ControlsMatch() || !PrivateBMatches()) return Fail("weak_or_private_B_undo_ownership");
        if(!executing_)for(std::size_t i=0;i<fresh_count_;++i)
            if(!FreshOwner(*fresh_children_[i],true))return Fail("fresh_child_undo_owner");
        if(executing_)for(std::size_t i=0;i<fresh_count_;++i)
            if(!fresh_children_[i]->execution_settled || !fresh_children_[i]->native_dead)return Fail("fresh_child_undo_death_unsettled");
        for(std::size_t i=0;i<count_;++i) {
            const auto& c=chunks_[i];
            if(!WritableRange(c.b->address,c.b->header_size())) return Fail("B_storage_not_writable");
        }
        if(executing_)for(std::size_t i=0;i<count_;++i)
            if(chunks_[i].a->kind==DynamicImage::Kind::ChildStrongReferences
                && At<int>(chunks_[i].a->address,8))return Fail("undo_live_child_ownership");
        if(executing_)execution_undo_started_=true;
        for(std::size_t i=0;i<count_;++i) {
            const auto& c=chunks_[i];
            if(!Copy(c.b->address,c.b->header.data(),c.b->header_size())) return Fail("B_storage_publication");
        }
        status=b_->Install();if(!status.ok()) return status;
        if(!executing_)for(std::size_t i=0;i<fresh_count_;++i) {
            fresh_children_[i]->strong.count=1;
            fresh_children_[i]->strong_phase=FreshChild::StrongPhase::Private;
        }
        published_=false;
        const auto executed=executing_;
        if(!ReleasePrivate()) return Fail("private_retirement");
        status=b_->ValidateValues();if(!status.ok())return status;
        if(executed)for(std::size_t i=0;i<fresh_count_;++i) {
            auto& child=*fresh_children_[i];
            child.execution_outcome=FreshChild::ExecutionOutcome::Recovered;
        }
        return Status::success();
    }
    Status ValidateCommit() const {
        if(fresh_count_ && (!executing_ || !execution_settled_))return Fail("fresh_child_execution_contract");
        if(private_children_.count && (!executing_ || !execution_settled_))return Fail("private_child_commit_execution_required");
        auto status=ValidatePublished();if(!status.ok()) return status;
        for(std::size_t i=0;i<fresh_count_;++i) {
            const auto& child=*fresh_children_[i];
            if(!child.execution_settled || !child.identity_retained || child.execution_outcome!=FreshChild::ExecutionOutcome::None
                || child.strong_phase!=FreshChild::StrongPhase::Published || child.strong.count
                || child.native_dead!=(At<int>(child.state.ref.controller,8)==0))return Fail("fresh_child_commit_outcome");
            if(child.native_dead) {
                if(Live(child.state.actor) || Live(child.state.mesh))return Fail("fresh_child_commit_death");
            } else if(!child.lease.Validate().ok() || !a_->OwnsNativeChild(child.state.ref))return Fail("fresh_child_commit_owner");
        }
        if(private_children_.count) {
            status=b_->PrepareExecution(*a_,*historical_a_,PrivateChildren());if(!status.ok())return status;
        }
        // A live strong pool or an image-owned expired weak lease retains each
        // controller. Validate all counts before retiring displaced B references.
        for(std::size_t i=0;i<control_count_;++i) {
            const auto& c=controls_[i];
            int parent_releases{};
            for(std::size_t j=0;j<private_children_.count;++j)
                parent_releases+=At<Ref>(private_children_.children[j].ref.state,0xa0).controller==c.address;
            if(c.weak+c.added-c.removed-parent_releases<1) return Fail("weak_commit_count");
        }
        for(std::size_t j=0;j<private_children_.count;++j) {
            const auto parent=At<Ref>(private_children_.children[j].ref.state,0xa0);
            if(parent.controller && std::none_of(controls_.begin(),controls_.begin()+control_count_,[&](const auto& c) {
                return c.address==parent.controller;
            }))return Fail("private_child_commit_parent_owner");
        }
        return Status::success();
    }
    Status Commit() {
        auto status=ValidateCommit();if(!status.ok())return status;
        if(private_children_.count) {
            // Enclosing seek has crossed its irreversible commit boundary.
            // Native removal consumes B-only weak/strong ownership, including
            // cached-parent release. An uncertain native step remains poisoned
            // and retains all backing for the host's final GPU retirement.
            status=b_->RetirePrivateForCommit(*a_,private_children_);if(!status.ok())return status;
        }
        for(std::size_t i=0;i<control_count_;++i) {
            auto& c=controls_[i];
            if(!PrivateChildControl(c.address))At<int>(c.address,12)-=c.removed;
            c.added=0; // A references now belong to the native published array.
        }
        for(std::size_t i=0;i<count_;++i) {
            auto& c=chunks_[i];
            auto* old=At<void*>(const_cast<std::byte*>(c.b->header.data()),0);
            c.storage=nullptr; // Transfer A before retiring B; never double free.
            if(old) reinterpret_cast<void(*)(void*)>(base_+0xd46a00)(old);
        }
        for(std::size_t i=0;i<fresh_count_;++i)fresh_children_[i]->execution_outcome=FreshChild::ExecutionOutcome::Committed;
        count_=control_count_=0;published_=ready_=false;
        executing_=execution_settled_=execution_undo_started_=false;execution_budget_=0;
        return Status::success();
    }
};

inline Status Sc6ReplayTraceState::PrepareStorage(const Sc6ReplayTraceState& current,
    std::size_t budget,Prepared& output,const Sc6ReplayVfxState::ParticleBirthSet* private_child_owners,
    const Sc6ReplayTraceState* survivors,std::span<FreshChild* const> fresh) const {
    if(validation_only_ || current.validation_only_)return Fail("validation_view_storage");
    if(output.ready_ || output.published_ || output.count_ || output.private_children_.count || output.fresh_count_) return Fail("storage_reuse");
    if(bool(survivors)!=!fresh.empty() || fresh.size()>output.fresh_children_.size())return Fail("fresh_child_inventory");
    output.a_=this;output.historical_a_=survivors?survivors:this;output.b_=&current;output.base_=base_;output.thread_=thread_;
    for(auto* child:fresh) {
        if(!child)return Fail("fresh_child_null_owner");
        output.fresh_children_[output.fresh_count_++]=child;
    }
    const bool private_children=current.HasAddedStates(*output.historical_a_);
    if(private_children && !private_child_owners)return Fail("historical_child_publication");
    auto status=fresh.empty()?(private_children?PrepareHistoricalChildren(current):Prepare(current)):output.PrepareInitial();if(!status.ok()) return status;
    if(fresh.empty() && private_children) {
        status=ValidateMaterialPublication(current);if(!status.ok())return status;
    }
    // Nonempty historical strong pools need their own reversible publication
    // contract. C capture/adoption does not grant that initial admission.
    for(std::size_t i=0;fresh.empty() && i<dynamic_count_;++i)
        if(dynamic_[i].kind==DynamicImage::Kind::ChildStrongReferences
            && (At<int>(const_cast<std::byte*>(dynamic_[i].header.data()),8)
                || (!private_children && At<int>(const_cast<std::byte*>(current.dynamic_[i].header.data()),8))))return Fail("historical_child_publication");
    if(sizeof(output)>budget) return Status::failure(FailureCode::CapacityExceeded);
    if(private_children) {
        status=current.PrepareHistoricalRetirement(*output.historical_a_,*private_child_owners,output.private_children_);
        if(!status.ok())return status;
    }
    const auto reject=[&](const char* reason) {
        return output.ReleasePrivate()?Fail(reason):Fail("private_retirement");
    };
    for(std::size_t i=0;i<output.fresh_count_;++i) {
        const auto& child=*output.fresh_children_[i];
        if(!output.Track(child.state.ref))return reject("fresh_child_weak_identity");
        if(child.cached_parent.controller && !output.Track(child.cached_parent))return reject("fresh_child_parent_weak_identity");
    }
    // Allocation aliases would make private-B retirement unsafe. Native arrays
    // own their individual backing; reject a departure from that contract.
    for(std::size_t i=0;i<dynamic_count_;++i) {
        const auto& b=current.dynamic_[i];const auto array=At<Array>(const_cast<std::byte*>(b.header.data()),0);
        if(!array.data) continue;
        for(std::size_t j=0;j<i;++j)
            if(At<void*>(const_cast<std::byte*>(current.dynamic_[j].header.data()),0)==array.data) return reject("dynamic_alias");
        const auto overlaps=[&](const auto& images) {
            const auto begin=reinterpret_cast<std::uintptr_t>(array.data),end=begin+b.bytes.size();
            return std::any_of(images.begin(),images.end(),[&](const auto& image) {
                const auto start=reinterpret_cast<std::uintptr_t>(image.address);
                return start<end && begin<start+image.bytes.size();
            });
        };
        if(overlaps(current.bindings_) || overlaps(current.values_)) return reject("dynamic_alias");
    }
    for(std::size_t i=0;i<dynamic_count_;++i) {
        const auto& a=dynamic_[i];const auto& b=current.dynamic_[i];
        const auto aa=At<Array>(const_cast<std::byte*>(a.header.data()),0);
        const auto bb=At<Array>(const_cast<std::byte*>(b.header.data()),0);
        // Even an identical container needs independent A storage. Sharing it
        // with retained B is only safe while publication remains held: native
        // resimulation can append, erase, or reallocate it before recovery.
        auto& chunk=output.chunks_[output.count_++];chunk.a=&a;chunk.b=&b;chunk.installed=a.header;chunk.displaced=b.header;
        if(private_children) {
            if(b.weak()) {
                if(output.private_children_.private_weak_count==output.private_children_.private_weak_headers.size())return reject("private_child_weak_capacity");
                output.private_children_.private_weak_headers[output.private_children_.private_weak_count++]=chunk.displaced.data();
            }
            if(b.kind==DynamicImage::Kind::ChildStrongReferences)
                for(std::size_t j=0;j<output.private_children_.count;++j)
                    if(b.address==reinterpret_cast<std::byte*>(output.private_children_.roots[j])+0x428)
                        output.private_children_.private_strong_headers[j]=chunk.displaced.data();
        }
        const auto quantize=reinterpret_cast<std::size_t(*)(std::size_t,unsigned)>(base_+0xd50dc0);
        chunk.charged=a.bytes.empty()?0:quantize(a.bytes.size(),0);
        chunk.old_charged=b.bytes.empty()?0:quantize(b.bytes.size(),0);
        if(chunk.charged<a.bytes.size() || chunk.old_charged<b.bytes.size() || output.owned_bytes()>budget) return reject("storage_budget");
        if(a.weak()) {
            for(int n=0;n<bb.count;++n) {
                const auto ref=At<Ref>(const_cast<std::byte*>(b.bytes.data()),std::size_t(n)*16);
                auto* control=output.Track(ref);if(!control) return reject("B_weak_owner");
                ++control->removed;
            }
        }
        if(!a.bytes.empty()) {
            chunk.storage=reinterpret_cast<void*(*)(std::size_t)>(base_+0x4a61c0)(a.bytes.size());
            if(!chunk.storage) return reject("storage_allocation");
            if(!Prepared::Copy(chunk.storage,a.bytes.data(),a.bytes.size())) return reject("storage_copy");
        }
        std::memcpy(chunk.installed.data(),&chunk.storage,sizeof(chunk.storage));
        if(a.weak()) for(int n=0;n<aa.count;++n) {
            const auto ref=At<Ref>(const_cast<std::byte*>(a.bytes.data()),std::size_t(n)*16);
            auto* control=output.Track(ref);if(!control || At<int>(ref.controller,12)==INT_MAX) return reject("A_weak_owner");
            ++At<int>(ref.controller,12);++control->added;
        }
    }
    if(!output.ControlsMatch()) return reject("weak_preparation");
    if(private_children) {
        if(output.private_children_.private_weak_count!=current.roots_.size()*3)return reject("private_child_weak_coverage");
        for(std::size_t i=0;i<output.private_children_.count;++i)
            if(!output.private_children_.private_strong_headers[i])return reject("private_child_strong_header");
        output.private_children_.storage=Retirement::Storage::Private;
    }
    output.ready_=true;
    return Status::success();
}
}
