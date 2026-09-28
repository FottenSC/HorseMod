#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UObjectArray.hpp>
#include <array>
#include <atomic>
#include <cstdint>
#include "ReplayDiagnosticTrace.hpp"
#include "NativeReplayMaterialTaskGuard.hpp"

namespace Horse::Deterministic {
// One ordinary-forward Start with the create flag set, selected by the existing
// particle diagnostic mask/tick window. Only copied scalars reach the UE4SS log,
// which the runner retains. Nothing here participates in checkpoint/canonical comparisons.
// No getter, serial allocation, UObject scan, callback, or native write.
// Double reads detect instability; they are not synchronization or a lease.
// Zero serials are normal lazy FUObjectItem state. Stable indexed snapshots
// can yield PROVISIONAL retained deltas, never generation/qualification proof.
// Identical-endpoint ABA with a zero serial is intrinsically unobservable.
class NativeReplayTraceStartDiagnostic final {
    static constexpr unsigned ListenerLimit=32,AttachmentLimit=16,RefLimit=256,KeyLimit=256,MemberLimit=4096;
    static constexpr unsigned OutputReservation=65536;
    struct RequestScalars {
        unsigned char parts_id{},packed_life_flag14{},kind_id{};
        bool operator==(const RequestScalars&) const = default;
    };
    struct Identity {
        std::uintptr_t address{},table{};
        std::int32_t index{-1},serial{-1};
        bool valid{};
        bool operator==(const Identity&) const = default;
    };
    struct Header {
        std::uintptr_t data{};std::int32_t count{},capacity{};
        bool operator==(const Header&) const = default;
    };
    struct Ref {
        std::uintptr_t state{},controller{};
        bool operator==(const Ref&) const = default;
    };
    struct RetainedRef {
        Ref pair{};
        std::uintptr_t attachment{};
        int strong{};
        bool operator==(const RetainedRef&) const = default;
    };
    struct Baseline {
        std::array<Identity,KeyLimit> keys{};
        Identity fighter{},manager{};
        unsigned count{};
        bool complete{},manager_read{},generation_unproven{true};
    };
    struct Member {
        bool applicable{},read{};int count{},matches{};
        bool operator==(const Member&) const = default;
    };
    struct Listener {
        std::uintptr_t pointer{},table{},target{};bool read{};
        bool operator==(const Listener&) const = default;
    };
    struct Attachment {
        Identity identity{},owner{};
        std::uintptr_t state{},controller{},target{},owner_pointer{};
        unsigned flags{},collection{},ordinal{};unsigned char type{};
        bool fields_read{},indexed_owner{},exact_owner{};
        std::array<Member,4> membership{};
    };
    struct Snapshot {
        Identity component{},fighter{},manager{};
        std::uintptr_t actor_root{},owner_target{},cached_owner{},transaction{};
        unsigned char dirty{};
        bool manager_read{},transaction_read{},dirty_read{},listeners_read{},traces_read{};
        Header listeners{};
        unsigned listener_count{},attachment_count{},null_attachments{};
        unsigned refs_expected{},refs_read{},collections_read{},null_pairs{},duplicate_refs{};
        unsigned key_count{},old_count{},new_count{},removed_count{},unclassified_count{};
        unsigned exact_keys{},provisional_keys{},removed_details{};
        bool identity_complete{},baseline_complete{},comparison_complete{},details_complete{};
        bool generation_unproven{true};
        bool invalid_header{},ref_overflow{},key_overflow{},detail_overflow{},removed_detail_overflow{},instability{};
        std::array<Identity,KeyLimit> keys{};
        std::array<Identity,AttachmentLimit> removed{};
        std::array<Listener,ListenerLimit> listener{};
        std::array<Attachment,AttachmentLimit> attachment{};
        const RC::CharType* owner_status=STR("no_safe_exact_owner");
        const RC::CharType* attachment_status=STR("unavailable");
    };
    // Bounded stack scratch plus the saved scalar baseline fit the owned
    // material guard's existing 1 MiB process reservation (no native retention).
    static_assert(sizeof(Snapshot)+RefLimit*sizeof(RetainedRef)+sizeof(Baseline)+sizeof(Attachment)+2*sizeof(Header)+4*sizeof(RequestScalars)<32768);
    inline static std::atomic<bool> selected_{};
    inline static Identity entry_component_{};
    inline static Baseline entry_{};
    inline static RequestScalars entry_request_{};
    inline static unsigned tick_{};
    inline static DWORD thread_{};

