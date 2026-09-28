    // Native MCP evidence: 140400590 -> collection+78; 141D38300 uses
    // collection+40/+50/+54/+64, entry stride40, storage+20, enabled+30.
    // Final tables14326BFE8/14374B760: byte-bound/one-argument weak UObject.
    // Constructors1403A3B70/14043D210 prove the same allocation and identity
    // fields; 1403EF060 does NOT consume +18 for the one-argument family.
    // No virtuals are called to observe either layout.
    static bool OneArgLayoutMatches() noexcept {
        // Read-only signature checks, not additional hooks. The image table
        // and checked executor must agree with the independently traced ABI.
        constexpr std::array<std::uintptr_t,14> slots{0x10acd10,0x549650,0x2fc0b0,0x549650,
            0x301490,0x3f18b0,0x2f08bb0,0x3c3b60,0x3f0670,0x8955b0,
            0x17dfea0,0x1e33070,0x3bbce0,0x3ef060};
        constexpr std::array<unsigned char,48> signature{72,137,92,36,8,72,137,116,36,16,87,72,131,236,32,72,139,217,72,139,242,72,131,193,8,232,114,51,185,0,72,133,192,116,40,72,141,75,8,232,100,51,185,0,76,139,67,16};
        std::array<std::uintptr_t,14> table{};std::array<unsigned char,48> bytes{};
        if(!Read(base_+0x374b760,table) || !Read(base_+0x3ef060,bytes) || bytes!=signature)return false;
        for(unsigned i=0;i<slots.size();++i)if(table[i]!=base_+slots[i])return false;
        return true;
    }
    struct HubRow {
        std::uintptr_t entry{},storage{},table{},callable{};
        std::int32_t enabled{},weak_index{-1},weak_serial{};
        unsigned char bound{};
        std::uint64_t handle{},producer{};
        Identity receiver{};
        const char* state{"unreadable"};
    };
    struct HubWriter {
        std::uintptr_t rva{},receiver{},callable{},out{},caller{},result{};
        std::uint64_t entry_ms{},return_ms{},handle{};
        std::array<std::uint64_t,4> scopes{};
        std::array<unsigned,4> kinds{};
        Identity receiver_identity{};
        DWORD thread{};
        unsigned bound{},scope_count{},parent{};
        bool returned{},handle_read{},scopes_truncated{},same_thread{},active{};
    };
    struct HubWitness {
        std::uintptr_t dispatcher{},collection{},storage{};
        std::uint64_t id{},scope_id{},entry_ms{},return_ms{};
        DWORD thread{};
        unsigned scope_kind{},row_count{},writer_count{};
        std::int32_t count{},capacity{},recursion{};
        bool active{},selected{},returned{},header_read{},snapshot_complete{},truncated{},overlap{},writer_overflow{},pending_at_close{};
        std::array<HubRow,2> rows{};
        std::array<HubWriter,16> writers{};
    };
    inline static HubWitness hub_{};
    static void SnapshotHub() noexcept {
        auto& h=hub_;
        h.header_read=Read(h.collection+0x40,h.storage) && Read(h.collection+0x50,h.count)
            && Read(h.collection+0x54,h.capacity) && Read(h.collection+0x64,h.recursion);
        if(!h.header_read || h.count<0 || h.capacity<h.count || h.recursion<0)return;
        h.truncated=h.count>2;
        const auto storage=h.storage?h.storage:h.collection;
        // One inline entry fits the collection's 0x40-byte inline area.
        if((!h.storage && h.count>1) || storage>~std::uintptr_t{}-0x80)return;
        bool complete=h.count==2 && !h.truncated;
        for(unsigned i=0;i<2 && i<static_cast<unsigned>(h.count);++i) {
            auto& row=h.rows[h.row_count++];row.entry=storage+i*0x40;
            std::uintptr_t heap{},heap_again{};std::int32_t enabled_again{};
            if(!Read(row.entry+0x20,heap) || !Read(row.entry+0x30,row.enabled)) {complete=false;continue;}
            row.storage=heap?heap:row.entry;
            if(!row.enabled) {
                row.state="disabled";
                if(!Read(row.entry+0x20,heap_again) || !Read(row.entry+0x30,enabled_again)
                    || heap_again!=heap || enabled_again!=row.enabled){row.state="unstable";complete=false;}
                continue;
            }
            if(!Read(row.storage,row.table)){complete=false;continue;}
            const auto expected_table=row.table;
            const bool one_arg=row.table==base_+0x374b760;
            if((row.table!=base_+0x326bfe8 && !one_arg) || (one_arg && !OneArgLayoutMatches())) {
                row.state="unknown_layout";complete=false;continue;
            }
            // This family allocates THREE 16-byte elements, so it cannot fit
            // the two-element inline area. Never read a handle over entry+20.
            if(row.enabled!=3 || !heap || row.storage>~std::uintptr_t{}-0x30) {
                row.state="unknown_storage";complete=false;continue;
            }
            std::array<unsigned char,0x30> bytes{},again{};
            if(!Read(row.storage,bytes)){complete=false;continue;}
            std::memcpy(&row.table,bytes.data(),8);
            std::memcpy(&row.weak_index,bytes.data()+8,4);std::memcpy(&row.weak_serial,bytes.data()+12,4);
            std::memcpy(&row.callable,bytes.data()+16,8);
            if(!one_arg)row.bound=bytes[24]; // +18 is unknown storage for one_arg.
            std::memcpy(&row.handle,bytes.data()+32,8);
            std::uintptr_t target{};std::int32_t serial{};
            if(Indexed(row.weak_index,target,serial) && serial==row.weak_serial && serial>0)
                row.receiver=ObserveIdentity(target);
            // Recheck selectors before recopying callback storage. Failed or
            // changed selectors terminate observation; never chase a new pointer.
            if(!Read(row.entry+0x20,heap_again) || !Read(row.entry+0x30,enabled_again)
                || heap_again!=heap || enabled_again!=row.enabled
                || !Read(row.storage,again) || bytes!=again) {
                row.state="unstable";complete=false;continue;
            }
            if(row.table!=expected_table || !row.callable || !row.handle
                || !row.receiver.valid || row.receiver.index!=row.weak_index || row.receiver.serial!=row.weak_serial) {
                row.state="unknown_receiver";complete=false;continue;
            }
            row.state="known";
        }
        std::uintptr_t after{};std::int32_t count{},capacity{},recursion{};
        h.snapshot_complete=complete && Read(h.collection+0x40,after) && after==h.storage
            && Read(h.collection+0x50,count) && count==h.count
            && Read(h.collection+0x54,capacity) && capacity==h.capacity
            && Read(h.collection+0x64,recursion) && recursion==h.recursion;
        // Guarded copies and matching bytes are not writer exclusion or an ABA proof.
    }
    static bool HubBefore(void* dispatcher) noexcept {
        if(!accepting_.load(std::memory_order_acquire))return false;
        AcquireSRWLockExclusive(&lock_);
        bool admitted=false;
        auto* current=ExecutionScope::current_;
        auto* outer=current?current->previous_:nullptr;
        bool writer_active=false;for(const auto& w:hub_.writers)writer_active|=w.active;
        if(hub_.active || writer_active) {hub_.overlap=true;hub_.snapshot_complete=false;}
        else if(!phase_closed_ && !hub_.selected && current && current->kind_==4 && outer
            && (outer->kind_==2 || outer->kind_==3)) {
            hub_={};auto& h=hub_;h.dispatcher=reinterpret_cast<std::uintptr_t>(dispatcher);
            h.id=current->id_;h.scope_id=outer->id_;h.scope_kind=outer->kind_;
            h.thread=GetCurrentThreadId();h.entry_ms=GetTickCount64();h.active=true;admitted=true;
            std::uintptr_t table{};
            if(h.dispatcher<=~std::uintptr_t{}-0xe0 && Read(h.dispatcher,table) && table==base_+0x337c4f8) {
                h.collection=h.dispatcher+0x78;SnapshotHub();
            }
        }
        ReleaseSRWLockExclusive(&lock_);return admitted;
    }
    static void HubCorrelate(Producer& p) noexcept {
        if(!hub_.active || p.rva!=rvas_[4] || p.thread!=hub_.thread)return;
        // Require the exact enclosing hub scope, not same-time/thread proximity.
        bool enclosed=false;for(unsigned i=0;i<p.owner_count;++i)
            if(p.owners[i].id==hub_.id && p.owners[i].kind==4)enclosed=true;
        if(!enclosed)return;
        unsigned matches=0;
        for(auto& r:hub_.rows)if(r.enabled && r.table==base_+0x326bfe8 && r.callable==base_+0x3c5250 && r.bound==p.bound
            && SameIdentity(r.receiver,p.object)) {
                if(r.producer)hub_.snapshot_complete=false;
                else r.producer=p.id;
                ++matches;
            }
        if(matches!=1)hub_.snapshot_complete=false;
    }
    static void HubAfter(bool admitted) noexcept {
        if(!admitted)return;
        AcquireSRWLockExclusive(&lock_);
        hub_.active=false;hub_.returned=true;hub_.return_ms=GetTickCount64();
        // No callback, entry, collection, receiver or task pointer is read here.
        if(hub_.selected)Persist();
        ReleaseSRWLockExclusive(&lock_);
    }
    static unsigned HubWriterBefore(unsigned site,void* collection,void* receiver,void* out,
        std::uintptr_t callable,unsigned bound,void* caller) noexcept {
        if(!enabled_.load(std::memory_order_acquire))return 16;
        AcquireSRWLockExclusive(&lock_);unsigned token=16;
        // Continue pairing the selected broadcast even if ClosePhase seals
        // ordinary admission inside it. Never read an unrelated collection.
        if(hub_.active && reinterpret_cast<std::uintptr_t>(collection)==hub_.collection) {
            if(hub_.writer_count==16)hub_.writer_overflow=true;
            else {
                token=hub_.writer_count++;auto& w=hub_.writers[token];
                w.rva=rvas_[site];w.receiver=reinterpret_cast<std::uintptr_t>(receiver);
                w.receiver_identity=ObserveIdentity(w.receiver);
                w.out=reinterpret_cast<std::uintptr_t>(out);w.callable=callable;w.bound=bound;
                w.caller=reinterpret_cast<std::uintptr_t>(caller);w.thread=GetCurrentThreadId();
                w.same_thread=w.thread==hub_.thread;w.entry_ms=GetTickCount64();w.active=true;
                for(unsigned i=0;i<token;++i)if(hub_.writers[i].active && hub_.writers[i].thread==w.thread)w.parent=i+1;
                auto* scope=ExecutionScope::current_;
                for(;scope && w.scope_count<4;scope=scope->previous_) {
                    w.scopes[w.scope_count]=scope->id_;w.kinds[w.scope_count++]=scope->kind_;
                }
                w.scopes_truncated=scope!=nullptr;
            }
            // Mutation falsifies the unchanged-two-row occurrence. The output
            // still retains paired native results; it never supplies receiver undo.
            hub_.snapshot_complete=false;if(hub_.selected)Persist();
        }
        ReleaseSRWLockExclusive(&lock_);return token;
    }
    static void HubWriterAfter(unsigned token,std::uintptr_t result) noexcept {
        if(token==16)return;
        AcquireSRWLockExclusive(&lock_);auto& w=hub_.writers[token];
        w.result=result;w.returned=true;w.active=false;w.return_ms=GetTickCount64();
        // Registration's caller-owned output is the sole return-side read.
        // Removal's RAX is retained uninterpreted: native has no boolean outcome.
        if(w.rva==rvas_[8])w.handle_read=Read(w.out,w.handle);
        if(hub_.selected)Persist();ReleaseSRWLockExclusive(&lock_);
    }
    __declspec(noinline) static void* HubRegister(void* collection,void* out,void* receiver,void* callable,unsigned char bound) {
        const auto error=GetLastError();const auto c18=C18WriterBefore(8,collection,reinterpret_cast<std::uintptr_t>(callable),_ReturnAddress());
        const auto token=HubWriterBefore(8,collection,receiver,out,
            reinterpret_cast<std::uintptr_t>(callable),bound,_ReturnAddress());SetLastError(error);
        auto* result=reinterpret_cast<void*(*)(void*,void*,void*,void*,unsigned char)>(originals_[8])(collection,out,receiver,callable,bound);
        const auto returned_error=GetLastError();C18WriterAfter(c18,reinterpret_cast<std::uintptr_t>(result));
        HubWriterAfter(token,reinterpret_cast<std::uintptr_t>(result));SetLastError(returned_error);return result;
    }
    __declspec(noinline) static std::uintptr_t HubRemove(void* collection,void* receiver) {
        const auto error=GetLastError();const auto c18=C18WriterBefore(9,collection,reinterpret_cast<std::uintptr_t>(receiver),_ReturnAddress());
        const auto token=HubWriterBefore(9,collection,receiver,nullptr,0,0,_ReturnAddress());SetLastError(error);
        const auto result=reinterpret_cast<std::uintptr_t(*)(void*,void*)>(originals_[9])(collection,receiver);
        const auto returned_error=GetLastError();C18WriterAfter(c18,result);HubWriterAfter(token,result);SetLastError(returned_error);return result;
    }
    static void HubJson() noexcept {
        Text(",\"disable_hub_witness_version\":1,\"disable_hub_onearg_layout_version\":1,\"disable_hub_witness\":");
        if(!hub_.selected){Text("null");return;}
        const auto& h=hub_;
        Text("{\"snapshot_boundary\":\"before_original_140400590\",\"selected\":true,\"writer_coverage_proven\":false,\"receiver_undo_proven\":false,\"receiver_b_participants\":\"unclassified\",\"hypothesis_proven\":false");
        Text(",\"selection\":\"first_custom_manager_hub_with_selected_disable_producer\",\"id\":");Number(h.id);
        Text(",\"image_base\":");Number(base_);
        Text(",\"writer_sites\":[");Number(rvas_[8]);Text(",");Number(rvas_[9]);Text("]");
        Text(",\"dispatcher\":");Number(h.dispatcher);Text(",\"collection\":");Number(h.collection);
        Text(",\"storage\":");Number(h.storage);Text(",\"count\":");Signed(h.count);
        Text(",\"capacity\":");Signed(h.capacity);Text(",\"recursion\":");Signed(h.recursion);
        Text(",\"header_read\":");Flag(h.header_read);Text(",\"snapshot_complete\":");Flag(h.snapshot_complete);
        Text(",\"truncated\":");Flag(h.truncated);Text(",\"overlap\":");Flag(h.overlap);
        Text(",\"writer_overflow\":");Flag(h.writer_overflow);Text(",\"returned\":");Flag(h.returned);
        Text(",\"pending_at_close\":");Flag(h.pending_at_close);
        Text(",\"thread\":");Number(h.thread);Text(",\"scope_id\":");Number(h.scope_id);Text(",\"scope_kind\":");Number(h.scope_kind);
        Text(",\"entry_ms\":");Number(h.entry_ms);Text(",\"return_ms\":");Number(h.return_ms);Text(",\"rows\":[");
        for(unsigned i=0;i<h.row_count;++i) {
            const auto& r=h.rows[i];if(i)Text(",");Text("{\"entry\":");Number(r.entry);
            Text(",\"storage\":");Number(r.storage);Text(",\"enabled\":");Signed(r.enabled);Text(",\"table\":");Number(r.table);
            Text(",\"weak_index\":");Signed(r.weak_index);Text(",\"weak_serial\":");Signed(r.weak_serial);
            Text(",\"receiver\":");IdentityJson(r.receiver);Text(",\"callable\":");Number(r.callable);
            Text(",\"bound_byte\":");if(r.table==base_+0x374b760)Text("null");else Number(r.bound);
            Text(",\"handle\":");Number(r.handle);Text(",\"producer_id\":");Number(r.producer);
            Text(",\"state\":\"");Text(r.state);Text("\"}");
        }
        Text("],\"writers\":[");
        for(unsigned i=0;i<h.writer_count;++i) {
            const auto& w=h.writers[i];if(i)Text(",");Text("{\"pair\":");Number(i+1);Text(",\"parent_pair\":");Number(w.parent);
            Text(",\"entry_rva\":");Number(w.rva);Text(",\"caller\":");Number(w.caller);Text(",\"receiver\":");Number(w.receiver);
            Text(",\"receiver_identity\":");IdentityJson(w.receiver_identity);
            Text(",\"callable\":");Number(w.callable);Text(",\"bound_byte\":");Number(w.bound);Text(",\"thread\":");Number(w.thread);
            Text(",\"same_thread\":");Flag(w.same_thread);Text(",\"entry_ms\":");Number(w.entry_ms);Text(",\"return_ms\":");Number(w.return_ms);
            Text(",\"returned\":");Flag(w.returned);Text(",\"result_raw\":");Number(w.result);Text(",\"output_address\":");Number(w.out);
            Text(",\"handle_read\":");Flag(w.handle_read);Text(",\"handle\":");Number(w.handle);Text(",\"scopes_truncated\":");Flag(w.scopes_truncated);
            Text(",\"scopes\":[");for(unsigned j=0;j<w.scope_count;++j){if(j)Text(",");Text("[");Number(w.scopes[j]);Text(",");Number(w.kinds[j]);Text("]");}Text("]}");
        }
        Text("]}");
    }
