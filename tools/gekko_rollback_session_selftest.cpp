#include "deterministic/GekkoRollbackSession.hpp"

#include <chrono>
#include <cstring>
#include <deque>
#include <iostream>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

using namespace Horse::Deterministic;

namespace
{
int failures{};

void expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

class Allowlist final : public IOnlineContentAllowlist
{
    bool IsQualified(const OnlineContentContract&) const noexcept override
    {
        return true;
    }
};

class Transport final : public IRollbackTransport
{
public:
    Status Start(std::uint64_t value) noexcept override
    {
        peer = value;
        started = true;
        return Status::success();
    }
    Status Send(const TransportMessage& message,
        TransportReliability reliability) noexcept override
    {
        if (!started) return Status::failure(FailureCode::TransportFailed);
        outbound.emplace_back(message, reliability);
        return Status::success();
    }
    std::optional<TransportMessage> Poll() noexcept override
    {
        if (inbound.empty()) return std::nullopt;
        auto result = inbound.front();
        inbound.pop_front();
        return result;
    }
    FailureCode TerminalFailure() const noexcept override
    {
        return FailureCode::None;
    }

    bool IsClearForStock() const noexcept override
    {
        return !started;
    }
    void Stop() noexcept override { started = false; }

    std::uint64_t peer{};
    bool started{};
    std::deque<TransportMessage> inbound;
    std::vector<std::pair<TransportMessage, TransportReliability>> outbound;
};

class Simulation final : public IGekkoSimulationSink
{
public:
    Status Save(std::int32_t frame, GekkoSaveKind,
        std::span<std::byte> destination, std::uint32_t& written,
        std::uint32_t& checksum) noexcept override
    {
        if (destination.size() < sizeof(state))
            return Status::failure(FailureCode::CapacityExceeded);
        std::memcpy(destination.data(), &state, sizeof(state));
        written = sizeof(state);
        checksum = static_cast<std::uint32_t>(state);
        last_saved_frame = frame;
        return Status::success();
    }
    Status Load(std::int32_t frame,
        std::span<const std::byte> source) noexcept override
    {
        if (source.size() != sizeof(state))
            return Status::failure(FailureCode::ProtocolMismatch);
        std::memcpy(&state, source.data(), sizeof(state));
        last_loaded_frame = frame;
        return Status::success();
    }
    Status Advance(const GekkoAdvanceValue& value) noexcept override
    {
        state += value.inputs[0].held + value.inputs[1].held;
        last_advanced_frame = value.frame;
        if (value.rolling_back) ++rollback_advances;
        return Status::success();
    }

    std::uint64_t state{};
    std::int32_t last_saved_frame{-1};
    std::int32_t last_loaded_frame{-1};
    std::int32_t last_advanced_frame{-1};
    std::uint32_t rollback_advances{};
};

bool claim_owned(OnlineCoordinator& coordinator, FrameCoordinate coordinate)
{
    return (coordinator.owns_simulation()
            || coordinator.BeginOwnedInputApplication().ok())
        && coordinator.NotifyOwnedTick(coordinate).ok();
}

OnlinePeerContract contract(std::uint8_t slot)
{
    OnlinePeerContract value{};
    value.session_id = 0xabcdef;
    value.lobby_id = 77;
    value.steam_ids = {1001, 1002};
    value.local_player_slot = slot;
    value.lobby_member_count = 2;
    value.casual_player_match = true;
    value.executable_id[0] = std::byte{1};
    value.build_id[0] = std::byte{2};
    std::memcpy(value.content.fighter_codes[0].data(), "ger", 4);
    std::memcpy(value.content.fighter_codes[1].data(), "tir", 4);
    std::memcpy(value.content.stage_code.data(), "017", 4);
    value.content.map_identity[0] = std::byte{0x17};
    constexpr char map_name[] = "Snow-Capped Showdown";
    std::memcpy(value.content.map_name.data(), map_name, sizeof(map_name));
    value.input_delay = 1;
    value.rollback_window = 12;
    return value;
}

void transfer(Transport& source, Transport& destination)
{
    for (auto& [message, reliability] : source.outbound)
    {
        (void)reliability;
        destination.inbound.push_back(message);
    }
    source.outbound.clear();
}

void exchange(Transport& first, Transport& second)
{
    transfer(first, second);
    transfer(second, first);
}

void pump_coordinators(OnlineCoordinator& first, Transport& first_transport,
    OnlineCoordinator& second, Transport& second_transport)
{
    for (int count = 0; count < 8; ++count)
    {
        exchange(first_transport, second_transport);
        expect(first.Pump().ok(), "first coordinator pump succeeds");
        expect(second.Pump().ok(), "second coordinator pump succeeds");
    }
}

void expect_hash_event(OnlineCoordinator& coordinator,
    FrameCoordinate coordinate, const CanonicalHash& hash,
    const char* message)
{
    const auto event = coordinator.PopGameplay();
    expect(event.has_value()
            && std::holds_alternative<OnlineStateHashPacket>(*event)
            && std::get<OnlineStateHashPacket>(*event).coordinate == coordinate
            && std::get<OnlineStateHashPacket>(*event).hash == hash,
        message);
}
}

