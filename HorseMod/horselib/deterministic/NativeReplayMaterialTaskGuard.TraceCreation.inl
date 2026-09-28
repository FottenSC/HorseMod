// Included inside the sole process-pinned material guard. Optional observation
// of one selected ordinary-forward Start; never acquisition or admission.
public:
    static constexpr unsigned CreationCapacity=8,CreationFamilyLimit=4;
    struct CreationCall {
        std::uint64_t entry{},end{};
        std::uintptr_t object{},argument{},output{},caller{},result{},state{},controller{};
        CreationIdentity identity{};
        DWORD thread{};
        int setting{};
        unsigned family{},parent{};
        unsigned char kind{};
        bool returned{},pair_stable{};
    };
    struct CreationObservation {
        std::uint64_t start{},sequence{},foreign_entries{},omitted{};
        std::array<std::uint64_t,2> attempted{};
        std::uintptr_t component{};
        DWORD thread{};
        unsigned count{},pending{};
        bool selected{},active{},ended{},ready{},invalidated{},reentry{},preexisting{};
        std::array<CreationCall,CreationCapacity> calls{};
        bool Complete() const noexcept {
            return selected && ready && ended && !active && !invalidated && !preexisting
                && !pending && !omitted && !foreign_entries;
        }
    };
    static void CopyCreationObservation(CreationObservation& out) noexcept {
        const auto error=GetLastError();AcquireSRWLockShared(&lock_);out=creation_;
        ReleaseSRWLockShared(&lock_);SetLastError(error);
    }
