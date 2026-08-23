#pragma once

#include "Interfaces.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <variant>

namespace Horse::Deterministic
{
struct OnlineContentContract
{
    std::array<std::uint32_t, 2> fighter_ids{};
    std::uint32_t stage_id{};
    std::uint32_t map_id{};

    friend constexpr bool operator==(
        const OnlineContentContract&,
        const OnlineContentContract&) = default;
};

struct OnlinePeerContract
{
    std::uint64_t session_id{};
    std::uint64_t lobby_id{};
    std::array<std::uint64_t, 2> steam_ids{};
    std::uint8_t local_player_slot{};
    std::uint8_t lobby_member_count{};
    bool casual_player_match{};
    CanonicalHash executable_id{};
    CanonicalHash build_id{};
    OnlineContentContract content{};
    std::uint32_t input_delay{};
    std::uint32_t rollback_window{};
};

class IOnlineContentAllowlist
{
public:
    virtual ~IOnlineContentAllowlist() = default;
    [[nodiscard]] virtual bool IsQualified(
        const OnlineContentContract& content) const noexcept = 0;
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

using OnlineGameplayEvent = std::variant<OnlineInputPacket, OnlineStateHashPacket>;

class OnlineCoordinator
{
public:
    OnlineCoordinator(
        IRollbackTransport& transport,
        const IOnlineContentAllowlist& allowlist) noexcept;

    Status Enable() noexcept;
    Status ObserveLobby(const OnlinePeerContract& contract) noexcept;
    Status Pump() noexcept;
    Status FreezeBaseline(
        std::uint64_t generation,
        const CanonicalHash& hash) noexcept;
    Status NotifyOwnedTick(FrameCoordinate coordinate) noexcept;
    Status SendInput(FrameCoordinate coordinate, const PlayerInput& input) noexcept;
    Status SendConfirmedHash(
        FrameCoordinate coordinate,
        const CanonicalHash& hash) noexcept;
    [[nodiscard]] std::optional<OnlineGameplayEvent> PopGameplay() noexcept;
    Status BeginRoundBarrier(
        std::uint64_t completed_generation,
        std::uint64_t next_generation,
        const CanonicalHash& confirmed_hash) noexcept;
    Status ReturnToLobby() noexcept;
    Status NotifyReturnedToLobby() noexcept;
    void Disable() noexcept;

    [[nodiscard]] OnlineState state() const noexcept { return state_; }
    [[nodiscard]] FailureCode terminal_failure() const noexcept { return failure_; }
    [[nodiscard]] OnlineFailureDisposition failure_disposition() const noexcept
    {
        return disposition_;
    }
    [[nodiscard]] bool owns_simulation() const noexcept { return owns_simulation_; }
    [[nodiscard]] std::optional<OnlinePeerContract> active_contract() const noexcept
    {
        return contract_;
    }

private:
    struct Baseline
    {
        std::uint64_t generation{};
        CanonicalHash hash{};

        friend constexpr bool operator==(const Baseline&, const Baseline&) = default;
    };

    struct RoundBoundary
    {
        std::uint64_t completed_generation{};
        std::uint64_t next_generation{};
        CanonicalHash confirmed_hash{};

        friend constexpr bool operator==(
            const RoundBoundary&,
            const RoundBoundary&) = default;
    };

    Status handle_message(const TransportMessage& message) noexcept;
    Status handle_handshake(const TransportMessage& message) noexcept;
    Status handle_baseline(const TransportMessage& message) noexcept;
    Status handle_gameplay(const TransportMessage& message) noexcept;
    Status handle_round_barrier(const TransportMessage& message) noexcept;
    Status send_contract(TransportMessageKind kind) noexcept;
    Status send_baseline(TransportMessageKind kind, const Baseline& value) noexcept;
    Status fail(FailureCode code) noexcept;
    void clear_session() noexcept;
    void try_activate() noexcept;
    void try_finish_round_barrier() noexcept;

    static constexpr std::size_t maximum_queued_gameplay_messages = 128;
    static constexpr std::size_t maximum_messages_per_pump = 64;

    IRollbackTransport& transport_;
    const IOnlineContentAllowlist& allowlist_;
    std::optional<OnlinePeerContract> contract_;
    std::optional<Baseline> local_baseline_;
    std::optional<Baseline> remote_baseline_;
    std::optional<RoundBoundary> local_round_boundary_;
    std::optional<RoundBoundary> remote_round_boundary_;
    std::deque<OnlineGameplayEvent> gameplay_messages_;
    OnlineState state_{OnlineState::Disabled};
    FailureCode failure_{FailureCode::None};
    OnlineFailureDisposition disposition_{OnlineFailureDisposition::None};
    bool peer_hello_received_{};
    bool peer_hello_ack_received_{};
    bool peer_baseline_ack_received_{};
    bool owns_simulation_{};
    std::uint64_t required_generation_{};
    std::optional<RoundBoundary> completed_round_boundary_;
};
}
