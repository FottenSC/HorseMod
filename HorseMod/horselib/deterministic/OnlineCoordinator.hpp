#pragma once

#include "OnlineContractTypes.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <variant>

namespace Horse::Deterministic
{
class IOnlineContentAllowlist
{
public:
    virtual ~IOnlineContentAllowlist() = default;
    [[nodiscard]] virtual bool IsQualified(
        const OnlineContentContract& content) const noexcept = 0;
};

struct OnlineMonotonicClock
{
    void* user{};
    std::uint64_t (*now_milliseconds)(void* user) noexcept{};
};

enum class OnlineFailureDisposition : std::uint8_t
{
    None,
    LeaveStockUntouched,
    TerminateMatchToLobby,
};

struct OnlineInputPacket
{
    FrameCoordinate coordinate{};
    PlayerInput input{};

    friend constexpr bool operator==(const OnlineInputPacket&, const OnlineInputPacket&) = default;
};

struct OnlineStateHashPacket
{
    FrameCoordinate coordinate{};
    CanonicalHash hash{};

    friend constexpr bool operator==(
        const OnlineStateHashPacket&,
        const OnlineStateHashPacket&) = default;
};

// Gekko confirms input availability independently of the native simulation
// fencepost. A confirmed input frame must not request a canonical hash for a
// coordinate the game has not completed yet.
[[nodiscard]] inline std::optional<FrameCoordinate>
PlanConfirmedHashPublication(FrameCoordinate baseline,
    std::int32_t next_hash_frame, std::int32_t gekko_confirmed_frame,
    FrameCoordinate completed_native) noexcept
{
    if (baseline.generation == 0 || next_hash_frame < 0
        || gekko_confirmed_frame < next_hash_frame
        || completed_native.generation != baseline.generation)
        return std::nullopt;
    const auto offset = static_cast<std::uint64_t>(next_hash_frame) + 1;
    if (baseline.frame > UINT64_MAX - offset) return std::nullopt;
    const FrameCoordinate candidate{
        baseline.generation, baseline.frame + offset};
    return candidate <= completed_native
        ? std::optional<FrameCoordinate>{candidate} : std::nullopt;
}

// A remote-first round barrier can complete synchronously inside the local
// BeginRoundBarrier call. Service re-arm therefore keys off the completed
// coordinator state plus its local pending token, not on observing the
// transient RoundBarrier state.
[[nodiscard]] inline bool RequiresRoundTransitionRearm(
    OnlineState state, bool transition_pending) noexcept
{
    return transition_pending && state == OnlineState::AwaitingBattle;
}

// Correction preflight starts at baseline+1. A peer exactly on the baseline
// has no such snapshot yet, even though the empty Gekko prefix is vacuously
// confirmed. Wait for at least one observed and confirmed prefix frame.
[[nodiscard]] inline bool CanCompleteOnlinePrefixCatchup(
    std::size_t prefix_frames, std::int32_t gekko_confirmed_frame,
    FrameCoordinate next, FrameCoordinate completed_native) noexcept
{
    if (prefix_frames == 0
        || prefix_frames - 1
            > static_cast<std::size_t>((std::numeric_limits<std::int32_t>::max)()))
        return false;
    return gekko_confirmed_frame
            >= static_cast<std::int32_t>(prefix_frames - 1)
        && next.generation == completed_native.generation
        && next > completed_native;
}

struct OnlineGekkoPacket
{
    std::uint64_t epoch{};
    std::uint16_t size{};
    std::array<std::byte, Schema::maximum_transport_payload> payload{};
};

enum class OnlineSceneExitBoundary : std::uint8_t
{
    None,
    CasualMatchPresenceExit,
    BattleTerminationCompleted,
};

struct OnlineSceneExitEvidence
{
    std::uint64_t session_id{};
    OnlineSceneExitBoundary boundary{OnlineSceneExitBoundary::None};
};

// Bounded value-only context for the first inbound message that terminates a
// coordinator.  Qualification must be able to distinguish a bad coordinate,
// a stale Gekko epoch, and a lifecycle error without retaining transport data
// or relying on an unbounded packet trace.
struct OnlineCoordinatorFailureContext
{
    bool has_inbound_message{};
    TransportMessageKind message_kind{};
    OnlineState state_before_message{OnlineState::Disabled};
    std::uint64_t payload_word0{};
    std::uint64_t payload_word1{};
};

using OnlineGameplayEvent = std::variant<OnlineInputPacket, OnlineStateHashPacket>;

class OnlineCoordinator
{
public:
    OnlineCoordinator(
        IRollbackTransport& transport,
        const IOnlineContentAllowlist& allowlist,
        OnlineMonotonicClock clock = {}) noexcept;

    Status Enable() noexcept;
    Status ObserveLobby(const OnlinePeerContract& contract) noexcept;
    Status Pump() noexcept;
    Status ReadyBaseline(FrameCoordinate earliest_safe_coordinate) noexcept;
    [[nodiscard]] std::optional<FrameCoordinate> baseline_target() const noexcept
    {
        return baseline_target_;
    }
    Status ObserveBaselineProgress(FrameCoordinate coordinate) noexcept;
    Status FreezeBaseline(
        FrameCoordinate coordinate,
        const CanonicalHash& hash,
        const CanonicalHash& loaded_map_identity) noexcept;
    Status NotifyOwnedTick(FrameCoordinate coordinate) noexcept;
    Status SendInput(FrameCoordinate coordinate, const PlayerInput& input) noexcept;
    Status SendConfirmedHash(
        FrameCoordinate coordinate,
        const CanonicalHash& hash) noexcept;
    Status SendGekkoPayload(std::span<const std::byte> payload) noexcept;
    [[nodiscard]] std::optional<OnlineGameplayEvent> PopGameplay() noexcept;
    [[nodiscard]] std::optional<OnlineGekkoPacket> PopGekkoPayload() noexcept;
    Status BeginRoundBarrier(
        FrameCoordinate completed_coordinate,
        std::uint64_t next_generation,
        const CanonicalHash& confirmed_hash) noexcept;
    Status ReturnToLobby() noexcept;
    Status NotifyReturnedToLobby(
        const OnlineSceneExitEvidence& evidence) noexcept;
    Status Abort(FailureCode code) noexcept;
    void Disable() noexcept;

