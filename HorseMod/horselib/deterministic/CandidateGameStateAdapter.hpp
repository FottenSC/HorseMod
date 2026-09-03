#pragma once

#include "CandidateCheckpoint.hpp"
#include "Interfaces.hpp"
#include "MotionBankSnapshot.hpp"
#include "MoveDispatchState.hpp"
#include "NativeCandidateRegions.hpp"
#include "StageWindGraphTransaction.hpp"

#include <algorithm>
#include <array>
#include <memory>

namespace Horse::Deterministic
{
using CandidateAdvanceFn = Status (*)(
    void* user,
    FrameCoordinate coordinate,
    const InputPair& inputs,
    bool suppress_ephemeral_presentation) noexcept;
using CandidateRebuildFn = Status (*)(void* user) noexcept;
using CandidateReconcileFn = Status (*)(
    void* user, FrameCoordinate coordinate) noexcept;

struct CandidateAdapterBinding
{
    NativeContext context{};
    BattleAudioSelectorState* battle_audio_selector{};
    HgCpuGenerationContext hgcpu_context{};
    HgCpuExecFn hgcpu_writer{};
    HgCpuExecFn hgcpu_reader{};
    MotionBankSnapshot* motion_banks{};
    MoveDispatchState* move_dispatch{};
    SecondaryEventState* secondary_events{};
    CharaAnimationState* chara_animation{};
    UcrtRandBroker* ucrt_broker{};
    StageWindTopologyProbe* wind_probe{};
    StageWindGraphTransaction* wind_transaction{};
    StageWindTopologyAddresses wind_addresses{};
    std::uint32_t simulation_thread_id{};
    void* action_user{};
    CandidateAdvanceFn advance{};
    CandidateRebuildFn rebuild{};
    CandidateReconcileFn reconcile{};
};

struct CandidatePhaseTimingStatus
{
    std::uint64_t samples{};
    std::uint64_t maximum_ns{};
    std::uint64_t p99_ns{};
};

struct CandidateAdapterPerformanceStatus
{
    static constexpr std::size_t scratch_owner_count = 7;
    CandidatePhaseTimingStatus total_capture{};
    CandidatePhaseTimingStatus typed_capture{};
    CandidatePhaseTimingStatus local_capture{};
    CandidatePhaseTimingStatus hgcpu_capture{};
    CandidatePhaseTimingStatus motion_capture{};
    CandidatePhaseTimingStatus ucrt_capture{};
    CandidatePhaseTimingStatus wind_capture{};
    CandidatePhaseTimingStatus encode{};
    CandidatePhaseTimingStatus local_restore{};
    CandidatePhaseTimingStatus typed_restore{};
    CandidatePhaseTimingStatus wind_restore{};
    CandidatePhaseTimingStatus ucrt_restore{};
    CandidatePhaseTimingStatus derived_repair{};
    CandidatePhaseTimingStatus total_restore{};
    std::size_t scratch_capacity_baseline_bytes{};
    std::size_t scratch_capacity_high_water_bytes{};
    std::uint64_t scratch_capacity_growth_events{};
    std::array<std::size_t, scratch_owner_count>
        scratch_capacity_baseline_by_owner{};
    std::array<std::size_t, scratch_owner_count>
        scratch_capacity_high_water_by_owner{};
};

struct PeerBaselineStateDiagnostic
{
    static constexpr std::size_t animation_clip_word_count = 0x20 / 4;
    static constexpr std::size_t animation_scheduler_word_count = 0x5C / 4;
    static constexpr std::size_t stage_emitter_word_count =
        native_stage_wind_emitter_state_size / 4;
    std::array<std::array<std::uint32_t,
        animation_clip_word_count>, 2> animation_clip_words{};
    std::array<std::array<std::uint32_t,
        animation_scheduler_word_count>, 2> animation_scheduler_words{};
    std::uint32_t stage_emitter_count{};
    std::array<std::uint32_t, stage_emitter_word_count>
        first_stage_emitter_words{};
};

enum class CandidateCapturePhase : std::uint8_t
{
    None = 0,
    NativeTyped = 1,
    BattleAudioSelector = 2,
    MoveDispatch = 3,
    SecondaryEvents = 4,
    CharaAnimation = 5,
    HgCpu = 6,
    MotionBanks = 7,
    Ucrt = 8,
    StageWind = 9,
    Encode = 10,
    CameraTopology = 11,
    CallbackTopology = 12,
    Adapter = 13,
};

static_assert(static_cast<std::uint8_t>(CandidateCapturePhase::NativeTyped) == 1
    && static_cast<std::uint8_t>(CandidateCapturePhase::Encode) == 10,
    "existing capture-phase diagnostic values are an append-only contract");

constexpr std::string_view candidate_capture_phase_name(
    CandidateCapturePhase phase) noexcept
{
    switch (phase)
    {
    case CandidateCapturePhase::None: return "none";
    case CandidateCapturePhase::CameraTopology: return "camera_topology";
    case CandidateCapturePhase::CallbackTopology: return "callback_topology";
    case CandidateCapturePhase::Adapter: return "adapter";
    case CandidateCapturePhase::NativeTyped: return "native_typed";
    case CandidateCapturePhase::BattleAudioSelector: return "battle_audio_selector";
    case CandidateCapturePhase::MoveDispatch: return "move_dispatch";
    case CandidateCapturePhase::SecondaryEvents: return "secondary_events";
    case CandidateCapturePhase::CharaAnimation: return "chara_animation";
    case CandidateCapturePhase::HgCpu: return "hgcpu";
    case CandidateCapturePhase::MotionBanks: return "motion_banks";
    case CandidateCapturePhase::Ucrt: return "ucrt";
    case CandidateCapturePhase::StageWind: return "stage_wind";
    case CandidateCapturePhase::Encode: return "encode";
    }
    return "unknown";
}

class CandidateGameStateAdapter final : public IGameStateAdapter
{
public:
    CandidateGameStateAdapter(
        NativeCandidateRegions& regions, HgCpuStreamShim& hgcpu) noexcept;

