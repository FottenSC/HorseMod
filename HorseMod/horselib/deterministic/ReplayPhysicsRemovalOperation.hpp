#pragma once
#include "ReplayPhysicsRemovalGuard.hpp"
#include "ReplayPhysicsTaskCompletion.hpp"
#include <cstring>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace Horse::Deterministic {
// Seek-local native operation. The enclosing debris owner retains the scene,
// NpActors, UObjects and render resources. This class never grants those leases
// or classifies collision feedback. No simulation step or global flush occurs.
class ReplayPhysicsRemovalOperation final {
public:
    enum class Phase { Empty, Retained, Removing, Submitted, Detached, Recovering, Recovered, Released, Poisoned };
    enum class Result { Rejected, Pending, Complete, Poisoned };
    ReplayPhysicsRemovalOperation()=default;
    ReplayPhysicsRemovalOperation(const ReplayPhysicsRemovalOperation&)=delete;
    ReplayPhysicsRemovalOperation& operator=(const ReplayPhysicsRemovalOperation&)=delete;
    ~ReplayPhysicsRemovalOperation() {
        // Development guard only. The host must keep pending/poisoned owners;
        // destruction cannot turn a timeout into native task cancellation.
        if(phase_!=Phase::Empty && phase_!=Phase::Retained && phase_!=Phase::Released)
            __fastfail(FAST_FAIL_INVALID_ARG);
    }
    Phase phase() const noexcept {return phase_;}
    const char* failed_check() const noexcept {return failed_check_;}
    bool input_storage_detached() const noexcept {return guard_.InputStorageDetached(Read);}
    std::size_t owned_bytes() const noexcept {return sizeof(*this);}
    bool Prepare(std::uintptr_t module,std::uintptr_t scene,std::span<const std::uintptr_t> bodies) noexcept {
        if(phase_!=Phase::Empty)return false;
        failed_check_="native_identity";
        if(!Identity(module))return false;
        failed_check_="notification_baseline";
        for(unsigned offset:{0x10c4u,0x10fcu}) {
            unsigned pending{};if(!Read(scene+offset,pending) || pending)return false;
        }
        if(!guard_.Capture(Read,module,scene,bodies)) {
            failed_check_=guard_.failed_check()==ReplayPhysicsRemovalGuard::Check::Nodes
                ?"node_work_pending_before_removal":"removal_domain";
            return false;
        }
        module_=module;scene_=scene;count_=static_cast<unsigned>(bodies.size());
        failed_check_="native_bindings";
        std::uintptr_t table{},get_manager{};
        if(!Read(scene+0x738,aabb_) || !Read(aabb_+0x100,sap_) || !Read(scene+0x758,islands_)
            || !Read(scene+0x730,context_) || !context_ || !islands_
            || !Read(scene,table) || !Read(table+0x398,get_manager) || get_manager!=module+0x4ce20
            || !Read(scene+0x1140,elements_) || !elements_)return false;
        if(!GetManager(get_manager,scene_,done_.manager) || !done_.manager)return false;
        unlocked_.manager=done_.manager;
        for(unsigned i=0;i<count_;++i) {
            bodies_[i]=bodies[i];
            if(!Values(module_,bodies[i],values_[i]))return false;
        }
        phase_=Phase::Retained;failed_check_="none";return true;
    }
    // One removal per application poll. Cancellation after the first removal
    // must finish this bounded native batch before Recover; it must not leave
    // pending island/SAP work or re-add into unfinished retirement.
    Result Advance() noexcept {
        if(phase_==Phase::Poisoned)return Result::Poisoned;
        if(phase_==Phase::Detached)return Result::Complete;
        if(phase_==Phase::Retained)phase_=Phase::Removing;
        if(phase_==Phase::Removing) {
            if(removed_<count_) {
                const auto body=bodies_[count_-1-removed_];
                if(!Remove(module_,scene_,body))return Poison("remove_actor");
                ++removed_;
                std::uintptr_t sim{};
                if(!Read(body+0x80,sim) || sim)return Poison("removed_body_binding");
                return Result::Pending;
            }
            if(!guard_.Detached(Read))return Poison("detached_membership");
            if(!Islands(module_,islands_))return Poison("island_retirement");
            unsigned node_mask{};
            if(!guard_.PrepareNodeRetirement(Read,node_mask))return Poison("node_retirement_ownership");
            if(!FinishDirtyNodes(module_,islands_,node_mask) || !guard_.NodeWorkIdle(Read))return Poison("node_retirement_completion");
            const auto* packet=guard_.BuildRemovalPacket(Read);
            if(!packet)return Poison("removal_packet");
            phase_=Phase::Submitted; // Native can publish references immediately.
            if(!Submit(module_,sap_,context_,packet,&done_,&unlocked_))return Poison("native_submission");
            done_.remove();
            return Result::Pending;
        }
        if(phase_!=Phase::Submitted)return Result::Rejected;
        if(!done_.valid() || !unlocked_.valid())return Poison("completion_receipt");
        if(!done_.completed_once() || !unlocked_.completed_once())return Result::Pending;
        std::uintptr_t bitmap{};std::size_t bitmap_bytes{};
        if(!guard_.PrepareIdRetirement(Read,bitmap,bitmap_bytes))return Poison("id_retirement_ownership");
        if(!Retire(module_,scene_,aabb_,sap_,elements_,bitmap,bitmap_bytes))return Poison("native_retirement");
        if(!guard_.Retired(Read))return Poison("retired_membership");
        phase_=Phase::Detached;return Result::Complete;
    }
    Result Recover() noexcept {
        if(phase_==Phase::Poisoned)return Result::Poisoned;
        if(phase_==Phase::Released || phase_==Phase::Recovered)return Result::Complete;
        if(phase_==Phase::Retained) {phase_=Phase::Released;return Result::Complete;}
        if(phase_==Phase::Removing || phase_==Phase::Submitted)return Result::Pending;
        if(phase_==Phase::Detached) {
            if(!guard_.Retired(Read))return Poison("recovery_membership");
            phase_=Phase::Recovering;
        }
        if(phase_!=Phase::Recovering)return Result::Rejected;
        if(recovered_<count_) {
            const auto body=bodies_[recovered_];
            if(!Add(module_,scene_,body))return Poison("readd_actor");
            ++recovered_; // Never repeat an accepted native acquisition.
            std::uintptr_t sim{};
            std::array<std::byte,0xe8> observed{};
            if(!Read(body+0x80,sim) || !sim || !Values(module_,body,observed)
                || observed!=values_[recovered_-1])return Poison("recovered_body_values");
            return Result::Pending;
        }
        // Native re-add queues wake bookkeeping when the scene has an event
        // callback, even without PxActorFlag::eSEND_SLEEP_NOTIFIES. Complete
        // only our callback-ineligible bookkeeping before granting B recovery.
        // Do not advance simulation or discard another owner's notifications.
        if(!RecoveryNotificationsOwned()) {failed_check_="recovery_notification_ownership";return Result::Rejected;}
        if(!FinishNotifications(module_,scene_))return Poison("recovery_notification_retirement");
        for(unsigned offset:{0x10c4u,0x10fcu}) {
            unsigned pending{};if(!Read(scene_+offset,pending) || pending)return Poison("recovery_notification_completion");
        }
        phase_=Phase::Recovered;return Result::Complete;
    }
    // Call only after a joined native boundary. If no later native update has
    // replaced the borrowed inputs, use the verified empty native setter path;
    // never process pending AABB updates merely to finish cancellation.
    bool ReleaseRecoveredStorage() noexcept {
        if(phase_==Phase::Released)return true;
        if(phase_!=Phase::Recovered || !RetireInputBinding())return false;
        phase_=Phase::Released;return true;
    }
    // Enclosing irreversible commit + native/GPU retirement is still required.
    // This only relinquishes the completed physics operation's storage.
    bool ReleaseCommittedStorage() noexcept {
        if(phase_==Phase::Released)return true;
        if(phase_!=Phase::Detached || !RetireInputBinding())return false;
        phase_=Phase::Released;return true;
    }
private:
    bool RecoveryNotificationsOwned() const noexcept {
        for(unsigned offset:{0x1090u,0x10c8u}) {
            // NpScene+10 -> ScScene. Native EE850 consumes dense cores, the
            // bucket backing and free links, and clears only BodySim bits.
            const auto set=scene_+offset;
            std::uintptr_t dense{},links{},buckets{};unsigned capacity{},bucket_count{},count{};
            if(!Read(set+8,dense) || !Read(set+0x10,links) || !Read(set+0x18,buckets)
                || !Read(set+0x20,capacity) || !Read(set+0x24,bucket_count) || !Read(set+0x34,count)
                || count>count_ || capacity>ReplayPhysicsRemovalGuard::max_handles
                || bucket_count>ReplayPhysicsRemovalGuard::max_handles || count>capacity
                || (count && (!dense || !links || !buckets || !bucket_count)))return false;
            std::array<bool,ReplayPhysicsRemovalGuard::max_bodies> found{};
            for(unsigned i=0;i<count;++i) {
                std::uintptr_t core{},sim{},owner{},bound{};unsigned char flags{};
                if(!Read(dense+i*8,core))return false;
                unsigned selected{};while(selected<count_ && bodies_[selected]+0x80!=core)++selected;
                if(selected==count_ || found[selected] || !Read(core,sim) || !sim
                    || !Read(sim+0x40,owner) || owner!=scene_+0x10
                    || !Read(sim+0x48,bound) || bound!=core || !Read(core+0xc,flags) || (flags&4))return false;
                found[selected]=true;
            }
        }
        return true;
    }
    static bool FinishNotifications(std::uintptr_t base,std::uintptr_t scene) noexcept {
        __try {reinterpret_cast<void(*)(void*)>(base+0xee850)(reinterpret_cast<void*>(scene+0x10));return true;}
        __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    struct Reader {
        template<class T> bool operator()(std::uintptr_t address,T& value) const noexcept {
            __try {if(!address)return false;std::memcpy(&value,reinterpret_cast<const void*>(address),sizeof(T));return true;}
            __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
        }
    };
    static constexpr Reader Read{};
    static bool Identity(std::uintptr_t base) noexcept {
        // Verified shipped-native routes. The enclosing session additionally
        // verifies its binary identity; prefixes reject redirected entrypoints.
        struct Signature {unsigned rva;unsigned char bytes[16];};
        static constexpr Signature signatures[]{
            {0x407b0,{0x48,0x89,0x5c,0x24,8,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xec,0x30,0x33}},
            {0x408c0,{0x48,0x89,0x5c,0x24,8,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x30,0x48}},
            {0xee850,{0x48,0x89,0x5c,0x24,0x20,0x55,0x48,0x83,0xec,0x20,0x48,0x89,0x74,0x24,0x30,0x33}},
            {0x1389d0,{0x48,0x89,0x5c,0x24,8,0x57,0x48,0x83,0xec,0x30,0x48,0x8d,0xb9,0x10,3,0}},
            {0x138a70,{0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xec,0x30,0x48}},
            {0x1569c0,{0x48,0x89,0x5c,0x24,8,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57}},
            {0x1570e0,{0x40,0x53,0x56,0x48,0x83,0xec,0x78,0x48,0x8b,2,0x33,0xf6,0x48,0x89,0x81,0x80}},
            {0x150cf0,{0x48,0x89,0x5c,0x24,0x10,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57}},
            {0x156bb0,{0x48,0x89,0x5c,0x24,8,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0x91,0xa0,1,0}},
            {0xe75d0,{0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0x48,0x8b,0x89,0x20,0x11,0,0}},
            {0xfdb00,{0x48,0x89,0x5c,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x33,0xff,0x48,0x8b,0xd9,0x39}},
            {0x149070,{0x44,0x88,0x4c,0x24,0x20,0x44,0x88,0x44,0x24,0x18,0x48,0x89,0x54,0x24,0x10,0x55}}
        };
        __try {
            if(!base || reinterpret_cast<std::uintptr_t>(GetProcAddress(reinterpret_cast<HMODULE>(base),
                "??0PxRigidDynamicGeneratedValues@physx@@QEAA@PEBVPxRigidDynamic@1@@Z"))!=base+0x7810)return false;
            for(const auto& s:signatures)if(std::memcmp(reinterpret_cast<void*>(base+s.rva),s.bytes,16))return false;
            return true;
        } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static bool GetManager(std::uintptr_t function,std::uintptr_t scene,void*& result) noexcept {
        __try {result=reinterpret_cast<void*(*)(void*)>(function)(reinterpret_cast<void*>(scene));return true;}
        __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static bool Values(std::uintptr_t base,std::uintptr_t body,std::array<std::byte,0xe8>& output) noexcept {
        __try {reinterpret_cast<void*(*)(void*,const void*)>(base+0x7810)(output.data(),reinterpret_cast<void*>(body));return true;}
        __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static bool Remove(std::uintptr_t base,std::uintptr_t scene,std::uintptr_t body) noexcept {
        __try {reinterpret_cast<void(*)(void*,void*,bool)>(base+0x408c0)(reinterpret_cast<void*>(scene),reinterpret_cast<void*>(body),false);return true;}
        __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static bool Add(std::uintptr_t base,std::uintptr_t scene,std::uintptr_t body) noexcept {
        __try {reinterpret_cast<void(*)(void*,void*)>(base+0x407b0)(reinterpret_cast<void*>(scene),reinterpret_cast<void*>(body));return true;}
        __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static bool Islands(std::uintptr_t base,std::uintptr_t owner) noexcept {
        __try {reinterpret_cast<void(*)(void*)>(base+0x1389d0)(reinterpret_cast<void*>(owner));
            reinterpret_cast<void(*)(void*)>(base+0x138a70)(reinterpret_cast<void*>(owner));return true;}
        __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static bool Submit(std::uintptr_t base,std::uintptr_t sap,std::uintptr_t context,const void* packet,void* done,void* unlocked) noexcept {
        __try {reinterpret_cast<void(*)(void*,unsigned,void*,const void*,void*,void*)>(base+0x1569c0)(
            reinterpret_cast<void*>(sap),1,reinterpret_cast<void*>(context),packet,done,unlocked);return true;}
        __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static bool FinishDirtyNodes(std::uintptr_t base,std::uintptr_t owner,unsigned mask) noexcept {
        __try {
            for(unsigned slot=0;slot<2;++slot)if(mask&(1u<<slot))
                reinterpret_cast<void(*)(void*,void*,bool,bool,unsigned)>(base+0x149070)(
                    reinterpret_cast<void*>(owner+(slot?0x310u:0xb0u)),reinterpret_cast<void*>(owner+0x30),true,false,1000);
            return true;
        } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static bool Retire(std::uintptr_t base,std::uintptr_t scene,std::uintptr_t aabb,std::uintptr_t sap,std::uintptr_t elements,std::uintptr_t bitmap,std::size_t bitmap_bytes) noexcept {
        __try {
            reinterpret_cast<void(*)(void*)>(base+0x150cf0)(reinterpret_cast<void*>(aabb));
            reinterpret_cast<void(*)(void*)>(base+0x156bb0)(reinterpret_cast<void*>(sap));
            reinterpret_cast<void(*)(void*)>(base+0xe75d0)(reinterpret_cast<void*>(scene+0x10));
            reinterpret_cast<void(*)(void*)>(base+0xfdb00)(reinterpret_cast<void*>(elements));
            // Exact F1ED0 tail after delayed element frees. Earlier active-body
            // publication/contact work is not replayed. The guard proved the
            // entire bitmap/list contains only our completed removals; FDB00
            // never reallocates this bitmap. No expected state is installed.
            if(bitmap_bytes)std::memset(reinterpret_cast<void*>(bitmap),0,bitmap_bytes);
            return true;
        } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static bool BindEmpty(std::uintptr_t base,std::uintptr_t sap,const void* packet) noexcept {
        __try {return (reinterpret_cast<std::uintptr_t(*)(void*,const void*)>(base+0x1570e0)(reinterpret_cast<void*>(sap),packet)&0xff)==1;}
        __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    bool RetireInputBinding() noexcept {
        if(guard_.InputStorageDetached(Read))return true;
        ReplayPhysicsRemovalGuard::Packet empty{};
        if(!done_.completed_once() || !unlocked_.completed_once() || !guard_.PrepareInputRetirement(Read,empty)) {
            failed_check_="input_retirement_admission";return false;
        }
        if(!BindEmpty(module_,sap_,&empty) || !guard_.InputStorageDetached(Read)) {
            Poison("input_retirement_publication");return false;
        }
        return true;
    }
    Result Poison(const char* check) noexcept {failed_check_=check;phase_=Phase::Poisoned;return Result::Poisoned;}
    ReplayPhysicsRemovalGuard guard_;
    ReplayPhysicsTaskCompletion done_,unlocked_;
    std::array<std::uintptr_t,ReplayPhysicsRemovalGuard::max_bodies> bodies_{};
    std::array<std::array<std::byte,0xe8>,ReplayPhysicsRemovalGuard::max_bodies> values_{};
    std::uintptr_t module_{},scene_{},aabb_{},sap_{},islands_{},context_{},elements_{};
    unsigned count_{},removed_{},recovered_{};
    Phase phase_{Phase::Empty};
    const char* failed_check_{"none"};
};
}
