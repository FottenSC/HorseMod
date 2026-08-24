#pragma once

#include "NativeCandidateRegions.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <vector>

namespace Horse::Deterministic
{
enum class StageBreakActorKind : std::uint8_t
{
    Wall = 1,
    Barrier = 2,
};

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

    friend bool operator==(
        const StageBreakListenerRecord&,
        const StageBreakListenerRecord&) = default;
};

struct StageBreakActorIdentity
{
    StageBreakActorKind kind{};
    std::int32_t actor_id{};
    std::uint16_t actor_order{};

    friend bool operator==(
        const StageBreakActorIdentity&,
        const StageBreakActorIdentity&) = default;
};

struct StageBreakListenerTopology
{
    std::uint64_t signature{};
    std::vector<StageBreakActorIdentity> actors;
    std::vector<StageBreakListenerRecord> listeners;

    friend bool operator==(
        const StageBreakListenerTopology&,
        const StageBreakListenerTopology&) = default;
};

class StageBreakListenerTopologyProbe
{
public:
    explicit StageBreakListenerTopologyProbe(INativeMemory& memory) noexcept;

    Status Capture(
        std::uintptr_t image_base,
        std::size_t image_size,
        std::span<const StageBreakActorRef> actors,
        StageBreakListenerTopology& output) noexcept;

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