    Status Configure(const CandidateAdapterBinding& binding) noexcept;
    void Reset() noexcept;
    void ReleaseScratchStorage() noexcept;

    Status BindContext(const NativeContext& context) noexcept override;
    Status PreflightCapture(FrameCoordinate coordinate) noexcept override;
    Status Capture(FrameCoordinate coordinate, Snapshot& output) noexcept override;
    Status CaptureCanonical(
        FrameCoordinate coordinate, Snapshot& output) noexcept;
    [[nodiscard]] Status GetLastCanonicalPeerDiagnostic(
        FrameCoordinate coordinate,
        PeerBaselineStateDiagnostic& output) const noexcept;
    Status PreflightRestore(const Snapshot& snapshot) noexcept override;
    Status Restore(const Snapshot& snapshot) noexcept override;
    Status RebuildDerivedState() noexcept override;
    Status VerifyRestoredState(const Snapshot& expected) noexcept override;
    Status AdvanceFrame(
        FrameCoordinate coordinate,
        const InputPair& inputs,
        bool suppress_ephemeral_presentation) noexcept override;
    Status ReconcilePresentation(FrameCoordinate coordinate) noexcept override;
    [[nodiscard]] CandidateAdapterPerformanceStatus performance_status()
        const noexcept;
    void ResetCapturePerformanceWindow() noexcept;
    [[nodiscard]] std::size_t owned_scratch_bytes() const noexcept;
    [[nodiscard]] CandidateCapturePhase last_capture_phase() const noexcept
    {
        return last_capture_phase_;
    }
    [[nodiscard]] std::uint32_t last_restore_difference_mask() const noexcept
    {
        return last_restore_difference_mask_;
    }
    [[nodiscard]] std::uint32_t last_restore_operation_failure_mask() const noexcept
    {
        return last_restore_operation_failure_mask_;
    }
    [[nodiscard]] std::array<std::uint16_t, 2>
    last_captured_movevm_short25() const noexcept
    {
        return last_captured_movevm_short25_;
    }
    [[nodiscard]] const NativeMoveVmStateShortImage&
    last_captured_movevm_state_shorts() const noexcept
    {
        return last_captured_movevm_state_shorts_;
    }
    [[nodiscard]] const NativeRngImage& last_captured_rng() const noexcept
    {
        return last_captured_rng_;
    }
    Status TraceLocalStreamOffset(std::size_t stream_offset,
        HgCpuWriteSpan& output) noexcept;

private:
    struct PhaseTimingHistogram
    {
        static constexpr std::uint64_t bucket_width_ns = 10'000;
        static constexpr std::size_t bucket_count = 502;

