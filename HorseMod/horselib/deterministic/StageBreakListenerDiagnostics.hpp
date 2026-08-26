#pragma once

#include "NativeCandidateRegions.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <span>
#include <string_view>

namespace Horse::Deterministic
{
template <typename T, std::size_t Capacity>
class StageBreakFixedSequence final
{
public:
    constexpr StageBreakFixedSequence() noexcept = default;
    constexpr StageBreakFixedSequence(std::initializer_list<T> values) noexcept
    {
        *this = values;
    }

    constexpr StageBreakFixedSequence& operator=(
        std::initializer_list<T> values) noexcept
    {
        clear();
        for (const auto& value : values)
        {
            if (!push_back(value)) break;
        }
        return *this;
    }

    [[nodiscard]] constexpr bool push_back(const T& value) noexcept
    {
        if (size_ >= Capacity) return false;
        values_[size_++] = value;
        return true;
    }
    constexpr void clear() noexcept
    {
        values_.fill({});
        size_ = 0;
    }
    constexpr void reserve(std::size_t) noexcept {}
    [[nodiscard]] constexpr std::size_t size() const noexcept { return size_; }
    [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] constexpr std::size_t capacity() const noexcept
    {
        return Capacity;
    }
    [[nodiscard]] constexpr T& operator[](std::size_t index) noexcept
    {
        return values_[index];
    }
    [[nodiscard]] constexpr const T& operator[](
        std::size_t index) const noexcept
    {
        return values_[index];
    }
    [[nodiscard]] constexpr T* begin() noexcept { return values_.data(); }
    [[nodiscard]] constexpr const T* begin() const noexcept
    {
        return values_.data();
    }
    [[nodiscard]] constexpr T* end() noexcept { return values_.data() + size_; }
    [[nodiscard]] constexpr const T* end() const noexcept
    {
        return values_.data() + size_;
    }

    friend constexpr bool operator==(
        const StageBreakFixedSequence& left,
        const StageBreakFixedSequence& right) noexcept
    {
        if (left.size_ != right.size_) return false;
        for (std::size_t index = 0; index < left.size_; ++index)
            if (!(left.values_[index] == right.values_[index])) return false;
        return true;
    }

private:
    std::array<T, Capacity> values_{};
    std::size_t size_{};
};

enum class StageBreakActorKind : std::uint8_t
{
    Wall = 1,
    Barrier = 2,
};

inline constexpr std::uint16_t no_repeated_actor_reference = 0xffff;
inline constexpr std::uint32_t no_bound_stage_break_callback = 0xffffffff;

struct StageBreakActorRef
{
    StageBreakActorKind kind{};
    std::uintptr_t address{};
};

struct StageBreakListenerRecord
{
    StageBreakActorKind kind{};
    std::int32_t actor_id{};
    std::uint16_t actor_order{};
    std::uint16_t dispatch_order{};
    std::uint16_t slot_index{};
    std::uint32_t listener_vtable_rva{};
    std::uint32_t callback_rva{};
    std::uint32_t bound_callback_rva{no_bound_stage_break_callback};

    friend bool operator==(
        const StageBreakListenerRecord&,
        const StageBreakListenerRecord&) = default;
};

struct StageBreakActorIdentity
{
    StageBreakActorKind kind{};
    std::int32_t actor_id{};
    std::uint16_t actor_order{};
    std::uint16_t repeated_reference_of{no_repeated_actor_reference};

    friend bool operator==(
        const StageBreakActorIdentity&,
        const StageBreakActorIdentity&) = default;
};

struct StageBreakListenerTopology
{
    static constexpr std::size_t maximum_actors = 64;
    static constexpr std::size_t maximum_listeners_per_actor = 32;
    static constexpr std::size_t maximum_listeners =
        maximum_actors * maximum_listeners_per_actor;

    std::uint64_t signature{};
    StageBreakFixedSequence<StageBreakActorIdentity, maximum_actors> actors;
    StageBreakFixedSequence<StageBreakListenerRecord, maximum_listeners> listeners;

    friend bool operator==(
        const StageBreakListenerTopology&,
        const StageBreakListenerTopology&) = default;
};

enum class StageBreakListenerProbeFault : std::uint8_t
{
    None,
    ActorReference,
    ActorLayoutOverflow,
    ActorRead,
    CollectionBounds,
    EntryAddressOverflow,
    EntryRead,
    ListenerVtableRead,
    ListenerVtableOutsideImage,
    CallbackRead,
    CallbackOutsideImage,
    BoundCallbackRead,
    BoundCallbackOutsideImage,
};

[[nodiscard]] std::string_view stage_break_listener_probe_fault_name(
    StageBreakListenerProbeFault fault) noexcept;

struct StageBreakListenerProbeFailure
{
    StageBreakListenerProbeFault fault{};
    StageBreakActorKind kind{};
    std::uint16_t actor_order{};
    std::uint16_t slot_index{};
    std::int32_t actor_id{};
    std::int32_t listener_count{};
    std::int32_t listener_capacity{};
    bool listener_override_present{};
};

class StageBreakListenerTopologyProbe
{
public:
    explicit StageBreakListenerTopologyProbe(INativeMemory& memory) noexcept;

    Status Capture(
        std::uintptr_t image_base,
        std::size_t image_size,
        std::span<const StageBreakActorRef> actors,
        StageBreakListenerTopology& output,
        StageBreakListenerProbeFailure* failure = nullptr) noexcept;

private:
    INativeMemory& memory_;
};

class StageBreakListenerRuntimeDiagnostics
{
public:
    explicit StageBreakListenerRuntimeDiagnostics(std::filesystem::path report_path);
    ~StageBreakListenerRuntimeDiagnostics();

    Status Observe(
        std::uintptr_t image_base,
        std::uint64_t frame,
        std::span<const StageBreakActorRef> actors) noexcept;
    void Finish() noexcept;
    [[nodiscard]] bool complete() const noexcept;

private:
    class ProcessMemory;

    void write_header();
    void write_topology(
        std::uint64_t frame,
        const StageBreakListenerTopology& topology);
    void write_failure(
        std::uint64_t frame,
        Status status,
        const StageBreakListenerProbeFailure& failure);

    static constexpr std::size_t maximum_samples = 600;

    std::filesystem::path report_path_;
    std::unique_ptr<ProcessMemory> memory_;
    std::unique_ptr<StageBreakListenerTopologyProbe> probe_;
    std::ofstream report_;
    std::uint64_t last_frame_{~std::uint64_t{0}};
    std::uint64_t last_signature_{};
    std::size_t samples_{};
    bool header_written_{};
    bool finished_{};
};
}
