#pragma once

#include "CallbackTopology.hpp"
#include "CandidateGameStateAdapter.hpp"
#include "Schema.hpp"
#include "SnapshotStore.hpp"
#include "StageWindTopology.hpp"
#include "StageWindGraphTransaction.hpp"

#include <memory>

namespace Horse::Deterministic
{
enum class CandidateCheckpointRole : std::uint8_t
{
    Landing,
    BatchEntry,
};

struct CandidateCheckpointCaptureStatus
{
    FailureCode failure{FailureCode::None};
    FrameCoordinate last_coordinate{};
    std::uint64_t captured{};
    std::size_t bytes_used{};
    std::size_t wind_node_count{};
    NativeCandidateValidationDiagnostic validation{};
};

class Sc6CandidateCheckpointCapture final
{
public:
    Sc6CandidateCheckpointCapture();
    ~Sc6CandidateCheckpointCapture();

    Status Initialize(
        std::uintptr_t image_base, UcrtRandBroker* ucrt_broker) noexcept;
    Status Capture(
        CandidateCheckpointRole role,
        std::uintptr_t battle_manager,
        FrameCoordinate coordinate,
        std::uint64_t session_generation,
        std::uint32_t simulation_thread_id) noexcept;
    void ReleaseBinding() noexcept;
    void Reset() noexcept;

    [[nodiscard]] CandidateCheckpointCaptureStatus status(
        CandidateCheckpointRole role) const noexcept;
    [[nodiscard]] const SnapshotStore& snapshots(
        CandidateCheckpointRole role) const noexcept;

private:
    class ProcessMemory;
    class ProcessStageWindAllocator;

    Status bind(
        std::uintptr_t battle_manager,
        FrameCoordinate coordinate,
        std::uint64_t session_generation,
        std::uint32_t simulation_thread_id) noexcept;
    Status resolve_move_dispatch(
        std::uintptr_t battle_manager,
        std::uintptr_t& output) noexcept;
    bool read_fighter_roots(std::array<std::uintptr_t, 2>& output) noexcept;
    Status capture_callback_topology(CallbackTopology& output) noexcept;

    static constexpr std::size_t checkpoint_memory_limit =
        Schema::replay_checkpoint_memory_budget / 2;
    static constexpr std::size_t maximum_checkpoints_per_role = 32768;

    std::unique_ptr<ProcessMemory> memory_;
    std::unique_ptr<NativeCandidateRegions> regions_;
    std::unique_ptr<CallbackTopologyProbe> callback_probe_;
    std::unique_ptr<StageWindTopologyProbe> wind_probe_;
    std::unique_ptr<ProcessStageWindAllocator> wind_allocator_;
    std::unique_ptr<StageWindGraphTransaction> wind_transaction_;
    HgCpuStreamShim hgcpu_{};
    std::unique_ptr<CandidateGameStateAdapter> adapter_;
    UcrtRandBroker* ucrt_broker_{};
    SnapshotStore landing_snapshots_{checkpoint_memory_limit,
        maximum_checkpoints_per_role, CapacityPolicy::RejectNew};
    SnapshotStore batch_entry_snapshots_{checkpoint_memory_limit,
        maximum_checkpoints_per_role, CapacityPolicy::RejectNew};
    CandidateCheckpointCaptureStatus landing_status_{};
    CandidateCheckpointCaptureStatus batch_entry_status_{};
    std::uintptr_t image_base_{};
    std::size_t image_size_{};
    std::uintptr_t bound_manager_{};
    std::uintptr_t bound_move_dispatch_{};
    std::uint64_t bound_session_generation_{};
    std::uint64_t bound_round_generation_{};
    CallbackTopology bound_callback_topology_{};
};
}