        void Record(std::uint64_t nanoseconds) noexcept
        {
            const auto bucket = static_cast<std::size_t>(std::min<std::uint64_t>(
                nanoseconds / bucket_width_ns, bucket_count - 1));
            ++buckets[bucket];
            ++samples;
            if (nanoseconds > maximum_ns) maximum_ns = nanoseconds;
        }

        [[nodiscard]] CandidatePhaseTimingStatus Status() const noexcept
        {
            CandidatePhaseTimingStatus result{samples, maximum_ns, 0};
            if (samples == 0) return result;
            const std::uint64_t target = (samples * 99 + 99) / 100;
            std::uint64_t cumulative{};
            for (std::size_t index = 0; index < buckets.size(); ++index)
            {
                cumulative += buckets[index];
                if (cumulative >= target)
                {
                    result.p99_ns = (index + 1) * bucket_width_ns;
                    break;
                }
            }
            return result;
        }

        std::array<std::uint64_t, bucket_count> buckets{};
        std::uint64_t samples{};
        std::uint64_t maximum_ns{};
    };

    [[nodiscard]] bool context_matches(const NativeContext& context) const noexcept;
    Status capture_image(
        CandidateCheckpointImage& output, bool include_local = true) noexcept;
    Status decode_and_preflight(
        const Snapshot& snapshot, CandidateCheckpointImage& output) noexcept;
    Status restore_image(const CandidateCheckpointImage& image) noexcept;
    bool undo_image(const CandidateCheckpointImage& image) noexcept;
    [[nodiscard]] std::size_t scratch_capacity_bytes() const noexcept;
    [[nodiscard]] std::array<std::size_t,
        CandidateAdapterPerformanceStatus::scratch_owner_count>
        scratch_capacity_by_owner() const noexcept;
    void observe_scratch_capacity() noexcept;

    NativeCandidateRegions& regions_;
    HgCpuStreamShim& hgcpu_;
    CandidateAdapterBinding binding_{};
    PhaseTimingHistogram total_capture_timing_{};
    PhaseTimingHistogram typed_capture_timing_{};
    PhaseTimingHistogram local_capture_timing_{};
    PhaseTimingHistogram hgcpu_capture_timing_{};
    PhaseTimingHistogram motion_capture_timing_{};
    PhaseTimingHistogram ucrt_capture_timing_{};
    PhaseTimingHistogram wind_capture_timing_{};
    PhaseTimingHistogram encode_timing_{};
    PhaseTimingHistogram local_restore_timing_{};
    PhaseTimingHistogram typed_restore_timing_{};
    PhaseTimingHistogram wind_restore_timing_{};
    PhaseTimingHistogram ucrt_restore_timing_{};
    PhaseTimingHistogram derived_repair_timing_{};
    PhaseTimingHistogram total_restore_timing_{};
    CandidateCheckpointImage capture_scratch_{};
    CandidateCheckpointImage canonical_capture_scratch_{};
    std::unique_ptr<CandidateCheckpointImage> transaction_target_scratch_{};
    std::unique_ptr<CandidateCheckpointImage> transaction_scratch_{};
    std::size_t scratch_capacity_baseline_bytes_{};
    std::size_t scratch_capacity_high_water_bytes_{};
    std::uint64_t scratch_capacity_growth_events_{};
    std::array<std::size_t,
        CandidateAdapterPerformanceStatus::scratch_owner_count>
        scratch_capacity_baseline_by_owner_{};
    std::array<std::size_t,
        CandidateAdapterPerformanceStatus::scratch_owner_count>
        scratch_capacity_high_water_by_owner_{};
    CandidateCapturePhase last_capture_phase_{};
    std::array<std::uint16_t, 2> last_captured_movevm_short25_{};
    NativeMoveVmStateShortImage last_captured_movevm_state_shorts_{};
    NativeRngImage last_captured_rng_{};
    FrameCoordinate last_canonical_capture_coordinate_{};
    std::uint32_t last_restore_difference_mask_{};
    std::uint32_t last_restore_operation_failure_mask_{};
    bool configured_{};
    bool bound_{};
};
}