private:
    struct CreationToken {unsigned family{},slot=CreationCapacity;};
    inline static CreationObservation creation_{};
    inline static std::array<std::uint64_t,2> creation_running_{};
    inline static bool creation_enabled_{},creation_ready_{};
    static_assert(sizeof(CreationObservation)*2<8192);

    static void InstallCreationObservation(std::uintptr_t base) noexcept {
        const auto error=GetLastError();
        try {
            // Pin at the existing verified initial-constructor boundary only.
            // Default play leaves both entries untouched. A particle diagnostic
            // configured after startup has no coverage and cannot install late.
            ReplayDiagnosticTrace startup;
            if(!startup.LoadQualification() || !(startup.mask&ReplayDiagnosticTrace::Particles)) {
                SetLastError(error);return;
            }
            creation_enabled_=true;
            // Read-only PE disassembly of f8904e4b04bca3b47bc52a683f6190365d2eb89ee8f44f8072759e9c5e04a553:
            // factory copied span=8: MOV [RSP+10],RDX; PUSH RBP/RBX/RSI.
            // wrapper copied span=10: MOV [RSP+8],RBX; MOV [RSP+10],RSI.
            // No relative instructions in either span. Exact VALLOC2 only;
            // MaterialDetour preflights instruction rounding before patching.
            // The direct private-child factory's raw signature gate remains
            // unchanged: it rejects a diagnostically patched entry. This
            // diagnostic process does not admit historical construction.
            constexpr std::array<unsigned char,30> factory{
                0x48,0x89,0x54,0x24,0x10,0x55,0x53,0x56,0x57,0x41,0x55,0x41,0x56,0x41,0x57,
                0x48,0x8d,0xac,0x24,0x30,0xff,0xff,0xff,0x48,0x81,0xec,0xd0,0x01,0x00,0x00};
            constexpr std::array<unsigned char,20> construct{
                0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x4c,0x89,0x44,0x24,0x18,
                0x57,0x48,0x83,0xec,0x60};
            constexpr std::array<unsigned char,5> start_call{0xe8,0x81,0x8b,0xff,0xff};
            constexpr std::array<unsigned char,5> construct_call{0xe8,0xfe,0x6f,0xff,0xff};
            std::array<unsigned char,30> f{};std::array<unsigned char,20> w{};
            std::array<unsigned char,5> a{},b{};
            if(!StartupDomain(base) || !Read(base+0x8d1c00,f) || f!=factory
                || !Read(base+0x8c8f40,w) || w!=construct
                || !Read(base+0x8d907a,a) || a!=start_call
                || !Read(base+0x8d1f3d,b) || b!=construct_call) {SetLastError(error);return;}
            const std::array<std::uintptr_t,2> targets{base+0x8d1c00,base+0x8c8f40};
            const std::array<std::uintptr_t,2> wrappers{
                reinterpret_cast<std::uintptr_t>(&TraceFactory),reinterpret_cast<std::uintptr_t>(&TraceConstruct)};
            for(unsigned i=0;i<2;++i) {
                auto* hook=new MaterialDetour(targets[i],wrappers[i],&originals_[9+i]);
                hooks_->entries[9+i].reset(hook);
                // Failure after one published entry keeps its trampoline and
                // forwarder pinned; readiness remains false, with no retry.
                if(!hook->InstallExact(targets[i],i?10:8) || !originals_[9+i]){SetLastError(error);return;}
            }
            AcquireSRWLockExclusive(&lock_);
            creation_ready_=!creation_.selected && !creation_running_[0] && !creation_running_[1] && StartupDomain(base);
            ReleaseSRWLockExclusive(&lock_);
        } catch(...) {} // Never retire a potentially published trampoline.
        SetLastError(error);
    }
    static void BeginCreationObservation(ProducerToken token,void* component) noexcept {
        if(!creation_enabled_)return;
        AcquireSRWLockExclusive(&lock_);auto& c=creation_;
        if(c.selected)c.invalidated=true;
        else {
            c.selected=c.active=true;c.ready=creation_ready_;c.start=token.generation;
            c.component=reinterpret_cast<std::uintptr_t>(component);c.thread=GetCurrentThreadId();
            c.preexisting=creation_running_[0] || creation_running_[1]
                || producers_.active[0]!=1 || producers_.active[1]!=0;
            c.invalidated=!c.start || token.slot>=ProducerCapacity || !installed_ || lost_;
        }
        ReleaseSRWLockExclusive(&lock_);
    }
    static void EndCreationObservation(ProducerToken token) noexcept {
        if(!creation_enabled_)return;
        AcquireSRWLockExclusive(&lock_);auto& c=creation_;
        c.invalidated|=!c.active || c.start!=token.generation || c.thread!=GetCurrentThreadId()
            || c.pending || creation_running_[0] || creation_running_[1];
        c.active=false;c.ended=true;ReleaseSRWLockExclusive(&lock_);
    }
    static CreationToken CreationEntry(unsigned family,void* object,void* argument,void* output,
        int setting,unsigned char kind,std::uintptr_t caller) noexcept {
        AcquireSRWLockExclusive(&lock_);auto& c=creation_;
        CreationToken token{family};++creation_running_[family];
        if(!c.active){ReleaseSRWLockExclusive(&lock_);return token;}
        ++c.attempted[family];
        if(c.thread!=GetCurrentThreadId()) {
            ++c.foreign_entries;c.invalidated=true;
            ReleaseSRWLockExclusive(&lock_);return token;
        }
        if(c.attempted[family]>CreationFamilyLimit || c.count==CreationCapacity) {
            ++c.omitted;c.invalidated=true;
            ReleaseSRWLockExclusive(&lock_);return token;
        }
        token.slot=c.count++;auto& r=c.calls[token.slot];
        r.entry=++c.sequence;r.family=family;r.object=reinterpret_cast<std::uintptr_t>(object);
        r.argument=reinterpret_cast<std::uintptr_t>(argument);r.output=reinterpret_cast<std::uintptr_t>(output);
        r.setting=setting;r.kind=kind;r.caller=caller;r.thread=GetCurrentThreadId();++c.pending;
        unsigned starts{};
        for(const auto& p:producer_records_)if(p.generation && p.family==0) {
            ++starts;
            if(p.generation!=c.start || p.thread!=c.thread)c.invalidated=true;
        }
        if(starts!=1)c.invalidated=true;
        if(!family) {
            if(caller!=base_+0x8d907f || r.object!=c.component)c.invalidated=true;
            if(creation_running_[0]!=1 || creation_running_[1])c.reentry=c.invalidated=true;
        } else {
            unsigned parents{};
            for(unsigned i=0;i<token.slot;++i)if(!c.calls[i].family && !c.calls[i].returned) {
                r.parent=i+1;++parents;
            }
            if(parents!=1 || caller!=base_+0x8d1f42)c.invalidated=true;
            if(creation_running_[0]!=1 || creation_running_[1]!=1)c.reentry=c.invalidated=true;
        }
        ReleaseSRWLockExclusive(&lock_);return token;
    }
    static void CreationReturn(CreationToken token,std::uintptr_t result) noexcept {
        AcquireSRWLockExclusive(&lock_);auto& c=creation_;
        if(token.slot<CreationCapacity) {
            auto& r=c.calls[token.slot];
            if(!c.active || r.returned || r.thread!=GetCurrentThreadId() || !c.pending)c.invalidated=true;
            else {
                r.result=result;r.returned=true;r.end=++c.sequence;--c.pending;
                if(c.ready && !c.invalidated && !c.preexisting) {
                    if(!token.family) {
                        // Read only the caller's still-live out cell immediately
                        // on native return; never borrow state/controller memory.
                        std::array<std::uintptr_t,2> pair{},again{};
                        r.pair_stable=result && result==r.output && Read(result,pair) && Read(result,again) && pair==again;
                        if(r.pair_stable){r.state=pair[0];r.controller=pair[1];}
                        else c.invalidated=true;
                    } else if(result) {
                        const auto identify=construction_diagnostic_.load(std::memory_order_acquire);
                        if(identify)identify(result,r.identity);
                    }
                }
            }
        }
        if(creation_running_[token.family])--creation_running_[token.family];else c.invalidated=true;
        ReleaseSRWLockExclusive(&lock_);
    }
    __declspec(noinline) static void* __fastcall TraceFactory(void* component,void* output,void* parts,int setting,unsigned char kind) {
        const auto incoming=GetLastError();
        const auto token=CreationEntry(0,component,parts,output,setting,kind,reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
        SetLastError(incoming);
        auto* result=reinterpret_cast<void*(__fastcall*)(void*,void*,void*,int,unsigned char)>(originals_[9])
            (component,output,parts,setting,kind);
        const auto error=GetLastError();CreationReturn(token,reinterpret_cast<std::uintptr_t>(result));
        SetLastError(error);return result;
    }
    __declspec(noinline) static void* __fastcall TraceConstruct(void* outer,void* cls,std::uint64_t name,std::uint32_t flags,
        void* object_template,bool copy_transients,void* graph) {
        const auto incoming=GetLastError();
        const auto token=CreationEntry(1,outer,cls,nullptr,0,0,reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
        SetLastError(incoming);
        auto* result=reinterpret_cast<void*(__fastcall*)(void*,void*,std::uint64_t,std::uint32_t,void*,bool,void*)>(originals_[10])
            (outer,cls,name,flags,object_template,copy_transients,graph);
        const auto error=GetLastError();CreationReturn(token,reinterpret_cast<std::uintptr_t>(result));
        SetLastError(error);return result;
    }
