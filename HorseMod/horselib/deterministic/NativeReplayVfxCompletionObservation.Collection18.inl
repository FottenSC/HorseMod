    // Known native mutation-entry receipts, NOT allocation generations. Native
    // may take a no-op branch. These probes neither exclude writers nor own B.
    // See rollback-g1-collection18-writers-2026-09-27.md for the verified routes.
    static constexpr unsigned C18Limit=32,C18RowLimit=64;
    struct C18Writer {
        std::uint64_t pair{},parent{},entry{},returned_sequence{};
        std::uintptr_t rva{},object{},argument{},caller{},result{};
        DWORD thread{};
        bool active{},returned{},preexisting{},association_unknown{},selected_match{};
        unsigned retained{C18Limit};
    };
    struct C18Witness {
        bool selected{},active{},returned{},invalidated{},reentry{},recursive_writer{},overflow{};
        bool unknown_writer{},header_valid{},pending_at_close{},overlap{};
        std::uintptr_t collection{},descriptor{},storage{};
        Identity hub{};
        std::int32_t count{},capacity{},recursion{};
        DWORD thread{};
        std::uint64_t entry{},end{},close_sequence{},attempted{},omitted{},tracking_overflow{};
        std::uint64_t untracked_at_entry{},untracked_at_close{};
        unsigned writer_count{},pending_writers{},pending_writers_at_close{};
        std::array<C18Writer,C18Limit> writers{};
    };
    inline static C18Witness c18_{};
    inline static NativeReplayMaterialTaskGuard::Correlation c18_correlation_scratch_{};
    inline static std::array<C18Writer,C18Limit> c18_active_{};
    inline static std::uint64_t c18_sequence_{};
    inline static std::uint64_t c18_tracking_overflow_{},c18_untracked_active_{},c18_start_sequence_{};
    // Selection coordinates only, never a lifetime or completion receipt. The
    // bridge arms after its real verified trajectory handoff at frame 170.
    // Native entry independently rechecks these small, indexed inputs. Startup
    // writer history is deliberately not reset by the arm.
    struct C18CombatSelection {
        bool required{},armed{},invalidated{};
        const char* failure{};
        Identity manager{},player{};
        std::uintptr_t player_slot{};
        DWORD thread{};
        std::uint64_t arm_sequence{},arm_epoch{},selected_epoch{},prearm_callbacks{};
        std::uint32_t arm_frame{},selected_frame{},selected_round_frame{};
    };
    inline static C18CombatSelection c18_combat_{};
    struct C18CombatCoordinates {
        std::uint64_t epoch{};
        std::uint32_t frame{},round_frame{};
        bool operator==(const C18CombatCoordinates&) const = default;
    };
    static bool C18CombatRun(const char* run) noexcept {
        return c18_combat_.required && run && std::strcmp(run,run_.data())==0;
    }
    static void C18CombatReject(const char* failure) noexcept {
        c18_combat_.invalidated=true;
        if(!c18_combat_.failure)c18_combat_.failure=failure;
    }
    static bool C18CombatIdentity(const Identity& a,const Identity& b) noexcept {
        // Serial zero is an indexed observation here, never a generation lease.
        return a.valid && b.valid && a.address==b.address && a.table==b.table
            && a.index==b.index && a.serial==b.serial;
    }
    static bool C18CombatSample(C18CombatCoordinates& out) noexcept {
        const auto& g=c18_combat_;
        const auto manager=ObserveIdentity(g.manager.address),player=ObserveIdentity(g.player.address);
        std::uintptr_t bound{},tracker{};unsigned char active{},phase{},round_state{};
        std::int32_t round{},world{};
        // Offsets and tracker identity are shared with ReadReplayTrajectory.
        // No reflection, native callback, getter, scan, or retained reference.
        return C18CombatIdentity(g.manager,manager) && C18CombatIdentity(g.player,player)
            && Read(g.player_slot,bound) && bound==player.address
            && Read(player.address+0x390,tracker) && tracker==base_+0x3290d20
            && Read(player.address+0x398,active) && active!=0
            && Read(player.address+0x39c,round) && round==0
            && Read(manager.address+0x1461,phase) && phase==2
            && Read(manager.address+0x1480,round_state) && round_state==2
            && Read(manager.address+0x1490,out.round_frame) && out.round_frame>0
            && Read(base_+0x4846364,world) && world==2
            && Read(base_+0x470d0c4,out.frame) && out.frame>=170 && out.frame<=220
            && Read(base_+0x4197170,out.epoch) && out.epoch>0;
    }
    static bool C18CombatDoubleSample(C18CombatCoordinates& out) noexcept {
        C18CombatCoordinates verify{};
        return C18CombatSample(out) && C18CombatSample(verify) && out==verify;
    }
    static std::uint64_t C18ArmCombat(const char* run,std::uintptr_t manager,std::uintptr_t slot,
        std::uintptr_t player,std::uint32_t frame,std::uint64_t epoch) noexcept {
        auto& g=c18_combat_;
        if(!enabled_.load(std::memory_order_acquire) || !C18CombatRun(run))return 0;
        if(g.armed || g.invalidated || phase_closed_ || c18_.selected || frame!=170 || !epoch
            || !slot || manager>~std::uintptr_t{}-0x1494 || player>~std::uintptr_t{}-0x3a0) {
            C18CombatReject("arm_boundary");Persist();return 0;
        }
        g.manager=ObserveIdentity(manager);g.player=ObserveIdentity(player);g.player_slot=slot;
        C18CombatCoordinates sample{};
        // The bridge passes ALuxBattleManager (constructor1403DC7F0), not its
        // ALuxVFxInstanceManager at+508, whose separate table is143356F68.
        if(!g.manager.valid || g.manager.table!=base_+0x327aa20 || !g.player.valid
            || !C18CombatDoubleSample(sample) || sample.frame!=frame || sample.epoch!=epoch) {
            C18CombatReject("arm_identity_or_phase");Persist();return 0;
        }
        g.armed=true;g.arm_sequence=++c18_sequence_;g.arm_frame=frame;g.arm_epoch=epoch;
        g.thread=GetCurrentThreadId();Persist();return write_failed_?0:g.arm_sequence;
    }
    static bool C18CombatSelect() noexcept {
        auto& g=c18_combat_;
        if(!g.required)return true;
        if(g.invalidated)return false;
        if(!g.armed) {if(g.prearm_callbacks!=~std::uint64_t{})++g.prearm_callbacks;return false;}
        C18CombatCoordinates sample{};
        if(GetCurrentThreadId()!=g.thread || !C18CombatDoubleSample(sample) || sample.epoch<g.arm_epoch) {
            C18CombatReject("entry_identity_phase_or_epoch");Persist();return false;
        }
        g.selected_frame=sample.frame;g.selected_epoch=sample.epoch;g.selected_round_frame=sample.round_frame;
        return true;
    }
    // Entry-visible identities and bounded MID header shapes only. No virtual
    // material call, proxy dereference, retain, generation allocation or exclusion.
    static constexpr unsigned C18ProviderLimit=128,C18MaterialLimit=1024,C18RegistryLimit=64;
    static constexpr unsigned C18RegistryBucketLimit=128;
    struct C18Object {
        std::uintptr_t address{},table{};
        std::int32_t index{},serial{};
        unsigned diagnostic_flags{}; // unused/zero in ordinary generation-checked census
        bool operator==(const C18Object&) const = default;
    };
    struct C18Descriptor {
        std::int32_t player{},life{},length{};
        std::uint32_t brightness{};
        unsigned char parts{},stop{},kind{},clut{};
        bool operator==(const C18Descriptor&) const = default;
    };
    struct C18RegistryRow {
        std::uintptr_t world{},manager{};
        std::int32_t next{};std::uint32_t bucket{};
        bool operator==(const C18RegistryRow&) const = default;
    };
    static_assert(sizeof(C18RegistryRow)==0x18);
    struct C18Registry {
        Header rows{};
        std::array<unsigned,4> inline_bits{},bits{};
        std::uintptr_t heap_bits{},heap_heads{};
        std::int32_t bit_count{},bit_capacity{},free_head{},free_count{};
        std::uint32_t buckets{};
        std::array<std::int32_t,C18RegistryBucketLimit> heads{};
        std::array<C18RegistryRow,C18RegistryLimit> entries{};
        bool operator==(const C18Registry&) const = default;
    };
    struct C18Listener {
        C18Object receiver{},level{},world{},battle{},fighter{},manager{},root{};
        Header players{};
        std::array<Header,2> refs{};
        std::uintptr_t row{},callable{},handle{},engine{};
        std::int32_t row_index{},registry_index{};
        bool operator==(const C18Listener&) const = default;
    };
    struct C18Provider {
        C18Object actor{},mesh{},asset{};
        Header overrides{},materials{};
        std::uintptr_t state{},controller{};
        std::int32_t strong{},weak{};
        unsigned listener{},array_offset{},ref_index{},slot_begin{},slot_count{};
        bool operator==(const C18Provider&) const = default;
    };
    struct C18Material {
        C18Object material{};
        std::uint16_t provider{},slot{};
        bool from_override{};
        bool operator==(const C18Material&) const = default;
    };
    struct C18MaterialCapture {
        C18Descriptor descriptor{};
        C18Registry registry{};
        std::array<C18Listener,C18RowLimit> listeners{};
        std::array<C18Provider,C18ProviderLimit> providers{};
        std::array<C18Material,C18MaterialLimit> slots{};
        unsigned listener_count{},provider_count{},slot_count{};
        bool operator==(const C18MaterialCapture&) const = default;
    };
    static constexpr unsigned C18MemoryShapeLimit=192;
    struct C18MemoryShape {
        Header vectors{};
        // Keep pointer values for comparison, publish occupancy only. Never
        // dereference a vector backing, proxy or cache through this receipt.
        std::array<std::uintptr_t,3> proxies{};
        std::array<std::uintptr_t,12> caches{};
        // Ordered slot occurrence in the matching material capture, not a
        // unique-MID key. Actor/provider/material identities already live there
        // and participate in its complete double-sample comparison.
        unsigned material_index{};
        bool operator==(const C18MemoryShape&) const = default;
    };
    static_assert(sizeof(C18MemoryShape)==144);
    struct C18MemoryCapture {
        std::array<C18MemoryShape,C18MemoryShapeLimit> rows{};
        unsigned matched{},count{},omitted{};
        const char* failure{};
        bool operator==(const C18MemoryCapture&) const = default;
    };
    // The first buffer becomes immutable at publication; only the selecting
    // call writes either buffer. Writers/closure serialize owned status only.
    inline static C18MaterialCapture c18_material_{},c18_material_verify_{};
    inline static bool c18_material_ready_{};
    inline static const char* c18_material_failure_{"pending"};
    inline static C18MemoryCapture c18_memory_{},c18_memory_verify_{};
    inline static const char* c18_memory_failure_{"pending"};
    inline static bool c18_memory_rows_valid_{};
    // Mutually exclusive capture tag: the two existing buffers are scratch for
    // either the ordinary census OR this distinct receipt, never both. A zero
    // serial sample is not an object generation, even when both reads match.
    struct C18TypeRejection {
        bool present{},fighter{},rva_available{};
        std::int32_t row_index{},index{},serial{};
        std::uint32_t rva{};
    };
    struct C18ZeroSerialDiagnostic {
        bool selected{},ready{},interrupted{};
        const char* failure{"pending"};
        C18Object hub{};
        Header collection{};
        C18TypeRejection rejected_type{};
    };
    inline static C18ZeroSerialDiagnostic c18_zero_{};
    static constexpr unsigned C18ZeroObjectLimit=256;
    inline static std::array<C18Object,C18ZeroObjectLimit> c18_zero_objects_{};
    inline static unsigned c18_zero_object_count_{};
    inline static const char* c18_zero_object_failure_{};
    static constexpr std::size_t C18MaterialOwnedBytes=sizeof(C18CombatSelection)+2*sizeof(C18MaterialCapture)
        +2*sizeof(C18MemoryCapture)+sizeof(c18_memory_failure_)+sizeof(c18_memory_rows_valid_)
        +sizeof(c18_material_ready_)+sizeof(c18_material_failure_)+sizeof(c18_zero_)
        +sizeof(c18_zero_objects_)+sizeof(c18_zero_object_count_)+sizeof(c18_zero_object_failure_)
        +2*sizeof(C18TypeRejection)+8192; // selecting-call and helper scratch
    static_assert(C18MaterialOwnedBytes<=256*1024);
    template<class T> static bool C18Read(std::uintptr_t p,std::size_t offset,T& value) noexcept {
        return p && offset<=~std::uintptr_t{}-p && Read(p+offset,value);
    }
    static bool C18Array(const Header& h,unsigned cap,unsigned stride) noexcept {
        return h.count>=0 && h.capacity>=h.count && static_cast<unsigned>(h.capacity)<=cap
            && (!h.capacity || h.data) && h.data<=~std::uintptr_t{}-std::size_t(h.capacity)*stride;
    }
    static bool C18ZeroCollectionShape(std::uintptr_t collection,const Header& h) noexcept {
        if(h.data)return C18Array(h,C18RowLimit,0x40);
        // Verified142050940/142050570 first insertion keeps capacity one in
        // the inline row read by141D38300. No generic null-backed array rule.
        return ((h.count==0 && h.capacity==0) || (h.count==1 && h.capacity==1))
            && collection && collection<=~std::uintptr_t{}-0x40;
    }
    static bool C18ObjectAt(std::uintptr_t p,C18Object& out) noexcept {
        const auto id=ObserveIdentity(p);
        if(!id.valid || id.serial<=0 || !id.table)return false;
        out={id.address,id.table,id.index,id.serial};return true;
    }
    static bool C18ObjectField(std::uintptr_t p,unsigned offset,C18Object& out) noexcept {
        std::uintptr_t address{};return C18Read(p,offset,address) && C18ObjectAt(address,out);
    }
    template<bool Diagnostic> static bool C18SampleObject(std::uintptr_t p,C18Object& out) noexcept {
        if constexpr(!Diagnostic)return C18ObjectAt(p,out);
        else {
            unsigned before{},after{};
            if(!C18Read(p,8,before))return false;
            const auto id=ObserveIdentity(p);
            if(!id.valid || id.serial<0 || !id.table || !C18Read(p,8,after) || before!=after)return false;
            out={id.address,id.table,id.index,id.serial,after};
            // Entry-local alias comparison only, discarded with this receipt.
            // No zero-serial object key escapes into an identity/lease service.
            for(unsigned i=0;i<c18_zero_object_count_;++i) {
                const auto& prior=c18_zero_objects_[i];
                if(prior.address==out.address || prior.index==out.index) {
                    if(prior==out)return true;
                    c18_zero_object_failure_="indexed_sample_changed";return false;
                }
            }
            if(c18_zero_object_count_==C18ZeroObjectLimit){c18_zero_object_failure_="object_capacity";return false;}
            c18_zero_objects_[c18_zero_object_count_++]=out;return true;
        }
    }
    template<bool Diagnostic> static bool C18SampleField(std::uintptr_t p,unsigned offset,C18Object& out) noexcept {
        std::uintptr_t address{};return C18Read(p,offset,address) && C18SampleObject<Diagnostic>(address,out);
    }
    // Independent of ordinary invalidated/header_valid, which stay false for
    // this receipt. Called under the metadata lock, never over native calls.
    static bool C18ZeroInterrupted() noexcept {
        return c18_zero_.interrupted || c18_.reentry || c18_.overflow || c18_.overlap
            || c18_.unknown_writer || c18_.attempted || c18_.pending_writers
            || c18_.untracked_at_entry || c18_.pending_at_close;
    }
    static bool C18DescriptorAt(std::uintptr_t p,C18Descriptor& d) noexcept {
        return C18Read(p,0,d.player) && C18Read(p,4,d.parts) && C18Read(p,8,d.life)
            && C18Read(p,0xc,d.stop) && C18Read(p,0x10,d.length)
            && C18Read(p,0x14,d.kind) && C18Read(p,0x15,d.clut)
            && C18Read(p,0x18,d.brightness) && d.player>=0 && d.stop<=1
            && (d.brightness&0x7f800000u)!=0x7f800000u;
    }
    static std::uint32_t C18RegistryBucket(std::uintptr_t world,std::uint32_t buckets) noexcept {
        // Verified lookup142018870 / rehash141D01080; all operations wrap at32 bits.
        // Call only with a validated nonzero power-of-two bucket count.
        std::uint32_t i=static_cast<std::uint32_t>(world>>4);
        std::uint32_t a=(0x9e3779b9u-i)^(i<<8);
        std::uint32_t b=(0u-(a+i))^(a>>13);
        std::uint32_t c=((i-a)-b)^(b>>12);
        a=((a-c)-b)^(c<<16);
        b=((b-a)-c)^(a>>5);
        c=((c-a)-b)^(b>>3);
        a=((a-c)-b)^(c<<10);
        return (((b-a)-c)^(a>>15))&(buckets-1);
    }
    static const char* C18RegistryAt(C18Registry& r) noexcept {
        const auto p=base_+0x406e4e0;
        if(!Read(p,r.rows) || !Read(p+0x10,r.inline_bits) || !Read(p+0x20,r.heap_bits)
            || !Read(p+0x28,r.bit_count) || !Read(p+0x2c,r.bit_capacity)
            || !Read(p+0x30,r.free_head) || !Read(p+0x34,r.free_count)
            || !Read(p+0x40,r.heap_heads) || !Read(p+0x48,r.buckets))return "registry_unreadable";
        // Only two heads fit inline. Native sizing grows from1 to16 buckets
        // at four live rows; a retained table need not be minimally sized.
        if(!r.buckets || r.buckets>C18RegistryBucketLimit || (r.buckets&(r.buckets-1))
            || (!r.heap_heads && r.buckets>2))return "registry_hash_layout";
        if(!C18Array(r.rows,C18RegistryLimit,0x18) || r.bit_count!=r.rows.count
            || r.bit_capacity<r.bit_count || r.bit_capacity>128
            || r.free_count<0 || r.free_count>r.rows.count
            || (r.free_count==0?r.free_head!=-1:(r.free_head<0 || r.free_head>=r.rows.count)))
            return "registry_shape";
        const auto bits=r.heap_bits?r.heap_bits:p+0x10;
        for(int i=0;i<(r.bit_count+31)/32;++i)
            if(!C18Read(bits,i*4,r.bits[i]))return "registry_unreadable";
        const auto heads=r.heap_heads?r.heap_heads:p+0x38;
        for(unsigned bucket=0;bucket<r.buckets;++bucket)
            if(!C18Read(heads,bucket*4,r.heads[bucket]))return "registry_unreadable";
        std::array<bool,C18RegistryLimit> seen{};unsigned visited{};
        for(unsigned bucket=0;bucket<r.buckets;++bucket) {
            for(auto index=r.heads[bucket];index!=-1;) {
                if(index<0 || index>=r.rows.count || seen[index] || visited==C18RegistryLimit)
                    return "registry_chain";
                if(!(r.bits[index/32]&(1u<<(index%32))))return "registry_unoccupied";
                seen[index]=true;++visited;auto& row=r.entries[index];
                if(!C18Read(r.rows.data,std::size_t(index)*0x18,row))return "registry_unreadable";
                if(row.bucket!=bucket || C18RegistryBucket(row.world,r.buckets)!=bucket)return "registry_chain";
                index=row.next;
            }
        }
        // Occupancy comparison is validation only; target resolution below
        // must traverse the computed bucket, never search this array densely.
        for(int i=0;i<r.rows.count;++i)
            if(seen[i]!=bool(r.bits[i/32]&(1u<<(i%32))))return "registry_membership";
        if(visited!=static_cast<unsigned>(r.rows.count-r.free_count)
            || (r.free_count && seen[r.free_head]))return "registry_membership";
        return nullptr;
    }
    static std::uintptr_t C18World(std::uintptr_t engine,std::uintptr_t receiver) noexcept {
        // The verified receiver's GetWorld target checks the immediate ULevel.
        // No observer lock spans this external native service. SEH contains an
        // unreadable borrow; it does not acquire lifetime or suppress callbacks.
        __try {
            return reinterpret_cast<std::uintptr_t(*)(void*,void*,int)>(base_+0x21784f0)(
                reinterpret_cast<void*>(engine),reinterpret_cast<void*>(receiver),1);
        } __except(EXCEPTION_EXECUTE_HANDLER) {return 0;}
    }
    template<bool Diagnostic=false> static bool C18MaterialCancelled() noexcept {
        AcquireSRWLockShared(&lock_);
        const bool stopped=Diagnostic?C18ZeroInterrupted():
            (c18_.invalidated || c18_.pending_at_close || c18_.reentry);
        ReleaseSRWLockShared(&lock_);return stopped;
    }
    template<bool Diagnostic=false> static void C18MemoryAt(C18MemoryCapture& c,const C18Provider& p,const C18Material& slot,unsigned material_index) noexcept {
        if(!slot.from_override || slot.material.table!=base_+0x391ee70)return;
        ++c.matched;
        if(c.count==C18MemoryShapeLimit){++c.omitted;return;}
        auto& row=c.rows[c.count++];
        row.material_index=material_index;
        if(c.failure)return;
        const auto address=slot.material.address;
        if(!C18Read(address,0xb8,row.vectors)
            || !C18Read(address,0xf0,row.proxies) || !C18Read(address,0x150,row.caches)) {
            c.failure="mid_shape_unreadable";return;
        }
        // Capacity is observed, not allocated/traversed. Its product is only
        // logical vector backing payload, not allocator/native/GPU ownership.
        if(!C18Array(row.vectors,0x7fffffffu,0x28)
            || bool(row.vectors.data)!=(row.vectors.capacity!=0) || (row.vectors.data&7)) {
            c.failure="vector_pointer_shape";return;
        }
        C18Object actor{},provider{},material{};Header again{};
        if(!C18SampleObject<Diagnostic>(p.actor.address,actor) || actor!=p.actor
            || !C18SampleObject<Diagnostic>(p.mesh.address,provider) || provider!=p.mesh
            || !C18SampleObject<Diagnostic>(address,material) || material!=slot.material) {
            c.failure="mid_shape_identity_changed";return;
        }
        if(!C18Read(address,0xb8,again) || again!=row.vectors)c.failure="vector_header_changed";
    }
    template<bool Diagnostic=false> static const char* C18ProviderAt(C18MaterialCapture& c,C18Provider& p,C18MemoryCapture& memory) noexcept {
        if(p.controller>~std::uintptr_t{}-0x108 || p.state!=p.controller+0x10)return "trace_controller";
        std::uintptr_t table{},link{},getter{};
        if(!C18Read(p.controller,0,table) || table!=base_+0x3362590
            || !C18Read(p.controller,8,p.strong) || !C18Read(p.controller,12,p.weak)
            || p.strong<=0 || p.weak<=0 || !C18SampleField<Diagnostic>(p.state,8,p.actor)
            || p.actor.table!=base_+0x3361660 || !C18SampleField<Diagnostic>(p.actor.address,0x398,p.mesh)
            || p.mesh.table!=base_+0x38829c0 || !C18Read(p.actor.address,0x168,link)
            || link!=p.mesh.address || !C18Read(p.mesh.address,0x190,link) || link!=p.actor.address)
            return "provider_identity";
        if(!C18Read(p.mesh.table,0x628,getter) || getter!=base_+0x1dc8a90
            || !C18Read(p.mesh.table,0x4e8,getter) || getter!=base_+0x1dc8220)return "provider_layout";
        std::uintptr_t asset{};
        if(!C18Read(p.mesh.address,0x910,asset) || !C18Read(p.mesh.address,0x808,p.overrides)
            || !C18Array(p.overrides,C18MaterialLimit,8))return "provider_array";
        if(asset && (!C18SampleObject<Diagnostic>(asset,p.asset) || !C18Read(asset,0xa0,p.materials)
            || !C18Array(p.materials,C18MaterialLimit,0x30)))return "asset_array";
        // Null asset => zero native count, including with nonempty overrides.
        p.slot_begin=c.slot_count;p.slot_count=static_cast<unsigned>(p.materials.count);
        if(p.slot_count>C18MaterialLimit-c.slot_count)return "material_capacity";
        for(unsigned i=0;i<p.slot_count;++i) {
            if(C18MaterialCancelled<Diagnostic>())return "occurrence_invalidated";
            std::uintptr_t material{};bool from_override=false;
            if(i<static_cast<unsigned>(p.overrides.count)) {
                if(!C18Read(p.overrides.data,i*8,material))return "material_unreadable";
                from_override=material!=0;
            }
            if(!from_override && !C18Read(p.materials.data,i*0x30,material))return "material_unreadable";
            auto& slot=c.slots[c.slot_count++];
            slot.provider=static_cast<std::uint16_t>(c.provider_count);
            slot.slot=static_cast<std::uint16_t>(i);slot.from_override=from_override;
            // A null native slot is still an ordered occurrence. Nonnull
            // objects get indexed identities, never a guessed MID layout.
            if(material && !C18SampleObject<Diagnostic>(material,slot.material))return "material_identity";
            C18MemoryAt<Diagnostic>(memory,p,slot,c.slot_count-1);
        }
        return nullptr;
    }
    struct C18MaterialInput {
        std::uintptr_t collection{},descriptor{},storage{};
        std::int32_t count{},capacity{};Identity hub{};
    };
    template<bool Diagnostic=false> static bool C18InputSame(const C18MaterialInput& in) noexcept {
        std::uintptr_t storage{},table{};std::int32_t count{},capacity{},recursion{};
        bool same{};
        if constexpr(Diagnostic) {
            C18Object hub{};
            same=C18SampleObject<true>(in.hub.address,hub) && hub==c18_zero_.hub && hub.serial==0;
        } else same=SameIdentity(in.hub,ObserveIdentity(in.hub.address));
        return same
            && C18Read(in.collection-0x7e8,0,table) && table==base_+0x337c4f8
            && C18Read(in.collection,0x40,storage) && storage==in.storage
            && C18Read(in.collection,0x50,count) && count==in.count
            && C18Read(in.collection,0x54,capacity) && capacity==in.capacity
            && C18Read(in.collection,0x64,recursion) && recursion==0;
    }
    static C18TypeRejection C18RejectedType(const C18Listener& l,bool fighter) noexcept {
        // Existing entry samples only. A representable base-relative offset is
        // not proof of image membership, a valid class, or a stable generation.
        const auto& o=fighter?l.fighter:l.receiver;
        C18TypeRejection r{};r.present=true;r.fighter=fighter;
        r.row_index=l.row_index;r.index=o.index;r.serial=o.serial;
        r.rva_available=o.table>=base_ && o.table-base_<=0xffffffffull;
        if(r.rva_available)r.rva=static_cast<std::uint32_t>(o.table-base_);
        return r;
    }
    template<bool Diagnostic=false> static const char* C18CaptureMaterials(const C18MaterialInput& in,C18MaterialCapture& c,C18MemoryCapture& memory,C18TypeRejection* rejected=nullptr) noexcept {
        if(C18MaterialCancelled<Diagnostic>())return "occurrence_invalidated";
        if(!C18InputSame<Diagnostic>(in))return "collection_changed";
        if(!C18DescriptorAt(in.descriptor,c.descriptor))return "descriptor_unreadable";
        if(!OneArgLayoutMatches())return "listener_layout";
        if(const auto failure=C18RegistryAt(c.registry))return failure;
        for(int i=in.count-1;i>=0;--i) {
            if(C18MaterialCancelled<Diagnostic>())return "occurrence_invalidated";
            auto& l=c.listeners[c.listener_count];l.row_index=i;
            l.row=(in.storage?in.storage:in.collection)+std::size_t(i)*0x40;
            std::uintptr_t table{},function{},receiver{},getter{},direct_world{};
            std::int32_t enabled{},index{},serial{},current_serial{};unsigned flags{};
            if(!C18Read(l.row,0x20,l.callable) || !C18Read(l.row,0x30,enabled)
                || enabled!=3 || !l.callable || !C18Read(l.callable,0,table)
                || table!=base_+0x374b760 || !C18Read(l.callable,8,index)
                || !C18Read(l.callable,12,serial) || !C18Read(l.callable,0x10,function)
                || function!=base_+0x3c5360 || !C18Read(l.callable,0x20,l.handle) || !l.handle)
                return "listener_layout";
            // Name only the first failed existing predicate. A stale weak
            // entry can be native input; never resolve/repair it or skip it to
            // manufacture a census. Ordinary census reasons stay unchanged.
            if(Diagnostic && serial<=0)return "listener_weak_serial_nonpositive";
            if(!Indexed(index,receiver,current_serial))return Diagnostic?"listener_indexed_lookup":"listener_identity";
            if(current_serial!=serial)return Diagnostic?"listener_weak_serial_mismatch":"listener_identity";
            if(!C18SampleObject<Diagnostic>(receiver,l.receiver))return Diagnostic?"listener_object_sample":"listener_identity";
            if(l.receiver.index!=index || l.receiver.serial!=serial)
                return Diagnostic?"listener_sample_identity_mismatch":"listener_identity";
            // Registration1409A8680 names LuxBattleTraceEventHandler; its
            // constructor1403ABFF0 and BeginPlay1403B54A0 bind this receiver.
            if(l.receiver.table!=base_+0x326b2e0) {
                if constexpr(Diagnostic)if(rejected)*rejected=C18RejectedType(l,false);
                return Diagnostic?"listener_receiver_vtable":"listener_identity";
            }
            if(!C18Read(l.receiver.table,0x138,getter) || getter!=base_+0x1c204e0
                || !C18Read(receiver,8,flags) || (flags&0x10)
                || !C18SampleField<Diagnostic>(receiver,0x20,l.level)
                || !C18Read(l.level.address,0xc0,direct_world)
                || !Read(base_+0x43b3068,l.engine) || !l.engine)return "world_layout";
            const auto world=C18World(l.engine,receiver);
            if(C18MaterialCancelled<Diagnostic>())return "occurrence_invalidated";
            if(world!=direct_world || !C18SampleObject<Diagnostic>(world,l.world))return "world_identity";
            l.registry_index=-1;
            const auto bucket=C18RegistryBucket(world,c.registry.buckets);unsigned traversed{};
            for(auto n=c.registry.heads[bucket];n!=-1;n=c.registry.entries[n].next) {
                // The owned copy was fully validated above; retain an explicit
                // traversal bound before every array access here as well.
                if(n<0 || n>=c.registry.rows.count || traversed++==C18RegistryLimit)return "registry_chain";
                if(c.registry.entries[n].world==world) {
                    if(l.registry_index!=-1)return "registry_ambiguous";
                    l.registry_index=n;
                }
            }
            // 1403EF7A0 returns ALuxBattleManager from the matched world row;
            // its +390 character array is not on ALuxVFxInstanceManager.
            if(l.registry_index<0 || !C18SampleObject<Diagnostic>(c.registry.entries[l.registry_index].manager,l.battle)
                || l.battle.table!=base_+0x327aa20
                || !C18Read(l.battle.address,0x390,l.players) || !C18Array(l.players,8,8)
                || c.descriptor.player>=l.players.count
                || !C18SampleField<Diagnostic>(l.players.data,c.descriptor.player*8,l.fighter))return "target_unresolved";
            // The selected character may differ from the listener. Name this
            // exact failed predicate using the existing bounded failure field.
            if(l.fighter.table!=base_+0x3268078) {
                if constexpr(Diagnostic)if(rejected)*rejected=C18RejectedType(l,true);
                return Diagnostic?"selected_fighter_vtable":"target_unresolved";
            }
            if(!C18SampleField<Diagnostic>(l.fighter.address,0x458,l.manager)
                || !C18SampleField<Diagnostic>(l.manager.address,0x3a8,l.root)
                || l.root.table!=base_+0x3360ca8)return "target_unresolved";
            std::uintptr_t link{};
            if(!C18Read(l.root.address,0x1c8,link) || link!=world
                || !C18Read(l.root.address,0x490,link) || link!=l.fighter.address)return "root_binding";
            for(unsigned a=0;a<2;++a) {
                const auto offset=0x418+a*0x10;
                if(!C18Read(l.root.address,offset,l.refs[a]) || !C18Array(l.refs[a],C18ProviderLimit,16))
                    return "trace_array";
                for(int n=0;n<l.refs[a].count;++n) {
                    if(C18MaterialCancelled<Diagnostic>())return "occurrence_invalidated";
                    if(c.provider_count==C18ProviderLimit)return "provider_capacity";
                    auto& p=c.providers[c.provider_count];p.listener=c.listener_count;
                    p.array_offset=offset;p.ref_index=n;
                    if(!C18Read(l.refs[a].data,std::size_t(n)*16,p.state)
                        || !C18Read(l.refs[a].data,std::size_t(n)*16+8,p.controller))return "trace_unreadable";
                    if(const auto failure=C18ProviderAt<Diagnostic>(c,p,memory))return failure;
                    ++c.provider_count;
                }
            }
            ++c.listener_count;
        }
        if(C18MaterialCancelled<Diagnostic>())return "occurrence_invalidated";
        return C18InputSame<Diagnostic>(in)?nullptr:"collection_changed";
    }
    static const char* C18MaterialFailure() noexcept {
        if(!c18_material_ready_)return "pending";
        if(c18_material_failure_)return c18_material_failure_;
        if(c18_.invalidated || c18_.reentry || c18_.overflow || c18_.overlap || c18_.pending_at_close)
            return "occurrence_invalidated";
        return nullptr;
    }
    static bool C18RowWriter(const C18Writer& w) noexcept {
        return w.rva==0x3a22d0 || w.rva==0x17dfea0 || w.rva==0x399420 || w.rva==0x3a1a70;
    }
    static bool C18Matches(const C18Writer& w) noexcept {
        if(w.rva==0x912df0)return c18_.collection>=0x810 && w.object==c18_.collection-0x810;
        if(C18RowWriter(w)) {
            const auto rows=c18_.storage?c18_.storage:c18_.collection;
            return c18_.header_valid && w.object>=rows && w.object-rows<static_cast<std::uintptr_t>(c18_.count)*0x40
                && (w.object-rows)%0x40==0;
        }
        return w.object==c18_.collection;
    }
    static void C18Retain(C18Writer& w) noexcept {
        const bool row=C18RowWriter(w);
        if(!C18Matches(w)) {
            // After a replacement the old range cannot classify another row.
            // Do not chase a fresh backing pointer or assume it is unrelated.
            if(!row || (!c18_.invalidated && c18_.header_valid))return;
            w.association_unknown=true;c18_.unknown_writer=true;
        }
        c18_.invalidated=true;
        ++c18_.attempted;
        w.selected_match=true;++c18_.pending_writers;
        c18_.recursive_writer|=w.thread==c18_.thread && !w.preexisting;
        c18_.overlap|=w.thread!=c18_.thread || w.preexisting;
        if(w.rva==0x43d210 && w.argument!=base_+0x3c5360)c18_.unknown_writer=true;
        if(c18_.writer_count==C18Limit){++c18_.omitted;c18_.overflow=true;return;}
        w.retained=c18_.writer_count++;
        auto& saved=c18_.writers[w.retained];saved=w;saved.pair=w.retained+1;saved.parent=0;
        for(const auto& parent:c18_active_)
            if(parent.active && parent.thread==w.thread && parent.entry<w.entry && parent.retained<C18Limit
                && parent.retained+1>saved.parent)saved.parent=parent.retained+1;
    }
    static unsigned C18WriterBefore(unsigned site,void* object,std::uintptr_t argument,void* caller) noexcept {
        // Hooks are installed only at validated initial constructor startup.
        // Track from hook installation, including calls spanning Start. The
        // diagnostic enable bit must not create an invisible active-writer gap.
        if(!base_)return C18Limit;
        AcquireSRWLockExclusive(&lock_);unsigned token=C18Limit;
        // Track pre-existing calls while admission is open; keep recording all
        // selected-call mutations after ClosePhase until its original returns.
        if(!phase_closed_ || c18_.active) {
            for(unsigned i=0;i<C18Limit;++i)if(!c18_active_[i].active){token=i;break;}
            if(token==C18Limit) {
                ++c18_tracking_overflow_;++c18_untracked_active_;++c18_sequence_;
                token=C18Limit+1; // balanced omission token, never an array index
                if(c18_.active){++c18_.tracking_overflow;c18_.overflow=true;c18_.invalidated=true;Persist();}
            } else {
                auto& w=c18_active_[token];w={};w.active=true;w.entry=++c18_sequence_;
                w.rva=rvas_[site];w.object=reinterpret_cast<std::uintptr_t>(object);
                w.argument=argument;w.caller=reinterpret_cast<std::uintptr_t>(caller);w.thread=GetCurrentThreadId();
                if(c18_.active){C18Retain(w);if(w.retained<C18Limit || c18_.overflow)Persist();}
            }
        }
        ReleaseSRWLockExclusive(&lock_);return token;
    }
    static void C18WriterAfter(unsigned token,std::uintptr_t result=0) noexcept {
        if(token==C18Limit)return;
        if(token==C18Limit+1) {
            AcquireSRWLockExclusive(&lock_);--c18_untracked_active_;++c18_sequence_;
            if(c18_.selected)Persist();ReleaseSRWLockExclusive(&lock_);return;
        }
        AcquireSRWLockExclusive(&lock_);auto& w=c18_active_[token];
        w.active=false;w.returned=true;w.result=result;w.returned_sequence=++c18_sequence_;
        if(w.selected_match)--c18_.pending_writers;
        if(w.retained<C18Limit) {
            auto& saved=c18_.writers[w.retained];saved.active=false;saved.returned=true;
            saved.result=result;saved.returned_sequence=w.returned_sequence;Persist();
        } else if(w.selected_match)Persist();
        // Only owned receipt metadata is accessed here. Even binder output and
        // destination rows may have been revoked before native returned.
        ReleaseSRWLockExclusive(&lock_);
    }
    static bool C18Before(void* collection,void* descriptor,void* caller) noexcept {
        if(!enabled_.load(std::memory_order_acquire))return false;
        AcquireSRWLockExclusive(&lock_);bool token=false;
        C18MaterialInput input{};bool collect=false,diagnose=false;
        const auto address=reinterpret_cast<std::uintptr_t>(collection);
        // Another executor invocation inside the selected interval cannot be
        // attributed to one listener merely because it shares this thread.
        if(c18_.active)NativeReplayMaterialTaskGuard::InvalidateC18Correlation();
        if(c18_.active && c18_zero_.selected)c18_zero_.interrupted=true;
        if(c18_.active && address==c18_.collection) {
            c18_.reentry=true;c18_.invalidated=true;Persist();
        } else if(!phase_closed_ && !c18_.selected && reinterpret_cast<std::uintptr_t>(caller)==base_+0x400a9a
            && C18CombatSelect()) {
            auto& h=c18_;h.selected=true;h.active=true;token=true;
            h.collection=address;h.descriptor=reinterpret_cast<std::uintptr_t>(descriptor);
            h.thread=GetCurrentThreadId();h.entry=++c18_sequence_;h.overflow=c18_tracking_overflow_!=0;
            h.tracking_overflow=c18_tracking_overflow_;h.untracked_at_entry=c18_untracked_active_;
            std::uintptr_t dispatcher_table{};
            if(address>=0x810 && address<=~std::uintptr_t{}-0x68) {
                h.hub=ObserveIdentity(address-0x810);
                h.header_valid=h.hub.valid && h.hub.serial>0 && h.hub.table==base_+0x337c2c8
                    && Read(address-0x7e8,dispatcher_table) && dispatcher_table==base_+0x337c4f8
                    && Read(address+0x40,h.storage) && Read(address+0x50,h.count)
                    && Read(address+0x54,h.capacity) && Read(address+0x64,h.recursion)
                    && h.count>=0 && h.count<=C18RowLimit && h.capacity>=h.count && h.capacity<=C18RowLimit
                    && h.recursion==0 && (h.storage || h.count<=1)
                    && (h.storage?h.storage:address)<=~std::uintptr_t{}-C18RowLimit*0x40;
            }
            h.invalidated=!h.header_valid || h.overflow;
            // Selection can occur inside a writer's native notification. Pair
            // that existing entry; never pretend its earlier effects are undo.
            // Active slots are reused. Select in entry order so an outer
            // retained writer receives its pair before a child in a lower slot.
            std::uint64_t previous{};
            for(unsigned n=0;n<C18Limit;++n) {
                C18Writer* first=nullptr;
                for(auto& w:c18_active_)if(w.active && w.entry>previous && (!first || w.entry<first->entry))first=&w;
                if(!first)break;previous=first->entry;first->preexisting=true;C18Retain(*first);
            }
            input={h.collection,h.descriptor,h.storage,h.count,h.capacity,h.hub};
            collect=h.header_valid && !h.invalidated;
            if(!collect){c18_material_ready_=true;c18_material_failure_="entry_invalidated";}
            if(h.hub.valid && h.hub.serial==0) {
                auto& z=c18_zero_;z.selected=true;
                std::int32_t recursion{};
                const char* failure=nullptr;
                if(C18ZeroInterrupted())failure="occurrence_invalidated";
                else if(!C18SampleObject<true>(h.hub.address,z.hub) || z.hub.serial!=0
                    || z.hub.address!=h.hub.address || z.hub.index!=h.hub.index || z.hub.table!=h.hub.table)
                    failure="entry_hub_identity";
                else if(z.hub.table!=base_+0x337c2c8)failure="entry_hub_vtable";
                else if(!Read(address-0x7e8,dispatcher_table) || dispatcher_table!=base_+0x337c4f8)
                    failure="entry_dispatcher_vtable";
                else if(!Read(address+0x40,z.collection.data) || !Read(address+0x50,z.collection.count)
                    || !Read(address+0x54,z.collection.capacity) || !Read(address+0x64,recursion))
                    failure="entry_header_unreadable";
                else if(recursion || C18ZeroInterrupted())failure="occurrence_invalidated";
                else if(!C18ZeroCollectionShape(address,z.collection))failure="entry_header_shape";
                // InputSame rechecks this exact header, hub and dispatcher at
                // both ends of each complete capture, including the inline case.
                diagnose=!failure;z.failure=failure;z.ready=!diagnose;
                if(diagnose)input={h.collection,h.descriptor,z.collection.data,
                    z.collection.count,z.collection.capacity,h.hub};
            }
            Persist();
        }
        ReleaseSRWLockExclusive(&lock_);
        if(token && collect) {
            // Only this first selecting call touches the two scratch buffers.
            // Native world lookup can reenter; no metadata lock spans it.
            auto failure=C18CaptureMaterials(input,c18_material_,c18_memory_);
            if(!failure)failure=C18CaptureMaterials(input,c18_material_verify_,c18_memory_verify_);
            if(!failure && !(c18_material_==c18_material_verify_))failure="entry_changed";
            AcquireSRWLockExclusive(&lock_);
            c18_memory_failure_=c18_memory_.failure?c18_memory_.failure:c18_memory_verify_.failure;
            if(!failure && !c18_memory_failure_ && !(c18_memory_==c18_memory_verify_))
                c18_memory_failure_="mid_shape_entry_changed";
            c18_memory_rows_valid_=!failure && !c18_memory_failure_;
            if(c18_memory_rows_valid_ && c18_memory_.omitted)c18_memory_failure_="detail_capacity";
            c18_material_failure_=failure;c18_material_ready_=true;Persist();
            ReleaseSRWLockExclusive(&lock_);
        }
        if(token && diagnose) {
            C18TypeRejection rejected{};
            auto failure=C18CaptureMaterials<true>(input,c18_material_,c18_memory_,&rejected);
            if(!failure)failure=C18CaptureMaterials<true>(input,c18_material_verify_,c18_memory_verify_,&rejected);
            if(!failure && c18_material_!=c18_material_verify_)failure="entry_changed";
            if(!failure)failure=c18_memory_.failure?c18_memory_.failure:c18_memory_verify_.failure;
            if(!failure && c18_memory_!=c18_memory_verify_)failure="mid_shape_entry_changed";
            if(!failure && c18_memory_.omitted)failure="detail_capacity";
            AcquireSRWLockExclusive(&lock_);
            if(c18_zero_object_failure_)failure=c18_zero_object_failure_;
            c18_zero_.failure=C18ZeroInterrupted()?"occurrence_invalidated":failure;
            // Publish owned scalar metadata only after capture has stopped.
            c18_zero_.rejected_type=rejected;
            c18_zero_.ready=true;Persist();ReleaseSRWLockExclusive(&lock_);
        }
        if(token) {
            AcquireSRWLockExclusive(&lock_);
            // Arm only after observer-owned world lookup/census has returned.
            // This interval encloses native broadcast, not our own sampling.
            NativeReplayMaterialTaskGuard::BeginC18Correlation(c18_.entry,
                phase_closed_ || c18_.reentry || c18_.overlap || c18_.overflow
                || c18_.unknown_writer || c18_.recursive_writer || c18_combat_.invalidated);
            Persist();ReleaseSRWLockExclusive(&lock_);
        }
        return token;
    }
    static void C18After(bool token,bool forwarded) noexcept {
        if(!token)return;
        AcquireSRWLockExclusive(&lock_);c18_.active=false;c18_.returned=forwarded;
        NativeReplayMaterialTaskGuard::EndC18Correlation(c18_.entry,forwarded,
            c18_.reentry || c18_.overlap || c18_.overflow || c18_.unknown_writer || c18_.recursive_writer
            || c18_combat_.invalidated);
        if(!forwarded)c18_zero_.interrupted=true;
        c18_.end=++c18_sequence_;c18_.invalidated|=!forwarded;Persist();
        ReleaseSRWLockExclusive(&lock_);
    }
    __declspec(noinline) static void* C18Append(void* collection,void* out,void* receiver,void* callable) {
        const auto error=GetLastError();const auto token=C18WriterBefore(10,collection,reinterpret_cast<std::uintptr_t>(callable),_ReturnAddress());SetLastError(error);
        auto* result=reinterpret_cast<void*(*)(void*,void*,void*,void*)>(originals_[10])(collection,out,receiver,callable);
        const auto returned_error=GetLastError();C18WriterAfter(token,reinterpret_cast<std::uintptr_t>(result));SetLastError(returned_error);return result;
    }
    __declspec(noinline) static void C18Compact(void* collection,bool threshold) {
        const auto error=GetLastError();const auto token=C18WriterBefore(11,collection,threshold,_ReturnAddress());SetLastError(error);
        reinterpret_cast<void(*)(void*,bool)>(originals_[11])(collection,threshold);
        const auto returned_error=GetLastError();C18WriterAfter(token);SetLastError(returned_error);
    }
    __declspec(noinline) static void C18RemoveRange(void* collection,int index,int count,bool shrink) {
        const auto error=GetLastError();const auto token=C18WriterBefore(12,collection,static_cast<std::uint32_t>(index),_ReturnAddress());SetLastError(error);
        reinterpret_cast<void(*)(void*,int,int,bool)>(originals_[12])(collection,index,count,shrink);
        const auto returned_error=GetLastError();C18WriterAfter(token);SetLastError(returned_error);
    }
    __declspec(noinline) static void C18Backing(void* collection,int old_count,unsigned capacity,std::uintptr_t stride) {
        const auto error=GetLastError();const auto token=C18WriterBefore(13,collection,capacity,_ReturnAddress());SetLastError(error);
        reinterpret_cast<void(*)(void*,int,unsigned,std::uintptr_t)>(originals_[13])(collection,old_count,capacity,stride);
        const auto returned_error=GetLastError();C18WriterAfter(token);SetLastError(returned_error);
    }
    __declspec(noinline) static void C18Destroy(void* collection) {
        const auto error=GetLastError();const auto token=C18WriterBefore(14,collection,0,_ReturnAddress());SetLastError(error);
        reinterpret_cast<void(*)(void*)>(originals_[14])(collection);
        const auto returned_error=GetLastError();C18WriterAfter(token);SetLastError(returned_error);
    }
    __declspec(noinline) static void C18DestroyHub(void* hub) {
        const auto error=GetLastError();const auto token=C18WriterBefore(15,hub,0,_ReturnAddress());SetLastError(error);
        reinterpret_cast<void(*)(void*)>(originals_[15])(hub);
        const auto returned_error=GetLastError();C18WriterAfter(token);SetLastError(returned_error);
    }
    __declspec(noinline) static void C18Grow(void* collection,int old_count) {
        const auto error=GetLastError();const auto token=C18WriterBefore(16,collection,static_cast<std::uint32_t>(old_count),_ReturnAddress());SetLastError(error);
        reinterpret_cast<void(*)(void*,int)>(originals_[16])(collection,old_count);
        const auto returned_error=GetLastError();C18WriterAfter(token);SetLastError(returned_error);
    }
    __declspec(noinline) static void C18Capacity(void* collection,unsigned capacity) {
        const auto error=GetLastError();const auto token=C18WriterBefore(17,collection,capacity,_ReturnAddress());SetLastError(error);
        reinterpret_cast<void(*)(void*,unsigned)>(originals_[17])(collection,capacity);
        const auto returned_error=GetLastError();C18WriterAfter(token);SetLastError(returned_error);
    }
    __declspec(noinline) static void C18ClearRow(void* row) {
        const auto error=GetLastError();const auto token=C18WriterBefore(18,row,0,_ReturnAddress());SetLastError(error);
        reinterpret_cast<void(*)(void*)>(originals_[18])(row);
        const auto returned_error=GetLastError();C18WriterAfter(token);SetLastError(returned_error);
    }
    __declspec(noinline) static void C18CopyRow(void* source,void* destination) {
        const auto error=GetLastError();const auto token=C18WriterBefore(19,destination,reinterpret_cast<std::uintptr_t>(source),_ReturnAddress());SetLastError(error);
        reinterpret_cast<void(*)(void*,void*)>(originals_[19])(source,destination);
        const auto returned_error=GetLastError();C18WriterAfter(token);SetLastError(returned_error);
    }
    __declspec(noinline) static void* C18ResizeRow(void* row,int bytes) {
        const auto error=GetLastError();const auto token=C18WriterBefore(20,row,static_cast<std::uint32_t>(bytes),_ReturnAddress());SetLastError(error);
        auto* result=reinterpret_cast<void*(*)(void*,int)>(originals_[20])(row,bytes);
        const auto returned_error=GetLastError();C18WriterAfter(token,reinterpret_cast<std::uintptr_t>(result));SetLastError(returned_error);return result;
    }
    __declspec(noinline) static void C18RowBacking(void* row,int preserve,unsigned count,std::uintptr_t stride) {
        const auto error=GetLastError();const auto token=C18WriterBefore(21,row,count,_ReturnAddress());SetLastError(error);
        reinterpret_cast<void(*)(void*,int,unsigned,std::uintptr_t)>(originals_[21])(row,preserve,count,stride);
        const auto returned_error=GetLastError();C18WriterAfter(token);SetLastError(returned_error);
    }
    static void C18ObjectJson(const C18Object& o) noexcept {
        Text("{\"address\":");Number(o.address);Text(",\"table\":");Number(o.table);
        Text(",\"index\":");Signed(o.address?o.index:-1);Text(",\"serial\":");Signed(o.serial);
        Text(",\"valid\":");Flag(o.address!=0);Text("}");
    }
    static void C18ArrayJson(const Header& h) noexcept {
        Text("{\"data\":");Number(h.data);Text(",\"count\":");Signed(h.count);
        Text(",\"capacity\":");Signed(h.capacity);Text("}");
    }
    static void C18MemoryJson(const char* provider_failure) noexcept {
        // No access to either scratch buffer until the selecting call publishes.
        // A failed shape never invalidates an otherwise valid provider census.
        const auto failure=provider_failure?"provider_census_invalid":c18_memory_failure_;
        Text(",\"memory_shape\":{\"version\":2,\"scope\":\"sampled_override_mid_shapes\"");
        Text(",\"row_limit\":");Number(C18MemoryShapeLimit);
        Text(",\"complete\":");Flag(!failure);
        Text(",\"failure\":\"");Text(failure?failure:"");Text("\"");
        Text(",\"total_retained_native_bytes\":null,\"ownership_permission\":false,\"resource_completion_proven\":false");
        Text(",\"covers_native_start_births\":false,\"writer_exclusion_proven\":false");
        Text(",\"matched_count\":");if(provider_failure)Text("null");else Number(c18_memory_.matched);
        Text(",\"omitted_count\":");if(provider_failure)Text("null");else Number(c18_memory_.omitted);
        Text(",\"entry_samples_match\":");Flag(!provider_failure && c18_memory_rows_valid_);
        Text(",\"rows\":[");
        if(!provider_failure && c18_memory_rows_valid_)for(unsigned i=0;i<c18_memory_.count;++i) {
            if(i)Text(",");const auto& row=c18_memory_.rows[i];
            const auto& slot=c18_material_.slots[row.material_index];const auto& p=c18_material_.providers[slot.provider];
            Text("{\"provider_index\":");Number(slot.provider);Text(",\"slot_index\":");Number(slot.slot);
            Text(",\"trace_actor\":");C18ObjectJson(p.actor);Text(",\"provider\":");C18ObjectJson(p.mesh);
            Text(",\"material\":");C18ObjectJson(slot.material);Text(",\"vector_array\":");C18ArrayJson(row.vectors);
            Text(",\"vector_pointer_shape_valid\":true,\"vector_payload_capacity_bytes\":");
            Number(std::uint64_t(row.vectors.capacity)*0x28);
            Text(",\"proxy_occupied\":[");
            for(unsigned n=0;n<row.proxies.size();++n){if(n)Text(",");Flag(row.proxies[n]!=0);}
            Text("],\"cache_occupied\":[");
            for(unsigned n=0;n<row.caches.size();++n){if(n)Text(",");Flag(row.caches[n]!=0);}
            Text("]}");
        }
        Text("]}");
    }
    static void C18MaterialJson() noexcept {
        const auto failure=C18MaterialFailure();const auto& c=c18_material_;
        Text(",\"material_provider_census\":{\"version\":3,\"scope\":\"entry_visible_providers\"");
        Text(",\"covers_native_start_births\":false,\"registry_bucket_limit\":");Number(C18RegistryBucketLimit);
        Text(",\"registry_bucket_count\":");if(failure)Text("null");else Number(c.registry.buckets);
        Text(",\"entry_copy_valid\":");
        Flag(!failure);Text(",\"owned_bytes\":");Number(C18MaterialOwnedBytes);
        Text(",\"failure\":\"");Text(failure?failure:"");Text("\",\"descriptor_fields\":");
        if(failure)Text("null");
        else {
            const auto& d=c.descriptor;
            Text("{\"source_player_index\":");Signed(d.player);Text(",\"trace_parts_id\":");Number(d.parts);
            Text(",\"packed_life\":");Signed(d.life);Text(",\"life_stop\":");Flag(d.stop!=0);
            Text(",\"length\":");Signed(d.length);Text(",\"trace_kind_id\":");Number(d.kind);
            Text(",\"clut_id\":");Number(d.clut);Text(",\"brightness\":");
            float brightness{};std::memcpy(&brightness,&d.brightness,4);
            char number[48]{};
            const auto result=std::to_chars(number,number+sizeof(number)-1,brightness,std::chars_format::general,9);
            if(result.ec!=std::errc{}){serialization_failed_=true;Text("null");}
            else {*result.ptr=0;Text(number);}
            Text("}");
        }
        Text(",\"listeners\":[");
        if(!failure)for(unsigned i=0;i<c.listener_count;++i) {
            if(i)Text(",");const auto& l=c.listeners[i];
            Text("{\"row_index\":");Signed(l.row_index);Text(",\"handle\":");Number(l.handle);
            Text(",\"receiver\":");C18ObjectJson(l.receiver);Text(",\"world\":");C18ObjectJson(l.world);
            Text(",\"battle_manager\":");C18ObjectJson(l.battle);Text(",\"selected_fighter\":");C18ObjectJson(l.fighter);
            Text(",\"trace_manager\":");C18ObjectJson(l.manager);Text(",\"trace_root\":");C18ObjectJson(l.root);
            Text(",\"registry\":");Number(base_+0x406e4e0);
            Text(",\"registry_row\":");Number(c.registry.rows.data+std::size_t(l.registry_index)*0x18);
            Text(",\"registry_row_index\":");Signed(l.registry_index);Text(",\"registry_bucket\":");
            Number(C18RegistryBucket(l.world.address,c.registry.buckets));
            Text(",\"trace_counts\":[");Signed(l.refs[0].count);Text(",");Signed(l.refs[1].count);Text("]}");
        }
        Text("],\"providers\":[");
        if(!failure)for(unsigned i=0;i<c.provider_count;++i) {
            if(i)Text(",");const auto& p=c.providers[i];
            Text("{\"listener_row_index\":");Signed(c.listeners[p.listener].row_index);
            Text(",\"trace_array_offset\":");Number(p.array_offset);Text(",\"trace_ref_index\":");Number(p.ref_index);
            Text(",\"state\":");Number(p.state);Text(",\"controller\":");Number(p.controller);
            Text(",\"trace_actor\":");C18ObjectJson(p.actor);Text(",\"provider\":");C18ObjectJson(p.mesh);
            Text(",\"asset\":");C18ObjectJson(p.asset);Text(",\"overrides\":");C18ArrayJson(p.overrides);
            Text(",\"materials\":");C18ArrayJson(p.materials);
            Text(",\"slot_begin\":");Number(p.slot_begin);Text(",\"slot_count\":");Number(p.slot_count);Text("}");
        }
        Text("],\"slots\":[");
        if(!failure)for(unsigned i=0;i<c.slot_count;++i) {
            if(i)Text(",");const auto& s=c.slots[i];const auto& p=c.providers[s.provider];
            Text("{\"listener_row_index\":");Signed(c.listeners[p.listener].row_index);
            Text(",\"trace_array_offset\":");Number(p.array_offset);Text(",\"trace_ref_index\":");Number(p.ref_index);
            Text(",\"slot_index\":");Number(s.slot);Text(",\"source\":\"");Text(s.from_override?"override":"asset");
            Text("\",\"state\":");Number(p.state);Text(",\"controller\":");Number(p.controller);
            Text(",\"trace_actor\":");C18ObjectJson(p.actor);Text(",\"provider\":");C18ObjectJson(p.mesh);
            Text(",\"asset\":");C18ObjectJson(p.asset);Text(",\"material\":");C18ObjectJson(s.material);Text("}");
        }
        Text("]");C18MemoryJson(failure);Text("}");
    }
    static void C18DiagnosticObjectJson(const C18Object& o) noexcept {
        Text("{\"address\":");Number(o.address);Text(",\"table\":");Number(o.table);
        Text(",\"index\":");Signed(o.index);Text(",\"serial\":");Signed(o.serial);
        Text(",\"flags\":");Number(o.diagnostic_flags);
        Text(",\"indexed_pointer_checked\":true,\"generation_unknown\":true}");
    }
    static void C18ZeroJson() noexcept {
        Text(",\"zero_serial_diagnostic\":");
        if(!c18_zero_.selected){Text("null");return;}
        const auto& z=c18_zero_;
        const auto failure=!z.ready?"pending":C18ZeroInterrupted()?"occurrence_invalidated":z.failure;
        Text("{\"version\":4,\"scope\":\"zero_serial_entry_pointer_samples\",\"status\":\"");
        Text(!z.ready?"pending":failure?"rejected":"sampled");
        Text("\",\"failure\":\"");Text(failure?failure:"");Text("\"");
        Text(",\"generation_unknown\":true,\"provider_census_valid\":false,\"ownership_permission\":false");
        Text(",\"resource_completion_proven\":false,\"rollback_admission\":false,\"total_retained_native_bytes\":null");
        Text(",\"writer_exclusion_proven\":false,\"covers_native_start_births\":false");
        Text(",\"entry_sequence\":");Number(c18_.entry);Text(",\"thread\":");Number(c18_.thread);
        Text(",\"collection\":");Number(c18_.collection);Text(",\"descriptor\":");Number(c18_.descriptor);
        Text(",\"row_limit\":");Number(C18MemoryShapeLimit);
        Text(",\"rejected_type\":");
        const auto& rejected=z.rejected_type;
        if(!z.ready || !rejected.present || !failure || std::strcmp(failure,
            rejected.fighter?"selected_fighter_vtable":"listener_receiver_vtable"))Text("null");
        else {
            Text("{\"scope\":\"single_entry_sample\",\"subject\":\"");
            Text(rejected.fighter?"selected_fighter":"listener_receiver");
            Text("\",\"listener_row_index\":");Signed(rejected.row_index);
            Text(",\"object_index\":");Signed(rejected.index);Text(",\"object_serial\":");Signed(rejected.serial);
            Text(",\"vtable_rva\":");if(rejected.rva_available)Number(rejected.rva);else Text("null");
            Text(",\"image_membership_proven\":false}");
        }
        Text(",\"entry_samples_match\":");Flag(!failure);
        Text(",\"matched_count\":");if(!failure || (z.ready && z.failure && !std::strcmp(z.failure,"detail_capacity")))Number(c18_memory_.matched);else Text("null");
        Text(",\"omitted_count\":");if(!failure || (z.ready && z.failure && !std::strcmp(z.failure,"detail_capacity")))Number(c18_memory_.omitted);else Text("null");
        Text(",\"hub\":");if(!failure)C18DiagnosticObjectJson(z.hub);else Text("null");
        Text(",\"collection_header\":");if(!failure)C18ArrayJson(z.collection);else Text("null");
        Text(",\"descriptor_fields\":");
        if(failure)Text("null");else {
            const auto& d=c18_material_.descriptor;
            Text("{\"player\":");Signed(d.player);Text(",\"parts\":");Number(d.parts);
            Text(",\"life\":");Signed(d.life);Text(",\"stop\":");Number(d.stop);
            Text(",\"length\":");Signed(d.length);Text(",\"kind\":");Number(d.kind);
            Text(",\"clut\":");Number(d.clut);Text(",\"brightness_bits\":");Number(d.brightness);Text("}");
        }
        Text(",\"rows\":[");
        if(!failure)for(unsigned i=0;i<c18_memory_.count;++i) {
            if(i)Text(",");const auto& row=c18_memory_.rows[i];
            const auto& slot=c18_material_.slots[row.material_index];
            const auto& p=c18_material_.providers[slot.provider];const auto& l=c18_material_.listeners[p.listener];
            Text("{\"listener_row_index\":");Signed(l.row_index);Text(",\"listener_row\":");Number(l.row);
            Text(",\"callable\":");Number(l.callable);Text(",\"handle\":");Number(l.handle);
            Text(",\"receiver\":");C18DiagnosticObjectJson(l.receiver);Text(",\"level\":");C18DiagnosticObjectJson(l.level);
            Text(",\"world\":");C18DiagnosticObjectJson(l.world);Text(",\"battle_manager\":");C18DiagnosticObjectJson(l.battle);
            Text(",\"fighter\":");C18DiagnosticObjectJson(l.fighter);Text(",\"trace_manager\":");C18DiagnosticObjectJson(l.manager);
            Text(",\"trace_root\":");C18DiagnosticObjectJson(l.root);
            Text(",\"registry\":");Number(base_+0x406e4e0);Text(",\"registry_row_index\":");Signed(l.registry_index);
            Text(",\"registry_row\":");Number(c18_material_.registry.rows.data+std::size_t(l.registry_index)*0x18);
            Text(",\"registry_bucket\":");Number(C18RegistryBucket(l.world.address,c18_material_.registry.buckets));
            Text(",\"registry_bucket_count\":");Number(c18_material_.registry.buckets);
            Text(",\"trace_array_offset\":");Number(p.array_offset);Text(",\"trace_ref_index\":");Number(p.ref_index);
            Text(",\"state\":");Number(p.state);Text(",\"controller\":");Number(p.controller);
            Text(",\"provider_index\":");Number(slot.provider);Text(",\"slot_index\":");Number(slot.slot);
            Text(",\"trace_actor\":");C18DiagnosticObjectJson(p.actor);Text(",\"provider\":");C18DiagnosticObjectJson(p.mesh);
            Text(",\"asset\":");C18DiagnosticObjectJson(p.asset);Text(",\"material\":");C18DiagnosticObjectJson(slot.material);
            Text(",\"overrides\":");C18ArrayJson(p.overrides);Text(",\"materials\":");C18ArrayJson(p.materials);
            Text(",\"vector_array\":");C18ArrayJson(row.vectors);
            Text(",\"vector_payload_capacity_bytes\":");Number(std::uint64_t(row.vectors.capacity)*0x28);
            Text(",\"proxy_occupied\":[");for(unsigned n=0;n<3;++n){if(n)Text(",");Flag(row.proxies[n]!=0);}
            Text("],\"cache_occupied\":[");for(unsigned n=0;n<12;++n){if(n)Text(",");Flag(row.caches[n]!=0);}Text("]}");
        }
        Text("]}");
    }
    static void C18Json() noexcept {
        const auto& g=c18_combat_;
        Text(",\"collection18_combat_selection\":");
        if(!g.required)Text("null");
        else {
            Text("{\"version\":1,\"scope\":\"trajectory_armed_entry_observation\",\"ownership_proven\":false,\"completion_proven\":false");
            Text(",\"armed\":");Flag(g.armed);Text(",\"invalidated\":");Flag(g.invalidated);
            Text(",\"failure\":");if(g.failure) {Text("\"");Text(g.failure);Text("\"");} else Text("null");
            Text(",\"arm_sequence\":");Number(g.arm_sequence);Text(",\"arm_frame\":");Number(g.arm_frame);
            Text(",\"arm_epoch\":");Number(g.arm_epoch);Text(",\"thread\":");Number(g.thread);
            Text(",\"manager\":");Number(g.manager.address);Text(",\"player\":");Number(g.player.address);
            Text(",\"player_slot\":");Number(g.player_slot);Text(",\"prearm_callbacks\":");Number(g.prearm_callbacks);
            Text(",\"selected_frame\":");Number(g.selected_frame);Text(",\"selected_epoch\":");Number(g.selected_epoch);
            Text(",\"selected_round_frame\":");Number(g.selected_round_frame);Text("}");
        }
        Text(",\"collection18_material_observation_version\":3");
        Text(",\"collection18_writer_observation_version\":2,\"collection18_witness\":");
        if(!c18_.selected){Text("null");return;}const auto& h=c18_;
        Text("{\"scope\":\"known_native_writers_only\",\"selected\":true,\"snapshot_complete\":false,\"entry_state_witness_implemented\":false,\"writer_coverage_proven\":false,\"allocation_generation_proven\":false,\"receiver_undo_proven\":false,\"native_completion_proven\":false");
        Text(",\"mutation_entry_is_write_proof\":false,\"sequence_is_allocation_generation\":false");
        Text(",\"image_base\":");Number(base_);Text(",\"observation_start_sequence\":");Number(c18_start_sequence_);
        Text(",\"close_sequence\":");Number(h.close_sequence);
        Text(",\"retained_writer_count\":");Number(h.writer_count);Text(",\"attempted_writer_count\":");Number(h.attempted);
        Text(",\"omitted_writer_count\":");Number(h.omitted);Text(",\"tracking_overflow_count\":");Number(h.tracking_overflow);
        Text(",\"untracked_at_entry\":");Number(h.untracked_at_entry);Text(",\"untracked_at_close\":");Number(h.untracked_at_close);
        Text(",\"pending_writer_count\":");Number(h.pending_writers);
        Text(",\"pending_writers_at_close\":");Number(h.pending_writers_at_close);
        Text(",\"collection\":");Number(h.collection);Text(",\"descriptor\":");Number(h.descriptor);
        Text(",\"storage\":");Number(h.storage);Text(",\"count\":");Signed(h.count);Text(",\"capacity\":");Signed(h.capacity);
        Text(",\"recursion\":");Signed(h.recursion);Text(",\"hub\":");IdentityJson(h.hub);
        Text(",\"thread\":");Number(h.thread);Text(",\"entry_sequence\":");Number(h.entry);Text(",\"return_sequence\":");Number(h.end);
        Text(",\"returned\":");Flag(h.returned);Text(",\"invalidated\":");Flag(h.invalidated);
        Text(",\"reentry_observed\":");Flag(h.reentry);Text(",\"recursive_writer_observed\":");Flag(h.recursive_writer);
        Text(",\"overflow\":");Flag(h.overflow);Text(",\"unknown_writer\":");Flag(h.unknown_writer);
        Text(",\"header_valid\":");Flag(h.header_valid);Text(",\"pending_at_close\":");Flag(h.pending_at_close);
        Text(",\"overlap\":");Flag(h.overlap);Text(",\"writers\":[");
        for(unsigned i=0;i<h.writer_count;++i) {
            const auto& w=h.writers[i];if(i)Text(",");Text("{\"pair\":");Number(w.pair);
            Text(",\"parent_pair\":");Number(w.parent);Text(",\"entry_rva\":");Number(w.rva);
            Text(",\"entry_sequence\":");Number(w.entry);Text(",\"return_sequence\":");Number(w.returned_sequence);
            Text(",\"object\":");Number(w.object);Text(",\"argument\":");Number(w.argument);
            Text(",\"caller\":");Number(w.caller);Text(",\"thread\":");Number(w.thread);Text(",\"raw_result\":");Number(w.result);
            Text(",\"returned\":");Flag(w.returned);Text(",\"preexisting\":");Flag(w.preexisting);
            Text(",\"association_unknown\":");Flag(w.association_unknown);Text("}");
        }
        Text("]");C18MaterialJson();C18ZeroJson();C18CorrelationJson();Text("}");
    }
    static void C18CorrelationJson() noexcept {
        // Lock order is VFX -> material only. Material hooks never acquire the
        // VFX lock or persist. Publication is retrospective, not crash coverage.
        auto& c=c18_correlation_scratch_;NativeReplayMaterialTaskGuard::CopyC18Correlation(c);
        Text(",\"start_helper_correlation\":");if(!c.selected){Text("null");return;}
        Text("{\"version\":1,\"scope\":\"selected_c18_broadcast_native_interval\",\"ownership_proven\":false");
        Text(",\"mid_writes_observed\":false,\"mid_slot\":null,\"native_tick\":null,\"provider_body\":null");
        Text(",\"color_source\":null,\"callback_listener_ordinal\":null,\"entry_persisted_before_native\":false");
        Text(",\"c18_entry_sequence\":");Number(c.c18_entry);Text(",\"thread\":");Number(c.thread);
        Text(",\"ended\":");Flag(c.ended);Text(",\"active\":");Flag(c.active);
        Text(",\"complete\":");Flag(c.Complete());Text(",\"guard_ready\":");Flag(c.guard_ready);
        Text(",\"guard_lost\":");Flag(c.guard_lost);Text(",\"preexisting\":");Flag(c.preexisting);
        Text(",\"invalidated\":");Flag(c.invalidated);Text(",\"attempted\":");Number(c.attempted);
        Text(",\"omitted\":");Number(c.omitted);Text(",\"foreign_entries\":");Number(c.foreign_entries);
        Text(",\"pending\":");Number(c.pending);
        const bool census=!C18MaterialFailure()
            || (c18_zero_.ready && !c18_zero_.failure && !C18ZeroInterrupted());
        Text(",\"entry_census_valid\":");Flag(census);
        Text(",\"calls\":[");
        for(unsigned i=0;i<c.count;++i) {
            const auto& r=c.calls[i];if(i)Text(",");Text("{\"id\":");Number(r.id);
            Text(",\"parent_id\":");Number(r.parent);Text(",\"start_id\":");Number(r.start);
            Text(",\"ordinal\":");Number(r.ordinal);Text(",\"family_ordinal\":");Number(r.family_ordinal);
            Text(",\"family\":\"");Text(r.family?"helper":"start");Text("\",\"entry_sequence\":");Number(r.entry);
            Text(",\"return_sequence\":");Number(r.end);Text(",\"thread\":");Number(r.thread);
            Text(",\"depth\":");Number(r.depth);Text(",\"caller\":");Number(r.caller);
            Text(",\"on_selected_thread\":");Flag(r.selected_thread);Text(",\"returned\":");Flag(r.returned);
            Text(",\"component\":");Number(r.component);Text(",\"request\":");Number(r.request);
            Text(",\"actor\":");Number(r.actor);Text(",\"provider\":");Number(r.provider);
            Text(",\"provider_read\":");Flag(r.provider_read);
            Text(",\"provider_link_matches\":");Flag(r.provider_link_matches);
            Text(",\"fname\":");Number(r.name);Text(",\"color_pointer\":");Number(r.color);
            Text(",\"color_read\":");Flag(r.color_read);Text(",\"color_stable\":");Flag(r.color_stable);
            Text(",\"color_bits\":[");for(unsigned n=0;n<4;++n){if(n)Text(",");Number(r.bits[n]);}Text("]");
            // Exact pointer equality to the independently sampled C18 graph.
            // These are entry-snapshot matches, never current membership, a
            // unique listener attribution, or proof of a particular MID write.
            std::uintptr_t component=r.component;
            if(r.family)for(unsigned n=0;n<c.count;++n)if(c.calls[n].id==r.start)component=c.calls[n].component;
            Text(",\"entry_listener_matches\":[");bool comma=false;
            if(census)for(unsigned n=0;n<c18_material_.listener_count;++n)
                if(c18_material_.listeners[n].root.address==component) {
                    if(comma)Text(",");Number(n);comma=true;
                }
            Text("],\"entry_provider_matches\":[");comma=false;
            if(census && r.family && r.provider_link_matches)for(unsigned n=0;n<c18_material_.provider_count;++n) {
                const auto& p=c18_material_.providers[n];
                if(c18_material_.listeners[p.listener].root.address==component
                    && p.actor.address==r.actor && p.mesh.address==r.provider) {
                    if(comma)Text(",");Number(n);comma=true;
                }
            }
            Text("]}");
        }
        const auto& m=c.material;
        Text("],\"material_calls\":{\"version\":1,\"capacity\":128,\"count_calls_observed\":false");
        Text(",\"ownership_proven\":false,\"vector_row_write_proven\":false,\"material_source\":null");
        Text(",\"concurrent_mutation_excluded\":false,\"native_tick\":null");
        Text(",\"ready\":");Flag(m.ready);Text(",\"invalidated\":");Flag(m.invalidated);
        Text(",\"reentry\":");Flag(m.reentry);Text(",\"attempted\":");Number(m.attempted);
        Text(",\"omitted\":");Number(m.omitted);Text(",\"foreign_entries\":");Number(m.foreign_entries);
        Text(",\"pending\":");Number(m.pending);Text(",\"complete\":");
        Flag(c.Complete() && m.ready && !m.invalidated && !m.pending && !m.omitted && !m.foreign_entries);
        Text(",\"calls\":[");
        for(unsigned i=0;i<m.count;++i) {
            const auto& r=m.calls[i];if(i)Text(",");Text("{\"id\":");Number(r.id);
            Text(",\"helper_id\":");Number(r.helper);Text(",\"start_id\":");Number(r.start);
            Text(",\"parent_call_id\":");Number(r.parent);Text(",\"ordinal\":");Number(i+1);
            Text(",\"family\":\"");Text(r.family?"set":"get");Text("\",\"entry_sequence\":");Number(r.entry);
            Text(",\"return_sequence\":");Number(r.end);Text(",\"thread\":");Number(r.thread);
            Text(",\"returned\":");Flag(r.returned);Text(",\"route_verified\":");Flag(r.route);
            Text(",\"caller_rva\":");Number(r.caller>=base_?r.caller-base_:0);
            Text(",\"caller\":");Number(r.caller);Text(",\"wrapper_caller\":");Number(r.wrapper_caller);
            Text(",\"wrapper_caller_rva\":");Number(r.wrapper_caller>=base_?r.wrapper_caller-base_:0);
            Text(",\"provider\":");Number(r.family?0:r.object);Text(",\"mid\":");Number(r.family?r.object:0);
            Text(",\"slot\":");Signed(r.slot);Text(",\"result\":");Number(r.result);
            Text(",\"getter_id\":");Number(r.getter);Text(",\"getter_linked\":");Flag(r.linked);
            Text(",\"fname\":");Number(r.name);Text(",\"color_pointer\":");Number(r.color);
            Text(",\"color_read\":");Flag(r.color_read);Text(",\"color_stable\":");Flag(r.color_stable);
            Text(",\"color_bits\":[");for(unsigned n=0;n<4;++n){if(n)Text(",");Number(r.bits[n]);}Text("]}");
        }
        Text("]");JsonC18RowDispatch(c);Text("}}");
    }
    static void JsonC18RowDispatch(const NativeReplayMaterialTaskGuard::Correlation& c) noexcept {
        const auto& m=c.material;const auto& d=m.dispatch;
        const bool complete=c.Complete() && m.ready && !m.invalidated && !m.pending && !m.omitted && !m.foreign_entries
            && !d.invalidated && !d.pending && !d.omitted && !d.foreign_entries && !d.unattributed;
        Text(",\"row_dispatch\":{\"version\":1,\"capacity\":64,\"cpu_write_branch_only\":true");
        Text(",\"ownership_proven\":false,\"proxy_completion_proven\":false,\"render_completion_proven\":false,\"gpu_completion_proven\":false");
        Text(",\"ready\":");Flag(m.ready);Text(",\"complete\":");Flag(complete);
        Text(",\"invalidated\":");Flag(d.invalidated);Text(",\"reentry\":");Flag(d.reentry);
        Text(",\"attempted\":");Number(d.attempted);Text(",\"omitted\":");Number(d.omitted);
        Text(",\"foreign_entries\":");Number(d.foreign_entries);Text(",\"unattributed\":");Number(d.unattributed);
        Text(",\"pending\":");Number(d.pending);Text(",\"calls\":[");
        for(unsigned i=0;i<d.count;++i) {
            const auto& r=d.calls[i];if(i)Text(",");Text("{\"id\":");Number(r.id);
            Text(",\"ordinal\":");Number(i+1);Text(",\"setter_id\":");Number(r.setter);
            Text(",\"getter_id\":");Number(r.getter);Text(",\"helper_id\":");Number(r.helper);
            Text(",\"start_id\":");Number(r.start);Text(",\"parent_dispatch_id\":");Number(r.parent);
            Text(",\"thread\":");Number(r.thread);Text(",\"entry_sequence\":");Number(r.entry);
            Text(",\"return_sequence\":");Number(r.end);Text(",\"returned\":");Flag(r.returned);
            Text(",\"caller\":");Number(r.caller);Text(",\"caller_rva\":");Number(r.caller>=base_?r.caller-base_:0);
            Text(",\"mid\":");Number(r.mid);Text(",\"fname\":");Number(r.name);
            Text(",\"row_read\":");Flag(r.row_read);Text(",\"row_stable\":");Flag(r.row_stable);
            Text(",\"matched\":");Flag(r.matched);Text(",\"color_bits\":[");
            for(unsigned n=0;n<4;++n){if(n)Text(",");Number(r.bits[n]);}Text("]}");
        }
        Text("],\"setters\":[");bool comma=false;
        for(unsigned i=0;i<m.count;++i) {
            const auto& s=m.calls[i];if(s.family!=1)continue;
            unsigned observed{},matched{};
            for(unsigned n=0;n<d.count;++n)if(d.calls[n].setter==s.id){++observed;matched+=d.calls[n].matched?1:0;}
            if(comma)Text(",");comma=true;Text("{\"setter_id\":");Number(s.id);
            Text(",\"observed_dispatches\":");Number(observed);Text(",\"matched_dispatches\":");Number(matched);
            Text(",\"cpu_row_write_branch\":");
            if(complete && s.linked && s.returned)Flag(matched!=0);else Text("null");
            Text("}");
        }
        Text("]}");
    }
