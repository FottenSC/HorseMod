#include "deterministic/OnlineCoordinator.hpp"
#include "deterministic/ProductionOnlineAllowlist.hpp"
#include "deterministic/OnlineLifecycle.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <deque>
#include <iostream>
#include <memory>
#include <utility>
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

class FakeAllowlist final : public IOnlineContentAllowlist
{
public:
    [[nodiscard]] bool IsQualified(
        const OnlineContentContract&) const noexcept override
    {
        return qualified;
    }

    bool qualified{true};
};

class FakeTransport final : public IRollbackTransport
{
public:
    Status Start(std::uint64_t validated_steam_peer) noexcept override
    {
        peer = validated_steam_peer;
        started = start_succeeds;
        return started ? Status::success()
                       : Status::failure(FailureCode::TransportFailed);
    }

    Status Send(const TransportMessage& message,
        TransportReliability reliability) noexcept override
    {
        if (!started || !send_succeeds)
            return Status::failure(FailureCode::TransportFailed);
        outbound.emplace_back(message, reliability);
        return Status::success();
    }

    std::optional<TransportMessage> Poll() noexcept override
    {
        if (inbound.empty()) return std::nullopt;
        TransportMessage message = inbound.front();
        inbound.pop_front();
        return message;
    }

    [[nodiscard]] FailureCode TerminalFailure() const noexcept override
    {
        return terminal;
    }

    [[nodiscard]] bool IsClearForStock() const noexcept override
    {
        return !started && terminal == FailureCode::None;
    }

    void Stop() noexcept override
    {
        started = false;
        ++stop_count;
    }

    std::uint64_t peer{};
    bool started{};
    bool start_succeeds{true};
    bool send_succeeds{true};
    int stop_count{};
    FailureCode terminal{FailureCode::None};
    std::deque<TransportMessage> inbound;
    std::vector<std::pair<TransportMessage, TransportReliability>> outbound;
};

struct FakeClock
{
    static std::uint64_t now(void* user) noexcept
    {
        return static_cast<FakeClock*>(user)->milliseconds;
    }

    [[nodiscard]] OnlineMonotonicClock interface() noexcept
    {
        return {this, &FakeClock::now};
    }

    std::uint64_t milliseconds{};
};

OnlinePeerContract contract(std::uint8_t slot)
{
    OnlinePeerContract value{};
    value.session_id = 0x12345678;
    value.lobby_id = 500;
    value.steam_ids = {1001, 1002};
    value.local_player_slot = slot;
    value.lobby_member_count = 2;
    value.casual_player_match = true;
    value.executable_id[0] = std::byte{0x11};
    value.build_id[0] = std::byte{0x22};
    std::memcpy(value.content.fighter_codes[0].data(), "ger", 4);
    std::memcpy(value.content.fighter_codes[1].data(), "tir", 4);
    std::memcpy(value.content.stage_code.data(), "017", 4);
    value.content.map_identity[0] = std::byte{0x17};
    constexpr char map_name[] = "Murakumo Shrine Grounds";
    std::memcpy(value.content.map_name.data(), map_name, sizeof(map_name));
    value.input_delay = 1;
    value.rollback_window = 12;
    return value;
}

CanonicalHash hash(std::uint8_t value)
{
    CanonicalHash result{};
    result.fill(static_cast<std::byte>(value));
    return result;
}

void transfer(FakeTransport& source, FakeTransport& destination)
{
    for (const auto& [message, reliability] : source.outbound)
    {
        (void)reliability;
        destination.inbound.push_back(message);
    }
    source.outbound.clear();
}

void exchange(FakeTransport& first, FakeTransport& second)
{
    transfer(first, second);
    transfer(second, first);
}

void complete_handshake(OnlineCoordinator& first, FakeTransport& first_transport,
    OnlineCoordinator& second, FakeTransport& second_transport)
{
    exchange(first_transport, second_transport);
    expect(first.Pump().ok(), "first peer processes hello");
    expect(second.Pump().ok(), "second peer processes hello");
    exchange(first_transport, second_transport);
    expect(first.Pump().ok(), "first peer processes hello acknowledgement");
    expect(second.Pump().ok(), "second peer processes hello acknowledgement");
    expect(first.state() == OnlineState::AwaitingBattle,
        "first peer awaits battle after bilateral handshake");
    expect(second.state() == OnlineState::AwaitingBattle,
        "second peer awaits battle after bilateral handshake");
}

