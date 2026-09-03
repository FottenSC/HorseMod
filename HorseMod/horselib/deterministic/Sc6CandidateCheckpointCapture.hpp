#pragma once

#include "CallbackTopology.hpp"
#include "CharaAnimationState.hpp"
#include "CandidateGameStateAdapter.hpp"
#include "MotionBankSnapshot.hpp"
#include "MoveDispatchState.hpp"
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

enum class CameraTopologyCaptureStage : std::uint8_t
{
    None,
    RootPointer,
    RootIdentity,
    RootReadable,
    DirectorVtables,
    ActionListOwner,
    ActionListBacking,
    ActionListSlots,
    ActionBackingTail,
    ActionSlotPointer,
    ActionVtable,
    ActionIndex,
    ActionOwner,
    ActionList,
    ActionType,
    BoundTopology,
};

[[nodiscard]] constexpr const char* camera_topology_capture_stage_name(
    CameraTopologyCaptureStage stage) noexcept
{
    switch (stage)
    {
    case CameraTopologyCaptureStage::None: return "none";
    case CameraTopologyCaptureStage::RootPointer: return "root_pointer";
    case CameraTopologyCaptureStage::RootIdentity: return "root_identity";
    case CameraTopologyCaptureStage::RootReadable: return "root_readable";
    case CameraTopologyCaptureStage::DirectorVtables: return "director_vtables";
    case CameraTopologyCaptureStage::ActionListOwner: return "action_list_owner";
    case CameraTopologyCaptureStage::ActionListBacking: return "action_list_backing";
    case CameraTopologyCaptureStage::ActionListSlots: return "action_list_slots";
    case CameraTopologyCaptureStage::ActionBackingTail: return "action_backing_tail";
    case CameraTopologyCaptureStage::ActionSlotPointer: return "action_slot_pointer";
    case CameraTopologyCaptureStage::ActionVtable: return "action_vtable";
    case CameraTopologyCaptureStage::ActionIndex: return "action_index";
    case CameraTopologyCaptureStage::ActionOwner: return "action_owner";
    case CameraTopologyCaptureStage::ActionList: return "action_list";
    case CameraTopologyCaptureStage::ActionType: return "action_type";
    case CameraTopologyCaptureStage::BoundTopology: return "bound_topology";
    }
    return "unknown";
}

struct CameraTopologyCaptureDiagnostic
{
    FailureCode intended_failure{FailureCode::None};
    CameraTopologyCaptureStage stage{CameraTopologyCaptureStage::None};
    std::uint32_t index{};
    std::uint64_t observed{};
    std::uint64_t expected{};
};

struct CandidateTransientCaptureDiagnostic
{
    FailureCode failure{FailureCode::None};
    CandidateCapturePhase phase{CandidateCapturePhase::None};
    NativeCandidateValidationDiagnostic validation{};
    CharaAnimationTopologyIssue animation_topology_issue{};
    std::uintptr_t animation_topology_observed{};
    CameraTopologyCaptureDiagnostic camera{};
    std::uint32_t identity_issue{};
    std::uint64_t identity_expected{};
    std::uint64_t identity_observed{};
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
    Status BindForCanonicalCapture(
        std::uintptr_t battle_manager,
        FrameCoordinate coordinate,
        std::uint64_t session_generation,
        std::uint32_t simulation_thread_id) noexcept;
    Status CaptureTransient(
        FrameCoordinate coordinate, Snapshot& output,
        CandidateTransientCaptureDiagnostic* diagnostic = nullptr) noexcept;
    Status CaptureCanonical(
        FrameCoordinate coordinate, Snapshot& output,
        CandidateTransientCaptureDiagnostic* diagnostic = nullptr) noexcept;
    Status StoreSynchronizedBatchEntry(const Snapshot& snapshot) noexcept;
    Status EnsureRestoreOwnership(std::uint32_t simulation_thread_id) noexcept;
    Status RestoreAndVerify(const Snapshot& snapshot) noexcept;
    Status RestoreBattleAudioSelectorForPresentation(
        const Snapshot& snapshot) noexcept;
    Status RestoreInputLogForReplay(const Snapshot& snapshot) noexcept;
    Status RestoreMoveDispatchMasksForReplay(
        const Snapshot& snapshot) noexcept;
    Status RestoreMoveDispatchMasksForReplay(
        const CanonicalMoveDispatchDiagnostic& diagnostic) noexcept;
    Status CaptureCameraSourceFrame(
        NativeCameraSourceFrameImage& output) noexcept;
    Status RestoreCameraSourceFrameForReplay(
        const NativeCameraSourceFrameImage& image) noexcept;
    Status PrepareInputLogForReplay(
        const CanonicalInputDiagnostic& expected,
        const InputPair& input) noexcept;
    void InvalidateHistory() noexcept;
    void ReleaseHistoryStorage() noexcept;
    void ReleaseBinding() noexcept;
    void ReleaseBindingStorage() noexcept;
    void Reset() noexcept;

