#include "deterministic/GekkoRollbackSession.hpp"

#include <algorithm>
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
        if (frame > available_through)
        {
            unavailable_operation = GekkoSimulationOperation::Save;
            unavailable_frame = frame;
            return Status::failure(FailureCode::MissingSnapshot);
        }
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
        if (frame > available_through)
        {
            unavailable_operation = GekkoSimulationOperation::Load;
            unavailable_frame = frame;
            return Status::failure(FailureCode::MissingSnapshot);
        }
        if (source.size() != sizeof(state))
            return Status::failure(FailureCode::ProtocolMismatch);
        std::memcpy(&state, source.data(), sizeof(state));
        last_loaded_frame = frame;
        loaded_frames.push_back(frame);
        return Status::success();
    }
    Status Advance(const GekkoAdvanceValue& value) noexcept override
    {
        if (value.frame > available_through && value.rolling_back)
        {
            unavailable_operation = GekkoSimulationOperation::Advance;
            unavailable_frame = value.frame;
            return Status::failure(FailureCode::MissingSnapshot);
        }
        state += value.inputs[0].held + value.inputs[1].held;
        last_advanced_frame = value.frame;
        advance_history.push_back(value);
        if (value.rolling_back)
        {
            ++rollback_advances;
            rollback_history.push_back(value);
        }
        else pending_native_frame = value.frame;
        return Status::success();
    }

    void CompleteNativeFrame() noexcept
    {
        if (pending_native_frame >= 0)
        {
            available_through = (std::max)(
                available_through, pending_native_frame);
            pending_native_frame = -1;
        }
    }

    std::uint64_t state{};
    std::int32_t last_saved_frame{-1};
    std::int32_t last_loaded_frame{-1};
    std::vector<std::int32_t> loaded_frames{};
    std::int32_t last_advanced_frame{-1};
    std::uint32_t rollback_advances{};
    std::vector<GekkoAdvanceValue> advance_history{};
    std::vector<GekkoAdvanceValue> rollback_history{};
    std::int32_t available_through{-1};
    std::int32_t pending_native_frame{-1};
    std::int32_t unavailable_frame{-1};
    GekkoSimulationOperation unavailable_operation{
        GekkoSimulationOperation::None};
};