    template<class T> static bool Read(std::uintptr_t p,std::size_t offset,T& out) noexcept {
        if(!p || offset>UINTPTR_MAX-p || sizeof(T)>UINTPTR_MAX-(p+offset))return false;
        SIZE_T copied{};
        return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(p+offset),
            &out,sizeof(out),&copied) && copied==sizeof(out);
    }
    static bool ReadRequest(std::uintptr_t p,RequestScalars& out) noexcept {
        // StartLuxTraceComponentTrace1408D8C40 takes a 0x30-byte request.
        // +9 false reconfigures collected entries; nonzero calls the factory,
        // which may still return null. Keep the raw flag byte, not a C++ bool.
        // This observes branch input, not a synchronized native branch or birth.
        RequestScalars first{},again{};
        if(!Read(p,0,first.parts_id) || !Read(p,9,first.packed_life_flag14) || !Read(p,0x10,first.kind_id)
            || !Read(p,0,again.parts_id) || !Read(p,9,again.packed_life_flag14) || !Read(p,0x10,again.kind_id)
            || first!=again)return false;
        out=first;return true;
    }
    static bool Indexed(std::int32_t index,std::uintptr_t& address,std::int32_t& serial) noexcept {
        if(index<0)return false;
        __try {
            auto* item=RC::Unreal::FUObjectArray::IndexToObject(index);
            if(!item || !item->IsValid(false))return false;
            address=reinterpret_cast<std::uintptr_t>(item->GetUObject());serial=item->GetSerialNumber();
            return address && serial>=0 && item->IsValid(false)
                && reinterpret_cast<std::uintptr_t>(item->GetUObject())==address && item->GetSerialNumber()==serial;
        } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static Identity Identify(std::uintptr_t p) noexcept {
        Identity result{};result.address=p;
        std::uintptr_t indexed{},table_again{};std::int32_t serial{},index_again{};unsigned flags{},flags_again{};
        if(!Read(p,0xc,result.index) || !Indexed(result.index,indexed,result.serial) || indexed!=p
            || !Read(p,0,result.table) || !Read(p,8,flags) || (flags&0x18000u)
            || !Read(p,0xc,index_again) || index_again!=result.index
            || !Read(p,0,table_again) || table_again!=result.table
            || !Read(p,8,flags_again) || flags_again!=flags
            || !Indexed(result.index,indexed,serial) || indexed!=p || serial!=result.serial)return result;
        result.valid=true;return result;
    }
    static bool Current(const Identity& id) noexcept {
        // Reacquire the saved slot before reading through its saved address.
        // A zero serial permits only stable pointer/index/vtable observation.
        std::uintptr_t indexed{};std::int32_t serial{};
        return id.valid && Indexed(id.index,indexed,serial) && indexed==id.address
            && serial==id.serial && Identify(indexed)==id;
    }
    static const RC::CharType* IdentityTier(const Identity& id) noexcept {
        return !id.valid?STR("UNAVAILABLE"):id.serial>0?STR("EXACT"):STR("PROVISIONAL");
    }
    static const RC::CharType* ObservationTier(const Snapshot& s) noexcept {
        return !s.identity_complete || !s.baseline_complete?STR("UNAVAILABLE"):
            s.generation_unproven?STR("PROVISIONAL"):STR("EXACT");
    }
    static bool Array(const Header& h,unsigned limit) noexcept {
        return h.count>=0 && unsigned(h.count)<=limit && h.capacity>=h.count
            && (!h.capacity || h.data);
    }
    static Member Membership(std::uintptr_t owner,unsigned offset,std::uintptr_t object,bool sparse) noexcept {
        Member m{};m.applicable=true;Header h{},again{};
        if(!Read(owner,offset,h) || !Array(h,MemberLimit))return m;
        m.count=h.count;
        std::uintptr_t bits{},bits_again{};int nbits{},free{},nbits_again{},free_again{};
        if(sparse && (!Read(owner,offset+0x20,bits) || !Read(owner,offset+0x28,nbits)
            || !Read(owner,offset+0x34,free) || nbits!=h.count || free<0 || free>h.count
            || (!bits && nbits>128)))return m;
        const auto bit_address=bits?bits:owner+offset+0x10;
        int live{};
        for(int i=0;i<h.count;++i) {
            unsigned mask{},verify{};
            if(sparse && (!Read(bit_address,std::size_t(i/32)*4,mask)
                || !Read(bit_address,std::size_t(i/32)*4,verify) || mask!=verify))return m;
            if(sparse && !(mask&(1u<<(i%32))))continue;
            std::uintptr_t p{},p_again{};
            if(!Read(h.data,std::size_t(i)*(sparse?16:8),p)
                || !Read(h.data,std::size_t(i)*(sparse?16:8),p_again) || p!=p_again)return m;
            ++live;m.matches+=p==object;
        }
        if(!Read(owner,offset,again) || h!=again)return m;
        if(sparse && (!Read(owner,offset+0x20,bits_again) || bits!=bits_again
            || !Read(owner,offset+0x28,nbits_again) || nbits!=nbits_again
            || !Read(owner,offset+0x34,free_again) || free!=free_again || live!=h.count-free))return m;
        m.read=true;return m;
    }
    static void Listeners(std::uintptr_t base,Snapshot& s) noexcept {
        // AllocateUObjectIndexAndNotifyListeners140F7D100: native TArray at
        // 1442A1290, signed count at1442A1298, NotifyUObjectCreated vslot+8.
        Header again{};
        if(!Read(base,0x42a1290,s.listeners) || !Array(s.listeners,ListenerLimit))return;
        bool complete=true;
        for(int i=0;i<s.listeners.count;++i) {
            auto& l=s.listener[s.listener_count++];Listener check{};
            l.read=Read(s.listeners.data,std::size_t(i)*8,l.pointer)
                && Read(l.pointer,0,l.table) && Read(l.table,8,l.target);
            check.read=Read(s.listeners.data,std::size_t(i)*8,check.pointer)
                && Read(check.pointer,0,check.table) && Read(check.table,8,check.target);
            l.read=l.read && check.read && l.pointer==check.pointer && l.table==check.table && l.target==check.target;
            complete&=l.read;
        }
        s.listeners_read=complete && Read(base,0x42a1290,again) && again==s.listeners;
    }
    static void Owner(std::uintptr_t base,Snapshot& s) noexcept {
        // Same exact target path as Sc6ReplayTraceState/C18: root+490 ->
        // character+458 -> manager+3A8 == root. No GetOwner call/Outer guess.
        std::uintptr_t p{},back{},cached_again{},root_again{},target_again{},fighter_again{},manager_again{};
        if(!s.component.valid || s.component.table!=base+0x3360ca8
            || !Read(s.component.address,0x490,p))return;
        s.fighter=Identify(p);
        if(!s.fighter.valid || !Read(p,0x458,p))return;
        s.manager=Identify(p);
        if(!s.manager.valid || s.manager.table!=base+0x3361f98
            || !Read(p,0x3a8,back) || back!=s.component.address
            || !Read(p,0x168,s.actor_root) || !Read(s.manager.table,0x70,s.owner_target)
            || !Read(s.component.address,0x190,s.cached_owner) || s.cached_owner!=p
            || !Read(s.component.address,0x190,cached_again) || cached_again!=p
            || !Read(s.component.address,0x490,fighter_again) || fighter_again!=s.fighter.address
            || !Read(s.fighter.address,0x458,manager_again) || manager_again!=s.manager.address
            || !Read(p,0x3a8,back) || back!=s.component.address
            || !Read(p,0x168,root_again) || root_again!=s.actor_root
            || !Read(s.manager.table,0x70,target_again) || target_again!=s.owner_target
            || !Current(s.component) || !Current(s.fighter) || !Current(s.manager))return;
        s.manager_read=true;s.owner_status=STR("indexed_manager_and_cached_owner_match");
    }
    static bool CurrentOwner(const Snapshot& s) noexcept {
        std::uintptr_t p{},fighter{},manager{},back{},root{},target{};
        return Current(s.component) && Current(s.fighter) && Current(s.manager)
            && Read(s.component.address,0x190,p) && p==s.cached_owner
            && Read(s.component.address,0x490,fighter) && fighter==s.fighter.address
            && Read(s.fighter.address,0x458,manager) && manager==s.manager.address
            && Read(s.manager.address,0x3a8,back) && back==s.component.address
            && Read(s.manager.address,0x168,root) && root==s.actor_root
            && Read(s.manager.table,0x70,target) && target==s.owner_target
            && Current(s.component) && Current(s.fighter) && Current(s.manager);
    }
    static void InspectAttachment(std::uintptr_t base,Snapshot& s,Attachment& a) noexcept {
        if(!a.identity.valid || a.identity.table!=base+0x3360370)return;
        unsigned flags_again{};unsigned char type_again{};std::uintptr_t owner_again{},target_again{};
        a.fields_read=Read(a.identity.address,0x188,a.flags) && Read(a.identity.address,0x18d,a.type)
            && Read(a.identity.address,0x190,a.owner_pointer) && Read(a.identity.table,0x370,a.target)
            && Read(a.identity.address,0x188,flags_again) && flags_again==a.flags
            && Read(a.identity.address,0x18d,type_again) && type_again==a.type
            && Read(a.identity.address,0x190,owner_again) && owner_again==a.owner_pointer
            && Read(a.identity.table,0x370,target_again) && target_again==a.target && Current(a.identity);
        if(!a.fields_read)return;
        a.owner=Identify(a.owner_pointer);
        // Membership offsets below are verified on the exact trace manager.
        // A different/missing owner is recorded, never treated as absent membership.
        a.indexed_owner=s.manager_read && a.owner.valid && a.owner==s.manager;
        a.exact_owner=a.indexed_owner && a.owner.serial>0;
        if(!a.indexed_owner)return;
        a.membership[0]=Membership(a.owner_pointer,0x2c0,a.identity.address,true);
        a.membership[1]=Membership(a.owner_pointer,0x310,a.identity.address,true);
        // DestroyActorComponent141D41970 and owner removal141C27A20:
        // type1/2 uses+360; other types pass through instance removal+370.
        const bool construction=a.type==1 || a.type==2;
        a.membership[construction?2:3]=Membership(a.owner_pointer,construction?0x360:0x370,a.identity.address,false);
        if(!Current(a.identity) || !Current(a.owner)) {
            a.indexed_owner=false;a.exact_owner=false;for(auto& m:a.membership)m.read=false;
        }
    }
    static bool Unstable(Snapshot& s) noexcept {
        s.instability=true;s.attachment_status=STR("unstable_or_changed_identity");
        s.identity_complete=s.comparison_complete=s.details_complete=s.traces_read=false;
        s.generation_unproven=true;
        s.old_count=s.new_count=s.removed_count=s.attachment_count=s.removed_details=0;
        s.unclassified_count=s.key_count;return false;
    }
    static bool ReadRetained(std::uintptr_t base,std::uintptr_t data,unsigned ordinal,
        RetainedRef& r,Snapshot& s) noexcept {
        Ref check{};std::uintptr_t table{},table_again{},p_again{};int strong_again{};
        if(!Read(data,std::size_t(ordinal)*sizeof(Ref),r.pair))return false;
        const auto& ref=r.pair;
        if(ref.state || ref.controller) {
            if(!ref.controller || ref.controller>UINTPTR_MAX-16 || ref.state!=ref.controller+16) {
                s.attachment_status=STR("invalid_retained_pair");return false;
            }
            if(!Read(ref.controller,0,table) || !Read(ref.controller,8,r.strong)
                || !Read(ref.state,0xb8,r.attachment) || !Read(ref.state,0xb8,p_again)
                || !Read(ref.controller,8,strong_again) || !Read(ref.controller,0,table_again))return false;
            if(table!=table_again || r.strong!=strong_again || r.attachment!=p_again)return Unstable(s);
            if(table!=base+0x3362590 || r.strong<=0) {
                s.attachment_status=STR("invalid_retained_controller");return false;
            }
        }
        if(!Read(data,std::size_t(ordinal)*sizeof(Ref),check))return false;
        return check==ref || Unstable(s);
    }
    static void Attachments(std::uintptr_t base,Snapshot& s,bool returning) noexcept {
        s.baseline_complete=returning && entry_.complete;
        if(!s.component.valid || s.component.table!=base+0x3360ca8 || !s.manager_read){Unstable(s);return;}
        s.attachment_status=STR("unreadable_retained_data");
        if(returning && entry_.complete && entry_.manager_read
            && (!s.manager_read || s.manager!=entry_.manager || s.fighter!=entry_.fighter)) {
            Unstable(s);return;
        }
        s.generation_unproven=s.component.serial==0 || !s.manager_read
            || s.manager.serial<=0 || s.fighter.serial<=0 || (returning && entry_.generation_unproven);
        // Start is void; these are strong retained state+B8 observations, not
        // returned factory pairs. Null factory births outside these arrays are
        // invisible. No weak promotion, serial allocation or refcount change.
        constexpr unsigned collections[]{0x418,0x428};
        std::array<Header,2> headers{};
        std::array<RetainedRef,RefLimit> refs{};
        for(unsigned c=0;c<2;++c) {
            auto& h=headers[c];
            if(!Read(s.component.address,collections[c],h))return;
            if(h.count<0 || h.capacity<h.count || (h.capacity && !h.data)
                || (h.count>0 && std::size_t(h.count)>(UINTPTR_MAX-h.data)/sizeof(Ref))) {
                s.invalid_header=true;s.attachment_status=STR("invalid_retained_header");return;
            }
            // RefLimit covers both arrays together, including duplicates/nulls.
            if(unsigned(h.count)>RefLimit-s.refs_expected) {
                s.ref_overflow=true;s.attachment_status=STR("retained_ref_overflow");return;
            }
            s.refs_expected+=unsigned(h.count);
        }
        unsigned pending_details{};
        for(unsigned c=0;c<2;++c) {
            const auto& h=headers[c];
            for(int i=0;i<h.count;++i) {
                auto& r=refs[s.refs_read];
                if(!ReadRetained(base,h.data,unsigned(i),r,s))return;
                ++s.refs_read;
                if(!r.pair.state){++s.null_pairs;continue;}
                if(!r.attachment){++s.null_attachments;continue;}
                const auto id=Identify(r.attachment);
                if(!id.valid || id.table!=base+0x3360370) {
                    Unstable(s);return;
                }
                // Zero serial is a stable snapshot candidate, not an exact
                // generation. Never allocate/promote it to make observation pass.
                bool duplicate=false;
                for(unsigned j=0;j<s.key_count;++j) {
                    const auto& key=s.keys[j];
                    if(key.address!=id.address && key.index!=id.index)continue;
                    if(key!=id){Unstable(s);return;}
                    duplicate=true;
                }
                if(duplicate){++s.duplicate_refs;continue;}
                if(s.key_count==KeyLimit) {
                    s.key_overflow=true;s.attachment_status=STR("attachment_key_overflow");return;
                }
                s.keys[s.key_count++]=id;
                if(id.serial>0)++s.exact_keys;
                else {++s.provisional_keys;s.generation_unproven=true;}
                if(!returning){++s.old_count;continue;}
                if(!entry_.complete){++s.unclassified_count;continue;}
                bool old=false;
                for(unsigned j=0;j<entry_.count;++j) {
                    const auto& key=entry_.keys[j];
                    if(key.address!=id.address && key.index!=id.index)continue;
                    if(key!=id){Unstable(s);return;}
                    old=true;
                }
                if(old){++s.old_count;continue;}
                ++s.new_count;
                if(pending_details==AttachmentLimit){s.detail_overflow=true;continue;}
                auto& a=s.attachment[pending_details++];a.identity=id;
                a.state=r.pair.state;a.controller=r.pair.controller;a.collection=collections[c];a.ordinal=unsigned(i);
            }
            ++s.collections_read;
        }
        // Only new keys consume detail work. Keep their records unpublished
        // until the final validation also covers reads made by membership.
        bool details_complete=!s.detail_overflow;
        for(unsigned i=0;i<pending_details;++i) {
            auto& a=s.attachment[i];InspectAttachment(base,s,a);
            if(!a.fields_read || !Current(a.identity) || (a.owner.valid && !Current(a.owner))){Unstable(s);return;}
            details_complete&=a.indexed_owner;
            for(const auto& m:a.membership)if(m.applicable && !m.read){Unstable(s);return;}
            // Fields and observed memberships must also survive their full
            // detail read. A zero-serial manager is allowed, with the caveat.
            Attachment check{};check.identity=a.identity;InspectAttachment(base,s,check);
            if(!check.fields_read || a.flags!=check.flags || a.type!=check.type
                || a.owner_pointer!=check.owner_pointer || a.target!=check.target
                || a.owner!=check.owner || a.indexed_owner!=check.indexed_owner
                || a.membership!=check.membership){Unstable(s);return;}
        }
        // Validate the entire borrowed array span, refs and generations again
        // after both collections; per-element double reads alone miss changes
        // to early elements while later elements are being sampled.
        unsigned ordinal{};
        for(unsigned c=0;c<2;++c) {
            const auto& h=headers[c];
            for(int i=0;i<h.count;++i) {
                RetainedRef again{};
                if(!ReadRetained(base,h.data,unsigned(i),again,s))return;
                if(again!=refs[ordinal++]){Unstable(s);return;}
            }
        }
        for(unsigned c=0;c<2;++c) {
            Header again{};
            if(!Read(s.component.address,collections[c],again))return;
            if(again!=headers[c]){Unstable(s);return;}
        }
        for(unsigned j=0;j<s.key_count;++j)if(!Current(s.keys[j])){Unstable(s);return;}
        // Missing retained references establish only removal from these arrays,
        // never destruction. An expired/reused indexed key is inconclusive.
        if(returning && entry_.complete) {
            for(unsigned j=0;j<entry_.count;++j) {
                const auto& key=entry_.keys[j];
                if(!Current(key)){Unstable(s);return;}
                bool retained=false;
                for(unsigned k=0;k<s.key_count;++k)retained|=key==s.keys[k];
                if(retained)continue;
                ++s.removed_count;
                if(s.removed_details<AttachmentLimit)s.removed[s.removed_details++]=key;
                else s.removed_detail_overflow=true;
            }
        }
        if(!Current(s.component) || (s.manager_read && !CurrentOwner(s))){Unstable(s);return;}
        s.identity_complete=true;
        if(!returning) {
            s.baseline_complete=true;s.details_complete=true;s.traces_read=true;
            s.attachment_status=STR("entry_retained_identity_baseline");return;
        }
        if(!entry_.complete){s.details_complete=false;s.attachment_status=STR("entry_baseline_incomplete");return;}
        s.comparison_complete=true;s.attachment_count=pending_details;
        s.details_complete=details_complete && !s.removed_detail_overflow;
        s.traces_read=s.details_complete && !s.instability;
        if(s.instability)return;
        s.attachment_status=s.detail_overflow?STR("new_attachment_detail_overflow"):
            s.removed_detail_overflow?STR("removed_attachment_detail_overflow"):
            !s.details_complete?STR("new_attachment_details_incomplete"):
            s.new_count?STR("added_retained_attachment_candidates"):
            s.removed_count?STR("removed_retained_attachment_candidates"):STR("no_retained_attachment_delta");
    }
    static void EmitCreation() {
        NativeReplayMaterialTaskGuard::CreationObservation c{};
        NativeReplayMaterialTaskGuard::CopyCreationObservation(c);
        if(!c.selected)return; // Direct census fixtures do not simulate a native call.
        // At most one summary, eight scalar rows and one end marker. Each row
        // is under 768 UTF-8 bytes even with all integers at maximum width.
        // Reserve separately so the existing 64 KiB census allowance is intact.
        const auto admitted=g_replay_diagnostics.Admit(ReplayDiagnosticTrace::Particles,tick_,8192);
        if(admitted!=1) {
            if(admitted<0)RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] trace creation unavailable event=1 reason=diagnostic_byte_limit required_bytes=8192 diagnostic_only=true\n"));
            return;
        }
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] trace creation summary event=1 tick={} start_token={} component={:x} thread={} ready={} ended={} complete={} invalidated={} reentry={} preexisting={} factory_calls={} construct_calls={} records={} pending={} omitted={} foreign_entries={} birth_journal_complete=false ownership_proven=false qualification=false read_only=true diagnostic_only=true\n"),
            tick_,c.start,c.component,c.thread,c.ready,c.ended,c.Complete(),c.invalidated,c.reentry,c.preexisting,
            c.attempted[0],c.attempted[1],c.count,c.pending,c.omitted,c.foreign_entries);
        for(unsigned i=0;i<c.count;++i) {
            const auto& r=c.calls[i];
            const auto* tier=!r.identity.valid?STR("UNAVAILABLE"):r.identity.serial>0?STR("EXACT"):STR("PROVISIONAL");
            RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] trace creation call event=1 start_token={} id={} family={} parent_factory={} thread={} entry={} end={} returned={} caller={:x} object={:x} argument={:x} output={:x} setting={} kind={} result={:x} pair_stable={} state={:x} controller={:x} index={} serial={} vtable={:x} identity_tier={} zero_serial_aba_excluded=false diagnostic_only=true\n"),
                c.start,i+1,r.family?STR("construct"):STR("factory"),r.parent,r.thread,r.entry,r.end,r.returned,r.caller,
                r.object,r.argument,r.output,r.setting,unsigned(r.kind),r.result,r.pair_stable,r.state,r.controller,
                r.identity.index,r.identity.serial,r.identity.table,tier);
        }
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] trace creation end event=1 output_complete=true diagnostic_only=true\n"));
    }
    static void Emit(const RC::CharType* phase,const Snapshot& s) {
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] trace Start diagnostic event=1 phase={} tick={} tick_coordinate=engine_frame470D0C4 thread={} request_source=entry_copy request_double_read=true create_branch={} packed_life_flag14={} parts_id={} kind_id={} diagnostic_only=true read_only=true qualification=false start_returns_void=true birth_journal_complete=false ownership_proven=false listeners_read={} listener_count={} listener_records={} listener_limit={} transaction_read={} transaction={:x} dirty_read={} dirty={}\n"),
            phase,tick_,thread_,entry_request_.packed_life_flag14!=0,unsigned(entry_request_.packed_life_flag14),
            unsigned(entry_request_.parts_id),unsigned(entry_request_.kind_id),s.listeners_read,s.listeners.count,s.listener_count,ListenerLimit,
            s.transaction_read,s.transaction,s.dirty_read,unsigned(s.dirty));
        for(unsigned i=0;i<s.listener_count;++i) {
            const auto& l=s.listener[i];
            RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] trace Start listener event=1 phase={} ordinal={} read={} pointer={:x} vtable={:x} vtable8_target={:x} diagnostic_only=true\n"),
                phase,i,l.read,l.pointer,l.table,l.target);
        }
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] trace Start owner event=1 phase={} status={} component={:x} component_index={} component_serial={} component_valid={} manager={:x} manager_index={} manager_serial={} manager_valid={} manager_vtable={:x} actor_root168={:x} owner_vtable70_target={:x} component_owner190={:x} traces_read={} attachment_status={} attachment_count={} null_attachment_states={} manager_identity_tier={} observation_tier={} generation_unproven={} qualification=false diagnostic_only=true\n"),
            phase,s.owner_status,s.component.address,s.component.index,s.component.serial,s.component.valid,
            s.manager.address,s.manager.index,s.manager.serial,s.manager.valid,s.manager.table,s.actor_root,
            s.owner_target,s.cached_owner,s.traces_read,s.attachment_status,s.attachment_count,s.null_attachments,
            IdentityTier(s.manager),ObservationTier(s),s.generation_unproven);
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] trace Start census event=1 phase={} traces_read={} status={} baseline_complete={} baseline_attachment_keys={} identity_coverage_complete={} comparison_complete={} details_complete={} collections_read={} collections_expected=2 refs_read={} refs_expected={} ref_limit={} unique_attachment_keys={} key_limit={} old_attachment_keys={} new_attachment_keys={} unclassified_attachment_keys={} duplicate_attachment_refs={} null_pairs={} null_attachment_states={} attachment_details={} detail_limit={} invalid_header={} ref_overflow={} key_overflow={} detail_overflow={} instability={} removed_attachment_keys={} removed_details={} removed_detail_overflow={} exact_generation_keys={} provisional_snapshot_keys={} observation_tier={} generation_unproven={} qualification=false identity_basis=indexed_pointer_index_vtable_optional_serial zero_serial_aba_excluded=false census_returned_pair_observed=false census_factory_null_births_unobserved=true birth_journal_complete=false diagnostic_only=true\n"),
            phase,s.traces_read,s.attachment_status,s.baseline_complete,entry_.count,s.identity_complete,
            s.comparison_complete,s.details_complete,s.collections_read,s.refs_read,s.refs_expected,RefLimit,
            s.key_count,KeyLimit,s.old_count,s.new_count,s.unclassified_count,s.duplicate_refs,s.null_pairs,
            s.null_attachments,s.attachment_count,AttachmentLimit,s.invalid_header,s.ref_overflow,s.key_overflow,
            s.detail_overflow,s.instability,s.removed_count,s.removed_details,s.removed_detail_overflow,
            s.exact_keys,s.provisional_keys,ObservationTier(s),s.generation_unproven);
        for(unsigned i=0;i<s.attachment_count;++i) {
            const auto& a=s.attachment[i];
            const bool generation_unproven=s.generation_unproven || !a.owner.valid || a.owner.serial<=0;
            RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] trace Start attachment event=1 phase={} ordinal={} source=retained_state change=added_candidate collection={:x} state_ordinal={} state={:x} controller={:x} pointer={:x} index={} serial={} valid={} vtable={:x} fields_read={} vtable370_target={:x} flags188={:x} type18d={} owner190={:x} owner_index={} owner_serial={} owner_valid={} exact_manager_owner={} owner_status={} indexed_manager_match={} identity_tier={} observation_tier={} generation_unproven={} qualification=false diagnostic_only=true\n"),
                phase,i,a.collection,a.ordinal,a.state,a.controller,a.identity.address,a.identity.index,a.identity.serial,
                a.identity.valid,a.identity.table,a.fields_read,a.target,a.flags,unsigned(a.type),a.owner_pointer,
                a.owner.index,a.owner.serial,a.owner.valid,a.exact_owner,
                a.indexed_owner?STR("indexed_manager_match"):STR("no_safe_indexed_owner"),a.indexed_owner,
                IdentityTier(a.identity),ObservationTier(s),generation_unproven);
            constexpr unsigned offsets[]{0x2c0,0x310,0x360,0x370};
            for(unsigned n=0;n<4;++n) {
                const auto& m=a.membership[n];
                RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] trace Start membership event=1 phase={} attachment={} offset={:x} applicable={} read={} count={} matches={} observation_tier={} generation_unproven={} qualification=false diagnostic_only=true\n"),
                    phase,i,offsets[n],m.applicable,m.read,m.count,m.matches,ObservationTier(s),generation_unproven);
            }
        }
        for(unsigned i=0;i<s.removed_details;++i) {
            const auto& key=s.removed[i];
            RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] trace Start removed event=1 phase={} ordinal={} source=retained_state change=removed_candidate pointer={:x} index={} serial={} vtable={:x} identity_tier={} observation_tier={} generation_unproven={} logical_destruction_proven=false qualification=false diagnostic_only=true\n"),
                phase,i,key.address,key.index,key.serial,key.table,IdentityTier(key),ObservationTier(s),s.generation_unproven);
        }
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] trace Start diagnostic end event=1 phase={} output_complete=true diagnostic_only=true\n"),phase);
    }
