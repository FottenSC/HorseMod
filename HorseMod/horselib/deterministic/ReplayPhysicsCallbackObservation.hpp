#pragma once
#include <Windows.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>

namespace Horse::Deterministic {
// Passive records owned by the permanent native hook owner. No receiver reads,
// leases, admission, generation claims or recovery decisions. Scene/collection
// addresses are opaque process-local arguments (including on native return).
class ReplayPhysicsCallbackObservation final {
public:
    // The selected admission-to-T483 phase exceeded 8192 records. Keep every
    // route, with bounded headroom; overflow still invalidates the observation.
    static constexpr unsigned MaxEvents=16384,NoToken=MaxEvents,MaxPending=128;
    static constexpr std::size_t OwnedBytes=2*1024*1024,LineBytes=1024;
    static bool Start(std::uintptr_t base,const wchar_t* path,const char* run) noexcept {
        const auto error=GetLastError();AcquireSRWLockExclusive(&lock_);
        const bool ok=StartLocked(base,path,run);
        ReleaseSRWLockExclusive(&lock_);SetLastError(error);return ok;
    }
    static unsigned Before(unsigned site,void* collection,void* scene,unsigned type,
        unsigned delta_bits,void* caller) noexcept {
        if(!accepting_.load(std::memory_order_acquire))return NoToken;
        AcquireSRWLockExclusive(&lock_);
        unsigned token=NoToken;
        if(!closed_) {
            // Learn only from verified scene-bearing routes, before forwarding.
            // Append-only opaque addresses: reuse is not a lifetime generation.
            const auto c=reinterpret_cast<std::uintptr_t>(collection),p=reinterpret_cast<std::uintptr_t>(scene);
            if(p && ((site==0 && (c==base_+0x4095f50 || c==base_+0x4095fc0))
                || (site==1 && c>=p && (c-p==0x10 || c-p==0x80))))LearnScene(p);
            Record r{};r.site=site;r.collection=c;r.scene=p;r.type=type;r.delta_bits=delta_bits;
            r.caller=reinterpret_cast<std::uintptr_t>(caller);token=Enter(r);
        }
        ReleaseSRWLockExclusive(&lock_);return token;
    }
    static unsigned BeforeWriter(unsigned site,void* collection,std::uintptr_t handle,
        void* receiver,void* method,void* caller) noexcept {
        if(!accepting_.load(std::memory_order_acquire))return NoToken;
        const auto c=reinterpret_cast<std::uintptr_t>(collection);
        std::uintptr_t scene{};
        // Fixed 32-address filter, no native reads, allocation, stack walk, file
        // I/O or lock for unrelated global delegate traffic. No caller allowlist.
        for(const auto& slot:scenes_) {
            const auto p=slot.load(std::memory_order_acquire);
            if(p && c>=p && (c-p==0x10 || c-p==0x80)){scene=p;break;}
        }
        if(!scene)return NoToken;
        AcquireSRWLockExclusive(&lock_);
        unsigned token=NoToken;
        if(!closed_) {
            Record r{};r.site=site;r.collection=c;r.scene=scene;r.type=~0u;r.handle_argument=handle;
            r.receiver=reinterpret_cast<std::uintptr_t>(receiver);r.method=reinterpret_cast<std::uintptr_t>(method);
            r.caller=reinterpret_cast<std::uintptr_t>(caller);
            if(count_+pending_+2>MaxEvents || pending_==active_.size())Drop();
            else {
                // OS unwind only, bounded; no task/object dereference or new hook.
                if(!CaptureStackBackTrace(1,static_cast<DWORD>(r.ancestry.size()),r.ancestry.data(),nullptr))Drop();
                token=Enter(r);
            }
        }
        ReleaseSRWLockExclusive(&lock_);return token;
    }
    static void After(unsigned token,void* returned_pointer=nullptr) noexcept {
        if(token==NoToken)return;
        AcquireSRWLockExclusive(&lock_);
        // Never inspect retired scene, collection, or receiver storage. Only
        // registration's caller-owned output is copied via a guarded OS read.
        auto& r=active_[token];
        if(r.site==2) {
            r.returned_pointer=reinterpret_cast<std::uintptr_t>(returned_pointer);
            SIZE_T copied{};
            r.handle_read=ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(r.handle_argument),
                &r.returned_handle,sizeof(r.returned_handle),&copied) && copied==sizeof(r.returned_handle);
            if(!r.handle_read)Drop();
        }
        ++count_;--pending_;Event(r,true);r.used=false;
        ReleaseSRWLockExclusive(&lock_);
    }
    static bool ClosePhase(std::uint64_t tick) noexcept {
        const auto error=GetLastError();AcquireSRWLockExclusive(&lock_);
        if(started_ && !closed_) {
            accepting_.store(false,std::memory_order_release);
            closed_=true;pending_at_close_=pending_;close_tick_=tick;
            Begin("phase");Text(",\"boundary\":\"CompleteTrajectory.physics_status\",\"end_tick\":");Number(tick);
            Text(",\"close_thread\":");Number(GetCurrentThreadId());Text(",\"records\":");Number(count_);
            Text(",\"pending_at_close\":");Number(pending_at_close_);Text(",\"dropped\":");Number(dropped_);
            Text(",\"persistence_failed\":");Flag(write_failed_);Text(",\"complete\":");Flag(Complete());Text("}");Emit();
            // A readable final line alone cannot prove FlushFileBuffers
            // succeeded. Publish the closed file only after actual successful
            // flush and handle close. Pending returns keep the journal-owned
            // staging file; an incomplete cutoff can never become complete.
            if(!pending_ && !write_failed_) {
                const bool closed=CloseHandle(file_)!=FALSE;
                file_=INVALID_HANDLE_VALUE;
                if(!closed || !MoveFileExW(temporary_.data(),path_.data(),MOVEFILE_WRITE_THROUGH))write_failed_=true;
                else published_=true;
            }
        }
        const bool ok=Complete() && published_ && close_tick_==tick;
        ReleaseSRWLockExclusive(&lock_);SetLastError(error);return ok;
    }
    static bool ReadStatus(std::uint64_t* output,std::size_t count) noexcept {
        if(!output || count!=4)return false;
        AcquireSRWLockExclusive(&lock_);
        const bool ok=started_;
        output[0]=count_;output[1]=dropped_;output[2]=write_failed_;output[3]=pending_;
        ReleaseSRWLockExclusive(&lock_);return ok;
    }
private:
    struct Record {
        std::uintptr_t collection{},scene{},caller{};
        std::uint64_t pair{},parent{};
        DWORD thread{};
        unsigned site{},type{},delta_bits{},depth{},same_thread_same_scene{},other_thread_same_scene{};
        std::uintptr_t handle_argument{},receiver{},method{},returned_pointer{};
        std::uint64_t returned_handle{};
        std::array<void*,8> ancestry{};
        unsigned handle_read{};
        bool used{};
    };
    static void Drop() noexcept {
        if(dropped_!=~0ull)++dropped_;
        if(dropped_==1){Begin("overflow");Text(",\"dropped\":1,\"lower_bound\":true}");Emit();}
    }
    static void LearnScene(std::uintptr_t scene) noexcept {
        for(auto& slot:scenes_) {
            const auto p=slot.load(std::memory_order_relaxed);
            if(p==scene)return;
            if(!p){slot.store(scene,std::memory_order_release);return;}
        }
        Drop(); // No eviction: a full registry invalidates coverage explicitly.
    }
    static unsigned Enter(Record r) noexcept {
        if(count_+pending_+2>MaxEvents || pending_==active_.size()){Drop();return NoToken;}
        unsigned token{};while(active_[token].used)++token;
        r.used=true;r.pair=++next_pair_;r.thread=GetCurrentThreadId();r.depth=1;
        for(const auto& a:active_)if(a.used) {
            if(a.scene==r.scene) {
                if(a.thread==r.thread)++r.same_thread_same_scene;
                else ++r.other_thread_same_scene;
            }
            if(a.thread==r.thread && a.depth>=r.depth){r.depth=a.depth+1;r.parent=a.pair;}
        }
        active_[token]=r;++pending_;++count_;Event(r,false);return token;
    }
    static bool Complete() noexcept {
        return started_ && closed_ && !pending_at_close_ && !pending_ && !dropped_ && !write_failed_;
    }
    static bool StartLocked(std::uintptr_t base,const wchar_t* path,const char* run) noexcept {
        if(started_ || !base || !path || !run)return false;
        unsigned n{};
        for(;run[n];++n)if(n>=96 || !((run[n]>='a'&&run[n]<='z') || (run[n]>='0'&&run[n]<='9') || run[n]=='-'))return false;
        if(!n)return false;
        unsigned length{};for(;path[length];++length)if(length>=path_.size()-5)return false;
        if(!length)return false;
        std::memcpy(path_.data(),path,(length+1)*sizeof(wchar_t));
        std::memcpy(temporary_.data(),path,length*sizeof(wchar_t));
        std::memcpy(temporary_.data()+length,L".tmp",5*sizeof(wchar_t));
        // Runner parks/journals this file. Never overwrite prior evidence.
        if(GetFileAttributesW(path)!=INVALID_FILE_ATTRIBUTES
            || GetFileAttributesW(temporary_.data())!=INVALID_FILE_ATTRIBUTES)return false;
        file_=CreateFileW(temporary_.data(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,nullptr);
        if(file_==INVALID_HANDLE_VALUE)return false;
        started_=true;base_=base;
        Begin("header");Text(",\"version\":2,\"run_id\":\"");Text(run);Text("\",\"pid\":");Number(GetCurrentProcessId());
        Text(",\"observation_only\":true,\"opaque_scene_identity\":true,\"lifetime_proven\":false,\"recovery_proven\":false");
        Text(",\"writer_coverage_proven\":false,\"native_quiescence_proven\":false,\"whole_process_coverage\":false");
        Text(",\"scope\":\"observer_admission_to_trajectory_completion\",\"max_events\":");Number(MaxEvents);
        Text(",\"overlap_sample\":\"entry\",\"preexisting_calls_covered\":false");
        Text(",\"writer_filter\":\"observed_scene_collections_32\",\"pre_discovery_writers_covered\":false");
        Text(",\"max_pending\":");Number(MaxPending);
        Text(",\"owned_bytes\":");Number(OwnedBytes);Text(",\"image_base\":");Number(base_);
        Text(",\"entry_rvas\":[");Number(0x29b4530);Text(",");Number(0x20113e0);Text(",");Number(0x21377c0);Text(",");Number(0x1f605a0);Text("]}");
        Emit();accepting_.store(!write_failed_,std::memory_order_release);return !write_failed_;
    }
    static const char* Route(const Record& r) noexcept {
        if(r.site==0) {
            if(r.collection==base_+0x4095f50)return "scene_startup";
            if(r.collection==base_+0x4095fc0)return "scene_shutdown";
            return "other_lifecycle_collection";
        }
        if(r.scene && r.collection>=r.scene) {
            if(r.collection-r.scene==0x10)return "physics_pre_step";
            if(r.collection-r.scene==0x80)return "physics_step";
        }
        return "other_physics_collection";
    }
    static void Event(const Record& r,bool returning) noexcept {
        Begin("event");Text(",\"entry_rva\":");Number(r.site==0?0x29b4530:r.site==1?0x20113e0:r.site==2?0x21377c0:0x1f605a0);
        Text(",\"boundary\":\"");Text(returning?"return":"entry");Text("\",\"route\":\"");Text(Route(r));
        Text("\",\"pair\":");Number(r.pair);Text(",\"parent_pair\":");Number(r.parent);
        Text(",\"thread\":");Number(GetCurrentThreadId());Text(",\"depth\":");Number(r.depth);
        Text(",\"same_thread_same_scene\":");Number(r.same_thread_same_scene);
        Text(",\"other_thread_same_scene\":");Number(r.other_thread_same_scene);
        Text(",\"collection\":");Number(r.collection);Text(",\"scene\":");Number(r.scene);
        Text(",\"scene_type\":");Number(r.type);Text(",\"delta_bits\":");Number(r.delta_bits);
        Text(",\"caller\":");Number(r.caller);
        if(r.site>=2) {
            Text(",\"handle_argument\":");Number(r.handle_argument);Text(",\"receiver\":");Number(r.receiver);
            Text(",\"method\":");Number(r.method);Text(",\"returned_pointer\":");Number(r.returned_pointer);
            Text(",\"returned_handle\":");Number(r.returned_handle);Text(",\"handle_read\":");Number(r.handle_read);
            Text(",\"ancestry\":[");
            for(unsigned i=0;i<r.ancestry.size();++i){if(i)Text(",");Number(reinterpret_cast<std::uintptr_t>(r.ancestry[i]));}
            Text("]");
        }
        Text("}");Emit();
    }
    static void Begin(const char* kind) noexcept {
        length_=0;serialization_failed_=false;Text("{\"kind\":\"");Text(kind);
        Text("\",\"time_ms\":");Number(GetTickCount64());
    }
    static void Text(const char* s) noexcept {
        while(*s){if(length_>=line_.size()){serialization_failed_=true;return;}line_[length_++]=*s++;}
    }
    static void Number(std::uint64_t value) noexcept {
        char digits[20];unsigned n{};do{digits[n++]=char('0'+value%10);value/=10;}while(value);
        while(n){char one[]{digits[--n],0};Text(one);}
    }
    static void Flag(bool value) noexcept {Text(value?"true":"false");}
    static void Emit() noexcept {
        // A partial line is never repaired or hidden. Stop writes on any error;
        // live status remains sticky and no later close can certify this prefix.
        if(write_failed_)return;
        Text("\n");DWORD written{};
        if(serialization_failed_ || !WriteFile(file_,line_.data(),length_,&written,nullptr)
            || written!=length_ || !FlushFileBuffers(file_))write_failed_=true;
    }
    // Process-pinned handle/storage. No destructor may close this under a
    // pending native call. ClosePhase seals admission only, never cancels work.
    inline static HANDLE file_=INVALID_HANDLE_VALUE;
    inline static SRWLOCK lock_=SRWLOCK_INIT;
    inline static std::atomic<bool> accepting_{};
    inline static std::array<Record,MaxPending> active_{};
    inline static std::array<std::atomic<std::uintptr_t>,32> scenes_{};
    inline static std::array<char,LineBytes> line_{};
    inline static std::array<wchar_t,1024> path_{},temporary_{};
    inline static std::uintptr_t base_{};
    inline static std::uint64_t next_pair_{},dropped_{},close_tick_{};
    inline static unsigned count_{},pending_{},pending_at_close_{},length_{};
    inline static bool started_{},closed_{},published_{},write_failed_{},serialization_failed_{};
    // Includes simultaneous records, serializer, bounded wrapper stack frames,
    // handle/lock bookkeeping; pinned detours are charged by the existing owner.
    static_assert(sizeof(active_)+sizeof(scenes_)+sizeof(line_)+sizeof(path_)+sizeof(temporary_)+MaxPending*2048+4096<=OwnedBytes);
};
}