void complete_baseline(OnlineCoordinator& first, FakeTransport& first_transport,
    OnlineCoordinator& second, FakeTransport& second_transport,
    std::uint64_t generation,
    std::vector<TransportMessage>* transcript = nullptr)
{
    const CanonicalHash baseline = hash(static_cast<std::uint8_t>(generation));
    expect(first.ReadyBaseline({generation, 10}).ok(),
        "first peer proposes earliest safe baseline coordinate");
    expect(second.ReadyBaseline({generation, 20}).ok(),
        "second peer proposes earliest safe baseline coordinate");
    if (transcript != nullptr)
    {
        for (const auto* transport : {&first_transport, &second_transport})
            for (const auto& [message, reliability] : transport->outbound)
            {
                (void)reliability;
                transcript->push_back(message);
            }
    }
    exchange(first_transport, second_transport);
    expect(first.Pump().ok(), "slot zero selects committed baseline");
    expect(second.Pump().ok(), "slot one receives peer readiness");
    if (transcript != nullptr)
    {
        for (const auto* transport : {&first_transport, &second_transport})
            for (const auto& [message, reliability] : transport->outbound)
            {
                (void)reliability;
                transcript->push_back(message);
            }
    }
    exchange(first_transport, second_transport);
    expect(first.Pump().ok(), "slot zero retains local baseline commitment");
    expect(second.Pump().ok(), "slot one validates baseline commitment");
    expect(first.baseline_target() == FrameCoordinate{generation, 140}
            && second.baseline_target() == FrameCoordinate{generation, 140},
        "slot zero commits max proposals plus 120 ticks");
    expect(first.FreezeBaseline({generation, 140}, baseline, hash(9)).ok(),
        "first peer freezes baseline");
    expect(second.FreezeBaseline({generation, 140}, baseline, hash(9)).ok(),
        "second peer freezes baseline");
    if (transcript != nullptr)
    {
        for (const auto& [message, reliability] : first_transport.outbound)
        {
            (void)reliability;
            transcript->push_back(message);
        }
    }
    exchange(first_transport, second_transport);
    expect(first.Pump().ok(), "first peer validates remote baseline");
    expect(second.Pump().ok(), "second peer validates remote baseline");
    if (transcript != nullptr)
    {
        for (const auto* transport : {&first_transport, &second_transport})
            for (const auto& [message, reliability] : transport->outbound)
            {
                (void)reliability;
                transcript->push_back(message);
            }
    }
    exchange(first_transport, second_transport);
    expect(first.Pump().ok(), "first peer validates baseline acknowledgement");
    expect(second.Pump().ok(), "second peer validates baseline acknowledgement");
    expect(first.state() == OnlineState::Active, "first peer becomes active");
    expect(second.state() == OnlineState::Active, "second peer becomes active");
}

void begin_pair(OnlineCoordinator& first, OnlineCoordinator& second)
{
    expect(first.Enable().ok(), "enable first online coordinator");
    expect(second.Enable().ok(), "enable second online coordinator");
    expect(first.ObserveLobby(contract(0)).ok(), "first observes qualified lobby");
    expect(second.ObserveLobby(contract(1)).ok(), "second observes qualified lobby");
}

bool claim_owned(OnlineCoordinator& coordinator, FrameCoordinate coordinate)
{
    return (coordinator.owns_simulation()
            || coordinator.BeginOwnedInputApplication().ok())
        && coordinator.NotifyOwnedTick(coordinate).ok();
}