bool claim_owned(OnlineCoordinator& coordinator, FrameCoordinate coordinate)
{
    return coordinator.NotifyOwnedTick(coordinate).ok();
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
    constexpr auto zero_stimulus =
        BuildQualificationCorrectionStimulusInputs({});
    constexpr auto held_stimulus =
        BuildQualificationCorrectionStimulusInputs(
            {.held = 1, .rising = 1});
    expect(zero_stimulus[0] == PlayerInput{.held = 1, .rising = 1}
            && zero_stimulus[1] == PlayerInput{}
            && held_stimulus[0] == PlayerInput{}
            && held_stimulus[1] == PlayerInput{.held = 1, .rising = 1},
        "qualification pair contains both bit-0 predictions in release order");
    expect(QualificationCorrectionTransportDelay(11, 12, 0) == 12
            && QualificationCorrectionTransportDelay(1, 12, 0) == 2
            && QualificationCorrectionTransportDelay(6, 12, 0) == 7
            && QualificationCorrectionTransportDelay(11, 12, 1) == 13
            && QualificationCorrectionTransportDelay(1, 12, 1) == 3
            && QualificationCorrectionTransportDelay(6, 12, 1) == 8,
        "SC6 slot-aware release updates preserve requested 11-1-6 depths");
    expect(QualificationCorrectionTransportDelay(12, 12, 0) == 0
            && QualificationCorrectionTransportDelay(0, 12, 0) == 0
            && QualificationCorrectionTransportDelay(1, 12, 2) == 0,
        "transport guard fails closed at invalid or overflowing depths");
    expect(!PlanQualificationCorrectionTrigger(29, 14, 92).has_value()
            && !PlanQualificationCorrectionTrigger(59, 14, 92).has_value()
            && PlanQualificationCorrectionTrigger(89, 14, 92)
                == std::optional<std::int32_t>{103},
        "91-frame prefix catch-up skips stale hashes and selects frame 103");
    expect(!PlanQualificationCorrectionTrigger(-1, 14, 0).has_value()
            && !PlanQualificationCorrectionTrigger(INT32_MAX, 14, 0)
                .has_value(),
        "correction trigger planning fails closed on invalid coordinates");

    expect(CanSealGekkoRound(89, 90, false),
        "fully confirmed idle generation can seal before history retirement");
    expect(!CanSealGekkoRound(88, 90, false)
            && !CanSealGekkoRound(89, 90, true),
        "round sealing rejects lagging confirmation and pending native advance");
    const auto crossed = PlanGekkoRoundSeal(
        237, 238, true, {2, 361}, {2, 360}, {2, 359}, {2, 361},
        {3, 361}, true);
    expect(crossed.sealed && crossed.discard_cross_generation_advance,
        "confirmed advance crossing the native generation fence is discarded");
    expect(!PlanGekkoRoundSeal(
                235, 238, true, {2, 361}, {2, 360}, {2, 359},
                {2, 359}, {3, 361}, true).sealed
            && !PlanGekkoRoundSeal(
                237, 238, true, {2, 362}, {2, 360}, {2, 359},
                {2, 361}, {3, 361}, true).sealed
            && !PlanGekkoRoundSeal(
                237, 238, true, {2, 361}, {2, 360}, {2, 359},
                {2, 361}, {3, 361}, false).sealed,
        "cross-generation discard rejects lagging, nonadjacent, and unmarked work");
    const auto deferred_marker = PlanGekkoRoundSeal(
        237, 238, true, {2, 361}, {2, 360}, {2, 359}, {2, 361},
        {2, 361}, true);
    expect(deferred_marker.sealed
            && deferred_marker.discard_cross_generation_advance,
        "canary-48 seals the adjacent confirmed advance when the deferred "
        "identity-replacement token precedes the native generation marker");
    const auto one_inflight_crossing = PlanGekkoRoundSeal(
        237, 239, true, {1, 361}, {1, 360}, {1, 359}, {1, 360},
        {1, 361}, true);
    expect(one_inflight_crossing.sealed
            && one_inflight_crossing.discard_cross_generation_advance,
        "Silver Wolves' Haven frame 361 retires the sole mixed-generation "
        "advance behind confirmed frame 237 for independent re-baselining");
    expect(!PlanGekkoRoundSeal(
                237, 239, true, {1, 361}, {1, 360}, {1, 361},
                {1, 360}, {1, 361}, true).sealed
            && !PlanGekkoRoundSeal(
                237, 239, true, {1, 361}, {1, 360}, {1, 359},
                {1, 360}, {1, 361}, false).sealed
            && !PlanGekkoRoundSeal(
                236, 239, true, {1, 361}, {1, 360}, {1, 359},
                {1, 359}, {1, 361}, true).sealed,
        "one-inflight retirement rejects unconfirmed canonical state, a "
        "missing replacement marker, and two unconfirmed inputs");
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
        "both peers independently freeze the exact committed baseline");
    pump_coordinators(first_online, first_transport,
        second_online, second_transport);
    expect(first_online.state() == OnlineState::Active
            && second_online.state() == OnlineState::Active,
        "identical baseline acknowledgement activates both coordinators");
    // Reproduce the immutable Silver Wolves' Haven prefix skew exactly: the
    // slot-0 sandbox completed two Gekko prefix frames while the slot-1 host
    // completed eleven. This phase difference is what made a full-window
    // twelve-update suppression request a thirteenth unknown remote input.
    for (std::uint32_t frame = 0; frame < 2; ++frame)
    {
        PlayerInput first_input{};
        first_input.held = (frame % 3 == 0) ? 1u : 0u;
        PlayerInput second_input{};
        second_input.held = (frame % 5 == 0) ? 2u : 0u;
        std::array<PlayerInput, 2> first_authoritative{};
        std::array<PlayerInput, 2> second_authoritative{};
        expect(first.Advance(first_input, first_authoritative).ok(),
            "first peer admits acknowledged preownership prefix input");
        first_simulation.CompleteNativeFrame();
        expect(first.CompleteDeferredSaves().ok(),
            "first peer completes prefix saves");
        transfer(first_transport, second_transport);
    }
    for (std::uint32_t frame = 0; frame < 11; ++frame)
    {
        PlayerInput second_input{};
        second_input.held = (frame % 5 == 0) ? 2u : 0u;
        std::array<PlayerInput, 2> second_authoritative{};
        expect(second.Advance(second_input, second_authoritative).ok(),
            "slot-1 host admits its eleven-frame preownership prefix");
        second_simulation.CompleteNativeFrame();
        expect(second.CompleteDeferredSaves().ok(),
            "slot-1 host completes its asymmetric prefix save");
        transfer(second_transport, first_transport);
    }
    for (int count = 0; count < 32
        && (first.confirmed_frame() < 1 || second.confirmed_frame() < 1);
        ++count)
    {
        expect(first.PollNetwork().ok(),
            "first peer drains prefix packets before ownership");
        transfer(first_transport, second_transport);
        expect(second.PollNetwork().ok(),
            "second peer drains prefix packets before ownership");
        transfer(second_transport, first_transport);
        expect(first.FlushCorrections().ok(),
            "first peer confirms the complete prefix before ownership");
        expect(second.FlushCorrections().ok(),
            "second peer confirms the complete prefix before ownership");
    }
    expect(first.confirmed_frame() >= 1 && second.confirmed_frame() >= 1,
        "qualification stimulus begins only after the complete prefix is confirmed");
    expect(claim_owned(first_online, {1, 129})
            && claim_owned(second_online, {1, 129}),
        "both peers claim simulation ownership");

    // The sandbox entered owned simulation about eight ticks before the host
    // in the immutable run. Reproduce that lifecycle ordering after retaining
    // the exact 2-versus-11 prefix, leaving the host one Gekko frame ahead
    // when both clients reach steady-state callbacks.
    for (std::uint32_t frame = 0; frame < 8; ++frame)
    {
        std::array<PlayerInput, 2> first_authoritative{};
        expect(first.FlushCorrections().ok()
                && first.Advance({}, first_authoritative).ok(),
            "slot-0 sandbox advances during the earlier ownership interval");
        first_simulation.CompleteNativeFrame();
        expect(first.CompleteDeferredSaves().ok(),
            "slot-0 sandbox completes its earlier owned save");
        transfer(first_transport, second_transport);
    }

    for (std::uint32_t frame = 0; frame < 40; ++frame)
    {
        std::array<PlayerInput, 2> first_authoritative{};
        std::array<PlayerInput, 2> second_authoritative{};
        expect(first.FlushCorrections().ok()
                && first.Advance({}, first_authoritative).ok(),
            "first peer reaches the mutual confirmed-hash boundary");
        first_simulation.CompleteNativeFrame();
        expect(first.CompleteDeferredSaves().ok(),
            "first peer completes pre-stimulus saves");
        transfer(first_transport, second_transport);
        expect(second.FlushCorrections().ok()
                && second.Advance({}, second_authoritative).ok(),
            "second peer reaches the mutual confirmed-hash boundary");
        second_simulation.CompleteNativeFrame();
        expect(second.CompleteDeferredSaves().ok(),
            "second peer completes pre-stimulus saves");
        transfer(second_transport, first_transport);
    }
    expect(first.confirmed_frame() >= 29 && second.confirmed_frame() >= 29,
        "both owned peers independently reach Gekko frame 29 before arming");
    const auto first_rollbacks_before_stimulus =
        first_simulation.rollback_advances;
    const auto second_rollbacks_before_stimulus =
        second_simulation.rollback_advances;
    expect(first.ArmQualificationCorrectionStimulus(
                QualificationCorrectionTransportDelay(11, 12, 0), 89).ok()
            && second.ArmQualificationCorrectionStimulus(
                QualificationCorrectionTransportDelay(11, 12, 1), 89).ok(),
        "mutual frame-29 hash arms one absolute frame-89 depth-11 correction");
    bool stimulus_healthy = true;
    for (std::uint32_t frame = 0; frame < 80; ++frame)
    {
        std::array<PlayerInput, 2> first_authoritative{};
        std::array<PlayerInput, 2> second_authoritative{};
        const auto first_flush = first.FlushCorrections();
        const auto first_advance = first_flush.ok()
            ? first.Advance({}, first_authoritative) : first_flush;
        if (!first_advance.ok())
        {
            const auto context = first.simulation_failure_context();
            std::cerr << "stimulus first peer failed at frame " << frame
                      << " status=" << failure_code_name(first_advance.code)
                      << " operation=" << static_cast<unsigned>(context.operation)
                      << " operation_frame=" << context.frame
                      << " unavailable_operation="
                      << static_cast<unsigned>(
                             first_simulation.unavailable_operation)
                      << " unavailable_frame="
                      << first_simulation.unavailable_frame
                      << '\n';
            stimulus_healthy = false;
            break;
        }
        first_simulation.CompleteNativeFrame();
        const auto first_saves = first.CompleteDeferredSaves();
        if (!first_saves.ok())
        {
            std::cerr << "stimulus first save failed at frame " << frame
                      << " status=" << failure_code_name(first_saves.code)
                      << '\n';
            stimulus_healthy = false;
            break;
        }
        transfer(first_transport, second_transport);
        const auto second_flush = second.FlushCorrections();
        const auto second_advance = second_flush.ok()
            ? second.Advance({}, second_authoritative) : second_flush;
        if (!second_advance.ok())
        {
            std::cerr << "stimulus second peer failed at frame " << frame
                      << " status=" << failure_code_name(second_advance.code)
                      << '\n';
            stimulus_healthy = false;
            break;
        }
        second_simulation.CompleteNativeFrame();
        const auto second_saves = second.CompleteDeferredSaves();
        if (!second_saves.ok())
        {
            std::cerr << "stimulus second save failed at frame " << frame
                      << " status=" << failure_code_name(second_saves.code)
                      << '\n';
            stimulus_healthy = false;
            break;
        }
        transfer(second_transport, first_transport);
    }
    expect(stimulus_healthy,
        "bilateral correction stimulus remains inside the prediction window");
    for (int count = 0; stimulus_healthy && count < 8; ++count)
    {
        expect(first.PollNetwork().ok(), "first stimulus final poll");
        transfer(first_transport, second_transport);
        expect(second.PollNetwork().ok(), "second stimulus final poll");
        transfer(second_transport, first_transport);
        expect(first.FlushCorrections().ok(),
            "first stimulus final correction flush");
        expect(second.FlushCorrections().ok(),
            "second stimulus final correction flush");
    }
    expect(first.qualification_correction_stimulus_injected()
            && second.qualification_correction_stimulus_injected()
            && first.qualification_correction_stimulus_released()
            && second.qualification_correction_stimulus_released(),
        "both qualification stimuli inject and release exactly once");
    expect(first_simulation.rollback_advances
                > first_rollbacks_before_stimulus
            && second_simulation.rollback_advances
                > second_rollbacks_before_stimulus,
        "bilateral late authenticated inputs force restore/resimulation on both peers");
    const auto has_staged_frame_90 = [](const auto& history,
                                          std::uint32_t first_p0,
                                          std::uint32_t first_p1) {
        bool partial{};
        for (const auto& value : history)
        {
            if (value.frame != 90) continue;
            if (!partial)
            {
                if (value.inputs[0].held != first_p0
                    || value.inputs[1].held != first_p1)
                    return false;
                partial = true;
                continue;
            }
            if (value.inputs[0].held == 1 && value.inputs[1].held == 1)
                return true;
        }
        return false;
    };
    if (!has_staged_frame_90(first_simulation.advance_history, 1, 0)
        || !has_staged_frame_90(second_simulation.advance_history, 0, 1))
    {
        const auto dump_frame_90 = [](const char* label, const auto& history) {
            std::cerr << label << " frame-90 rollback inputs:";
            for (const auto& value : history)
            {
                if (value.frame == 90)
                    std::cerr << ' ' << value.inputs[0].held << '/'
                              << value.inputs[1].held;
            }
            std::cerr << '\n';
        };
        dump_frame_90("first", first_simulation.advance_history);
        dump_frame_90("second", second_simulation.advance_history);
    }
    expect(has_staged_frame_90(first_simulation.advance_history, 1, 0)
            && has_staged_frame_90(second_simulation.advance_history, 0, 1),
        "Gekko revisits frame 90 after local-only knowledge with both delayed "
        "authenticated inputs; the first remote value is not immutable");
    expect(first_simulation.state == second_simulation.state,
        "bilateral depth-11 restore/resimulation converges independently");

    expect(!MayRearmQualificationCorrectionStimulus(true, true, 89, 89)
            && MayRearmQualificationCorrectionStimulus(true, true, 119, 89),
        "stimulus re-arm waits for a later mutually confirmed boundary");
    expect(QualificationCorrectionStimulusLead(12) == 14
            && 29 + 14 < 59 && 59 + 14 < 89 && 89 + 14 < 235,
        "11-1-6 scheduler fits all three triggers before the observed round boundary");

    const auto run_rearmed_stimulus = [&](std::uint8_t depth) {
        expect(first.confirmed_frame() == second.confirmed_frame(),
            "re-arm begins at a mutually confirmed Gekko boundary");
        const auto trigger = first.confirmed_frame()
            + QualificationCorrectionStimulusLead(12);
        const auto first_rollbacks = first_simulation.rollback_advances;
        const auto second_rollbacks = second_simulation.rollback_advances;
        const auto second_history_begin = second_simulation.advance_history.size();
        expect(first.ArmQualificationCorrectionStimulus(
                    QualificationCorrectionTransportDelay(
                        depth, 12, 0), trigger).ok()
                && second.ArmQualificationCorrectionStimulus(
                    QualificationCorrectionTransportDelay(
                        depth, 12, 1), trigger).ok(),
            "released qualification stimulus re-arms at a later shared boundary");
        bool healthy = true;
        for (std::uint32_t frame = 0; frame < 80 && healthy; ++frame)
        {
            PlayerInput first_input{};
            // Reproduce the live depth-1 miss: slot 0's actual input acquired
            // bit 0 on the trigger frame.  The old stimulus XORed that current
            // value back to the peer's previous all-zero prediction, so slot 1
            // observed no misprediction and performed no restore.
            if (depth == 1
                && first.qualification_next_local_input_frame() == trigger)
                first_input = {.held = 1, .rising = 1};
            std::array<PlayerInput, 2> first_authoritative{};
            std::array<PlayerInput, 2> second_authoritative{};
            // The two live SC6 processes consistently reach each shared
            // confirmation with slot 1 first and slot 0 second.  Preserve
            // that callback phase here: the former slot-0-first schedule
            // gave its delayed packet a receiver update that does not exist
            // in the authenticated runtime and masked a one-sided depth-1
            // correction.
            healthy = second.FlushCorrections().ok()
                && second.Advance({}, second_authoritative).ok();
            if (!healthy) break;
            second_simulation.CompleteNativeFrame();
            healthy = second.CompleteDeferredSaves().ok();
            transfer(second_transport, first_transport);
            healthy = healthy && first.FlushCorrections().ok()
                && first.Advance(first_input, first_authoritative).ok();
            if (!healthy) break;
            first_simulation.CompleteNativeFrame();
            healthy = first.CompleteDeferredSaves().ok();
            transfer(first_transport, second_transport);
        }
        for (int count = 0; healthy && count < 8; ++count)
        {
            healthy = first.PollNetwork().ok();
            transfer(first_transport, second_transport);
            healthy = healthy && second.PollNetwork().ok();
            transfer(second_transport, first_transport);
            healthy = healthy && first.FlushCorrections().ok()
                && second.FlushCorrections().ok();
        }
        expect(healthy,
            "re-armed bilateral correction remains inside the prediction window");
        expect(first.qualification_correction_stimulus_released()
                && second.qualification_correction_stimulus_released(),
            "both peers release the re-armed delayed payload");
        const auto first_rollback_delta =
            first_simulation.rollback_advances - first_rollbacks;
        const auto second_rollback_delta =
            second_simulation.rollback_advances - second_rollbacks;
        if (first_rollback_delta == 0 || second_rollback_delta == 0)
            std::cerr << "re-armed depth " << static_cast<unsigned>(depth)
                      << " rollback deltas slot0=" << first_rollback_delta
                      << " slot1=" << second_rollback_delta << '\n';
        expect(first_rollback_delta != 0 && second_rollback_delta != 0,
            "re-armed authenticated delay forces another restore/resimulation");
        const auto has_exact_transition = [](const auto& history,
                std::size_t begin, std::int32_t target,
                std::uint32_t initial_p0, std::uint32_t initial_p1) {
            bool initial{};
            for (std::size_t index = begin; index < history.size(); ++index)
            {
                const auto& value = history[index];
                if (value.frame != target) continue;
                if (!initial)
                {
                    if (value.inputs[0].held != initial_p0
                        || value.inputs[1].held != initial_p1)
                        return false;
                    initial = true;
                }
                else if (value.inputs[0].held == 1
                    && value.inputs[1].held == 1)
                    return true;
            }
            return false;
        };
        const bool second_transition = has_exact_transition(
            second_simulation.advance_history,
            second_history_begin, trigger + 1, 0, 1);
        if (!second_transition)
            std::cerr << "re-armed depth " << static_cast<unsigned>(depth)
                      << " slot-0 stimulus was not corrected by slot 1"
                      << " target=" << trigger + 1 << '\n';
        expect(second_transition,
            "slot 0's re-armed edge differs from slot 1's prior prediction");
        expect(first_simulation.state == second_simulation.state,
            "re-armed restore/resimulation converges independently");
    };
    run_rearmed_stimulus(1);
    run_rearmed_stimulus(6);

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
        first_simulation.CompleteNativeFrame();
        expect(first.CompleteDeferredSaves().ok(),
            "first Gekko session completes post-advance saves");
        transfer(first_transport, second_transport);
        expect(second.FlushCorrections().ok(),
            "second Gekko session flushes corrections before advance");
        expect(second.Advance(second_input, second_authoritative).ok(),
            "second Gekko session advances");
        second_simulation.CompleteNativeFrame();
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
    expect(std::any_of(first_simulation.loaded_frames.begin(),
               first_simulation.loaded_frames.end(),
               [](std::int32_t frame) { return frame >= 0; })
            && std::any_of(second_simulation.loaded_frames.begin(),
                   second_simulation.loaded_frames.end(),
                   [](std::int32_t frame) { return frame >= 0; }),
        "confirmed-anchor maintenance exercises retained historical loads at "
        "the depth-12 boundary");
    expect(first_simulation.available_through >= 28
            && second_simulation.available_through >= 28
            && first_simulation.unavailable_operation
                == GekkoSimulationOperation::None
            && second_simulation.unavailable_operation
                == GekkoSimulationOperation::None,
        "frame-28 Gekko work reaches coordinate 152 without requesting "
        "state newer than the completed native fencepost");
    expect(first.terminal_failure() == FailureCode::None
            && second.terminal_failure() == FailureCode::None,
        "both Gekko sessions remain healthy");

    for (std::uint64_t frame : {30u, 60u})
    {
        CanonicalHash confirmed_hash{};
        confirmed_hash[0] = static_cast<std::byte>(frame);
        const FrameCoordinate coordinate{1, frame + 240};
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
    expect(first_online.BeginRoundBarrier({1, 360}, 2, round_hash).ok()
            && second_online.BeginRoundBarrier(
                {1, 360}, 2, round_hash).ok(),
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
                {2, 0}, second_baseline, second_baseline).ok()
            && second_online.FreezeBaseline(
                {2, 0}, second_baseline, second_baseline).ok(),
        "both peers freeze the exact owned-round baseline without initial lead");
    pump_coordinators(first_online, first_transport,
        second_online, second_transport);
    expect(claim_owned(first_online, {2, 1})
            && claim_owned(second_online, {2, 1}),
        "both peers re-enter owned simulation in generation two");
    expect(first_online.ReturnToLobby().ok()
            && second_online.ReturnToLobby().ok(),
        "owned sessions begin terminal lobby return");
    first.Stop();
    second.Stop();
    expect(first_online.NotifyReturnedToLobby({0xabcdef,
                OnlineSceneExitBoundary::BattleTerminationCompleted}).ok()
            && second_online.NotifyReturnedToLobby({0xabcdef,
                OnlineSceneExitBoundary::BattleTerminationCompleted}).ok(),
        "coordinators clear all per-match state for lobby re-entry");
    expect(first_online.state() == OnlineState::ObservingLobby
            && second_online.state() == OnlineState::ObservingLobby,
        "clean teardown ends in stock lobby observation");

    if (failures == 0)
        std::cout << "GekkoRollbackSessionSelfTest passed\n";
    return failures == 0 ? 0 : 1;
}
