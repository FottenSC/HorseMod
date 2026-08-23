#include "deterministic/OnlineCoordinator.hpp"

#include <deque>
#include <iostream>
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
    value.content.fighter_ids = {10, 20};
    value.content.stage_id = 273;
    value.content.map_id = 17;
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
    std::uint64_t generation)
{
    const CanonicalHash baseline = hash(static_cast<std::uint8_t>(generation));
    expect(first.FreezeBaseline(generation, baseline).ok(),
        "first peer freezes baseline");
    expect(second.FreezeBaseline(generation, baseline).ok(),
        "second peer freezes baseline");
    exchange(first_transport, second_transport);
    expect(first.Pump().ok(), "first peer validates remote baseline");
    expect(second.Pump().ok(), "second peer validates remote baseline");
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
    complete_baseline(first, first_transport, second, second_transport, 1);

    second_transport.inbound.push_back(duplicate_hello);
    expect(second.Pump().ok(), "duplicate hello is idempotent after activation");
    second_transport.outbound.clear();
    expect(first.NotifyOwnedTick({1, 0}).ok(), "first peer owns native tick");
    expect(second.NotifyOwnedTick({1, 0}).ok(), "second peer owns native tick");

    PlayerInput input{};
    input.buttons = 7;
    input.axis_x = -2;
    expect(first.SendInput({1, 4}, input).ok(), "send active gameplay input");
    expect(first_transport.outbound.back().second
            == TransportReliability::Unreliable,
        "gameplay input uses unreliable transport");
    first_transport.outbound.push_back(first_transport.outbound.back());
    input.buttons = 8;
    expect(first.SendInput({1, 3}, input).ok(),
        "send reordered earlier gameplay input");
    expect(!first.SendConfirmedHash({1, 29}, hash(3)).ok(),
        "state hash outside confirmed cadence is rejected");
    expect(first.SendConfirmedHash({1, 30}, hash(3)).ok(),
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
            && std::get<OnlineInputPacket>(*remote_input).input.axis_x == -2,
        "signed input axes round-trip through canonical wire encoding");
    expect(reordered_input
            && std::holds_alternative<OnlineInputPacket>(*reordered_input)
            && std::get<OnlineInputPacket>(*reordered_input).coordinate.frame == 3,
        "reordered gameplay input retains its exact coordinate");
    expect(remote_hash
            && std::holds_alternative<OnlineStateHashPacket>(*remote_hash),
        "received state hash is typed");

    expect(first.BeginRoundBarrier(1, 2, hash(9)).ok(),
        "first peer enters round barrier");
    expect(second.BeginRoundBarrier(1, 2, hash(9)).ok(),
        "second peer enters round barrier");
    const TransportMessage old_barrier = first_transport.outbound.front().first;
    exchange(first_transport, second_transport);
    expect(first.Pump().ok(), "first peer completes round barrier");
    expect(second.Pump().ok(), "second peer completes round barrier");
    expect(first.state() == OnlineState::AwaitingBattle,
        "first peer awaits next round");
    expect(!first.FreezeBaseline(1, hash(1)).ok(),
        "old native generation cannot be frozen");
    second_transport.inbound.push_back(old_barrier);
    expect(second.Pump().ok(), "duplicate completed round barrier is ignored");
    complete_baseline(first, first_transport, second, second_transport, 2);
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
    expect(second.NotifyReturnedToLobby().ok(),
        "pre-ownership failure can resume lobby observation");
    expect(second.state() == OnlineState::ObservingLobby,
        "pre-ownership recovery returns to observing lobby");

    first.Disable();
    second.Disable();
    begin_pair(first, second);
    complete_handshake(first, first_transport, second, second_transport);
    complete_baseline(first, first_transport, second, second_transport, 1);
    expect(first.NotifyOwnedTick({1, 0}).ok(), "own tick before failure");
    first_transport.terminal = FailureCode::PeerDisconnected;
    expect(!first.Pump().ok(), "post-ownership transport failure is terminal");
    expect(first.failure_disposition()
            == OnlineFailureDisposition::TerminateMatchToLobby,
        "post-ownership failure requires lobby termination");
    expect(first.NotifyReturnedToLobby().ok(),
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
}

int main()
{
    test_bilateral_activation_and_round_reentry();
    test_failure_disposition_and_reentry();
    test_fail_closed_lobby_contract();
    test_preownership_exit();
    test_bilateral_agreement_mismatch();
    test_reordered_handshake_control();
    if (failures == 0)
        std::cout << "OnlineCoordinatorSelfTest passed\n";
    return failures == 0 ? 0 : 1;
}