void test_bilateral_activation_and_round_reentry()
{
    FakeAllowlist allowlist;
    FakeTransport first_transport;
    FakeTransport second_transport;
    OnlineCoordinator first{first_transport, allowlist};
    OnlineCoordinator second{second_transport, allowlist};
    begin_pair(first, second);
    const TransportMessage duplicate_hello = first_transport.outbound.front().first;
    complete_handshake(first, first_transport, second, second_transport);
    std::vector<TransportMessage> first_generation_baseline;
    complete_baseline(first, first_transport, second, second_transport, 1,
        &first_generation_baseline);

    second_transport.inbound.push_back(duplicate_hello);
    expect(second.Pump().ok(), "duplicate hello is idempotent after activation");
    second_transport.outbound.clear();
    expect(claim_owned(first, {1, 141}), "first peer owns native tick");
    expect(claim_owned(second, {1, 141}), "second peer owns native tick");

    PlayerInput input{};
    input.held = 7;
    input.rising = 2;
    expect(first.SendInput({1, 144}, input).ok(), "send active gameplay input");
    expect(first_transport.outbound.back().second
            == TransportReliability::Unreliable,
        "gameplay input uses unreliable transport");
    first_transport.outbound.push_back(first_transport.outbound.back());
    input.held = 8;
    expect(first.SendInput({1, 143}, input).ok(),
        "send reordered earlier gameplay input");
    expect(!first.SendConfirmedHash({1, 149}, hash(3)).ok(),
        "state hash outside confirmed cadence is rejected");
    expect(first.SendConfirmedHash({1, 150}, hash(3)).ok(),
        "send confirmed state hash");
    expect(first_transport.outbound.back().second
            == TransportReliability::Reliable,
        "confirmed state hash uses reliable transport");
    transfer(first_transport, second_transport);
    expect(second.Pump().ok(), "second peer accepts typed gameplay messages");
    const auto remote_input = second.PopGameplay();
    const auto reordered_input = second.PopGameplay();
    const auto remote_hash = second.PopGameplay();
    expect(remote_input && std::holds_alternative<OnlineInputPacket>(*remote_input),
        "received gameplay input is typed");
    expect(remote_input && std::holds_alternative<OnlineInputPacket>(*remote_input)
            && std::get<OnlineInputPacket>(*remote_input).input.rising == 2,
        "rising input word round-trips through canonical wire encoding");
    expect(reordered_input
            && std::holds_alternative<OnlineInputPacket>(*reordered_input)
            && std::get<OnlineInputPacket>(*reordered_input).coordinate.frame == 143,
        "reordered gameplay input retains its exact coordinate");
    expect(remote_hash
            && std::holds_alternative<OnlineStateHashPacket>(*remote_hash),
        "received state hash is typed");

    const std::array<std::byte, 4> gekko_payload{
        std::byte{0x10}, std::byte{0x20}, std::byte{0x30}, std::byte{0x40}};
    expect(first.SendGekkoPayload(gekko_payload).ok(),
        "send active Gekko payload");
    const auto prior_round_gekko = first_transport.outbound.back().first;
    expect(first_transport.outbound.back().second
            == TransportReliability::Unreliable,
        "Gekko gameplay payload uses unreliable transport");
    transfer(first_transport, second_transport);
    expect(second.Pump().ok(), "second peer accepts Gekko gameplay payload");
    const auto remote_gekko = second.PopGekkoPayload();
    expect(remote_gekko && remote_gekko->size == gekko_payload.size()
            && std::equal(gekko_payload.begin(), gekko_payload.end(),
                remote_gekko->payload.begin()),
        "Gekko gameplay payload round-trips through bounded queue");

    expect(first.BeginRoundBarrier(1, 2, hash(9)).ok(),
        "first peer enters round barrier");
    const TransportMessage old_barrier = first_transport.outbound.front().first;
    transfer(first_transport, second_transport);
    expect(second.Pump().ok(),
        "remote-first barrier is retained before the local fence arrives");
    expect(second.BeginRoundBarrier(1, 2, hash(9)).ok(),
        "second peer enters round barrier after the remote arrival");
    transfer(second_transport, first_transport);
    expect(first.Pump().ok(), "first peer completes round barrier");
    expect(second.Pump().ok(), "second peer completes round barrier");
    expect(first.state() == OnlineState::AwaitingBattle,
        "first peer awaits next round");
    for (const auto& delayed : first_generation_baseline)
        second_transport.inbound.push_back(delayed);
    expect(second.Pump().ok()
            && second.state() == OnlineState::AwaitingBattle,
        "delayed prior-generation baseline is ignored after the round fence");
    second_transport.inbound.push_back(prior_round_gekko);
    expect(second.Pump().ok() && !second.PopGekkoPayload().has_value(),
        "late prior-round opaque Gekko payload cannot enter the new session");
    expect(!first.FreezeBaseline({1, 140}, hash(1), hash(9)).ok(),
        "old native generation cannot be frozen");
    second_transport.inbound.push_back(old_barrier);
    expect(second.Pump().ok(), "duplicate completed round barrier is ignored");
    complete_baseline(first, first_transport, second, second_transport, 2);
    expect(claim_owned(first, {2, 141})
            && claim_owned(second, {2, 141}),
        "both peers re-enter ownership after the first barrier");
    expect(first.BeginRoundBarrier(2, 3, hash(10)).ok()
            && second.BeginRoundBarrier(2, 3, hash(10)).ok(),
        "a second owned round barrier starts without stale state");
    exchange(first_transport, second_transport);
    expect(first.Pump().ok() && second.Pump().ok()
            && first.state() == OnlineState::AwaitingBattle
            && second.state() == OnlineState::AwaitingBattle,
        "the second round barrier completes bilaterally");
}

