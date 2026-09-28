    // Registration is already after Start's effects. Copy the existing scope's
    // pre-payload identity, never infer a lease or rewind native state here.
    // Start tail-jumps to the binder: its frame need not appear in the stack.
    struct RegistrationScope {
        std::uint64_t id{},entry_sequence{},return_sequence{},epoch{};
        std::uintptr_t task{},argument{},caller{},function{},function_table{},scheduled_task{},world{},completion{};
        Identity owner{};
        std::uint64_t registration_epoch{};
        std::int32_t registration_owner_index{-1},registration_owner_serial{};
        DWORD thread{};
        unsigned kind{},native_thread{};
        bool read{},epoch_read{},returned{},identity_matches_at_registration{};
    };
    struct RegistrationWitness {
        std::uint64_t pair{},entry_sequence{},return_sequence{};
        DWORD thread{};
        std::uintptr_t caller{};
        // Two nested tasks suffice to falsify the single serial-owner
        // hypothesis. Further ancestry remains a labelled stack prefix.
        std::array<RegistrationScope,2> scopes{};
        std::array<void*,32> ancestry{};
        unsigned scope_count{},ancestry_count{};
        bool returned{},scopes_truncated{},ancestry_at_capacity{},overlap{};
    };
    // The retained positive phase contains exactly 16 registrations. This
    // bounded prefix fits the existing reservation; any excess is explicit.
    static constexpr unsigned MaxRegistrationScopes=4,MaxRegistrations=16;
    inline static std::array<RegistrationScope,MaxRegistrationScopes> registration_scopes_{};
    inline static std::array<RegistrationWitness,MaxRegistrations> registrations_{};
    inline static unsigned registration_count_{};
    inline static std::uint64_t registration_sequence_{},registration_overflow_{};
    inline static bool registration_pending_at_close_{};
    static void ReadRegistrationTask(RegistrationScope& s) noexcept {
        std::uintptr_t table{},owner{};
        s.epoch_read=Read(base_+0x4197170,s.epoch);
        s.read=s.task && s.task<=~std::uintptr_t{}-0x48
            && Read(s.task,table) && table==base_+0x39cd9c0
            && Read(s.task+0x10,s.function) && Read(s.task+0x28,s.world)
            && Read(s.task+0x40,s.completion) && s.function && s.function<=~std::uintptr_t{}-0x58
            && Read(s.function,s.function_table)
            && (s.function_table==base_+0x381c720 || s.function_table==base_+0x3865f98)
            && Read(s.function+0x18,s.scheduled_task) && Read(s.function+0x50,owner);
        if(s.read)s.owner=ObserveIdentity(owner);
    }
    static bool RegistrationTaskMatches(const RegistrationScope& a,const RegistrationScope& b) noexcept {
        return a.read && b.read && a.owner.valid && a.owner.serial>0
            && SameIdentity(a.owner,b.owner) && a.function==b.function && a.function_table==b.function_table
            && a.scheduled_task==a.task && b.scheduled_task==a.task
            && a.world==b.world && a.completion==b.completion
            && a.argument==a.task+0x40 && a.thread==GetCurrentThreadId();
    }
    static void RegistrationScopeBoundary(ExecutionScope& scope,bool entering) noexcept {
        if(scope.kind_==4)return; // A hub is not a pre-payload task owner.
        const auto error=GetLastError();AcquireSRWLockExclusive(&lock_);
        if(entering && !phase_closed_) {
            for(unsigned i=0;i<registration_scopes_.size();++i)if(!registration_scopes_[i].id) {
                scope.observation_slot_=i;auto& s=registration_scopes_[i];s={};
                s.id=scope.id_;s.kind=scope.kind_;s.task=scope.context_;s.argument=scope.argument_;
                s.caller=scope.caller_;s.native_thread=scope.native_thread_;s.thread=GetCurrentThreadId();
                s.entry_sequence=++registration_sequence_;ReadRegistrationTask(s);
                break;
            }
            // Bounded scratch exhausted: missing entry cannot acquire ancestry
            // later. Do not store native pointers in an unbounded TLS map.
            if(scope.observation_slot_==~0u)++registration_overflow_;
        } else if(!entering && scope.observation_slot_<registration_scopes_.size()) {
            auto& s=registration_scopes_[scope.observation_slot_];
            if(s.id==scope.id_) {
                const auto sequence=++registration_sequence_;bool selected=false;
                for(unsigned i=0;i<registration_count_;++i)for(unsigned j=0;j<registrations_[i].scope_count;++j) {
                    auto& copy=registrations_[i].scopes[j];
                    if(copy.id==s.id) {copy.returned=true;copy.return_sequence=sequence;selected=true;}
                }
                s={}; // No task/event/UObject reads after native return.
                if(selected)Persist();
            }
        }
        ReleaseSRWLockExclusive(&lock_);SetLastError(error);
    }
    static void RegistrationBefore(const Record& r) noexcept {
        if(r.rva!=rvas_[0])return;
        if(registration_count_==registrations_.size()){++registration_overflow_;return;}
        auto& w=registrations_[registration_count_++];w={};w.pair=r.pair;w.caller=r.caller;w.thread=r.thread;
        w.entry_sequence=++registration_sequence_;
        w.overlap=r.active_same_thread_same_collection || r.active_other_thread_same_collection;
        auto* scope=ExecutionScope::current_;unsigned traversed{};
        for(;scope && traversed<MaxRegistrationScopes;scope=scope->previous_,++traversed) {
            if(scope->kind_==4)continue;
            if(w.scope_count==w.scopes.size()){w.scopes_truncated=true;break;}
            auto& copy=w.scopes[w.scope_count++];
            if(scope->observation_slot_<registration_scopes_.size()
                && registration_scopes_[scope->observation_slot_].id==scope->id_) {
                copy=registration_scopes_[scope->observation_slot_];
                RegistrationScope now{};now.task=copy.task;ReadRegistrationTask(now);
                copy.registration_epoch=now.epoch;
                copy.registration_owner_index=now.owner.index;copy.registration_owner_serial=now.owner.serial;
                copy.identity_matches_at_registration=RegistrationTaskMatches(copy,now);
            } else {copy.id=scope->id_;copy.kind=scope->kind_;w.scopes_truncated=true;}
        }
        if(scope)w.scopes_truncated=true;
        w.ancestry_count=CaptureStackBackTrace(0,static_cast<DWORD>(w.ancestry.size()),w.ancestry.data(),nullptr);
        w.ancestry_at_capacity=w.ancestry_count==w.ancestry.size();
    }
    static void RegistrationAfter(const Record& r) noexcept {
        if(r.rva!=rvas_[0])return;
        for(unsigned i=0;i<registration_count_;++i)if(registrations_[i].pair==r.pair) {
            auto& w=registrations_[i];w.returned=true;w.return_sequence=++registration_sequence_;break;
        }
    }
    static bool RegistrationPending() noexcept {
        for(unsigned i=0;i<registration_count_;++i) {
            if(!registrations_[i].returned || registrations_[i].scopes_truncated)return true;
            for(unsigned j=0;j<registrations_[i].scope_count;++j)if(!registrations_[i].scopes[j].returned)return true;
        }
        return false;
    }
    static const char* RegistrationReason(const RegistrationWitness& w) noexcept {
        if(w.scopes_truncated)return "scope_capacity";
        if(!w.scope_count)return "missing_scope";
        if(w.overlap || w.scope_count!=1)return "overlapping_or_nested_scope";
        const auto& s=w.scopes[0];
        if(s.kind==3)return "continuation_not_entry";
        if(!s.identity_matches_at_registration)return "identity_changed";
        if(w.ancestry_at_capacity || !w.ancestry_count)return "stack_prefix_only";
        return "correlation_only";
    }
    static void RegistrationJson() noexcept {
        Text(",\"registration_ancestry_version\":1,\"registration_scope_overflow\":");Number(registration_overflow_);
        Text(",\"registration_pending_at_close\":");Flag(registration_pending_at_close_);
        Text(",\"registration_ancestry\":[");
        for(unsigned i=0;i<registration_count_;++i) {
            const auto& w=registrations_[i];if(i)Text(",");
            Text("{\"pair\":");Number(w.pair);Text(",\"caller\":");Number(w.caller);
            Text(",\"trace_start_writer\":");Flag(w.caller==base_+0x8cdc8c);
            Text(",\"thread\":");Number(w.thread);Text(",\"entry_sequence\":");Number(w.entry_sequence);
            Text(",\"return_sequence\":");Number(w.return_sequence);Text(",\"returned\":");Flag(w.returned);
            Text(",\"ownership_proven\":false,\"before_trace_effects_proven\":false,\"reason\":\"");Text(RegistrationReason(w));
            Text("\",\"overlap\":");Flag(w.overlap);Text(",\"scopes_truncated\":");Flag(w.scopes_truncated);
            Text(",\"ancestry_at_capacity\":");Flag(w.ancestry_at_capacity);Text(",\"ancestry\":[");
            for(unsigned j=0;j<w.ancestry_count;++j){if(j)Text(",");Number(reinterpret_cast<std::uintptr_t>(w.ancestry[j]));}
            Text("],\"scopes\":[");
            for(unsigned j=0;j<w.scope_count;++j) {
                const auto& s=w.scopes[j];if(j)Text(",");
                Text("{\"id\":");Number(s.id);Text(",\"kind\":");Number(s.kind);
                Text(",\"task\":");Number(s.task);Text(",\"argument\":");Number(s.argument);
                Text(",\"caller\":");Number(s.caller);Text(",\"function\":");Number(s.function);
                Text(",\"function_table\":");Number(s.function_table);Text(",\"scheduled_task\":");Number(s.scheduled_task);
                Text(",\"world\":");Number(s.world);Text(",\"completion\":");Number(s.completion);
                Text(",\"owner\":");IdentityJson(s.owner);Text(",\"thread\":");Number(s.thread);
                Text(",\"native_thread\":");Number(s.native_thread);Text(",\"entry_sequence\":");Number(s.entry_sequence);
                Text(",\"return_sequence\":");Number(s.return_sequence);Text(",\"epoch\":");Number(s.epoch);
                Text(",\"registration_epoch\":");Number(s.registration_epoch);
                Text(",\"registration_owner_index\":");Signed(s.registration_owner_index);
                Text(",\"registration_owner_serial\":");Signed(s.registration_owner_serial);
                Text(",\"read\":");Flag(s.read);Text(",\"epoch_read\":");Flag(s.epoch_read);
                Text(",\"returned\":");Flag(s.returned);Text(",\"identity_matches_at_registration\":");Flag(s.identity_matches_at_registration);Text("}");
            }
            Text("]}");
        }
        Text("]");
    }
