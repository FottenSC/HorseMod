#pragma once

#include "TutorialConsumer.hpp"

namespace Horse::Deterministic
{
// A separately scheduled actor transaction, not a BattleManager input
// publication. Preserve calls that advance no native simulation coordinate.
struct InputProducerState
{
    std::uintptr_t owner{};
    std::uintptr_t vtable{};
    std::array<std::uintptr_t, 8> dispatch{}; // +5F8, +620..+650
    std::array<std::uint32_t, 12> scalars{}; // +390..+3BF
    std::array<std::uintptr_t, 2> receiver_arrays{};
    std::array<std::int32_t, 2> receiver_counts{};
    std::array<std::int32_t, 2> receiver_capacities{};
    friend bool operator==(const InputProducerState&,
        const InputProducerState&) = default;
};

struct InputProducerObservation
{
    TutorialParentGuard parent{};
    InputProducerState before{};
    InputProducerState after{};
    std::uint32_t native_frame_before{};
    std::uint32_t native_frame_after{};
    std::uint32_t thread_id{};
    float delta_seconds{};
    std::uint8_t preceding_consumers{};
    // Native logical samples before current-input dispatch. These are usable
    // for unchanged offline reconstruction only under an exact entry-state
    // check (including LocalInputMask), never as speculative online ingress.
    std::array<std::uint32_t, 2> sampled_inputs{};
    std::uint8_t sampled_slots{};
    bool valid{};
};
inline constexpr std::size_t maximum_input_producers_per_interval = 4;

template<class Read>
bool CaptureInputProducerState(Read&& read, std::uintptr_t owner,
    InputProducerState& output) noexcept
{
    output = {};
    output.owner = owner;
    if (owner == 0 || !read(owner, output.vtable) || output.vtable == 0)
        return false;
    constexpr std::array<std::uintptr_t, 8> methods{
        0x5F8, 0x620, 0x628, 0x630, 0x638, 0x640, 0x648, 0x650};
    for (std::size_t i = 0; i < methods.size(); ++i)
        if (!read(output.vtable + methods[i], output.dispatch[i])
            || output.dispatch[i] == 0) return false;
    for (std::size_t i = 0; i < output.scalars.size(); ++i)
        if (!read(owner + 0x390 + i * 4, output.scalars[i])) return false;
    for (std::size_t i = 0; i < output.receiver_arrays.size(); ++i)
    {
        const auto header = owner + 0x43C0 + i * 0x10;
        if (!read(header, output.receiver_arrays[i])
            || !read(header + 8, output.receiver_counts[i])
            || !read(header + 12, output.receiver_capacities[i])
            || output.receiver_counts[i] < 0
            || output.receiver_capacities[i] < output.receiver_counts[i]
            || (output.receiver_capacities[i] != 0
                && output.receiver_arrays[i] == 0)) return false;
    }
    return true;
}
// Read-only admission witness for the native current-input dispatch path.
// 1403D73F0 -> wrapper143298810/+60 -> owner+390 -> 140428D70.
struct ReplayInputRoute {
    std::uintptr_t input_log{}, entries{}, handle{}, wrapper{};
    std::int32_t count{},capacity{},matches{};
    bool valid{},registered{};
};
template<class Read>
ReplayInputRoute InspectReplayInputRoute(Read&& read,std::uintptr_t base,
    std::uintptr_t manager,std::uintptr_t replay) noexcept {
    ReplayInputRoute out;
    if(!manager || !replay || !read(manager+0x478,out.input_log) || !out.input_log
        || !read(replay+0x3c8,out.handle) || !read(out.input_log+0x43d0,out.entries)
        || !read(out.input_log+0x43d8,out.count) || !read(out.input_log+0x43dc,out.capacity)
        || out.count<0 || out.count>64 || out.capacity<out.count
        || (out.capacity && !out.entries)) return out;
    for(int i=0;i<out.count;++i) {
        const auto entry=out.entries+std::uintptr_t(i)*0x40;
        std::int32_t active{};std::uintptr_t external{},vtable{},owner{},handle{};
        if(!read(entry+0x30,active) || !read(entry+0x20,external)) return out;
        if(!active) continue;
        const auto wrapper=external?external:entry;
        if(!read(wrapper,vtable)) return out;
        if(vtable!=base+0x3298810) continue; // Other recipients are not mutated.
        if(!read(wrapper+8,owner) || !read(wrapper+0x18,handle)) return out;
        if(owner!=replay) continue;
        ++out.matches;out.wrapper=wrapper;
        if(active!=2 || !handle || handle!=out.handle) return out;
    }
    out.valid=out.matches<=1;
    out.registered=out.valid && out.matches==1 && out.handle!=0;
    return out;
}

}