void test_failure_disposition_and_reentry()
{
    FakeAllowlist allowlist;
    FakeTransport first_transport;
    FakeTransport second_transport;
    OnlineCoordinator first{first_transport, allowlist};
    OnlineCoordinator second{second_transport, allowlist};
    begin_pair(first, second);
    second_transport.inbound.push_back(first_transport.outbound.front().first);
    second_transport.inbound.back().session_id = 999;
    expect(second.Pump().code == FailureCode::StaleSession,
        "stale session packet fails before payload parsing");
    expect(second.failure_disposition()
            == OnlineFailureDisposition::LeaveStockUntouched,
        "pre-ownership failure leaves stock behavior untouched");
    expect(second.NotifyReturnedToLobby({}).ok(),
        "pre-ownership failure can resume lobby observation");
    expect(second.state() == OnlineState::ObservingLobby,
        "pre-ownership recovery returns to observing lobby");

    first.Disable();
    second.Disable();
    begin_pair(first, second);
    complete_handshake(first, first_transport, second, second_transport);
    complete_baseline(first, first_transport, second, second_transport, 1);
    expect(claim_owned(first, {1, 141}), "own tick before failure");
    first_transport.terminal = FailureCode::PeerDisconnected;
    expect(!first.Pump().ok(), "post-ownership transport failure is terminal");
    expect(first.failure_disposition()
            == OnlineFailureDisposition::TerminateMatchToLobby,
        "post-ownership failure requires lobby termination");
    expect(first.NotifyReturnedToLobby({}).code
            == FailureCode::IdentityMismatch,
        "owned failure rejects an unverified scene exit");
    expect(first.NotifyReturnedToLobby({999, true}).code
            == FailureCode::IdentityMismatch,
        "owned failure rejects scene evidence for another session");
    expect(first.NotifyReturnedToLobby({0x12345678, true}).ok(),
        "post-ownership failure acknowledges lobby return");
    expect(first.state() == OnlineState::ObservingLobby,
        "coordinator permits a clean later match re-entry");
}

void test_fail_closed_lobby_contract()
{
    FakeAllowlist allowlist;
    FakeTransport transport;
    OnlineCoordinator online{transport, allowlist};
    expect(online.Enable().ok(), "enable lobby validation fixture");
    auto invalid = contract(0);
    invalid.lobby_member_count = 3;
    expect(online.ObserveLobby(invalid).code
            == FailureCode::InvalidConfiguration,
        "more-than-two-member lobby is rejected");
    invalid = contract(0);
    invalid.casual_player_match = false;
    expect(online.ObserveLobby(invalid).code
            == FailureCode::InvalidConfiguration,
        "non-casual Player Match is rejected");
    allowlist.qualified = false;
    expect(online.ObserveLobby(contract(0)).code
            == FailureCode::UnsupportedContent,
        "unqualified content is rejected before transport start");
    expect(!transport.started, "rejected lobby never starts transport");
}