public:
    static void IdentifyConstruction(std::uintptr_t object,NativeReplayMaterialTaskGuard::CreationIdentity& out) noexcept {
        // Called synchronously at the native constructor wrapper's return,
        // after PostInitProperties. No later census dereferences this pointer.
        const auto id=Identify(object);
        out={id.table,id.index,id.serial,id.valid};
    }
    static bool Observe(std::uintptr_t base,void* component,void* request,bool returning) noexcept {
        // Guard wrapper preserves caller/native LastError around both callbacks.
        // Verify the request before claiming selection or diagnostic budget.
        // Nested/concurrent Starts forward normally and cannot overwrite the
        // winner's state. Exit uses only the copied entry request, never rereads it.
        if(!returning) {
            if(selected_.load(std::memory_order_acquire) || !(g_replay_diagnostics.mask&ReplayDiagnosticTrace::Particles))return false;
            unsigned tick{};
            if(!Read(base,0x470d0c4,tick) || tick<g_replay_diagnostics.first_tick || tick>g_replay_diagnostics.last_tick)return false;
            RequestScalars copied{};
            if(!ReadRequest(reinterpret_cast<std::uintptr_t>(request),copied) || !copied.packed_life_flag14)return false;
            if(selected_.exchange(true,std::memory_order_acq_rel))return false;
            const auto admitted=g_replay_diagnostics.Admit(ReplayDiagnosticTrace::Particles,tick,OutputReservation);
            if(admitted!=1) {
                if(admitted<0)try {
                    RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] trace Start diagnostic unavailable tick={} reason=diagnostic_byte_limit required_bytes={} read_only=true diagnostic_only=true\n"),tick,OutputReservation);
                } catch(...) {}
                return false;
            }
            entry_request_=copied;
            tick_=tick;thread_=GetCurrentThreadId();entry_component_=Identify(reinterpret_cast<std::uintptr_t>(component));
            entry_={};
        }
        // Serialize only copied return scalars, independently of whether the
        // retained census can reacquire the component after native Start.
        if(returning)try {EmitCreation();} catch(...) {}
        try {
            Snapshot s{};
            Listeners(base,s);
            s.transaction_read=Read(base,0x418b108,s.transaction);
            s.dirty_read=Read(base,0x418b12f,s.dirty);
            // Reacquire the indexed snapshot on this synchronous Start thread.
            // Zero serial cannot prove a generation, but must not suppress a
            // stable PROVISIONAL census. Current never allocates a serial.
            const bool live=!returning || (thread_==GetCurrentThreadId()
                && reinterpret_cast<std::uintptr_t>(component)==entry_component_.address && Current(entry_component_));
            if(live)s.component=Identify(reinterpret_cast<std::uintptr_t>(component));
            if(!live || s.component!=entry_component_) {
                s.component={};s.owner_status=STR("entry_generation_not_reacquired");
                s.attachment_status=STR("entry_generation_not_reacquired");
                s.instability=true;s.baseline_complete=entry_.complete;
            } else {
                Owner(base,s);Attachments(base,s,returning);
                if(!returning && s.identity_complete) {
                    entry_.keys=s.keys;entry_.count=s.key_count;entry_.complete=true;
                    entry_.fighter=s.fighter;entry_.manager=s.manager;entry_.manager_read=s.manager_read;
                    entry_.generation_unproven=s.generation_unproven;
                }
            }
            Emit(returning?STR("exit"):STR("entry"),s);
        } catch(...) {
            // Diagnostic formatting/storage failure cannot suppress native Start.
            // An absent phase is incomplete evidence, never a successful census.
        }
        return !returning;
    }
};
}
