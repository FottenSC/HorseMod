#pragma once
#include "ReplaySourceState.hpp"
#include "Types.hpp"
#include "ReplayRollingCorrections.hpp"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectArray.hpp>
#include <polyhook2/Detour/x64Detour.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

namespace Horse::Deterministic {
struct ReplayInputRevision {
    ReplaySourceState recording;
    std::uint64_t id{}, session{};
    int source_index{},source_serial{};
    std::vector<ReplayInputOverride> overrides;
    std::size_t owned_bytes() const noexcept {
        return sizeof(*this)+64+overrides.capacity()*sizeof(ReplayInputOverride);
    }
};
using ReplayInputRevisionHandle=std::shared_ptr<const ReplayInputRevision>;

class Sc6ReplayInputSource final {
public:
    bool Bind(std::uintptr_t base,std::uint64_t session) {
        if(active_ || !base || !session) return false;
        constexpr unsigned char signature[]{0x40,0x55,0x41,0x56,0x48,0x83,0xec,0x28,0x80,0x79,0x08,0x00};
        if(std::memcmp(reinterpret_cast<void*>(base+0x428d70),signature,sizeof(signature))) return false;
        base_=base;session_=session;thread_=GetCurrentThreadId();
        hook_=std::make_unique<PLH::x64Detour>(base+0x428d70,reinterpret_cast<std::uint64_t>(&ReadInputs),&original_);
        active_=this;
        if(hook_->hook()) return true;
        active_=nullptr;hook_.reset();return false;
    }
    bool Stop() {
        if(active_!=this) return true;
        if(GetCurrentThreadId()!=thread_ || reading_) return false;
        if(hook_ && hook_->isHooked() && !hook_->unHook()) return false;
        active_=nullptr;hook_.reset();revision_.reset();return true;
    }
    ~Sc6ReplayInputSource() {if(!Stop()) __fastfail(FAST_FAIL_INVALID_ARG);}
    const ReplayInputRevisionHandle& revision() const noexcept {return revision_;}
    std::uint64_t revision_id() const noexcept {return revision_?revision_->id:0;}
    std::size_t owned_bytes() const noexcept {
        return sizeof(*this)+sizeof(PLH::x64Detour)+65536+(revision_?revision_->owned_bytes():0);
    }
    Status Validate(const ReplaySourceState& source,const ReplayInputRevisionHandle& revision) const noexcept {
        if(active_!=this || GetCurrentThreadId()!=thread_ || reading_)
            return Status::failure(FailureCode::IllegalTransition);
        if(revision && (!SameDomain(source,revision->recording) || !revision->id || revision->session!=session_ || !Live(*revision)))
            return Status::failure(FailureCode::GenerationMismatch);
        return Status::success();
    }
    Status Install(const ReplaySourceState& source,const ReplayInputRevisionHandle& expected,
        const ReplayInputRevisionHandle& target) noexcept {
        const auto status=Validate(source,target);
        if(!status.ok()) return status;
        if(revision_!=expected) return Status::failure(FailureCode::GenerationMismatch);
        // Both images are retained by the host before this allocation-free swap.
        revision_=target;tracker_.store(target?target->recording.owner+0x390:0);return Status::success();
    }
    Status Revise(const ReplaySourceState& source,std::uint64_t expected,
        std::span<const ReplayInputOverride> edits,std::size_t available,std::uint64_t& result) noexcept {
        result=revision_id();
        const auto status=Validate(source,revision_);
        if(!status.ok()) return status;
        if(expected!=revision_id() || source.round<0 || !source.tracker_active || edits.empty())
            return Status::failure(FailureCode::IllegalTransition);
        const auto prior=revision_?revision_->overrides.size():0;
        constexpr std::size_t maximum=65536;
        if(prior>maximum || edits.size()>maximum-prior || next_id_==UINT64_MAX)
            return Status::failure(FailureCode::CapacityExceeded);
        const auto reservation=sizeof(ReplayInputRevision)+64+(prior+edits.size())*sizeof(ReplayInputOverride);
        if(reservation>available) return Status::failure(FailureCode::CapacityExceeded);
        for(std::size_t i=0;i<edits.size();++i) {
            const auto& edit=edits[i];
            // A correction to a published sample needs an earlier checkpoint;
            // changing filled bits or patching reader returns is insufficient.
            if(edit.round!=source.round || edit.sample<static_cast<unsigned>(source.cursor)
                || !edit.players || (edit.players&~3) || (i && edits[i-1].sample>=edit.sample))
                return Status::failure(FailureCode::IllegalTransition);
            for(unsigned player=0;player<2;++player) if(edit.players&(1u<<player)) {
                std::uint32_t unused{};
                if(!AuthoredSample(source,player,edit.sample,unused)) return Status::failure(FailureCode::UnsupportedContent);
            }
        }
        try {
            auto next=std::make_shared<ReplayInputRevision>();
            next->recording=source;next->id=next_id_+1;next->session=session_;
            auto* object=reinterpret_cast<RC::Unreal::UObject*>(source.owner);
            std::array<int,2> weak{};
            reinterpret_cast<void(*)(void*,const void*)>(base_+0xf7bad0)(weak.data(),object);
            next->source_index=weak[0];next->source_serial=weak[1];
            if(!Live(*next)) return Status::failure(FailureCode::GenerationMismatch);
            next->overrides.reserve(prior+edits.size());
            if(revision_) next->overrides=revision_->overrides;
            for(const auto& edit:edits) {
                auto it=std::lower_bound(next->overrides.begin(),next->overrides.end(),edit,Less);
                if(it!=next->overrides.end() && it->round==edit.round && it->sample==edit.sample) {
                    for(unsigned player=0;player<2;++player) if(edit.players&(1u<<player)) it->raw[player]=edit.raw[player];
                    it->players|=edit.players;
                } else next->overrides.insert(it,edit);
            }
            if(next->owned_bytes()>available) return Status::failure(FailureCode::CapacityExceeded);
            next_id_=next->id;revision_=std::move(next);tracker_.store(source.owner+0x390);result=revision_id();
            return Status::success();
        } catch(...) {return Status::failure(FailureCode::CaptureFailed);}
    }
    bool AuthoredSample(const ReplaySourceState& source,unsigned player,unsigned sample,std::uint32_t& value) const noexcept {
        __try {
            if(player>=2 || source.round<0 || source.round>=source.recording_count) return false;
            auto* owner=reinterpret_cast<void*>(source.owner);
            if(!owner || Read<std::uintptr_t>(owner,0x390)!=base_+0x3290d20
                || Read<std::uintptr_t>(owner,0x3b8)!=source.recordings
                || Read<int>(owner,0x3c0)!=source.recording_count) return false;
            auto* round=reinterpret_cast<const std::byte*>(source.recordings)+source.round*16;
            if(Read<int>(round,8)!=2 || Read<int>(round,12)<2) return false;
            auto* recorders=Read<const std::byte*>(round);
            if(!recorders) return false;
            auto* recorder=recorders+player*24;
            auto* object=Read<void*>(recorder,16);
            if(!object || sample>=Read<unsigned>(recorder,4)
                || Read<std::uintptr_t>(object)!=base_+0x328e948) return false;
            const auto size=Read<int>(object,16),capacity=Read<int>(object,20);
            const auto* data=Read<const unsigned*>(object,8);
            if(size<0 || (size&3) || capacity<size || sample>=static_cast<unsigned>(size/4) || !data) return false;
            value=data[sample];return true;
        } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
private:
    template<class T> static T Read(const void* p,std::size_t offset=0) {
        return *reinterpret_cast<const T*>(static_cast<const std::byte*>(p)+offset);
    }
    static bool Live(const ReplayInputRevision& revision) noexcept {
        __try {
            const auto* item=RC::Unreal::FUObjectArray::IndexToObject(revision.source_index);
            return item && item->GetUObject()==reinterpret_cast<void*>(revision.recording.owner)
                && item->IsValid(false) && item->GetSerialNumber()==revision.source_serial;
        } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static bool SameDomain(const ReplaySourceState& a,const ReplaySourceState& b) noexcept {
        return a.owner && a.owner==b.owner && a.recordings==b.recordings && a.reset_images==b.reset_images
            && a.recording_count==b.recording_count && a.reset_count==b.reset_count;
    }
    static bool Less(const ReplayInputOverride& a,const ReplayInputOverride& b) noexcept {
        return a.round<b.round || (a.round==b.round && a.sample<b.sample);
    }
    static void ReadInputs(void* tracker,unsigned* inputs) {
        auto& self=*active_;
        if(GetCurrentThreadId()!=self.thread_) {
            if(reinterpret_cast<std::uintptr_t>(tracker)==self.tracker_.load()) __fastfail(FAST_FAIL_INVALID_ARG);
            reinterpret_cast<void(*)(void*,unsigned*)>(self.original_)(tracker,inputs);return;
        }
        if(!self.revision_ || reinterpret_cast<std::uintptr_t>(tracker)!=self.revision_->recording.owner+0x390) {
            reinterpret_cast<void(*)(void*,unsigned*)>(self.original_)(tracker,inputs);return;
        }
        if(self.reading_ || !Live(*self.revision_)) __fastfail(FAST_FAIL_INVALID_ARG);
        self.reading_=true;
        const auto round=Read<int>(tracker,12),sample=Read<int>(tracker,16);
        const auto active=Read<unsigned char>(tracker,8);
        reinterpret_cast<void(*)(void*,unsigned*)>(self.original_)(tracker,inputs);
        if(active && round>=0) {
            if(sample<0 || Read<int>(tracker,16)!=sample+1) __fastfail(FAST_FAIL_INVALID_ARG);
            const ReplayInputOverride key{round,static_cast<unsigned>(sample)};
            const auto& entries=self.revision_->overrides;
            const auto it=std::lower_bound(entries.begin(),entries.end(),key,Less);
            if(it!=entries.end() && it->round==round && it->sample==static_cast<unsigned>(sample)) {
                auto source=self.revision_->recording;source.round=round;
                for(unsigned player=0;player<2;++player) if(it->players&(1u<<player)) {
                    std::uint32_t authored{};
                    if(!self.AuthoredSample(source,player,sample,authored) || (inputs[player]&~0x10u)!=(authored&~0x10u)) __fastfail(FAST_FAIL_INVALID_ARG);
                    // Native tracker preserves destination bit10, never replay bit10.
                    inputs[player]=(inputs[player]&0x10u)|(it->raw[player]&~0x10u);
                }
            }
        }
        self.reading_=false;
    }
    inline static Sc6ReplayInputSource* active_{};
    std::uintptr_t base_{};DWORD thread_{};
    std::uint64_t original_{},next_id_{},session_{};
    bool reading_{};
    std::atomic<std::uintptr_t> tracker_{};
    std::unique_ptr<PLH::x64Detour> hook_;
    ReplayInputRevisionHandle revision_;
};
}