void test_preownership_exit()
{
    FakeAllowlist allowlist;
    FakeTransport transport;
    OnlineCoordinator online{transport, allowlist};
    expect(online.Enable().ok(), "enable pre-ownership exit fixture");
    expect(online.ObserveLobby(contract(0)).ok(),
        "start handshake before pre-ownership exit");
    expect(online.ReturnToLobby().ok(),
        "pre-ownership exit stops only Horse transport");
    expect(online.state() == OnlineState::ObservingLobby,
        "pre-ownership exit resumes stock lobby observation immediately");
    expect(!online.owns_simulation(),
        "pre-ownership exit never claims simulation ownership");
}

void test_bilateral_agreement_mismatch()
{
    FakeAllowlist allowlist;
    FakeTransport first_transport;
    FakeTransport second_transport;
    OnlineCoordinator first{first_transport, allowlist};
    OnlineCoordinator second{second_transport, allowlist};
    expect(first.Enable().ok(), "enable first mismatch peer");
    expect(second.Enable().ok(), "enable second mismatch peer");
    auto second_contract = contract(1);
    second_contract.build_id[0] = std::byte{0x44};
    expect(first.ObserveLobby(contract(0)).ok(), "start first mismatch peer");
    expect(second.ObserveLobby(second_contract).ok(),
        "start second mismatch peer");
    exchange(first_transport, second_transport);
    expect(first.Pump().code == FailureCode::ProtocolMismatch,
        "build disagreement fails before ownership");
    expect(second.Pump().code == FailureCode::ProtocolMismatch,
        "build disagreement is bilateral");
    expect(first.failure_disposition()
            == OnlineFailureDisposition::LeaveStockUntouched,
        "agreement mismatch leaves stock simulation untouched");
}

void test_reordered_handshake_control()
{
    FakeAllowlist allowlist;
    FakeTransport first_transport;
    FakeTransport second_transport;
    OnlineCoordinator first{first_transport, allowlist};
    OnlineCoordinator second{second_transport, allowlist};
    begin_pair(first, second);
    TransportMessage remote_hello = second_transport.outbound.front().first;
    TransportMessage early_ack = remote_hello;
    early_ack.kind = TransportMessageKind::HelloAck;
    first_transport.inbound.push_back(early_ack);
    expect(first.Pump().ok(), "early hello acknowledgement is buffered");
    expect(first.state() == OnlineState::Handshaking,
        "early acknowledgement cannot complete a unilateral handshake");
    first_transport.inbound.push_back(remote_hello);
    expect(first.Pump().ok(), "later hello completes reordered handshake");
    expect(first.state() == OnlineState::AwaitingBattle,
        "reordered reliable control reaches the same lifecycle state");
}

