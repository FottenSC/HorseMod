#pragma once

#include "CallbackTopology.hpp"
#include "CharaAnimationState.hpp"
#include "CandidateGameStateAdapter.hpp"
#include "MotionBankSnapshot.hpp"
#include "SecondaryEventState.hpp"
#include "Schema.hpp"
#include "SnapshotStore.hpp"
#include "StageWindTopology.hpp"
#include "StageWindGraphTransaction.hpp"

#include <algorithm>
#include <array>
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
    std::uint64_t capture_samples{};
    std::uint64_t capture_total_ns{};
    std::uint64_t capture_max_ns{};
    std::uint64_t capture_p99_ns{};
    std::uint64_t store_max_ns{};
    std::uint64_t store_p99_ns{};
    CandidateAdapterPerformanceStatus adapter_performance{};
    NativeCandidateValidationDiagnostic validation{};
    CharaAnimationTopologyIssue animation_topology_issue{};
    std::uintptr_t animation_topology_observed{};
    std::array<std::uintptr_t, 2> animation_fighters{};
    CandidateCapturePhase capture_phase{};
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
    Status CaptureTransient(
        FrameCoordinate coordinate, Snapshot& output) noexcept;
    Status EnsureRestoreOwnership(std::uint32_t simulation_thread_id) noexcept;
    Status RestoreAndVerify(const Snapshot& snapshot) noexcept;
    void InvalidateHistory() noexcept;
    void ReleaseBinding() noexcept;
    void Reset() noexcept;

    [[nodiscard]] CandidateCheckpointCaptureStatus status(
        CandidateCheckpointRole role) const noexcept;
    [[nodiscard]] const SnapshotStore& snapshots(
        CandidateCheckpointRole role) const noexcept;
    [[nodiscard]] NativeCandidateValidationDiagnostic restore_validation()
        const noexcept;
    [[nodiscard]] CandidateAdapterPerformanceStatus adapter_performance()
        const noexcept;
    Status TraceLocalStreamOffset(
        std::size_t stream_offset, HgCpuWriteSpan& output,
        std::array<std::uintptr_t, 2>& fighter_roots,
        std::uintptr_t& image_base) noexcept;

private:
    class ProcessMemory;
    class ProcessStageWindAllocator;

    struct CameraTopology
    {
        std::uintptr_t camera_root{};
        std::uintptr_t action_backing{};
        std::array<std::uintptr_t, 17> action_vtables{};
        std::array<std::uint32_t, 17> action_types{};

        friend bool operator==(const CameraTopology&, const CameraTopology&) = default;
    };

    Status bind(
        std::uintptr_t battle_manager,
        FrameCoordinate coordinate,
        std::uint64_t session_generation,
        std::uint32_t simulation_thread_id) noexcept;
    Status resolve_move_dispatch(
        std::uintptr_t battle_manager,
        std::uintptr_t& output) noexcept;
    bool read_fighter_roots(std::array<std::uintptr_t, 2>& output) noexcept;
    Status capture_camera_topology(CameraTopology& output) noexcept;
    Status capture_callback_topology(CallbackTopology& output) noexcept;

    static constexpr std::size_t checkpoint_memory_limit =
        Schema::replay_checkpoint_memory_budget / 2;
    static constexpr std::size_t maximum_checkpoints_per_role = 32768;

    struct TimingHistogram
    {
        static constexpr std::uint64_t bucket_width_ns = 10'000;
        static constexpr std::size_t bucket_count = 502;

        void Record(std::uint64_t nanoseconds) noexcept
        {
            const auto bucket = static_cast<std::size_t>(std::min<std::uint64_t>(
                nanoseconds / bucket_width_ns, bucket_count - 1));
            ++buckets[bucket];
            ++samples;
            total_ns += nanoseconds;
            if (nanoseconds > maximum_ns) maximum_ns = nanoseconds;
        }

        [[nodiscard]] std::uint64_t Percentile99() const noexcept
        {
            if (samples == 0) return 0;
            const std::uint64_t target = (samples * 99 + 99) / 100;
            std::uint64_t cumulative{};
            for (std::size_t index = 0; index < buckets.size(); ++index)
            {
                cumulative += buckets[index];
                if (cumulative >= target)
                    return (index + 1) * bucket_width_ns;
            }
            return bucket_count * bucket_width_ns;
        }

        std::array<std::uint64_t, bucket_count> buckets{};
        std::uint64_t samples{};
        std::uint64_t total_ns{};
        std::uint64_t maximum_ns{};
    };

    std::unique_ptr<ProcessMemory> memory_;
    std::unique_ptr<NativeCandidateRegions> regions_;
    std::unique_ptr<MotionBankSnapshot> motion_banks_;
    std::unique_ptr<SecondaryEventState> secondary_events_;
    std::unique_ptr<CharaAnimationState> chara_animation_;
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
    TimingHistogram landing_capture_timing_{};
    TimingHistogram batch_entry_capture_timing_{};
    TimingHistogram landing_store_timing_{};
    TimingHistogram batch_entry_store_timing_{};
    std::uintptr_t image_base_{};
    std::size_t image_size_{};
    std::uintptr_t bound_manager_{};
    std::uintptr_t bound_move_dispatch_{};
    std::uint64_t bound_session_generation_{};
    std::uint64_t bound_round_generation_{};
    CameraTopology bound_camera_topology_{};
    CallbackTopology bound_callback_topology_{};
};
}