    [[nodiscard]] OnlineState state() const noexcept { return state_; }
    [[nodiscard]] FailureCode terminal_failure() const noexcept { return failure_; }
    [[nodiscard]] OnlineState failure_origin_state() const noexcept
    {
        return failure_origin_state_;
    }
    [[nodiscard]] OnlineCoordinatorFailureContext failure_context() const noexcept
    {
        return failure_context_;
    }
    [[nodiscard]] std::optional<FrameCoordinate> local_baseline_ready() const noexcept
    {
        return local_baseline_ready_;
    }
    [[nodiscard]] std::optional<FrameCoordinate> remote_baseline_ready() const noexcept
    {
        return remote_baseline_ready_;
    }
    [[nodiscard]] OnlineFailureDisposition failure_disposition() const noexcept
    {
        return disposition_;
    }
    [[nodiscard]] bool owns_simulation() const noexcept { return owns_simulation_; }
    [[nodiscard]] bool IsClearForStock() const noexcept
    {
        return state_ == OnlineState::Disabled && !contract_
            && !owns_simulation_ && !local_baseline_ && !remote_baseline_
            && !local_baseline_ready_ && !remote_baseline_ready_
            && !baseline_target_ && gameplay_size_ == 0 && gekko_size_ == 0;
    }
    [[nodiscard]] std::optional<OnlinePeerContract> active_contract() const noexcept
    {
        return contract_;
    }

private:
    struct Baseline
    {
        FrameCoordinate coordinate{};
        CanonicalHash hash{};
        CanonicalHash loaded_map_identity{};

        friend constexpr bool operator==(const Baseline&, const Baseline&) = default;
    };

    struct RoundBoundary
    {
        FrameCoordinate completed_coordinate{};
        std::uint64_t next_generation{};
        CanonicalHash confirmed_hash{};

        friend constexpr bool operator==(
            const RoundBoundary&,
            const RoundBoundary&) = default;
    };

    Status handle_message(const TransportMessage& message) noexcept;
    Status handle_handshake(const TransportMessage& message) noexcept;
    Status handle_baseline_ready(const TransportMessage& message) noexcept;
    Status handle_baseline_commit(const TransportMessage& message) noexcept;
    Status handle_baseline(const TransportMessage& message) noexcept;
    Status handle_gameplay(const TransportMessage& message) noexcept;
    Status handle_gekko(const TransportMessage& message) noexcept;
    Status handle_round_barrier(const TransportMessage& message) noexcept;
    Status send_contract(TransportMessageKind kind) noexcept;
    Status send_coordinate(
        TransportMessageKind kind, FrameCoordinate coordinate) noexcept;
    Status send_baseline(TransportMessageKind kind, const Baseline& value) noexcept;
    Status fail(FailureCode code) noexcept;
    void clear_session() noexcept;
    void try_activate() noexcept;
    Status try_commit_baseline() noexcept;
    void try_finish_round_barrier() noexcept;
    [[nodiscard]] std::uint64_t now_milliseconds() const noexcept;
    void arm_deadline(std::uint64_t duration_milliseconds) noexcept;
    Status check_deadline() noexcept;

    static constexpr std::size_t maximum_queued_gameplay_messages = 128;
    static constexpr std::size_t maximum_queued_gekko_messages = 64;
    static constexpr std::size_t maximum_messages_per_pump = 64;

    IRollbackTransport& transport_;
    const IOnlineContentAllowlist& allowlist_;
    OnlineMonotonicClock clock_{};
    std::optional<OnlinePeerContract> contract_;
    std::optional<FrameCoordinate> local_baseline_ready_;
    std::optional<FrameCoordinate> remote_baseline_ready_;
    std::optional<FrameCoordinate> baseline_target_;
    std::optional<Baseline> local_baseline_;
    std::optional<Baseline> remote_baseline_;
    std::optional<RoundBoundary> local_round_boundary_;
    std::optional<RoundBoundary> remote_round_boundary_;
    std::array<std::optional<OnlineGameplayEvent>,
        maximum_queued_gameplay_messages> gameplay_messages_{};
    std::size_t gameplay_head_{};
    std::size_t gameplay_size_{};
    std::array<std::optional<OnlineGekkoPacket>, maximum_queued_gekko_messages>
        gekko_messages_{};
    std::size_t gekko_head_{};
    std::size_t gekko_size_{};
    OnlineState state_{OnlineState::Disabled};
    FailureCode failure_{FailureCode::None};
    OnlineState failure_origin_state_{OnlineState::Disabled};
    OnlineFailureDisposition disposition_{OnlineFailureDisposition::None};
    bool peer_hello_received_{};
    bool peer_hello_ack_received_{};
    bool peer_baseline_ack_received_{};
    bool owns_simulation_{};
    std::uint64_t required_generation_{};
    std::uint64_t gekko_epoch_{};
    OnlineCoordinatorFailureContext failure_context_{};
    std::optional<RoundBoundary> completed_round_boundary_;
    std::uint64_t deadline_milliseconds_{};
};
}