void test_monotonic_phase_deadlines()
{
    FakeAllowlist allowlist;

    {
        FakeClock clock;
        FakeTransport transport;
        auto online = std::make_unique<OnlineCoordinator>(
            transport, allowlist, clock.interface());
        expect(online->Enable().ok() && online->ObserveLobby(contract(0)).ok(),
            "handshake timeout fixture starts");
        clock.milliseconds = 10'000;
        expect(online->Pump().code == FailureCode::Timeout,
            "authenticated hello phase has a ten-second monotonic deadline");
        expect(online->failure_disposition()
                == OnlineFailureDisposition::LeaveStockUntouched,
            "pre-ownership handshake timeout leaves stock untouched");
    }

    {
        FakeClock clock;
        FakeTransport first_transport;
        FakeTransport second_transport;
        auto first_owner = std::make_unique<OnlineCoordinator>(
            first_transport, allowlist, clock.interface());
        auto second_owner = std::make_unique<OnlineCoordinator>(
            second_transport, allowlist, clock.interface());
        auto& first = *first_owner;
        auto& second = *second_owner;
        begin_pair(first, second);
        complete_handshake(first, first_transport, second, second_transport);
        clock.milliseconds = 10'000;
        expect(first.Pump().code == FailureCode::Timeout,
            "baseline ready/commit phase has a ten-second deadline");
    }

    {
        FakeClock clock;
        FakeTransport first_transport;
        FakeTransport second_transport;
        auto first_owner = std::make_unique<OnlineCoordinator>(
            first_transport, allowlist, clock.interface());
        auto second_owner = std::make_unique<OnlineCoordinator>(
            second_transport, allowlist, clock.interface());
        auto& first = *first_owner;
        auto& second = *second_owner;
        begin_pair(first, second);
        complete_handshake(first, first_transport, second, second_transport);
        expect(first.ReadyBaseline({1, 10}).ok()
                && second.ReadyBaseline({1, 20}).ok(),
            "baseline target timeout peers publish readiness");
        exchange(first_transport, second_transport);
        expect(first.Pump().ok() && second.Pump().ok(),
            "slot zero computes target for timeout fixture");
        exchange(first_transport, second_transport);
        expect(first.Pump().ok() && second.Pump().ok(),
            "slot one receives target for timeout fixture");
        expect(first.ObserveBaselineProgress({1, 141}).code
                == FailureCode::GenerationMismatch,
            "missing the exact committed target is terminal");
    }

    {
        FakeClock clock;
        FakeTransport first_transport;
        FakeTransport second_transport;
        auto first_owner = std::make_unique<OnlineCoordinator>(
            first_transport, allowlist, clock.interface());
        auto second_owner = std::make_unique<OnlineCoordinator>(
            second_transport, allowlist, clock.interface());
        auto& first = *first_owner;
        auto& second = *second_owner;
        begin_pair(first, second);
        complete_handshake(first, first_transport, second, second_transport);
        expect(first.ReadyBaseline({1, 10}).ok()
                && second.ReadyBaseline({1, 20}).ok(),
            "target deadline fixture publishes readiness");
        exchange(first_transport, second_transport);
        expect(first.Pump().ok() && second.Pump().ok(),
            "target deadline fixture computes commitment");
        exchange(first_transport, second_transport);
        expect(first.Pump().ok() && second.Pump().ok(),
            "target deadline fixture receives commitment");
        clock.milliseconds = 10'000;
        expect(first.Pump().code == FailureCode::Timeout,
            "committed baseline target has a ten-second deadline");
    }

    {
        FakeClock clock;
        FakeTransport first_transport;
        FakeTransport second_transport;
        auto first_owner = std::make_unique<OnlineCoordinator>(
            first_transport, allowlist, clock.interface());
        auto second_owner = std::make_unique<OnlineCoordinator>(
            second_transport, allowlist, clock.interface());
        auto& first = *first_owner;
        auto& second = *second_owner;
        begin_pair(first, second);
        complete_handshake(first, first_transport, second, second_transport);
        expect(first.ReadyBaseline({1, 10}).ok()
                && second.ReadyBaseline({1, 20}).ok(),
            "baseline acknowledgement fixture publishes readiness");
        exchange(first_transport, second_transport);
        expect(first.Pump().ok() && second.Pump().ok(),
            "baseline acknowledgement fixture computes commitment");
        exchange(first_transport, second_transport);
        expect(first.Pump().ok() && second.Pump().ok(),
            "baseline acknowledgement fixture receives commitment");
        expect(first.FreezeBaseline({1, 140}, hash(1), hash(9)).ok(),
            "one peer captures exact committed baseline");
        clock.milliseconds = 10'000;
        expect(first.Pump().code == FailureCode::Timeout,
            "baseline hash acknowledgement has a ten-second deadline");
    }

    {
        FakeClock clock;
        FakeTransport first_transport;
        FakeTransport second_transport;
        auto first_owner = std::make_unique<OnlineCoordinator>(
            first_transport, allowlist, clock.interface());
        auto second_owner = std::make_unique<OnlineCoordinator>(
            second_transport, allowlist, clock.interface());
        auto& first = *first_owner;
        auto& second = *second_owner;
        begin_pair(first, second);
        complete_handshake(first, first_transport, second, second_transport);
        complete_baseline(first, first_transport, second, second_transport, 1);
        clock.milliseconds = 5'000;
        expect(first.Pump().code == FailureCode::Timeout,
            "prefix catch-up has a five-second monotonic deadline");
    }

    {
        FakeClock clock;
        FakeTransport first_transport;
        FakeTransport second_transport;
        auto first_owner = std::make_unique<OnlineCoordinator>(
            first_transport, allowlist, clock.interface());
        auto second_owner = std::make_unique<OnlineCoordinator>(
            second_transport, allowlist, clock.interface());
        auto& first = *first_owner;
        auto& second = *second_owner;
        begin_pair(first, second);
        complete_handshake(first, first_transport, second, second_transport);
        complete_baseline(first, first_transport, second, second_transport, 1);
        expect(claim_owned(first, {1, 141}),
            "round timeout fixture takes ownership");
        expect(first.BeginRoundBarrier(1, 2, hash(4)).ok(),
            "round timeout fixture enters owned barrier");
        clock.milliseconds = 10'000;
        expect(first.Pump().code == FailureCode::Timeout,
            "owned round barrier has a ten-second monotonic deadline");
        expect(first.failure_disposition()
                == OnlineFailureDisposition::TerminateMatchToLobby,
            "owned round timeout remains fail-closed");
    }
}