int main()
{
    Allowlist allowlist;
    Transport first_transport;
    Transport second_transport;
    OnlineCoordinator first_online{first_transport, allowlist};
    OnlineCoordinator second_online{second_transport, allowlist};
    expect(first_online.Enable().ok() && second_online.Enable().ok(),
        "coordinators enable");
    expect(first_online.ObserveLobby(contract(0)).ok()
            && second_online.ObserveLobby(contract(1)).ok(),
        "coordinators observe qualified two-peer lobby");
    pump_coordinators(first_online, first_transport,
        second_online, second_transport);
    expect(first_online.state() == OnlineState::AwaitingBattle
            && second_online.state() == OnlineState::AwaitingBattle,
        "bilateral contract reaches battle wait");

    Simulation first_simulation;
    Simulation second_simulation;
    GekkoRollbackSession first;
    GekkoRollbackSession second;
    expect(first.Start(first_online, first_simulation, 0, 1, 12,
            sizeof(std::uint64_t)).ok(), "first Gekko session starts");
    expect(second.Start(second_online, second_simulation, 1, 1, 12,
            sizeof(std::uint64_t)).ok(), "second Gekko session starts");
    for (int count = 0; count < 128
        && (!first.ReadyForBaseline() || !second.ReadyForBaseline()); ++count)
    {
        expect(first.PollNetwork().ok(), "first Gekko prebaseline poll");
        transfer(first_transport, second_transport);
        expect(second.PollNetwork().ok(), "second Gekko prebaseline poll");
        transfer(second_transport, first_transport);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    expect(first.ReadyForBaseline() && second.ReadyForBaseline(),
        "Gekko peers synchronize neutral input-delay prefix before baseline");
    if (!first.ReadyForBaseline() || !second.ReadyForBaseline())
        return 1;

    CanonicalHash baseline{};
    baseline[0] = std::byte{9};
    expect(first_online.ReadyBaseline({1, 0}).ok()
            && second_online.ReadyBaseline({1, 0}).ok(),
        "both peers publish first-round baseline readiness");
    pump_coordinators(first_online, first_transport,
        second_online, second_transport);
    expect(first_online.FreezeBaseline({1, 120}, baseline, baseline).ok()
            && second_online.FreezeBaseline({1, 120}, baseline, baseline).ok(),
        "both peers freeze identical baseline");
    pump_coordinators(first_online, first_transport,
        second_online, second_transport);
    expect(first_online.state() == OnlineState::Active
            && second_online.state() == OnlineState::Active,
        "baseline acknowledgement activates both coordinators");
    expect(claim_owned(first_online, {1, 121})
            && claim_owned(second_online, {1, 121}),
        "both peers claim simulation ownership");

    for (std::uint32_t frame = 0; frame < 90; ++frame)
    {
        PlayerInput first_input{};
        first_input.held = (frame % 7 == 0) ? 1u : 0u;
        PlayerInput second_input{};
        second_input.held = (frame % 11 == 0) ? 2u : 0u;
        std::array<PlayerInput, 2> first_authoritative{};
        std::array<PlayerInput, 2> second_authoritative{};
        expect(first.FlushCorrections().ok(),
            "first Gekko session flushes corrections before advance");
        expect(first.Advance(first_input, first_authoritative).ok(),
            "first Gekko session advances");
        expect(first.CompleteDeferredSaves().ok(),
            "first Gekko session completes post-advance saves");
        transfer(first_transport, second_transport);
        expect(second.FlushCorrections().ok(),
            "second Gekko session flushes corrections before advance");
        expect(second.Advance(second_input, second_authoritative).ok(),
            "second Gekko session advances");
        expect(second.CompleteDeferredSaves().ok(),
            "second Gekko session completes post-advance saves");
        transfer(second_transport, first_transport);
    }
    for (int count = 0; count < 16; ++count)
    {
        expect(first.PollNetwork().ok(), "first final network poll");
        transfer(first_transport, second_transport);
        expect(second.PollNetwork().ok(), "second final network poll");
        transfer(second_transport, first_transport);
    }
    expect(first.confirmed_frame() >= 80 && second.confirmed_frame() >= 80,
        "both Gekko peers confirm the sustained in-memory session");
    expect(first_simulation.rollback_advances != 0,
        "predicted peer performs real rollback advances");
    expect(first.terminal_failure() == FailureCode::None
            && second.terminal_failure() == FailureCode::None,
        "both Gekko sessions remain healthy");

    for (std::uint64_t frame : {30u, 60u})
    {
        CanonicalHash confirmed_hash{};
        confirmed_hash[0] = static_cast<std::byte>(frame);
        const FrameCoordinate coordinate{1, frame + 120};
        expect(first_online.SendConfirmedHash(
                   coordinate, confirmed_hash).ok()
                && second_online.SendConfirmedHash(
                    coordinate, confirmed_hash).ok(),
            "both peers publish a confirmed 30-frame canonical hash");
        pump_coordinators(first_online, first_transport,
            second_online, second_transport);
        expect_hash_event(first_online, coordinate, confirmed_hash,
            "first peer receives exact confirmed hash");
        expect_hash_event(second_online, coordinate, confirmed_hash,
            "second peer receives exact confirmed hash");
    }

    CanonicalHash round_hash{};
    round_hash[0] = std::byte{0x5a};
    expect(first_online.BeginRoundBarrier(1, 2, round_hash).ok()
            && second_online.BeginRoundBarrier(1, 2, round_hash).ok(),
        "both peers enter the same round barrier");
    pump_coordinators(first_online, first_transport,
        second_online, second_transport);
    expect(first_online.state() == OnlineState::AwaitingBattle
            && second_online.state() == OnlineState::AwaitingBattle,
        "round barrier returns both peers to the prebaseline state");
    first.Stop();
    second.Stop();
    expect(first.Start(first_online, first_simulation, 0, 1, 12,
            sizeof(std::uint64_t)).ok()
            && second.Start(second_online, second_simulation, 1, 1, 12,
                sizeof(std::uint64_t)).ok(),
        "new per-round Gekko sessions start without stale state");
    for (int count = 0; count < 128
        && (!first.ReadyForBaseline() || !second.ReadyForBaseline()); ++count)
    {
        expect(first.PollNetwork().ok(), "first re-entry Gekko poll");
        transfer(first_transport, second_transport);
        expect(second.PollNetwork().ok(), "second re-entry Gekko poll");
        transfer(second_transport, first_transport);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    expect(first.ReadyForBaseline() && second.ReadyForBaseline(),
        "second-round Gekko delay prefixes synchronize");
    CanonicalHash second_baseline{};
    second_baseline[0] = std::byte{0x22};
    expect(first_online.ReadyBaseline({2, 0}).ok()
            && second_online.ReadyBaseline({2, 0}).ok(),
        "both peers publish second-round baseline readiness");
    pump_coordinators(first_online, first_transport,
        second_online, second_transport);
    expect(first_online.FreezeBaseline(
                {2, 120}, second_baseline, second_baseline).ok()
            && second_online.FreezeBaseline(
                {2, 120}, second_baseline, second_baseline).ok(),
        "both peers freeze the required next-generation baseline");
    pump_coordinators(first_online, first_transport,
        second_online, second_transport);
    expect(claim_owned(first_online, {2, 121})
            && claim_owned(second_online, {2, 121}),
        "both peers re-enter owned simulation in generation two");
    expect(first_online.ReturnToLobby().ok()
            && second_online.ReturnToLobby().ok(),
        "owned sessions begin terminal lobby return");
    first.Stop();
    second.Stop();
    expect(first_online.NotifyReturnedToLobby({0xabcdef, true}).ok()
            && second_online.NotifyReturnedToLobby({0xabcdef, true}).ok(),
        "coordinators clear all per-match state for lobby re-entry");
    expect(first_online.state() == OnlineState::ObservingLobby
            && second_online.state() == OnlineState::ObservingLobby,
        "clean teardown ends in stock lobby observation");

    if (failures == 0)
        std::cout << "GekkoRollbackSessionSelfTest passed\n";
    return failures == 0 ? 0 : 1;
}