    [[nodiscard]] CandidateCheckpointCaptureStatus status(
        CandidateCheckpointRole role) const noexcept;
    [[nodiscard]] const SnapshotStore& snapshots(
        CandidateCheckpointRole role) const noexcept;
    Status ReplaceCorrectionSnapshots(
        std::span<Snapshot> landing_replacements,
        std::span<const CanonicalHash> expected_landing_hashes,
        std::span<Snapshot> batch_entry_replacements,
        std::span<const CanonicalHash> expected_batch_entry_hashes) noexcept;
    [[nodiscard]] Status ValidateCorrectionSnapshots(
        std::span<const Snapshot> landing_replacements,
        std::span<const CanonicalHash> expected_landing_hashes,
        std::span<const Snapshot> batch_entry_replacements,
        std::span<const CanonicalHash> expected_batch_entry_hashes) const noexcept;
    [[nodiscard]] NativeCandidateValidationDiagnostic restore_validation()
        const noexcept;
    [[nodiscard]] std::uint32_t restore_difference_mask() const noexcept;
    [[nodiscard]] std::uint32_t restore_operation_failure_mask() const noexcept;
    [[nodiscard]] std::uint8_t restore_failure_phase() const noexcept
    {
        return restore_failure_phase_;
    }
    [[nodiscard]] CandidateAdapterPerformanceStatus adapter_performance()
        const noexcept;
    void ResetCapturePerformanceWindow() noexcept;
    [[nodiscard]] std::size_t owned_scratch_bytes() const noexcept;
    [[nodiscard]] CandidateCapturePhase transient_capture_phase() const noexcept;
    [[nodiscard]] std::array<std::uint16_t, 2>
    last_captured_movevm_short25() const noexcept;
    [[nodiscard]] NativeMoveVmStateShortImage
    last_captured_movevm_state_shorts() const noexcept;
    [[nodiscard]] NativeRngImage last_captured_rng() const noexcept;
    [[nodiscard]] Status GetLastCanonicalPeerDiagnostic(
        FrameCoordinate coordinate,
        PeerBaselineStateDiagnostic& output) const noexcept;
    [[nodiscard]] CharaAnimationTopologyIssue
    transient_animation_topology_issue() const noexcept;
    [[nodiscard]] std::uintptr_t
    transient_animation_topology_observed() const noexcept;
    [[nodiscard]] std::uint32_t transient_identity_issue() const noexcept;
    [[nodiscard]] std::uint64_t transient_identity_expected() const noexcept;
    [[nodiscard]] std::uint64_t transient_identity_observed() const noexcept;
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

        friend bool operator==(const CameraTopology& left,
            const CameraTopology& right) noexcept
        {
            // LuxCameraFuncList reconstructs action objects in place. The
            // backing address is topology; each slot's vtable and cached type
            // are mutable deterministic state and are validated on every
            // capture rather than frozen as generation identity.
            return left.camera_root == right.camera_root
                && left.action_backing == right.action_backing;
        }
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
    Status capture_camera_topology(CameraTopology& output,
        CameraTopologyCaptureDiagnostic* diagnostic = nullptr) noexcept;
    Status capture_callback_topology(CallbackTopology& output) noexcept;
    Status finish_transient_capture(Status status,
        const CandidateTransientCaptureDiagnostic& diagnostic,
        CandidateTransientCaptureDiagnostic* output) noexcept;

    // Every admitted SC6 checkpoint owns the fixed MotionBankTriples image.
    // Derive the slot ceiling from that proven allocation floor so reserving
    // heavyweight Snapshot objects cannot consume the payload budget itself.
    static constexpr std::size_t maximum_landing_checkpoints =
        Schema::replay_landing_checkpoint_memory_budget
        / (motion_bank_image_bytes + sizeof(Snapshot));
    static constexpr std::size_t maximum_batch_entry_checkpoints =
        Schema::replay_batch_entry_checkpoint_memory_budget
        / (motion_bank_image_bytes + sizeof(Snapshot));

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
    std::unique_ptr<BattleAudioSelectorState> battle_audio_selector_;
    std::unique_ptr<MotionBankSnapshot> motion_banks_;
    std::unique_ptr<MoveDispatchState> move_dispatch_;
    std::unique_ptr<SecondaryEventState> secondary_events_;
    std::unique_ptr<CharaAnimationState> chara_animation_;
    std::unique_ptr<CallbackTopologyProbe> callback_probe_;
    std::unique_ptr<StageWindTopologyProbe> wind_probe_;
    std::unique_ptr<ProcessStageWindAllocator> wind_allocator_;
    std::unique_ptr<StageWindGraphTransaction> wind_transaction_;
    HgCpuStreamShim hgcpu_{};
    std::unique_ptr<CandidateGameStateAdapter> adapter_;
    UcrtRandBroker* ucrt_broker_{};
    SnapshotStore landing_snapshots_{
        Schema::replay_landing_checkpoint_memory_budget,
        maximum_landing_checkpoints, CapacityPolicy::RejectNew};
    SnapshotStore batch_entry_snapshots_{
        Schema::replay_batch_entry_checkpoint_memory_budget,
        maximum_batch_entry_checkpoints, CapacityPolicy::RejectNew};
    Snapshot landing_capture_scratch_{};
    Snapshot batch_entry_capture_scratch_{};
    std::unique_ptr<CandidateCheckpointImage> auxiliary_decode_scratch_{};
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
    CallbackTopology callback_topology_scratch_{};
    std::uint32_t transient_identity_issue_{};
    CandidateCapturePhase transient_capture_phase_{};
    std::uint8_t restore_failure_phase_{};
    std::uint64_t transient_identity_expected_{};
    std::uint64_t transient_identity_observed_{};
};
}