void test_terminal_cleanup_lifecycle()
{
    OnlineLifecycle lifecycle;
    expect(lifecycle.ArmPreOwnership().ok(),
        "cleanup lifecycle arms before ownership");
    lifecycle.BeginFailure(false);
    expect(!lifecycle.RequiresOwnedInput(),
        "pre-ownership failure never claims input");
    OnlineStockClearance clear{};
    clear.resources.fill(true);
    for (std::size_t fault = 0; fault < clear.resources.size(); ++fault)
    {
        auto faulted = clear;
        faulted.resources[fault] = false;
        expect(lifecycle.CompleteSceneExitCleanup(faulted).code
                == FailureCode::RestoreVerificationFailed,
            "each uncleared cleanup resource withholds clear-for-stock");
        expect(!lifecycle.IsClearForStock(),
            "status seven cannot precede complete cleanup conjunction");
    }
    expect(lifecycle.CompleteSceneExitCleanup(clear).ok()
            && lifecycle.IsClearForStock(),
        "all cleanup probes jointly restore stock eligibility");

    expect(lifecycle.ArmPreOwnership().ok() && lifecycle.MarkOwned().ok(),
        "cleanup lifecycle supports a later owned re-entry");
    expect(!lifecycle.CanUnloadModule(),
        "module reload is deferred while Horse owns native input");
    lifecycle.BeginFailure(true);
    expect(!lifecycle.CanUnloadModule(),
        "fail-closed ownership remains pinned until verified scene cleanup");
    expect(lifecycle.RequiresOwnedInput(),
        "post-ownership failure remains fail-closed");
    expect(lifecycle.TakeLobbyRequest() && !lifecycle.TakeLobbyRequest(),
        "post-ownership lobby request is issued exactly once");
    expect(lifecycle.BeginSceneExitCleanup().ok(),
        "verified scene-exit evidence enters cleanup without releasing the module pin");
    expect(!lifecycle.CanUnloadModule(),
        "incomplete cleanup remains non-unloadable even after ownership ends");
    expect(lifecycle.CompleteSceneExitCleanup(clear).ok(),
        "verified scene exit completes post-ownership teardown");
}
}

int main()
{
    {
        ProductionOnlineAllowlist production{};
        OnlineContentContract content{};
        std::memcpy(content.fighter_codes[0].data(), "012", 4);
        std::memcpy(content.fighter_codes[1].data(), "015", 4);
        std::memcpy(content.stage_code.data(), "273", 4);
        constexpr char package[] = "/Game/DLC/07/Stage/STG011_R";
        std::memcpy(content.map_name.data(), package, sizeof(package));
        assert(HashOnlineSelectionIdentity(content, content.map_identity).ok());
        assert(!production.IsQualified(content));
        assert(!production.LoadAndPublish("missing-production-allowlist.ini",
            hash(1), hash(2), "0000000000000000000000000000000000000000").ok());
        assert(!production.IsQualified(content));
    }
    test_bilateral_activation_and_round_reentry();
    test_failure_disposition_and_reentry();
    test_fail_closed_lobby_contract();
    test_preownership_exit();
    test_bilateral_agreement_mismatch();
    test_reordered_handshake_control();
    test_monotonic_phase_deadlines();
    test_terminal_cleanup_lifecycle();
    if (failures == 0)
        std::cout << "OnlineCoordinatorSelfTest passed\n";
    return failures == 0 ? 0 : 1;
}
