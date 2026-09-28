#pragma once
#include "ReplayStatePolicy.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace Horse::Deterministic {
// One native swap-remove and its already linked, sole GT dependency. This
// value protocol does not acquire UObject/task ownership: the native guard
// must supply indexed liveness, writer exclusion and exclusive queue entry.
class ReplayPrerequisiteConsumer final {
public:
    struct Identity {
        std::uintptr_t object{};
        std::int32_t index{},serial{};
        friend bool operator==(const Identity&,const Identity&)=default;
        bool valid() const noexcept {return object && index>=0 && serial>0;}
    };
    struct Edge {
        std::int32_t index{},serial{};
        std::uintptr_t tick{};
        friend bool operator==(const Edge&,const Edge&)=default;
    };
    static_assert(sizeof(Edge)==16);
    struct Contract {
        Identity owner,source,world;
        std::uintptr_t tick{},level{},tick_table{},owner_table{},consumer{};
        std::array<Edge,8> edges{};
        std::uint32_t count{};
        ReplayStatePolicy::Stamp policy{ReplayStatePolicy::current};
        friend bool operator==(const Contract&,const Contract&)=default;
        bool valid() const noexcept {
            return owner.valid() && world.valid() && tick==owner.object+0x110
                && level && tick_table && owner_table && consumer && count<=edges.size()
                && ReplayStatePolicy::Accepts(policy);
        }
    };
    struct Task {
        std::uintptr_t task{},tick{},event{},world{},table{};
        std::uint64_t epoch{},event_head{};
        std::int32_t admitted{},queued{},dependencies{};
        std::uint32_t os_thread{},native_thread{},desired_thread{},payload_thread{};
        std::uint8_t flags{},enabled{},constructed{};
        friend bool operator==(const Task&,const Task&)=default;
        bool queued_on_gt(std::uintptr_t expected_tick,std::uintptr_t expected_world,
            std::uintptr_t expected_table,std::uint32_t expected_thread) const noexcept {
            const auto stamped=[&](std::int32_t value) {
                return static_cast<std::uint64_t>(static_cast<std::int64_t>(value))==epoch;
            };
            // Native142163BA0's GT branch encodes tick+0xC bit0x10 in
            // task+0x24;1421679C0 copies that entire token to task+8. The
            // current named-thread identifier is separately and exactly 2.
            const std::uint32_t gt_token=2u | (static_cast<std::uint32_t>(flags&0x10u)<<5);
            return task && event && tick==expected_tick && world==expected_world && table==expected_table
                && epoch && stamped(admitted) && stamped(queued) && (flags&0x40) && enabled==1
                && constructed==1 && os_thread==expected_thread && native_thread==2
                && desired_thread==gt_token && payload_thread==gt_token && dependencies>=0
                && !(event_head&(1ull<<26));
        }
    };
    enum class Phase : std::uint8_t { Empty, BeforeNative, Changed, Dispatching, Completed };
    enum class Admission : std::uint8_t { Stable, DrainCurrentApplication, Reject };

    bool BeforeRemove(const Contract& expected,const Contract& observed,const Task& mesh,const Task& source,
        std::uintptr_t active_source_task,std::uintptr_t native_table,std::uint32_t thread) noexcept {
        if(phase_!=Phase::Empty || !expected.valid() || expected!=observed || !expected.source.valid()
            || expected.count!=1 || expected.edges[0]!=Edge{expected.source.index,expected.source.serial,expected.source.object+0x110}
            || !mesh.queued_on_gt(expected.tick,expected.world.object,native_table,thread)
            || !source.queued_on_gt(expected.source.object+0x110,expected.world.object,native_table,thread)
            || mesh.epoch!=source.epoch || mesh.dependencies!=1 || source.dependencies!=0
            || active_source_task!=source.task || mesh.task==source.task || mesh.event==source.event)return false;
        expected_=expected;mesh_=mesh;source_=source;table_=native_table;thread_=thread;
        phase_=Phase::BeforeNative;return true;
    }
    bool AfterRemove(const Contract& observed,const Task& mesh,const Task& source) noexcept {
        auto removed=expected_;removed.edges={};removed.count=0;
        if(phase_!=Phase::BeforeNative || observed!=removed || mesh!=mesh_ || source!=source_)return false;
        phase_=Phase::Changed;return true;
    }
    Admission BeforeDispatch(const Contract& expected,const Contract& observed,const Task& task,
        std::uintptr_t native_table,std::uint32_t thread) noexcept {
        if(phase_==Phase::BeforeNative || !expected.valid() || !task.queued_on_gt(expected.tick,expected.world.object,native_table,thread)
            || task.dependencies!=0)return Admission::Reject;
        if(phase_==Phase::Empty || expected.tick!=expected_.tick)
            return expected==observed?Admission::Stable:Admission::Reject;
        auto removed=expected_;removed.edges={};removed.count=0;
        auto released=mesh_;released.dependencies=0;
        if(phase_!=Phase::Changed || expected!=expected_ || observed!=removed || table_!=native_table || thread_!=thread
            || task!=released)return Admission::Reject;
        phase_=Phase::Dispatching;return Admission::DrainCurrentApplication;
    }
    bool Completed(std::uintptr_t task,std::uint64_t epoch) noexcept {
        // The allocation/event may already be recycled; saved identities only.
        if(phase_!=Phase::Dispatching || task!=mesh_.task || epoch!=mesh_.epoch)return false;
        phase_=Phase::Completed;return true;
    }
    Phase phase() const noexcept {return phase_;}
    bool changed() const noexcept {return phase_>=Phase::Changed;}
    const Contract& contract() const noexcept {return expected_;}
    const Task& task() const noexcept {return mesh_;}
private:
    Contract expected_{};
    Task mesh_{},source_{};
    std::uintptr_t table_{};
    std::uint32_t thread_{};
    Phase phase_{};
};
}
