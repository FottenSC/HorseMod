#pragma once

#include "OnlineCoordinator.hpp"

#ifndef GEKKONET_STATIC
#define GEKKONET_STATIC
#endif
#include <gekkonet.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace Horse::Deterministic
{
[[nodiscard]] inline constexpr std::int32_t
QualificationCorrectionStimulusLead(std::uint32_t rollback_window) noexcept
{
    // One complete prediction window plus two peer-phase fenceposts keeps the
    // absolute trigger in both peers' future without wasting an entire second.
    return static_cast<std::int32_t>(rollback_window + 2);
}

[[nodiscard]] inline constexpr std::uint8_t
QualificationCorrectionTransportDelay(std::uint8_t requested_depth,
    std::uint8_t rollback_window) noexcept
{
    // SC6's two owned callbacks enter the same Gekko coordinate one native
    // tick apart, and transport polling can release a payload before the
    // receiver has simulated the requested prediction depth. Hold every
    // qualification payload for one additional tick. The requested maximum
    // remains 11 so the guarded delay stays inside the 12-frame window.
    if (requested_depth == 0 || requested_depth >= rollback_window)
        return 0;
    const auto guarded = static_cast<std::uint16_t>(requested_depth)
        + 1u;
    return guarded <= rollback_window
        ? static_cast<std::uint8_t>(guarded) : 0;
}

[[nodiscard]] inline constexpr std::optional<std::int32_t>
PlanQualificationCorrectionTrigger(std::int32_t mutually_confirmed_frame,
    std::int32_t lead_frames, std::int32_t next_local_input_frame) noexcept
{
    if (mutually_confirmed_frame < 0 || lead_frames <= 0
        || mutually_confirmed_frame > INT32_MAX - lead_frames)
        return std::nullopt;
    const auto trigger = mutually_confirmed_frame + lead_frames;
    return trigger >= next_local_input_frame
        ? std::optional<std::int32_t>{trigger} : std::nullopt;
}

[[nodiscard]] inline constexpr bool
MayRearmQualificationCorrectionStimulus(bool armed, bool payload_released,
    std::int32_t mutually_confirmed_frame,
    std::int32_t prior_trigger_frame) noexcept
{
    return armed && payload_released
        && mutually_confirmed_frame > prior_trigger_frame;
}

struct GekkoAdvanceValue
{
    std::int32_t frame{};
    std::array<PlayerInput, 2> inputs{};
    bool rolling_back{};
    bool running_ahead{};
};

enum class GekkoSimulationOperation : std::uint8_t
{
    None,
    Save,
    Load,
    Advance,
};

struct GekkoSimulationFailureContext
{
    GekkoSimulationOperation operation{GekkoSimulationOperation::None};
    std::int32_t frame{-1};
    GekkoSaveKind save_kind{GekkoSaveBaseline};
    bool rolling_back{};
    bool running_ahead{};
    bool deferred{};
};

[[nodiscard]] inline bool CanSealGekkoRound(
    std::int32_t confirmed_frame, std::int32_t next_frame,
    bool current_advance_pending) noexcept
{
    return next_frame >= 0 && !current_advance_pending
        && confirmed_frame >= next_frame - 1;
}

struct GekkoRoundSealPlan
{
    bool sealed{};
    bool discard_cross_generation_advance{};
};

[[nodiscard]] inline GekkoRoundSealPlan PlanGekkoRoundSeal(
    std::int32_t confirmed_frame, std::int32_t next_frame,
    bool current_advance_pending, FrameCoordinate pending_coordinate,
    FrameCoordinate retired_last_observed,
    FrameCoordinate current_coordinate,
    bool identity_replacement_pending = false) noexcept
{
    if (CanSealGekkoRound(
            confirmed_frame, next_frame, current_advance_pending))
        return {true, false};
    if (!current_advance_pending || next_frame <= 0
        || confirmed_frame < next_frame - 1
        || retired_last_observed.generation == 0
        || retired_last_observed.frame == UINT64_MAX
        || pending_coordinate.generation
            != retired_last_observed.generation
        || pending_coordinate.frame != retired_last_observed.frame + 1
        || (current_coordinate.generation
                <= retired_last_observed.generation
            && !(identity_replacement_pending
                && current_coordinate.generation
                    == retired_last_observed.generation))
        || current_coordinate.frame != pending_coordinate.frame)
        return {};
    return {true, true};
}

class IGekkoSimulationSink
{
public:
    virtual ~IGekkoSimulationSink() = default;
    virtual Status Save(std::int32_t frame, GekkoSaveKind kind,
        std::span<std::byte> destination, std::uint32_t& written,
        std::uint32_t& checksum) noexcept = 0;
    virtual Status Load(
        std::int32_t frame, std::span<const std::byte> state) noexcept = 0;
    virtual Status Advance(const GekkoAdvanceValue& value) noexcept = 0;
};

class GekkoRollbackSession final
{
public:
    static constexpr std::size_t maximum_state_bytes = 256;

    GekkoRollbackSession() noexcept = default;
    ~GekkoRollbackSession();
    GekkoRollbackSession(const GekkoRollbackSession&) = delete;
    GekkoRollbackSession& operator=(const GekkoRollbackSession&) = delete;

    Status Start(OnlineCoordinator& coordinator, IGekkoSimulationSink& sink,
        std::uint8_t local_player_slot, std::uint8_t input_delay,
        std::uint8_t rollback_window, std::uint32_t state_bytes) noexcept;
    Status PollNetwork() noexcept;
    Status FlushCorrections() noexcept;
    [[nodiscard]] bool ReadyForBaseline() const noexcept;
    [[nodiscard]] bool started() const noexcept { return started_; }
    [[nodiscard]] bool IsClearForStock() const noexcept { return !started_; }
    Status Advance(const PlayerInput& local_input,
        std::array<PlayerInput, 2>& authoritative) noexcept;
    Status CompleteDeferredSaves() noexcept;
    // Qualification-only stimulus: suppress a bounded run of outbound Gekko
    // payloads starting at one absolute Gekko frame and inject one local input
    // edge at that frame. The next authenticated payload carries the delayed
    // input history, forcing the peer through real restore/resimulation. A
    // released stimulus may be re-armed only after the caller has established
    // a later mutually matching confirmed-hash boundary.
    Status ArmQualificationCorrectionStimulus(
        std::uint8_t delay_frames, std::int32_t trigger_frame) noexcept;
    [[nodiscard]] bool qualification_correction_stimulus_injected()
        const noexcept { return qualification_stimulus_injected_; }
    [[nodiscard]] bool qualification_correction_stimulus_released()
        const noexcept { return qualification_stimulus_released_; }
    [[nodiscard]] std::int32_t confirmed_frame() const noexcept;
    [[nodiscard]] FailureCode terminal_failure() const noexcept;
    [[nodiscard]] GekkoSimulationFailureContext simulation_failure_context()
        const noexcept { return simulation_failure_context_; }
    void Stop() noexcept;

private:
    static void SendData(
        GekkoNetAddress* address, const char* data, int length) noexcept;
    static GekkoNetResult** ReceiveData(int* length) noexcept;
    static void FreeData(void* data) noexcept;

    Status process_session_events() noexcept;
    Status process_game_events(GekkoGameEvent** events, int count,
        std::array<PlayerInput, 2>* authoritative) noexcept;
    Status complete_save(GekkoGameEvent& event, bool deferred) noexcept;
    Status fail(FailureCode code) noexcept;

    static thread_local GekkoRollbackSession* current_adapter_owner_;
    static constexpr std::size_t maximum_receive_batch = 64;

    OnlineCoordinator* coordinator_{};
    IGekkoSimulationSink* sink_{};
    GekkoSession* session_{};
    GekkoNetAdapter adapter_{};
    std::uint64_t peer_address_token_{1};
    std::array<OnlineGekkoPacket, maximum_receive_batch> receive_packets_{};
    std::array<GekkoNetResult, maximum_receive_batch> receive_results_{};
    std::array<GekkoNetResult*, maximum_receive_batch> receive_result_ptrs_{};
    std::uint32_t state_bytes_{};
    std::uint8_t local_player_slot_{};
    std::uint8_t input_delay_{};
    FailureCode failure_{FailureCode::None};
    bool started_{};
    bool session_started_{};
    std::array<GekkoGameEvent*, 4> deferred_saves_{};
    std::size_t deferred_save_count_{};
    GekkoSimulationFailureContext simulation_failure_context_{};
    std::uint8_t qualification_delay_remaining_{};
    std::int32_t next_local_input_frame_{};
    std::int32_t qualification_trigger_frame_{-1};
    bool qualification_stimulus_armed_{};
    bool qualification_delay_active_{};
    bool qualification_stimulus_injected_{};
    bool qualification_stimulus_released_{};
};
}
