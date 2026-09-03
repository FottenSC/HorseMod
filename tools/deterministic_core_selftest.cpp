#include "deterministic/CanonicalHashTimeline.hpp"
#include "deterministic/AudioPresentation.hpp"
#include "deterministic/AuthoritativeInputGate.hpp"
#include "deterministic/DeterministicHookSet.hpp"
#include "deterministic/InputTimeline.hpp"
#include "deterministic/Config.hpp"
#include "deterministic/FloatingPointEnvironment.hpp"
#include "deterministic/NativeReplayMaterializer.hpp"
#include "deterministic/NativeBatchTimeline.hpp"
#include "deterministic/NativeAudioPresentationController.hpp"
#include "deterministic/NativePresentationJournal.hpp"
#include "deterministic/OnlineQualificationMetrics.hpp"
#include "deterministic/ParticlePresentation.hpp"
#include "deterministic/PresentationJournal.hpp"
#include "deterministic/ReplayCoordinator.hpp"
#include "deterministic/ReplaySeekPlanner.hpp"
#include "deterministic/Sc6ReplayNativeBridge.hpp"
#include "deterministic/Sc6ReplayRuntime.hpp"
#include "deterministic/SnapshotStore.hpp"
#include "deterministic/StagePresentation.hpp"
#include "deterministic/UcrtRandBroker.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <xmmintrin.h>
#include <vector>

using namespace Horse::Deterministic;

namespace
{
int failures = 0;

static_assert(candidate_capture_phase_name(CandidateCapturePhase::None)
    == "none");
static_assert(candidate_capture_phase_name(
    CandidateCapturePhase::CameraTopology) == "camera_topology");
static_assert(candidate_capture_phase_name(
    CandidateCapturePhase::CallbackTopology) == "callback_topology");
static_assert(candidate_capture_phase_name(CandidateCapturePhase::Adapter)
    == "adapter");
static_assert(camera_topology_capture_stage_name(
    CameraTopologyCaptureStage::ActionBackingTail) == "action_backing_tail");
static_assert(camera_topology_capture_stage_name(
    CameraTopologyCaptureStage::BoundTopology) == "bound_topology");

void expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void test_online_qualification_metrics_are_bounded_and_resettable()
{
    OnlineQualificationMetrics metrics{};
    metrics.SetPreMatchOwnedBytes(500);
    metrics.BeginStatus4(1000);
    metrics.ObserveOwnedBytes(1000);
    metrics.RecordCorrection(1'000'000);
    metrics.RecordCorrection(2'000'000);
    metrics.RecordCorrection(3'000'000);
    metrics.RecordCapacityFailure();
    auto status = metrics.status();
    expect(status.correction_samples == 3
            && status.correction_p50_ns == 2'010'000
            && status.correction_p95_ns == 3'010'000
            && status.correction_p99_ns == 3'010'000
            && status.correction_max_ns == 3'000'000,
        "online timing histogram reports bounded upper quantiles and exact max");
    expect(status.post_status4_growth_events == 0
            && status.capacity_failures == 1
            && status.pre_match_owned_bytes == 500,
        "online metrics retain growth and capacity failure evidence");
    metrics.ObserveOwnedBytes(1001);
    expect(metrics.status().post_status4_growth_events == 1,
        "online metrics detect allocator-accounted post-status-4 growth");
    metrics.Reset();
    status = metrics.status();
    expect(status.correction_samples == 0
            && status.post_status4_growth_events == 0
            && status.capacity_failures == 0,
        "online metrics reset completely between owned matches");
}

struct InputGateFixture
{
    bool publish_result{true};
    bool commit_result{true};
    std::uint32_t publish_calls{};
    std::uint32_t commit_calls{};
    PlayerInput published[2]{};
};

bool publish_input_gate_pair(
    void* context, const PlayerInput (&input)[2]) noexcept
{
    auto& fixture = *static_cast<InputGateFixture*>(context);
    ++fixture.publish_calls;
    fixture.published[0] = input[0];
    fixture.published[1] = input[1];
    return fixture.publish_result;
}

bool commit_input_gate_ownership(void* context) noexcept
{
    auto& fixture = *static_cast<InputGateFixture*>(context);
    ++fixture.commit_calls;
    return fixture.commit_result;
}

void test_authoritative_input_gate_is_transactional_and_fail_closed()
{
    const PlayerInput stock[2]{{0x11, 0x01}, {0x22, 0x02}};
    const PlayerInput selected[2]{{0x33, 0x03}, {0x44, 0x04}};
    InputGateFixture fixture{};
    auto result = ApplyAuthoritativeInputGate(
        AuthoritativeInputDisposition::PreparedTakeover, true, stock, selected,
        publish_input_gate_pair, &fixture,
        commit_input_gate_ownership, &fixture);
    expect(result.action
                == AuthoritativeInputGateAction::ContinueAuthoritative
            && result.applied && result.before_valid
            && result.before[0] == selected[0]
            && fixture.publish_calls == 1 && fixture.commit_calls == 1,
        "first ownership commits only after a complete paired publication");

    fixture = {};
    fixture.publish_result = false;
    result = ApplyAuthoritativeInputGate(
        AuthoritativeInputDisposition::PreparedTakeover, true, stock, selected,
        publish_input_gate_pair, &fixture,
        commit_input_gate_ownership, &fixture);
    expect(result.action == AuthoritativeInputGateAction::AbortBeforeConsume
            && result.failed_closed && !result.applied
            && fixture.publish_calls == 1 && fixture.commit_calls == 0,
        "failed first-owned publication aborts before commit or consumption");

    fixture = {};
    fixture.commit_result = false;
    result = ApplyAuthoritativeInputGate(
        AuthoritativeInputDisposition::PreparedTakeover, true, stock, selected,
        publish_input_gate_pair, &fixture,
        commit_input_gate_ownership, &fixture);
    expect(result.action == AuthoritativeInputGateAction::AbortBeforeConsume
            && result.failed_closed && !result.applied
            && fixture.publish_calls == 1 && fixture.commit_calls == 1,
        "failed ownership commit aborts the already-published frame before consumption");

    fixture = {};
    result = ApplyAuthoritativeInputGate(
        AuthoritativeInputDisposition::FailClosed, false, stock, selected,
        publish_input_gate_pair, &fixture,
        commit_input_gate_ownership, &fixture);
    expect(result.action == AuthoritativeInputGateAction::AbortBeforeConsume
            && result.failed_closed && !result.before_valid
            && fixture.publish_calls == 0 && fixture.commit_calls == 0,
        "unreadable or failed owned input never substitutes neutral or advances stock");
}

void test_aborted_outer_tick_reaches_post_completion_callback()
{
    struct Fixture { bool called{}; bool saw_abort{}; } fixture{};
    OuterTickObservation observation{};
    observation.authoritative_input_aborted_before_consume = true;
    const auto callback = [](void* context,
        const OuterTickObservation& completed) noexcept {
            auto& value = *static_cast<Fixture*>(context);
            value.called = true;
            value.saw_abort = completed.authoritative_input_aborted_before_consume;
        };
    DispatchCompletedOuterTick(&fixture, callback, observation);
    expect(fixture.called && fixture.saw_abort,
        "pre-consumption abort is delivered through the post-original outer callback");
}

void test_canonical_hash_timeline_is_immutable_and_bounded()
{
    CanonicalHashTimeline timeline{2};
    CanonicalHash first{};
    CanonicalHash second{};
    first[0] = std::byte{0x11};
    second[0] = std::byte{0x22};

    CanonicalComponentFingerprint first_components{1, 2, 3, 4, 5};
    CanonicalComponentFingerprint second_components{6, 7, 8, 9, 10};
    CanonicalWindFingerprint first_wind{};
    CanonicalWindFingerprint second_wind{};
    CanonicalWindNodeDiagnostic first_node{};
    CanonicalWindNodeDiagnostic second_node{};
    first_node.life_bits = 33;
    second_node.life_bits = 44;
    first_wind[0] = 11;
    second_wind[0] = 22;
    CanonicalNativeFingerprint native{};
    CanonicalInputDiagnostic input{};
    CanonicalWindSemanticDiagnostic wind_detail{};
    expect(timeline.Append({1, 10}, first, first_components, native, {}, input,
                wind_detail, first_wind,
                first_node, {}, {}).ok()
            && timeline.Append({1, 11}, second, second_components,
                native, {}, input, wind_detail, second_wind, second_node,
                {}, {}).ok(),
        "canonical timeline accepts a strictly increasing baseline");
    expect(timeline.Append({1, 10}, first, first_components, native, {}, input,
                wind_detail, first_wind,
                first_node, {}, {}).ok(),
        "canonical timeline treats an exact resumed frame as validation");
    expect(timeline.Append({1, 10}, second, first_components, native, {}, input,
                wind_detail, first_wind,
                first_node, {}, {}).code
            == FailureCode::StateHashMismatch,
        "canonical timeline rejects divergence without replacing baseline");
    expect(timeline.Append({1, 9}, first, first_components, native, {}, input,
                wind_detail, first_wind,
                first_node, {}, {}).code
            == FailureCode::IdentityMismatch,
        "canonical timeline rejects out-of-order history mutation");
    expect(timeline.Append({1, 12}, first, first_components, native, {}, input,
                wind_detail, first_wind,
                first_node, {}, {}).code
            == FailureCode::CapacityExceeded,
        "canonical timeline stops cleanly at its fixed capacity");
    expect(timeline.GetExact({1, 10}).has_value()
            && timeline.GetExact({1, 10})->hash == first
            && timeline.GetExact({1, 10})->components == first_components
            && timeline.GetExact({1, 10})->wind == first_wind
            && timeline.GetExact({1, 10})->wind_node == first_node
            && timeline.GetExact({1, 12}) == std::nullopt,
        "canonical timeline retains only the immutable bounded baseline");

    const auto old_first = *timeline.GetExact({1, 10});
    const auto old_second = *timeline.GetExact({1, 11});
    auto new_first = old_first;
    auto new_second = old_second;
    new_first.hash[1] = std::byte{0x33};
    new_second.hash[1] = std::byte{0x44};
    const std::array expected{old_first, old_second};
    const std::array replacement{new_first, new_second};
    expect(timeline.ReplaceExactRange(expected, replacement).ok()
            && timeline.GetExact({1, 10})->hash == new_first.hash
            && timeline.GetExact({1, 11})->hash == new_second.hash,
        "canonical timeline atomically replaces an exact corrected range");
    auto stale = expected;
    stale[1].hash[2] = std::byte{0x55};
    expect(timeline.ReplaceExactRange(stale, expected).code
            == FailureCode::StateHashMismatch
            && timeline.GetExact({1, 10})->hash == new_first.hash
            && timeline.GetExact({1, 11})->hash == new_second.hash,
        "canonical corrected range rejects stale expectations without mutation");
}

void test_round_transition_selects_the_last_canonicalized_fencepost()
{
    CanonicalHashTimeline timeline{4};
    CanonicalHash retired_hash{};
    CanonicalHash current_hash{};
    retired_hash[0] = std::byte{0x58};
    current_hash[0] = std::byte{0x61};
    const CanonicalComponentFingerprint components{};
    const CanonicalNativeFingerprint native{};
    const CanonicalInputDiagnostic input{};
    const CanonicalWindSemanticDiagnostic wind_semantic{};
    const CanonicalWindFingerprint wind{};
    const CanonicalWindNodeDiagnostic wind_node{};
    expect(timeline.Append({2, 358}, retired_hash, components, native, {},
                input, wind_semantic, wind, wind_node, {}, {}).ok()
            && timeline.Append({3, 361}, current_hash, components, native, {},
                input, wind_semantic, wind, wind_node, {}, {}).ok(),
        "round-transition fixture retains only successful canonical fenceposts");
    const auto retired = timeline.GetLastInGeneration(2);
    expect(retired.has_value() && retired->coordinate == FrameCoordinate{2, 358}
            && retired->hash == retired_hash
            && !timeline.GetExact({2, 359}).has_value(),
        "an observed but noncanonical frame 359 cannot replace canonical frame 358");
    expect(timeline.GetLastInGeneration(3).has_value()
            && timeline.GetLastInGeneration(3)->coordinate
                == FrameCoordinate{3, 361}
            && !timeline.GetLastInGeneration(1).has_value(),
        "canonical generation lookup never crosses the round boundary");
}

void test_round_rearm_clears_prediction_before_checkpoint_reservation()
{
    expect(!CanFreezeOnlineBaseline(
                {2, 123}, {2, 123}, true, false)
            && !CanFreezeOnlineBaseline(
                {2, 123}, {2, 124}, false, true),
        "canary-45 baseline 2:123 waits for both canonical and exact batch-entry state");
    expect(CanFreezeOnlineBaseline(
                {2, 123}, {2, 124}, true, true),
        "baseline 2:123 freezes after the following batch captures its entry image");
    constexpr FrameCoordinate observed{3, 379};
    constexpr FrameCoordinate target{3, 499};
    expect(!CanRequireOnlineBaselineCheckpoint(target, observed, true),
        "next-round checkpoint reservation rejects the retired prediction owner");
    expect(CanRequireOnlineBaselineCheckpoint(target, observed, false),
        "next-round checkpoint reservation admits the target after prediction re-arm");
    expect(!CanRequireOnlineBaselineCheckpoint(
                {2, 499}, observed, false)
            && !CanRequireOnlineBaselineCheckpoint(
                {3, 378}, observed, false),
        "next-round checkpoint reservation remains generation and order exact");
    expect(CanRequireOnlineBaselineCheckpoint(
            {3, 378}, observed, false, true),
        "next-round reservation accepts a retained exact historical target");
    expect(!CanRequireOnlineBaselineCheckpoint(
            {3, 378}, observed, false, false),
        "next-round reservation rejects a historical target without its exact snapshot");
    expect(!CanRequireOnlineBaselineCheckpoint(
            {5, 863}, {6, 963}, false, true),
        "next-round reservation rejects the canary-40 target after generation drift");
    expect(PlanOnlineRoundBaselineProposal({3, 366})
                == FrameCoordinate{3, 390}
            && PlanOnlineRoundBaselineProposal({3, 379})
                == FrameCoordinate{3, 390},
        "canary-41 peers reserve the same future checkpoint before proposing it");
    expect(PlanOnlineRoundBaselineProposal({3, 390})
                == FrameCoordinate{3, 420},
        "an exact checkpoint proposes the next strict cadence instead of a past frame");
    expect(!PlanOnlineRoundBaselineProposal({}).has_value()
            && !PlanOnlineRoundBaselineProposal({3, UINT64_MAX}).has_value(),
        "round baseline proposal rejects absent identity and coordinate overflow");
    expect(CanRetargetOnlineBaselineCheckpoint(
            {3, 390}, {3, 420}, {3, 380}),
        "a lower peer may retarget its future reservation to the committed maximum");
    expect(!CanRetargetOnlineBaselineCheckpoint(
                {3, 390}, {3, 389}, {3, 380})
            && !CanRetargetOnlineBaselineCheckpoint(
                {3, 390}, {4, 420}, {3, 380})
            && !CanRetargetOnlineBaselineCheckpoint(
                {3, 390}, {3, 420}, {3, 421}),
        "round reservation retargeting rejects regression, identity drift, and missed targets");
}

enum class AdapterFailure
{
    None,
    CapturePreflight,
    Capture,
    RestorePreflight,
    RestoreWrite,
    Repair,
    Verify,
};

class FakeAdapter final : public IGameStateAdapter
{
public:
    Status BindContext(const NativeContext& context) noexcept override
    {
        identity = context.battle_identity;
        generation = context.generation;
        return Status::success();
    }

    Status PreflightCapture(FrameCoordinate) noexcept override
    {
        return consume(AdapterFailure::CapturePreflight, FailureCode::CapturePreflightFailed);
    }

    Status Capture(FrameCoordinate coordinate, Snapshot& output) noexcept override
    {
        const Status status = consume(AdapterFailure::Capture, FailureCode::CaptureFailed);
        if (!status.ok()) return status;
        output.coordinate = coordinate;
        output.context_identity = identity;
        output.bytes.resize(sizeof(value));
        std::memcpy(output.bytes.data(), &value, sizeof(value));
        return Status::success();
    }

    Status PreflightRestore(const Snapshot& snapshot) noexcept override
    {
        if (snapshot.coordinate.generation != generation
            || snapshot.context_identity != identity)
        {
            return Status::failure(FailureCode::IdentityMismatch);
        }
        return consume(AdapterFailure::RestorePreflight, FailureCode::RestorePreflightFailed);
    }

    Status Restore(const Snapshot& snapshot) noexcept override
    {
        ++restore_calls;
        if (fail_undo_restore && restore_calls >= 2)
        {
            return Status::failure(FailureCode::RestoreWriteFailed);
        }
        std::memcpy(&value, snapshot.bytes.data(), sizeof(value));
        return consume(AdapterFailure::RestoreWrite, FailureCode::RestoreWriteFailed);
    }

    Status RebuildDerivedState() noexcept override
    {
        return consume(AdapterFailure::Repair, FailureCode::DerivedStateRepairFailed);
    }

    Status VerifyRestoredState(const Snapshot& expected) noexcept override
    {
        const Status injected = consume(
            AdapterFailure::Verify,
            FailureCode::RestoreVerificationFailed);
        if (!injected.ok()) return injected;
        int expected_value{};
        std::memcpy(&expected_value, expected.bytes.data(), sizeof(expected_value));
        return value == expected_value
            ? Status::success()
            : Status::failure(FailureCode::RestoreVerificationFailed);
    }

    Status AdvanceFrame(FrameCoordinate, const InputPair& inputs, bool) noexcept override
    {
        value += static_cast<int>(inputs.players[0].held);
        return Status::success();
    }

    Status ReconcilePresentation(FrameCoordinate) noexcept override
    {
        ++reconcile_count;
        return Status::success();
    }

    Status consume(AdapterFailure phase, FailureCode code) noexcept
    {
        if (failure != phase) return Status::success();
        failure = AdapterFailure::None;
        return Status::failure(code);
    }

    int value{};
    int reconcile_count{};
    std::uint64_t identity{};
    std::uint64_t generation{};
    AdapterFailure failure{AdapterFailure::None};
    int restore_calls{};
    bool fail_undo_restore{};
};

class CountingSink final : public IPresentationSink
{
public:
    Status Publish(const PresentationEvent&) noexcept override
    {
        ++count;
        return Status::success();
    }
    int count{};
};

class DecodingAudioSink final : public IPresentationSink
{
public:
    Status Publish(const PresentationEvent& event) noexcept override
    {
        AudioTerminalEvent terminal{};
        const Status status = DecodeAudioPresentation(event, terminal);
        if (status.ok()) terminals.push_back(terminal);
        return status;
    }

    std::vector<AudioTerminalEvent> terminals;
};

class FakeGenerationMaterializer final : public IReplayGenerationMaterializer
{
public:
    Status Preflight(const ReplayGenerationTarget& target) noexcept override
    {
        return target.expected_context.generation != 0
            && target.baseline.generation == target.expected_context.generation
            && target.round_image_identity != 0
            ? Status::success()
            : Status::failure(FailureCode::NativeGenerationMaterializationFailed);
    }

    Status Request(const ReplayGenerationTarget& target) noexcept override
    {
        if (requested.has_value())
        {
            return Status::failure(FailureCode::IllegalTransition);
        }
        requested = target;
        ++request_count;
        return Status::success();
    }

    std::optional<ReplayGenerationMaterialized> Poll() noexcept override
    {
        if (!ready || !requested.has_value())
        {
            return std::nullopt;
        }
        ReplayGenerationMaterialized result{
            requested->expected_context,
            requested->baseline,
            requested->native_round_index,
            requested->round_image_identity};
        if (corrupt_identity)
        {
            ++result.context.battle_identity;
        }
        requested.reset();
        ready = false;
        return result;
    }

    FailureCode TerminalFailure() const noexcept override { return terminal; }

    void Cancel() noexcept override
    {
        requested.reset();
        ready = false;
    }

    std::optional<ReplayGenerationTarget> requested;
    FailureCode terminal{FailureCode::None};
    int request_count{};
    bool ready{};
    bool corrupt_identity{};
};

class FakeReplayNativeBridge final : public IReplayNativeBridge
{
public:
    Status InspectRound(
        std::uint32_t native_round_index,
        ReplayNativeRoundView& output) noexcept override
    {
        if (failure != FailureCode::None)
        {
            return Status::failure(failure);
        }
        if (native_round_index >= view.round_count)
        {
            return Status::failure(FailureCode::IdentityMismatch);
        }
        output = view;
        return Status::success();
    }

    Status RequestRoundReset(
        std::uint32_t native_round_index,
        std::uint64_t round_image_identity) noexcept override
    {
        if (native_round_index >= view.round_count
            || round_image_identity != view.round_image_identity)
        {
            return Status::failure(FailureCode::IdentityMismatch);
        }
        ++request_count;
        view.move_state = 4;
        view.pending_dispatch = 0;
        view.round_image_applied = 0;
        return Status::success();
    }

    void CompleteFence() noexcept
    {
        view.move_state = 0;
        view.pending_dispatch = 1;
        view.round_image_applied = 1;
    }

    ReplayNativeRoundView view{};
    FailureCode failure{FailureCode::None};
    int request_count{};
};

struct RawReplayBridgeFixture
{
    enum class SetterMode { Normal, Ignore, Corrupt };

    RawReplayBridgeFixture()
    {
        round_images.fill(std::byte{0x31});
        manager.fill(std::byte{0x52});
        replay.fill(std::byte{0});
        write(replay, Schema::Sc6ReplayLayout::replay_enabled, std::uint8_t{1});
        write(replay, Schema::Sc6ReplayLayout::round_images, round_images.data());
        write(replay, Schema::Sc6ReplayLayout::round_count, std::int32_t{2});
        write(replay, Schema::Sc6ReplayLayout::round_capacity, std::int32_t{2});
        write(manager, Schema::Sc6ReplayLayout::manager_status, std::uint8_t{2});
        write(manager, Schema::Sc6ReplayLayout::manager_move_state, std::uint8_t{0});
        write(manager, Schema::Sc6ReplayLayout::manager_pending_dispatch, std::uint8_t{1});
    }

    template <std::size_t Size, typename T>
    static void write(
        std::array<std::byte, Size>& storage,
        std::size_t offset,
        const T& value)
    {
        std::memcpy(storage.data() + offset, &value, sizeof(value));
    }

    static void* replay_resolver(void* user) noexcept
    {
        return static_cast<RawReplayBridgeFixture*>(user)->replay.data();
    }
    static void* manager_resolver(void* user) noexcept
    {
        return static_cast<RawReplayBridgeFixture*>(user)->manager.data();
    }
    static void* fighter_one_resolver(void* user) noexcept
    {
        return &static_cast<RawReplayBridgeFixture*>(user)->fighter_one;
    }
    static void* fighter_two_resolver(void* user) noexcept
    {
        return &static_cast<RawReplayBridgeFixture*>(user)->fighter_two;
    }
    static void* stage_resolver(void* user) noexcept
    {
        return &static_cast<RawReplayBridgeFixture*>(user)->stage;
    }
    static void set_move_state(void* battle_manager, std::uint8_t state) noexcept
    {
        auto* fixture = active_setter_fixture;
        if (fixture == nullptr || fixture->setter_mode == SetterMode::Ignore)
        {
            return;
        }
        const std::uint8_t written = fixture->setter_mode == SetterMode::Corrupt ? 5 : state;
        std::memcpy(
            static_cast<std::byte*>(battle_manager)
                + Schema::Sc6ReplayLayout::manager_move_state,
            &written,
            sizeof(written));
    }

    Sc6ReplayResolvers resolvers() noexcept
    {
        active_setter_fixture = this;
        return {this,
            replay_resolver,
            manager_resolver,
            fighter_one_resolver,
            fighter_two_resolver,
            stage_resolver,
            set_move_state,
            true};
    }

    inline static RawReplayBridgeFixture* active_setter_fixture{};
    std::array<std::byte, 0x3b8> replay{};
    std::array<std::byte, 0x1481> manager{};
    std::array<std::byte, Schema::replay_round_image_size * 2> round_images{};
    std::uint64_t fighter_one{1};
    std::uint64_t fighter_two{2};
    std::uint64_t stage{3};
    SetterMode setter_mode{SetterMode::Normal};
};

struct Fixture
{
    FakeAdapter adapter;
    InputTimeline inputs{128};
    SnapshotStore snapshots{1024 * 1024, 64, CapacityPolicy::RejectNew};
    PresentationJournal journal{128, 1024 * 1024};
    SimulationSession simulation{adapter, inputs, snapshots, journal};
};

NativeContext context()
{
    return NativeContext{1, 99, {101, 102}, 201};
}

NativeContext second_context()
{
    return NativeContext{2, 199, {301, 302}, 401};
}

InputPair one_input(bool confirmed = true)
{
    InputPair input;
    input.players[0].held = 1;
    input.remote_confirmed = confirmed;
    return input;
}

void test_public_config_contract()
{
    const auto path = std::filesystem::temp_directory_path()
        / "horsemod_deterministic_config_selftest.ini";
    {
        std::ofstream output(path, std::ios::trunc);
        output << "config_version=1\n"
               << "enabled=true\n"
               << "rollback_window=12\n"
               << "input_delay=1\n"
               << "trace=false\n"
               << "correction_probe=true\n"
               << "forced_depth7_qualification=true\n"
               << "legacy_transport=udp\n"
               << "legacy_mode=lab\n";
    }
    const ConfigLoadResult loaded = LoadConfig(path);
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    expect(loaded.status.ok(), "load public deterministic config");
    expect(loaded.config.enabled, "parse deterministic enabled flag");
    expect(loaded.config.correction_probe,
        "parse baseline-preserving owned correction probe flag");
    expect(loaded.config.forced_depth7_qualification,
        "parse forced depth-7 qualification flag");
    expect(loaded.diagnostics.size() == 1, "legacy config emits one diagnostic");
}

void test_input_replacement_and_invalidation()
{
    InputTimeline timeline{2};
    expect(timeline.AppendAuthoritative({1, 1}, one_input(false)).ok(), "append predicted input");
    expect(timeline.AppendAuthoritative({1, 0}, one_input(false)).ok(),
        "append an earlier coordinate into reserved sorted storage");
    PlayerInput remote;
    remote.held = 7;
    expect(timeline.ReplacePredicted({1, 0}, 1, remote).ok(), "replace predicted input");
    expect(timeline.GetExact({1, 0})->players[1].held == 7, "confirmed input stored");
    const auto confirmed = *timeline.GetExact({1, 0});
    auto corrected = confirmed;
    corrected.post_filter_players[1].held = 9;
    expect(timeline.CompareExchange({1, 0}, confirmed, corrected).ok()
            && timeline.GetExact({1, 0})->post_filter_players[1].held == 9,
        "input timeline atomically publishes corrected source-frame data");
    expect(timeline.CompareExchange({1, 0}, confirmed, confirmed).code
            == FailureCode::IdentityMismatch
            && timeline.GetExact({1, 0})->post_filter_players[1].held == 9,
        "input timeline rejects stale transactional undo");
    const std::array coordinates{FrameCoordinate{1, 0}, FrameCoordinate{1, 1}};
    const std::array expected_inputs{corrected, one_input(false)};
    auto replacement_inputs = expected_inputs;
    replacement_inputs[1].post_filter_players[0].held = 11;
    expect(timeline.CompareExchangeRange(
                coordinates, expected_inputs, replacement_inputs).ok()
            && timeline.GetExact({1, 1})->post_filter_players[0].held == 11,
        "input timeline atomically publishes a corrected range");
    auto stale_inputs = expected_inputs;
    stale_inputs[0].players[0].held = 99;
    expect(timeline.CompareExchangeRange(
                coordinates, stale_inputs, expected_inputs).code
            == FailureCode::IdentityMismatch
            && timeline.GetExact({1, 1})->post_filter_players[0].held == 11,
        "input corrected range rejects stale expectations without mutation");
    remote.held = 8;
    expect(
        timeline.ReplacePredicted({1, 0}, 1, remote).code == FailureCode::IdentityMismatch,
        "confirmed input cannot be rewritten");
    expect(timeline.AppendAuthoritative({1, 2}, one_input(false)).code
            == FailureCode::CapacityExceeded,
        "reserved input capacity remains fail-closed");
    timeline.InvalidateGeneration(1);
    expect(!timeline.GetExact({1, 0}).has_value(), "input generation invalidated");
}

void test_native_batch_timeline_is_exact_and_bounded()
{
    NativeBatchEnvelope dense_audio{};
    dense_audio.batch_id = 1;
    dense_audio.entry_coordinate = {};
    dense_audio.exit_coordinate = {1, 1};
    dense_audio.coordinate_count = 1;
    dense_audio.battle_audio_dispatches = 17;
    dense_audio.battle_audio_journal_count = 17;
    dense_audio.presentation_order_journal_count = 17;
    for (std::uint8_t index = 0; index < 17; ++index)
    {
        dense_audio.battle_audio_journal[index].direct = 1;
        dense_audio.presentation_order_journal[index] = {
            PresentationEventFamily::BattleAudioDispatch, index};
    }
    const std::array dense_audio_coordinate{FrameCoordinate{1, 1}};
    NativeBatchTimeline dense_audio_timeline{1, 1};
    expect(dense_audio_timeline.Append(
               dense_audio, dense_audio_coordinate).ok(),
        "batch storage admits the observed 17-dispatch authored audio burst");

    NativeBatchTimeline timeline{2, 4};
    NativeBatchEnvelope first{};
    first.batch_id = 10;
    first.entry_coordinate = {};
    first.exit_coordinate = {1, 2};
    first.coordinate_count = 2;
    first.stage_wall_calls = 1;
    first.stage_wall_journal_count = 1;
    first.stage_wall_journal[0].owner_logical_id = 0x1234;
    first.stage_wall_journal[0].payload_size = 1;
    first.stage_wall_journal[0].canonical_before_size = 12;
    first.stage_wall_journal[0].particle_count = 1;
    first.stage_wall_journal[0].semantic[0] = std::byte{0x2a};
    first.battle_audio_blueprint_calls = 1;
    first.battle_audio_blueprint_journal_count = 1;
    first.battle_audio_blueprint_journal[0].semantic[0] = std::byte{0x4d};
    first.battle_audio_blueprint_journal[0].handler_slot = 1;
    first.battle_audio_blueprint_journal[0].direct = 1;
    first.battle_audio_stop_all_calls = 1;
    first.battle_audio_stop_all_journal_count = 1;
    first.battle_audio_stop_all_journal[0].owner_slot = 2;
    first.battle_audio_stop_all_journal[0].control = 1;
    first.audio_terminal_calls = 1;
    first.audio_terminal_journal_count = 1;
    first.audio_terminal_journal[0] = {AudioTerminalOperation::StopAll,
        {AudioOwnerDomain::BattleSharedPlayer, 0, 0},
        audio_invalid_playback_id, 0, -1, 1};
    first.particle_spawn_calls = 1;
    first.particle_spawn_journal_count = 1;
    first.particle_spawn_journal[0].semantic[0] = std::byte{3};
    first.particle_spawn_journal[0].semantic[5] = std::byte{0x7f};
    first.presentation_order_journal_count = 5;
    first.presentation_order_journal[0] = {
        PresentationEventFamily::StageWall, 0};
    first.presentation_order_journal[1] = {
        PresentationEventFamily::BattleAudioBlueprint, 0};
    first.presentation_order_journal[2] = {
        PresentationEventFamily::ParticleSpawn, 0};
    first.presentation_order_journal[3] = {
        PresentationEventFamily::BattleAudioStopAll, 0};
    first.presentation_order_journal[4] = {
        PresentationEventFamily::AudioTerminal, 0};
    const std::array first_coordinates{
        FrameCoordinate{1, 1}, FrameCoordinate{1, 2}};
    expect(timeline.Append(first, first_coordinates).ok(),
        "first native batch must append");

    const auto membership = timeline.FindCoordinate({1, 2});
    expect(membership.has_value() && membership->batch_index == 0
            && membership->offset_in_batch == 1,
        "coordinate lookup must preserve its exact batch and offset");
    expect(timeline.GetBatch(0) != nullptr
            && timeline.GetBatch(0)->batch_id == 10,
        "batch lookup must return the stored envelope");
    expect(timeline.GetBatch(0)->stage_wall_journal_count == 1
            && timeline.GetBatch(0)->stage_wall_journal[0].payload_size == 1
            && timeline.GetBatch(0)->stage_wall_journal[0].semantic[0]
                == std::byte{0x2a}
            && timeline.GetBatch(0)->particle_spawn_journal_count == 1
            && timeline.GetBatch(0)->particle_spawn_journal[0].semantic[0]
                == std::byte{3}
            && timeline.GetBatch(0)->particle_spawn_journal[0].semantic[5]
                == std::byte{0x7f},
        "batch storage preserves ordered pointer-free presentation source values");
    expect(timeline.GetBatch(0)->battle_audio_blueprint_journal_count == 1
            && timeline.GetBatch(0)->battle_audio_blueprint_journal[0]
                    .semantic[0]
                == std::byte{0x4d}
            && timeline.GetBatch(0)->battle_audio_blueprint_journal[0]
                    .handler_slot
                == 1
            && timeline.GetBatch(0)->battle_audio_blueprint_journal[0].direct
                == 1,
        "batch storage preserves exact reflected battle-audio publications");
    expect(timeline.GetBatch(0)->battle_audio_stop_all_journal_count == 1
            && timeline.GetBatch(0)->battle_audio_stop_all_journal[0].owner_slot
                == 2
            && timeline.GetBatch(0)->battle_audio_stop_all_journal[0].control
                == 1
            && timeline.GetBatch(0)->audio_terminal_journal_count == 1
            && timeline.GetBatch(0)->audio_terminal_journal[0].owner.domain
                == AudioOwnerDomain::BattleSharedPlayer,
        "batch storage preserves stable audio owner and terminal identity");
    expect(timeline.GetBatch(0)->presentation_order_journal_count == 5
            && timeline.GetBatch(0)->presentation_order_journal[1].family
                == PresentationEventFamily::BattleAudioBlueprint
            && timeline.GetBatch(0)->presentation_order_journal[2].family
                == PresentationEventFamily::ParticleSpawn,
        "batch storage preserves exact cross-family presentation order");
    const auto original_batch = *timeline.GetBatch(0);
    auto corrected_batch = original_batch;
    corrected_batch.stage_wall_journal[0].semantic[0] = std::byte{0x31};
    expect(timeline.ReplaceBatch(0, original_batch, corrected_batch).ok()
            && timeline.GetBatch(0)->stage_wall_journal[0].semantic[0]
                == std::byte{0x31},
        "native batch timeline replaces an exact corrected presentation batch");
    auto invalid_replacement = corrected_batch;
    invalid_replacement.presentation_order_journal_count = 4;
    expect(timeline.ReplaceBatch(0, corrected_batch, invalid_replacement).code
            == FailureCode::IdentityMismatch
            && timeline.GetBatch(0)->presentation_order_journal_count == 5,
        "native batch replacement rejects malformed presentation atomically");

    NativeBatchTimeline duplicate_order_timeline{1, 2};
    NativeBatchEnvelope duplicate_order = first;
    duplicate_order.batch_id = 1;
    duplicate_order.entry_coordinate = {};
    duplicate_order.exit_coordinate = {1, 2};
    duplicate_order.presentation_order_journal[3] = {
        PresentationEventFamily::StageWall, 0};
    expect(duplicate_order_timeline.Append(duplicate_order, first_coordinates).code
            == FailureCode::IdentityMismatch,
        "batch storage rejects duplicate cross-family ordinals");

    NativeBatchTimeline invalid_source_timeline{1, 2};
    NativeBatchEnvelope invalid_source = first;
    invalid_source.batch_id = 1;
    invalid_source.entry_coordinate = {};
    invalid_source.exit_coordinate = {1, 2};
    invalid_source.presentation_order_journal[2].source_offset = 3;
    expect(invalid_source_timeline.Append(invalid_source, first_coordinates).code
            == FailureCode::IdentityMismatch,
        "batch storage rejects presentation beyond its native coordinate span");

    NativeBatchEnvelope source_terminal{};
    source_terminal.batch_id = 1;
    source_terminal.entry_coordinate = {};
    source_terminal.exit_coordinate = {1, 1};
    source_terminal.coordinate_count = 1;
    source_terminal.battle_audio_source_calls = 1;
    source_terminal.battle_audio_source_journal_count = 1;
    source_terminal.battle_audio_source_journal[0].presentation_order_count = 2;
    source_terminal.battle_audio_source_journal[0].terminal_count = 1;
    source_terminal.audio_terminal_calls = 1;
    source_terminal.audio_terminal_journal_count = 1;
    source_terminal.audio_terminal_journal[0] = {
        AudioTerminalOperation::SetParameter,
        {AudioOwnerDomain::BattleSharedPlayer, 0, 0},
        audio_invalid_playback_id, 1, -1, 0x3f800000};
    source_terminal.presentation_order_journal_count = 2;
    source_terminal.presentation_order_journal[0] = {
        PresentationEventFamily::BattleAudioSource, 0};
    source_terminal.presentation_order_journal[1] = {
        PresentationEventFamily::AudioTerminal, 0};
    const std::array source_terminal_coordinate{FrameCoordinate{1, 1}};
    NativeBatchTimeline source_terminal_timeline{1, 1};
    expect(source_terminal_timeline.Append(
               source_terminal, source_terminal_coordinate).ok(),
        "battle-audio source spans admit their nested stable terminals");
    source_terminal.battle_audio_source_journal[0].first_terminal = 1;
    NativeBatchTimeline malformed_source_terminal_timeline{1, 1};
    expect(malformed_source_terminal_timeline.Append(
               source_terminal, source_terminal_coordinate).code
            == FailureCode::IdentityMismatch,
        "battle-audio source spans reject terminal ranges outside the journal");

    NativeBatchTimeline zero_width_timeline{1, 1};
    NativeBatchEnvelope zero_width = first;
    zero_width.batch_id = 1;
    zero_width.entry_coordinate = {1, 2};
    zero_width.exit_coordinate = zero_width.entry_coordinate;
    zero_width.native_frame_before = 2;
    zero_width.native_frame_after = 2;
    zero_width.coordinate_count = 0;
    expect(zero_width_timeline.Append(zero_width, {}).ok(),
        "batch storage accepts offset zero presentation at a zero-width entry");
    zero_width.presentation_order_journal[0].source_offset = 1;
    NativeBatchTimeline invalid_zero_width_timeline{1, 1};
    expect(invalid_zero_width_timeline.Append(zero_width, {}).code
            == FailureCode::IdentityMismatch,
        "batch storage rejects nonzero presentation offset in zero-width batch");

    NativeBatchTimeline malformed_timeline{1, 1};
    NativeBatchEnvelope malformed{};
    malformed.batch_id = 1;
    malformed.entry_coordinate = {1, 0};
    malformed.exit_coordinate = {1, 1};
    malformed.coordinate_count = 1;
    malformed.particle_spawn_calls = 1;
    const std::array malformed_coordinate{FrameCoordinate{1, 1}};
    expect(malformed_timeline.Append(malformed, malformed_coordinate).code
            == FailureCode::IdentityMismatch,
        "batch storage rejects missing ordered presentation source values");

    NativeBatchTimeline malformed_blueprint_timeline{1, 1};
    malformed.particle_spawn_calls = 0;
    malformed.battle_audio_blueprint_calls = 1;
    expect(malformed_blueprint_timeline.Append(
               malformed, malformed_coordinate).code
            == FailureCode::IdentityMismatch,
        "batch storage rejects missing reflected battle-audio publications");

    NativeBatchEnvelope second{};
    second.batch_id = 12;
    second.entry_coordinate = {1, 2};
    second.exit_coordinate = {2, 1};
    second.coordinate_count = 2;
    const std::array second_coordinates{
        FrameCoordinate{1, 3}, FrameCoordinate{2, 1}};
    expect(timeline.Append(second, second_coordinates).ok(),
        "a batch may contain an explicit generation transition");

    NativeBatchEnvelope overflow{};
    overflow.batch_id = 13;
    overflow.entry_coordinate = {2, 1};
    overflow.exit_coordinate = {2, 2};
    overflow.coordinate_count = 1;
    const std::array overflow_coordinates{FrameCoordinate{2, 2}};
    expect(timeline.Append(overflow, overflow_coordinates).code
            == FailureCode::CapacityExceeded,
        "batch capacity exhaustion must fail closed");
    expect(timeline.batch_count() == 2 && timeline.coordinate_count() == 4
            && !timeline.FindCoordinate({2, 2}).has_value(),
        "failed batch append must leave the timeline unchanged");

    timeline.Clear();
    NativeBatchEnvelope zero{};
    zero.batch_id = 20;
    expect(timeline.Append(zero, {}).ok() && timeline.batch_count() == 1
            && timeline.coordinate_count() == 0,
        "zero-coordinate native batches must be retained explicitly");

    NativeBatchTimeline generations{3, 3};
    NativeBatchEnvelope generation_one{};
    generation_one.batch_id = 30;
    generation_one.entry_coordinate = {1, 0};
    generation_one.exit_coordinate = {1, 1};
    generation_one.coordinate_count = 1;
    const std::array generation_one_coordinates{FrameCoordinate{1, 1}};
    expect(generations.Append(
            generation_one, generation_one_coordinates).ok(),
        "append first native generation");
    NativeBatchEnvelope generation_two{};
    generation_two.batch_id = 31;
    generation_two.entry_coordinate = {2, 0};
    generation_two.exit_coordinate = {2, 1};
    generation_two.coordinate_count = 1;
    const std::array generation_two_coordinates{FrameCoordinate{2, 1}};
    expect(generations.Append(
            generation_two, generation_two_coordinates).ok(),
        "retain an explicit native generation discontinuity");
    NativeBatchEnvelope invalid_gap = generation_two;
    invalid_gap.batch_id = 32;
    invalid_gap.entry_coordinate = {2, 4};
    invalid_gap.exit_coordinate = {2, 5};
    const std::array invalid_gap_coordinates{FrameCoordinate{2, 5}};
    expect(generations.Append(invalid_gap, invalid_gap_coordinates).code
            == FailureCode::IdentityMismatch,
        "reject an unexplained same-generation batch gap");
}

void test_snapshot_capacity_is_atomic()
{
    SnapshotStore store{sizeof(Snapshot) + 64, 1, CapacityPolicy::RejectNew};
    Snapshot first{{1, 0}, 1, {}, {}, {}, {}, {}, {}, {}, {}, {}, {},
        std::vector<std::byte>(4)};
    Snapshot second{{1, 1}, 1, {}, {}, {}, {}, {}, {}, {}, {}, {}, {},
        std::vector<std::byte>(4)};
    const auto reserved_bytes = store.BytesUsed();
    expect(store.Save(first).ok(), "save first snapshot");
    const auto bytes_before = store.BytesUsed();
    expect(store.Save(second).code == FailureCode::CapacityExceeded, "reject full snapshot store");
    expect(store.BytesUsed() == bytes_before, "capacity rejection does not mutate store");
    expect(store.Load({1, 0}).has_value(), "original snapshot survives rejection");
    store.Clear();
    expect(store.BytesUsed() == reserved_bytes
            && !store.Load({1, 0}).has_value(),
        "snapshot store clear releases payload while retaining bounded slots");

    SnapshotStore prewarmed{1024 * 1024, 3, CapacityPolicy::RejectNew};
    Snapshot prototype{};
    prototype.coordinate = {5, 1};
    prototype.bytes.resize(64, std::byte{0x41});
    prototype.local_images.resize(1);
    prototype.local_images[0].bytes.resize(32, std::byte{0x42});
    expect(prewarmed.PrewarmCopySlots(prototype).ok(),
        "prewarm bounded checkpoint copy slots from native shape");
    const auto prewarmed_bytes = prewarmed.BytesUsed();
    expect(prewarmed.SaveCopyPrewarmed(prototype).ok()
            && prewarmed.BytesUsed() == prewarmed_bytes,
        "prewarmed checkpoint save does not grow allocator-accounted storage");
    auto next = prototype;
    next.coordinate = {5, 2};
    expect(prewarmed.SaveCopyPrewarmed(next).ok()
            && prewarmed.BytesUsed() == prewarmed_bytes,
        "subsequent checkpoint copies retain the fixed allocation ceiling");
    auto oversized = prototype;
    oversized.coordinate = {5, 3};
    oversized.bytes.resize(1024);
    expect(prewarmed.SaveCopyPrewarmed(oversized).ok()
            && prewarmed.BytesUsed() >= prewarmed_bytes
            && prewarmed.FindExact({5, 3}) != nullptr,
        "checkpoint shape may rewarm the bounded pool before ownership");
    const auto grown_bytes = prewarmed.BytesUsed();
    oversized.bytes[0] = std::byte{0x55};
    expect(prewarmed.SaveCopyPrewarmed(oversized).ok()
            && prewarmed.BytesUsed() == grown_bytes,
        "stabilized checkpoint shape copies without allocator growth");
    prewarmed.Clear();
    expect(prewarmed.BytesUsed() == grown_bytes
            && prewarmed.FindExact({5, 1}) == nullptr,
        "prewarmed clear retains buffers while releasing all identities");

    SnapshotStore transactional{1024 * 1024, 4, CapacityPolicy::RejectNew};
    Snapshot old_a{};
    old_a.coordinate = {7, 10};
    old_a.canonical_hash.fill(std::byte{0x11});
    old_a.bytes.resize(32, std::byte{0x21});
    Snapshot old_b{};
    old_b.coordinate = {7, 20};
    old_b.canonical_hash.fill(std::byte{0x12});
    old_b.bytes.resize(32, std::byte{0x22});
    expect(transactional.Save(old_a).ok() && transactional.Save(old_b).ok(),
        "seed corrected checkpoint transaction");
    std::array<Snapshot, 2> replacements{old_a, old_b};
    replacements[0].canonical_hash.fill(std::byte{0x31});
    replacements[1].canonical_hash.fill(std::byte{0x32});
    const std::array valid_hashes{old_a.canonical_hash, old_b.canonical_hash};
    auto stale_hashes = valid_hashes;
    stale_hashes[1].fill(std::byte{0x7f});
    expect(transactional.ValidateExactReplacement(
            replacements, stale_hashes).code == FailureCode::IdentityMismatch
            && transactional.FindExact({7, 10})->canonical_hash
                == old_a.canonical_hash
            && transactional.FindExact({7, 20})->canonical_hash
                == old_b.canonical_hash,
        "stale corrected checkpoint range is rejected without mutation");
    expect(transactional.ValidateExactReplacement(
            replacements, valid_hashes).ok(),
        "validate complete corrected checkpoint range before publication");
    transactional.CommitValidatedExactReplacement(replacements);
    CanonicalHash corrected_a{};
    corrected_a.fill(std::byte{0x31});
    CanonicalHash corrected_b{};
    corrected_b.fill(std::byte{0x32});
    expect(transactional.FindExact({7, 10})->canonical_hash == corrected_a
            && transactional.FindExact({7, 20})->canonical_hash == corrected_b,
        "publish corrected checkpoint range into immutable entry slots");

    SnapshotStore generations{1024 * 1024, 8, CapacityPolicy::RejectNew};
    expect(generations.Save({{1, 3}, 1, {}, {}}).ok(),
        "save first generation resimulation base");
    expect(generations.Save({{1, 21}, 1, {}, {}}).ok(),
        "save later first generation resimulation base");
    expect(generations.Save({{2, 4}, 1, {}, {}}).ok(),
        "save second generation resimulation base");
    expect(generations.FindExact({1, 21}) != nullptr
            && generations.FindExact({1, 21})->coordinate
                == FrameCoordinate{1, 21},
        "non-copying exact lookup returns the retained snapshot");
    expect(!generations.NearestAtOrBefore({1, 2}).has_value(),
        "coordinate before first base remains uncovered");
    expect(generations.NearestAtOrBefore({1, 20})->coordinate
            == FrameCoordinate{1, 3},
        "nearest lookup selects prior base in the same generation");
    expect(generations.NearestAtOrBefore({1, 21})->coordinate
            == FrameCoordinate{1, 21},
        "nearest lookup selects an exact base");
    expect(!generations.NearestAtOrBefore({2, 3}).has_value(),
        "nearest lookup never crosses a generation boundary");
    expect(generations.FindNearestAtOrBefore({1, 20}) != nullptr
            && generations.FindNearestAtOrBefore({1, 20})->coordinate
                == FrameCoordinate{1, 3},
        "non-copying nearest lookup preserves generation-scoped ordering");

    SnapshotStore ring{1024 * 1024, 2, CapacityPolicy::EvictOldest};
    Snapshot ring_first{};
    ring_first.coordinate = {3, 10};
    ring_first.bytes.resize(64, std::byte{0x11});
    ring_first.local_images.resize(1);
    ring_first.local_images[0].bytes.resize(256, std::byte{0x22});
    Snapshot ring_second{};
    ring_second.coordinate = {3, 11};
    ring_second.bytes.resize(64, std::byte{0x33});
    expect(ring.Save(std::move(ring_first)).ok()
            && ring.Save(std::move(ring_second)).ok(),
        "warm bounded qualification snapshot ring");
    Snapshot reusable{};
    expect(ring.TakeOldestIfFull(reusable)
            && reusable.coordinate == FrameCoordinate{3, 10}
            && reusable.bytes.capacity() >= 64
            && reusable.local_images.size() == 1
            && reusable.local_images[0].bytes.capacity() >= 256
            && !ring.Load({3, 10}).has_value(),
        "full qualification ring transfers oldest owned buffers for reuse");
    reusable.coordinate = {3, 12};
    expect(ring.Save(std::move(reusable)).ok()
            && ring.Load({3, 11}).has_value()
            && ring.Load({3, 12}).has_value(),
        "recycled qualification slot re-enters the bounded ring");
}

void test_checkpoint_memory_matches_capture_cadence()
{
    constexpr std::uint64_t maximum_resimulation_distance =
        Schema::checkpoint_interval - 1;
    constexpr std::uint64_t batch_entry_spacing =
        maximum_resimulation_distance
        - Schema::maximum_supported_native_batch_width + 1;
    static_assert(batch_entry_spacing == 18);
    static_assert(Schema::checkpoint_interval == 30);
    static_assert(
        Schema::replay_landing_checkpoint_memory_budget
            + Schema::replay_batch_entry_checkpoint_memory_budget
        == Schema::replay_checkpoint_memory_budget);

    // Equal-sized checkpoint images require five batch-entry slots for every
    // three landing slots to cover the same native-frame horizon. This is the
    // exact retention boundary used by PlanResimulationBase: a 12-wide native
    // batch must still remain within the 29-coordinate replay limit.
    expect(
        Schema::replay_landing_checkpoint_memory_budget / 3
            == Schema::replay_batch_entry_checkpoint_memory_budget / 5,
        "checkpoint memory is split by the 30-frame landing and 18-frame "
        "batch-entry capture cadence");

    SnapshotStore landing{1024 * 1024, 3, CapacityPolicy::RejectNew};
    SnapshotStore batch_entry{1024 * 1024, 5, CapacityPolicy::RejectNew};
    for (std::uint64_t frame = 0; frame < 90; frame += 30)
        expect(landing.Save({{1, frame}, 1, {}, {}}).ok(),
            "landing checkpoint retains the shared 90-frame horizon");
    for (std::uint64_t frame = 0; frame < 90; frame += 18)
        expect(batch_entry.Save({{1, frame}, 1, {}, {}}).ok(),
            "batch-entry checkpoint retains the shared 90-frame horizon");
    expect(landing.Save({{1, 90}, 1, {}, {}}).code
                == FailureCode::CapacityExceeded
            && batch_entry.Save({{1, 90}, 1, {}, {}}).code
                == FailureCode::CapacityExceeded,
        "both checkpoint roles exhaust at the same native-frame boundary");
}

void test_resimulation_base_planning_respects_batch_width()
{
    constexpr std::uint32_t maximum_batch_width = 12;
    constexpr std::uint64_t maximum_distance = 29;
    expect(PlanResimulationBase(std::nullopt, {1, 3},
            maximum_batch_width, maximum_distance)
            == ResimulationBaseAction::Capture,
        "first known native batch entry requires a base");
    expect(PlanResimulationBase(FrameCoordinate{1, 3}, {1, 20},
            maximum_batch_width, maximum_distance)
            == ResimulationBaseAction::Retain,
        "retain a base while the widest next batch remains within 29 frames");
    expect(PlanResimulationBase(FrameCoordinate{1, 3}, {1, 21},
            maximum_batch_width, maximum_distance)
            == ResimulationBaseAction::Capture,
        "capture before the widest next batch could exceed 29 frames");
    expect(PlanResimulationBase(FrameCoordinate{1, 21}, {2, 1},
            maximum_batch_width, maximum_distance)
            == ResimulationBaseAction::Capture,
        "a native generation change always requires a new base");
    expect(PlanResimulationBase(FrameCoordinate{2, 4}, {2, 3},
            maximum_batch_width, maximum_distance)
            == ResimulationBaseAction::Invalid,
        "a same-generation rewind is not a valid batch entry");
}

void test_owned_gekko_retention_boundary_fails_before_history_discard()
{
    expect(IsIdentityReplacementStatus(FailureCode::GenerationMismatch)
            && IsIdentityReplacementStatus(FailureCode::IdentityMismatch)
            && !IsIdentityReplacementStatus(FailureCode::CaptureFailed),
        "round-entry camera identity replacement defers only proven identity statuses");
    expect(PlanOwnedRoundReplacementGeneration(2, 2) == 3
            && PlanOwnedRoundReplacementGeneration(2, 3) == 3
            && !PlanOwnedRoundReplacementGeneration(3, 2).has_value(),
        "a lagging round marker assigns replacement generation 3 exactly once");
    constexpr FrameCoordinate baseline{7, 4830};
    constexpr FrameCoordinate current{7, 4920};
    const auto anchor = PlanGekkoStateCoordinate(baseline, 77);
    expect(anchor == FrameCoordinate{7, 4908},
        "canary-43 Gekko frame 77 maps to canonical coordinate 4908");
    expect(PlanGekkoStateCoordinate(baseline, -1) == baseline
            && !PlanGekkoStateCoordinate(baseline, -2).has_value(),
        "Gekko baseline and invalid negative frame mapping are exact");

    CanonicalHashTimeline timeline{128};
    for (std::uint64_t frame = baseline.frame; frame <= current.frame; ++frame)
    {
        CanonicalHash hash{};
        hash[0] = static_cast<std::byte>(frame & 0xffu);
        expect(timeline.Append({baseline.generation, frame}, hash,
                   {}, {}, {}, {}, {}, {}, {}, {}, {}).ok(),
            "retain every canonical coordinate through the depth-12 boundary");
    }
    const auto saved = timeline.GetExact(*anchor);
    expect(saved.has_value(),
        "frame-77 state exists when the confirmed anchor is saved");
    expect(!CanRebaselineOnlineTimeline(true),
        "owned identity drift cannot discard live Gekko history");
    expect(timeline.GetExact(*anchor).has_value()
            && timeline.GetExact(*anchor)->hash == saved->hash,
        "rejected owned rebaseline preserves frame-77 canonical evidence");
    expect(CanRebaselineOnlineTimeline(false),
        "preownership identity drift remains eligible for clean rebaseline");

    expect(!CanCommitDeferredOnlineRebaseline(
                true, true, true, true)
            && !CanCommitDeferredOnlineRebaseline(
                true, false, true, false)
            && !CanCommitDeferredOnlineRebaseline(
                true, false, false, true),
        "frame-360 replacement cannot discard history before Gekko release, "
        "bilateral round acknowledgement, and the completed outer-tick boundary");
    expect(CanCommitDeferredOnlineRebaseline(
                true, false, true, true),
        "frame-360 replacement commits only after the retired generation is sealed");

    expect(CanReviseObservedRemoteInput(
               true, false, std::optional<std::size_t>{1}, 1)
            && CanReviseObservedRemoteInput(
               true, true, std::nullopt, 1),
        "staged Gekko knowledge and the explicit offline probe may revise an "
        "already observed remote row");
    expect(!CanReviseObservedRemoteInput(
               true, false, std::nullopt, 1)
            && !CanReviseObservedRemoteInput(
               true, false, std::optional<std::size_t>{0}, 1),
        "authored rows and the non-predicted player remain immutable");
}

void test_batch_aware_replay_seek_planning()
{
    NativeBatchTimeline batches{4, 8};
    NativeBatchEnvelope first{};
    first.batch_id = 1;
    first.entry_coordinate = {1, 0};
    first.exit_coordinate = {1, 3};
    first.coordinate_count = 3;
    const std::array first_coordinates{
        FrameCoordinate{1, 1}, FrameCoordinate{1, 2}, FrameCoordinate{1, 3}};
    expect(batches.Append(first, first_coordinates).ok(),
        "append multi-coordinate seek batch");

    NativeBatchEnvelope second{};
    second.batch_id = 2;
    second.entry_coordinate = {1, 3};
    second.exit_coordinate = {1, 4};
    second.coordinate_count = 1;
    const std::array second_coordinates{FrameCoordinate{1, 4}};
    expect(batches.Append(second, second_coordinates).ok(),
        "append batch after seek landing boundary");
    expect(batches.GetBatchCoordinate(0, 0) != nullptr
            && batches.GetBatchCoordinate(0, 0)->coordinate
                == FrameCoordinate{1, 1}
            && batches.GetBatchCoordinate(0, 2) != nullptr
            && batches.GetBatchCoordinate(0, 2)->coordinate
                == FrameCoordinate{1, 3}
            && batches.GetBatchCoordinate(0, 3) == nullptr,
        "batch-coordinate lookup is exact and bounded");

    SnapshotStore entries{1024 * 1024, 8, CapacityPolicy::RejectNew};
    expect(entries.Save({{1, 0}, 1, {}, {}}).ok(),
        "save generation baseline batch entry");
    ReplaySeekPlan plan{};
    expect(PlanReplaySeek({1, 2}, batches, entries, 29, plan).ok(),
        "plan a seek landing inside a multi-coordinate batch");
    expect(plan.resimulation_base == FrameCoordinate{1, 0}
            && plan.first_batch_index == 0
            && plan.landing_batch_index == 0
            && plan.landing_offset_in_batch == 1
            && plan.coordinates_after_landing == 1
            && plan.resimulation_coordinates == 2
            && plan.landing_requires_batch_replay,
        "mid-batch plan preserves its base, landing offset, and batch tail");

    expect(entries.Save({{1, 3}, 1, {}, {}}).ok(),
        "save exact batch-entry boundary");
    expect(PlanReplaySeek({1, 3}, batches, entries, 29, plan).ok(),
        "plan an exact batch-boundary seek");
    expect(plan.resimulation_base == FrameCoordinate{1, 0}
            && plan.first_batch_index == 0
            && plan.landing_batch_index == 0
            && plan.landing_offset_in_batch == 2
            && plan.resimulation_coordinates == 3
            && plan.landing_requires_batch_replay,
        "exact entry checkpoint is not mistaken for the earlier canonical "
        "fencepost at the same coordinate");

    SnapshotStore missing{1024 * 1024, 8, CapacityPolicy::RejectNew};
    expect(PlanReplaySeek({1, 2}, batches, missing, 29, plan).code
            == FailureCode::MissingSnapshot,
        "seek before the first captured base fails without mutation");
    expect(entries.Save({{2, 0}, 1, {}, {}}).ok(),
        "retain a separate generation entry");
    expect(PlanReplaySeek({1, 2}, batches, entries, 1, plan).code
            == FailureCode::AdapterUnqualified,
        "seek planning fails closed beyond the reconstruction bound");

    ReplayCorrectionPlan correction{};
    expect(PlanReplayCorrection(
            {1, 2}, {1, 4}, batches, entries, 29, correction).ok(),
        "plan a correction through the current completed native batch");
    expect(correction.resimulation_base == FrameCoordinate{1, 0}
            && correction.first_batch_index == 0
            && correction.final_batch_index == 1
            && correction.resimulation_coordinates == 4,
        "correction starts before the earliest changed coordinate and replays to now");
    expect(PlanReplayCorrection(
            {1, 4}, {1, 4}, batches, entries, 29, correction).ok(),
        "plan a depth-one correction from an exact prior batch boundary");
    expect(correction.resimulation_base == FrameCoordinate{1, 3}
            && correction.first_batch_index == 1
            && correction.final_batch_index == 1
            && correction.resimulation_coordinates == 1,
        "depth-one correction does not restore the already-changed landing image");
    expect(PlanReplayCorrection(
            {1, 2}, {1, 2}, batches, entries, 29, correction).code
            == FailureCode::IdentityMismatch,
        "correction rejects a current coordinate inside an unfinished native batch");
    expect(PlanReplayCorrection(
            {1, 2}, {1, 4}, batches, entries, 3, correction).code
            == FailureCode::AdapterUnqualified,
        "correction fails closed when replay-to-now exceeds its bound");
    expect(PlanReplayCorrection(
            {2, 1}, {1, 4}, batches, entries, 29, correction).code
            == FailureCode::InvalidConfiguration,
        "correction never crosses native generations");

    // Section 5.2 takeover invariant: each peer retains its own local
    // pre-ownership history.  A delayed confirmation of the first prefix
    // input must remain reconstructible at the maximum supported distance;
    // no authority snapshot transfer is permitted to fill this store.
    NativeBatchTimeline prefix_batches{64, 64};
    SnapshotStore prefix_entries{
        1024 * 1024, 64, CapacityPolicy::RejectNew};
    constexpr FrameCoordinate baseline{7, 123};
    std::optional<FrameCoordinate> previous_base{FrameCoordinate{7, 111}};
    std::optional<FrameCoordinate> required_base{baseline};
    expect(prefix_entries.Save({*previous_base, 1, {}, {}}).ok(),
        "retain the preceding periodic checkpoint");
    for (std::uint64_t offset = 0; offset < 29; ++offset)
    {
        const FrameCoordinate entry{baseline.generation,
            baseline.frame + offset};
        const FrameCoordinate exit{baseline.generation,
            baseline.frame + offset + 1};
        const auto action = PlanResimulationBase(previous_base, entry,
            12, 29, required_base);
        expect(action != ResimulationBaseAction::Invalid,
            "online prefix history planning remains monotonic");
        if (action == ResimulationBaseAction::Capture)
        {
            expect(prefix_entries.Save({entry, 1, {}, {}}).ok(),
                "retain an independent local online prefix base");
            previous_base = entry;
            if (required_base.has_value() && entry == *required_base)
                required_base.reset();
        }
        NativeBatchEnvelope batch{};
        batch.batch_id = offset + 1;
        batch.entry_coordinate = entry;
        batch.exit_coordinate = exit;
        batch.coordinate_count = 1;
        const std::array coordinates{exit};
        expect(prefix_batches.Append(batch, coordinates).ok(),
            "append independently observed online prefix batch");
    }
    expect(PlanReplayCorrection({7, 124}, {7, 152}, prefix_batches,
            prefix_entries, 29, correction).ok()
            && correction.resimulation_base == baseline
            && correction.resimulation_coordinates == 29,
        "oldest prefix correction uses the local baseline-entry history at "
        "the exact depth-29 bound");
    SnapshotStore periodic_only{
        1024 * 1024, 64, CapacityPolicy::RejectNew};
    expect(periodic_only.Save({{7, 111}, 1, {}, {}}).ok()
            && PlanReplayCorrection({7, 124}, {7, 152}, prefix_batches,
                periodic_only, 29, correction).code
                == FailureCode::MissingSnapshot,
        "periodic prewarming alone cannot retain the negotiated baseline at "
        "the frame-152 correction horizon");
    SnapshotStore absent_prefix_history{
        1024 * 1024, 64, CapacityPolicy::RejectNew};
    expect(PlanReplayCorrection({7, 124}, {7, 152}, prefix_batches,
            absent_prefix_history, 29, correction).code
            == FailureCode::MissingSnapshot,
        "online correction fails closed when local prefix history was not "
        "captured instead of accepting a peer snapshot");

    // Canary 50 reproduced an asymmetric catch-up: the sandbox caught up at
    // 125, while the host did not finish until 206.  Once Gekko confirms the
    // entire prefix, frame 124 is no longer a correction candidate.  Asking
    // the takeover preflight to replay that immutable prefix incorrectly
    // exceeds the 29-frame bound even though the current boundary has a
    // valid independently captured local base.
    NativeBatchTimeline delayed_prefix_batches{128, 128};
    SnapshotStore delayed_prefix_entries{
        1024 * 1024, 128, CapacityPolicy::RejectNew};
    previous_base = std::nullopt;
    required_base = baseline;
    for (std::uint64_t offset = 0; offset < 83; ++offset)
    {
        const FrameCoordinate entry{baseline.generation,
            baseline.frame + offset};
        const FrameCoordinate exit{baseline.generation,
            baseline.frame + offset + 1};
        const auto action = PlanResimulationBase(previous_base, entry,
            12, 29, required_base);
        expect(action != ResimulationBaseAction::Invalid,
            "delayed prefix base planning remains monotonic");
        if (action == ResimulationBaseAction::Capture)
        {
            expect(delayed_prefix_entries.Save({entry, 1, {}, {}}).ok(),
                "retain delayed prefix checkpoint");
            previous_base = entry;
            if (required_base.has_value() && entry == *required_base)
                required_base.reset();
        }
        NativeBatchEnvelope batch{};
        batch.batch_id = offset + 1;
        batch.entry_coordinate = entry;
        batch.exit_coordinate = exit;
        batch.coordinate_count = 1;
        const std::array coordinates{exit};
        expect(delayed_prefix_batches.Append(batch, coordinates).ok(),
            "append delayed online prefix batch");
    }
    constexpr FrameCoordinate delayed_current{7, 206};
    expect(PlanReplayCorrection({7, 124}, delayed_current,
            delayed_prefix_batches, delayed_prefix_entries, 29, correction).code
            == FailureCode::AdapterUnqualified,
        "first confirmed prefix frame is not a valid delayed takeover preflight");
    expect(PlanReplayCorrection(delayed_current, delayed_current,
            delayed_prefix_batches, delayed_prefix_entries, 29, correction).ok()
            && correction.resimulation_coordinates <= 29,
        "delayed takeover preflights the current completed boundary within "
        "the retained correction horizon");
}

void test_presentation_exactly_once()
{
    PresentationJournal journal{4, 64};
    CountingSink sink;
    PresentationEvent event{};
    event.coordinate = {1, 4};
    event.source_ordinal = 1;
    event.kind = 3;
    event.identity = 44;
    event.payload_size = 1;
    event.payload[0] = std::byte{1};
    expect(journal.Record(event).ok(), "record presentation event");
    expect(journal.Record(event).ok(), "deduplicate pending presentation event");
    expect(journal.CommitThrough({1, 4}, sink).ok(), "commit presentation event");
    expect(journal.Record(event).ok(), "deduplicate committed presentation event");
    expect(journal.CommitThrough({1, 4}, sink).ok(), "repeat presentation commit");
    expect(sink.count == 1, "presentation published exactly once");

    PresentationJournal bounded{1, 64};
    CountingSink bounded_sink;
    for (std::uint64_t frame = 0; frame < 100; ++frame)
    {
        event.coordinate.frame = frame;
        event.identity = frame + 1;
        expect(bounded.Record(event).ok(), "record after prior event committed");
        expect(bounded.CommitThrough(event.coordinate, bounded_sink).ok(), "commit bounded event");
    }
    expect(bounded_sink.count == 100, "committed-event dedup metadata stays bounded");
}

void test_native_audio_presentation_preserves_cross_family_order()
{
    NativeBatchEnvelope batch{};
    batch.entry_coordinate = {4, 100};
    batch.exit_coordinate = {4, 102};
    batch.audio_terminal_calls = 2;
    batch.audio_terminal_journal_count = 2;
    batch.audio_terminal_journal[0] = {AudioTerminalOperation::Create,
        {AudioOwnerDomain::BattleSharedPlayer, 0, 0},
        MakeLogicalAudioPlaybackId(101, 0), 3, 19, 0};
    batch.audio_terminal_journal[1] = {AudioTerminalOperation::StopOne,
        {AudioOwnerDomain::BattleSharedPlayer, 0, 0},
        MakeLogicalAudioPlaybackId(101, 0), 0, -1, 1};
    batch.battle_audio_blueprint_calls = 1;
    batch.battle_audio_blueprint_journal_count = 1;
    batch.battle_audio_blueprint_journal[0].handler_slot = 2;
    batch.battle_audio_blueprint_journal[0].direct = 1;
    batch.battle_audio_blueprint_journal[0].semantic[7] = std::byte{0x51};
    batch.presentation_order_journal_count = 4;
    batch.presentation_order_journal[0] = {
        PresentationEventFamily::BattleAudioSource, 0, 0};
    batch.presentation_order_journal[1] = {
        PresentationEventFamily::AudioTerminal, 0, 1};
    batch.presentation_order_journal[2] = {
        PresentationEventFamily::BattleAudioBlueprint, 0, 1};
    batch.presentation_order_journal[3] = {
        PresentationEventFamily::AudioTerminal, 1, 2};

    PresentationJournal journal{8, 512};
    expect(RecordNativeAudioPresentation(batch, journal).ok()
            && journal.pending_count() == 3,
        "native audio terminals retain source frames and cross-family ordinals");

    NativeBatchEnvelope invalid = batch;
    invalid.presentation_order_journal[3].family_index = 0;
    PresentationJournal rejected{8, 512};
    expect(RecordNativeAudioPresentation(invalid, rejected).code
            == FailureCode::ProtocolMismatch
            && rejected.pending_count() == 0,
        "duplicate native terminal identities fail before journal mutation");

    NativeBatchEnvelope transition = batch;
    transition.exit_coordinate.generation = 5;
    std::array<PresentationEvent, 4> transition_events{};
    std::size_t transition_count{};
    expect(BuildNativeAudioPresentation(
            transition, transition_events, transition_count).ok()
            && transition_count == 3
            && transition_events[0].coordinate == FrameCoordinate{4, 101}
            && transition_events[1].coordinate == FrameCoordinate{4, 101}
            && transition_events[2].coordinate == FrameCoordinate{5, 102},
        "round-fencepost batches assign source events to the observed generation split");
}

void test_native_audio_presentation_correction_is_atomic()
{
    NativeBatchEnvelope original{};
    original.entry_coordinate = {4, 100};
    original.exit_coordinate = {4, 102};
    original.audio_terminal_calls = 2;
    original.audio_terminal_journal_count = 2;
    original.audio_terminal_journal[0] = {AudioTerminalOperation::Create,
        {AudioOwnerDomain::BattleSharedPlayer, 0, 0},
        MakeLogicalAudioPlaybackId(101, 0), 3, 19, 0};
    original.audio_terminal_journal[1] = {AudioTerminalOperation::StopOne,
        {AudioOwnerDomain::BattleSharedPlayer, 0, 0},
        MakeLogicalAudioPlaybackId(101, 0), 0, -1, 1};
    original.presentation_order_journal_count = 2;
    original.presentation_order_journal[0] = {
        PresentationEventFamily::AudioTerminal, 0, 1};
    original.presentation_order_journal[1] = {
        PresentationEventFamily::AudioTerminal, 1, 2};

    NativeAudioPresentationController controller{8, 512, 8};
    expect(controller.BeginGeneration(4).ok()
            && controller.RecordSpeculative(original).ok(),
        "audio controller records a bounded speculative native batch");
    NativeBatchEnvelope corrected = original;
    corrected.audio_terminal_journal[1] = {AudioTerminalOperation::StopAll,
        {AudioOwnerDomain::BattleSharedPlayer, 0, 0},
        audio_invalid_playback_id, 0, -1, 1};
    const std::array corrected_batches{corrected};
    expect(controller.ReplaceCorrected({4, 102}, corrected_batches).ok()
            && controller.pending_count() == 2,
        "audio correction retains the prefix and atomically replaces its suffix");
    DecodingAudioSink sink;
    expect(controller.CommitThrough({4, 102}, sink).ok()
            && sink.terminals.size() == 1
            && sink.terminals[0] == corrected.audio_terminal_journal[1],
        "confirmation reuses unchanged speculative terminals and publishes only the corrected suffix");
    controller.EndGeneration();
    expect(controller.generation() == 0 && controller.pending_count() == 0,
        "audio presentation lifecycle invalidates all generation-bound values");

    NativeBatchEnvelope transition = original;
    transition.exit_coordinate = {5, 102};
    NativeAudioPresentationController transition_controller{8, 512, 8};
    expect(transition_controller.BeginGeneration(4).ok()
            && transition_controller.RecordSpeculative(transition).ok()
            && transition_controller.pending_count() == 2
            && transition_controller.BeginGeneration(5).ok()
            && transition_controller.pending_count() == 1,
        "audio generation transition removes retired events and retains replacement-generation events");
    DecodingAudioSink transition_sink;
    expect(transition_controller.CommitThrough({5, 102}, transition_sink).ok()
            && transition_sink.terminals.empty()
            && transition_controller.pending_count() == 0
            && transition_controller.statistics().committed == 1,
        "replacement-generation commit confirms the already-presented retained side of a fencepost batch");
}

void test_callsite_qualified_particle_values()
{
    ParticlePresentationValue create{};
    create.coordinate = {1, 40};
    create.source_ordinal = 1;
    create.route = ParticleRoute::BarrierHit;
    create.operation = ParticleOperation::Create;
    create.owner_logical_id = 17;
    create.asset_logical_id = 29;
    create.event_logical_id = 37;
    create.effect_logical_id = 41;
    create.location = {1.0f, 2.0f, 3.0f};
    create.rotation_degrees = {4.0f, 5.0f, 6.0f};
    create.scale = {1.0f, 1.0f, 1.0f};
    create.auto_activate = true;

    PresentationEvent encoded{};
    expect(EncodeParticlePresentation(create, encoded).ok(),
        "encode a qualified static particle route");
    expect(encoded.payload_size == Schema::particle_presentation_payload_size
            && encoded.identity == create.event_logical_id,
        "particle encoding uses the generated schema and logical identity");
    ParticlePresentationValue decoded{};
    expect(DecodeParticlePresentation(encoded, decoded).ok() && decoded == create,
        "particle value round-trips without native pointers");
    const PresentationEvent create_event = encoded;

    PresentationEvent dynamic_route = encoded;
    dynamic_route.payload[2] = std::byte{4};
    expect(DecodeParticlePresentation(dynamic_route, decoded).code
            == FailureCode::ProtocolMismatch,
        "dynamic Blueprint particle routes fail closed");

    ParticlePresentationValue stop{};
    stop.coordinate = create.coordinate;
    stop.source_ordinal = 2;
    stop.route = ParticleRoute::BarrierHit;
    stop.operation = ParticleOperation::Stop;
    stop.owner_logical_id = 17;
    stop.event_logical_id = 43;
    stop.effect_logical_id = 41;
    expect(EncodeParticlePresentation(stop, encoded).ok(),
        "encode a canonical value-only stop");
    stop.asset_logical_id = 29;
    expect(EncodeParticlePresentation(stop, encoded).code
            == FailureCode::InvalidConfiguration,
        "stop events reject stale create-only values");
    stop.asset_logical_id = 0;
    expect(EncodeParticlePresentation(stop, encoded).ok(),
        "restore canonical stop after invalid input");

    PresentationJournal journal{8, 1024};
    CountingSink sink;
    expect(journal.Record(create_event).ok() && journal.Record(encoded).ok()
            && journal.Record(create_event).ok() && journal.Record(encoded).ok(),
        "journal retains distinct same-coordinate operations and deduplicates repeats");
    expect(journal.CommitThrough(stop.coordinate, sink).ok() && sink.count == 2,
        "particle operations commit exactly once");
}

void test_replay_checkpoint_seek_and_resume()
{
    Fixture fixture;
    ReplayCoordinator replay{fixture.simulation};
    expect(replay.Begin(context(), {1, 0}, 0, 7001).ok(), "begin replay capture");
    for (std::uint64_t frame = 0; frame < 35; ++frame)
    {
        expect(replay.RecordAndAdvance({1, frame}, one_input()).ok(), "record replay frame");
    }
    expect(fixture.adapter.value == 35, "normal replay advanced 35 frames");
    expect(replay.FinishCapture().ok(), "finish replay capture");
    expect(replay.Seek({1, 32}).ok(), "seek from nearest checkpoint");
    expect(fixture.adapter.value == 32, "seek resimulates exact state");
    expect(fixture.adapter.reconcile_count == 1, "seek reconciles presentation once");
    expect(replay.Resume().ok(), "resume after seek");
    expect(replay.RecordAndAdvance({1, 32}, one_input()).ok(), "advance after seek");
    expect(fixture.adapter.value == 33, "resumed replay advances normally");
    expect(replay.captured_end() == FrameCoordinate{1, 35}, "seek does not truncate capture extent");
    expect(replay.Seek({1, 35}).ok(), "seek forward within preserved capture extent");
    expect(fixture.adapter.value == 35, "forward seek lands at preserved capture end");
}

void test_cross_generation_seek_materializes_before_restore()
{
    Fixture fixture;
    FakeGenerationMaterializer materializer;
    ReplayCoordinator replay{fixture.simulation, &materializer};
    expect(replay.Begin(context(), {1, 0}, 0, 7001).ok(), "begin generation one");
    for (std::uint64_t frame = 0; frame < 5; ++frame)
    {
        expect(replay.RecordAndAdvance({1, frame}, one_input()).ok(), "record generation one");
    }
    expect(
        replay.BeginGeneration(second_context(), {2, 0}, 1, 7002).ok(),
        "capture generation two baseline after native round transition");
    for (std::uint64_t frame = 0; frame < 3; ++frame)
    {
        expect(replay.RecordAndAdvance({2, frame}, one_input()).ok(), "record generation two");
    }
    expect(replay.FinishCapture().ok(), "finish multi-generation capture");

    expect(replay.Seek({1, 4}).ok(), "request cross-generation seek");
    expect(replay.state() == ReplayState::Seeking, "seek waits for native materialization");
    expect(materializer.request_count == 1, "native round image requested once");
    expect(replay.PollSeek().ok(), "poll pending materialization");
    expect(replay.state() == ReplayState::Seeking, "pending poll cannot restore early");
    expect(fixture.adapter.value == 8, "pending materialization cannot mutate gameplay state");
    materializer.ready = true;
    expect(replay.PollSeek().ok(), "consume completed native materialization");
    expect(replay.state() == ReplayState::Resuming, "completed materialization reaches resume");
    expect(fixture.adapter.generation == 1, "adapter rebound to target native generation");
    expect(fixture.adapter.value == 4, "target generation restored and resimulated exactly");
    expect(replay.Resume().ok(), "resume cross-generation seek");

    expect(replay.Seek({2, 2}).ok(), "request forward cross-generation seek");
    materializer.ready = true;
    expect(replay.PollSeek().ok(), "complete forward native materialization");
    expect(fixture.adapter.generation == 2, "adapter rebound to second generation");
    expect(fixture.adapter.value == 7, "second generation baseline and inputs retained");
}

void test_cross_generation_identity_mismatch_fails_before_restore()
{
    Fixture fixture;
    FakeGenerationMaterializer materializer;
    ReplayCoordinator replay{fixture.simulation, &materializer};
    expect(replay.Begin(context(), {1, 0}, 0, 7001).ok(), "begin mismatch generation one");
    expect(replay.RecordAndAdvance({1, 0}, one_input()).ok(), "record mismatch generation one");
    expect(
        replay.BeginGeneration(second_context(), {2, 0}, 1, 7002).ok(),
        "begin mismatch generation two");
    expect(replay.RecordAndAdvance({2, 0}, one_input()).ok(), "record mismatch generation two");
    expect(replay.FinishCapture().ok(), "finish mismatch capture");
    const int value_before_seek = fixture.adapter.value;
    expect(replay.Seek({1, 1}).ok(), "request mismatch cross-generation seek");
    materializer.corrupt_identity = true;
    materializer.ready = true;
    expect(
        replay.PollSeek().code == FailureCode::IdentityMismatch,
        "reject mismatched materialized identities");
    expect(replay.state() == ReplayState::Failed, "identity mismatch is terminal");
    expect(fixture.adapter.value == value_before_seek, "identity mismatch performs no restore write");
    expect(!materializer.requested.has_value(), "failed seek cancels native materializer");
}

void test_native_replay_materializer_requires_state4_fencepost()
{
    FakeReplayNativeBridge bridge;
    bridge.view.context = context();
    bridge.view.context.generation = 0;
    bridge.view.replay_player_identity = 9001;
    bridge.view.round_image_identity = 7001;
    bridge.view.round_count = 2;
    bridge.view.round_capacity = 2;
    bridge.view.manager_status = 2;
    bridge.view.replay_enabled = true;

    NativeReplayMaterializer materializer{bridge};
    const ReplayGenerationTarget target{context(), {1, 0}, 0, 7001};
    expect(materializer.Preflight(target).ok(), "preflight native replay round image");
    expect(materializer.Request(target).ok(), "request native state-4 round reset");
    expect(bridge.request_count == 1, "native round reset requested exactly once");
    expect(!materializer.Poll().has_value(), "state 4 is not a completion fencepost");
    expect(
        materializer.TerminalFailure() == FailureCode::None,
        "waiting state 4 is nonterminal");
    bridge.CompleteFence();
    const auto completed = materializer.Poll();
    expect(completed.has_value(), "publish after state-4 callback cleanup fencepost");
    expect(completed->context == context(), "publish exact expected native identities");

    materializer.Cancel();
    bridge.view.round_image_identity = 8001;
    expect(
        materializer.Preflight(target).code == FailureCode::IdentityMismatch,
        "reject changed native round image before mutation");
    expect(bridge.request_count == 1, "failed image preflight performs no native request");
}

void test_sc6_replay_bridge_transaction_and_undo()
{
    RawReplayBridgeFixture fixture;
    Sc6ReplayNativeBridge bridge{fixture.resolvers()};
    ReplayNativeRoundView view;
    expect(bridge.InspectRound(1, view).ok(), "inspect bounded SC6 replay round image");
    expect(view.replay_enabled, "inspect native replay enable state");
    expect(view.round_count == 2 && view.round_capacity == 2, "inspect round array bounds");

    const auto destination = fixture.manager.data()
        + Schema::Sc6ReplayLayout::manager_round_image;
    const auto source = fixture.round_images.data() + Schema::replay_round_image_size;
    expect(
        bridge.RequestRoundReset(1, view.round_image_identity).ok(),
        "transactionally copy round image and request state 4");
    expect(
        std::memcmp(destination, source, Schema::replay_round_image_size) == 0,
        "native bridge copies exactly one 0xc0 image");
    std::uint8_t state{};
    std::memcpy(
        &state,
        fixture.manager.data() + Schema::Sc6ReplayLayout::manager_move_state,
        sizeof(state));
    expect(state == 4, "native bridge publishes move state only after image copy");

    RawReplayBridgeFixture rejected;
    Sc6ReplayNativeBridge rejected_bridge{rejected.resolvers()};
    const auto before_reject = rejected.manager;
    expect(
        rejected_bridge.RequestRoundReset(0, view.round_image_identity + 1).code
            == FailureCode::RestorePreflightFailed,
        "reject wrong round-image identity before mutation");
    expect(rejected.manager == before_reject, "failed bridge preflight is a zero mutation");

    RawReplayBridgeFixture ignored;
    ignored.setter_mode = RawReplayBridgeFixture::SetterMode::Ignore;
    Sc6ReplayNativeBridge ignored_bridge{ignored.resolvers()};
    ReplayNativeRoundView ignored_view;
    expect(ignored_bridge.InspectRound(0, ignored_view).ok(), "inspect ignored-setter case");
    const auto ignored_before = ignored.manager;
    expect(
        ignored_bridge.RequestRoundReset(0, ignored_view.round_image_identity).code
            == FailureCode::RestoreVerificationFailed,
        "setter verification failure returns typed failure");
    expect(ignored.manager == ignored_before, "setter verification failure restores exact undo");

    RawReplayBridgeFixture corrupt;
    corrupt.setter_mode = RawReplayBridgeFixture::SetterMode::Corrupt;
    Sc6ReplayNativeBridge corrupt_bridge{corrupt.resolvers()};
    ReplayNativeRoundView corrupt_view;
    expect(corrupt_bridge.InspectRound(0, corrupt_view).ok(), "inspect corrupt-setter case");
    expect(
        corrupt_bridge.RequestRoundReset(0, corrupt_view.round_image_identity).code
            == FailureCode::UndoFailed,
        "failed state undo is terminal and typed");
}

void test_transactional_restore_failures_undo()
{
    for (const AdapterFailure phase : {
             AdapterFailure::RestorePreflight,
             AdapterFailure::CapturePreflight,
             AdapterFailure::Capture,
             AdapterFailure::RestoreWrite,
             AdapterFailure::Repair,
             AdapterFailure::Verify})
    {
        Fixture fixture;
        expect(fixture.simulation.BindAndCaptureBaseline(context(), {1, 0}).ok(), "bind restore fixture");
        for (std::uint64_t frame = 0; frame < 10; ++frame)
        {
            expect(fixture.simulation.Advance({1, frame}, one_input()).ok(), "advance restore fixture");
        }
        expect(fixture.simulation.CaptureCheckpoint({1, 10}).ok(), "capture restore target");
        fixture.adapter.value = 20;
        fixture.adapter.failure = phase;
        const Status restored = fixture.simulation.RestoreAndResimulate({1, 10}, {1, 10});
        expect(!restored.ok(), "injected restore phase fails");
        expect(fixture.adapter.value == 20, "failed restore returns exact undo image");
    }

    Fixture fixture;
    expect(fixture.simulation.BindAndCaptureBaseline(context(), {1, 0}).ok(), "bind undo-failure fixture");
    for (std::uint64_t frame = 0; frame < 2; ++frame)
    {
        expect(fixture.simulation.Advance({1, frame}, one_input()).ok(), "advance undo-failure fixture");
    }
    expect(fixture.simulation.CaptureCheckpoint({1, 2}).ok(), "capture undo-failure target");
    fixture.adapter.value = 8;
    fixture.adapter.failure = AdapterFailure::Repair;
    fixture.adapter.fail_undo_restore = true;
    expect(
        fixture.simulation.RestoreAndResimulate({1, 2}, {1, 2}).code == FailureCode::UndoFailed,
        "failed undo is terminal and typed");
}

void test_floating_point_environment_capture_is_raw_and_non_mutating()
{
    const auto original = CaptureFloatingPointEnvironment();
    const auto original_mxcsr = _mm_getcsr();
    const auto next_rounding = (original_mxcsr + 0x2000u) & 0x6000u;
    _mm_setcsr((original_mxcsr & ~0x6000u) | next_rounding);
    const auto changed = CaptureFloatingPointEnvironment();
    _mm_setcsr(original_mxcsr);
    const auto restored = CaptureFloatingPointEnvironment();

    expect(!FloatingPointControlMatches(original, changed),
        "FP capture distinguishes MXCSR control changes");
    expect(FloatingPointX87StatusMatches(original, changed),
        "MXCSR control changes do not masquerade as x87 status changes");
    expect(FloatingPointControlMatches(original, restored)
            && FloatingPointStatusMatches(original, restored),
        "read-only FP capture preserves and recovers exact caller environment");

    ScopedFloatingPointEnvironment scope;
    _mm_setcsr(original_mxcsr ^ 0x01u);
    expect(scope.Finish().ok(),
        "scoped FP environment restores MXCSR sticky status exactly");
    expect(CaptureFloatingPointEnvironment() == original,
        "scoped FP restoration returns the complete caller environment");
}

void test_ucrt_broker_is_callsite_and_thread_bound()
{
    constexpr unsigned seed = 0x12345678u;
    std::array<int, 4> expected{};
    std::srand(seed);
    for (auto& value : expected) value = std::rand();

    UcrtRandBroker broker;
    expect(broker.Start().ok(), "start UCRT broker before its native stream exists");
    broker.HandleSrand(77, Schema::Sc6UcrtLayout::rng_init_srand_return_rva,
        seed, &std::srand);
    expect(broker.owner_thread_id() == 77,
        "allowlisted native seed binds the broker's simulation thread");
    expect(broker.AcquireOwnership(77).ok(),
        "UCRT broker acquires only after an allowlisted seed");
    expect(broker.EnsureOwnership(77).ok(),
        "UCRT ownership transition is idempotent for its owner");
    expect(broker.EnsureOwnership(78).code == FailureCode::WrongThread,
        "UCRT ownership transition rejects a different thread");
    for (const auto value : expected)
    {
        expect(broker.HandleRand(77,
                Schema::Sc6UcrtLayout::movevm_rand_return_rva, &std::rand)
                == value,
            "private UCRT algorithm matches the imported CRT sequence");
    }

    UcrtRandBrokerImage saved{};
    expect(broker.Capture(77, saved).ok() && saved.draws == expected.size(),
        "capture value-only UCRT state and draw count");
    const int advanced = broker.HandleRand(77,
        Schema::Sc6UcrtLayout::movevm_rand_return_rva, &std::rand);
    expect(broker.Restore(77, saved).ok()
            && broker.HandleRand(77,
                Schema::Sc6UcrtLayout::movevm_rand_return_rva, &std::rand)
                == advanced,
        "restored private UCRT state reproduces the exact next draw");
    expect(broker.ReleaseOwnership(77).ok()
            && broker.mode() == UcrtRandBrokerMode::Observing,
        "qualification cleanup releases UCRT ownership without disabling observation");
    expect(broker.EnsureOwnership(77).ok()
            && broker.mode() == UcrtRandBrokerMode::Owned,
        "a later qualification cycle can reacquire the same synchronized stream");

    std::srand(seed);
    const int forwarded = broker.HandleRand(99, 0x1111, &std::rand);
    std::srand(seed);
    expect(forwarded == std::rand() && broker.mode() == UcrtRandBrokerMode::Owned,
        "non-allowlisted calls on other threads forward without broker mutation");
    broker.HandleRand(99, Schema::Sc6UcrtLayout::movevm_rand_return_rva,
        &std::rand);
    expect(broker.mode() == UcrtRandBrokerMode::Failed
            && broker.failure() == FailureCode::WrongThread,
        "allowlisted callsite migration fails the broker terminally");
}

void test_audio_presentation_identities_are_epoch_bound()
{
    AudioOwnerResolver owners;
    AudioPlaybackMap playback;
    const AudioOwnerSelector class_player{
        AudioOwnerDomain::BattleClassPlayer, 3, 0};
    const AudioOwnerSelector shared{
        AudioOwnerDomain::BattleSharedPlayer, 0, 0};
    expect(owners.BeginEpoch(7) && owners.Bind(7, 0x1000, class_player)
            && owners.Bind(7, 0x2000, shared) && owners.Seal(7),
        "audio owner resolver seals one bounded lifecycle graph");
    AudioOwnerSelector resolved{};
    std::uintptr_t owner{};
    expect(owners.Resolve(7, 0x1000, resolved) && resolved == class_player
            && owners.ResolveOwner(7, shared, owner) && owner == 0x2000,
        "audio owner resolver maps pointers only inside the current epoch");
    expect(!owners.Resolve(8, 0x1000, resolved)
            && !owners.Bind(7, 0x3000, {
                AudioOwnerDomain::BattleClassPlayer, 4, 0}),
        "audio owner resolver rejects stale epochs and post-seal mutation");
    AudioOwnerResolver aliases;
    expect(aliases.BeginEpoch(9)
            && aliases.Bind(9, 0x4000, class_player)
            && aliases.Bind(9, 0x4000, class_player)
            && !aliases.Bind(9, 0x4000, shared)
            && !aliases.Bind(9, 0x5000, class_player),
        "audio owner resolver accepts exact repeats but rejects pointer and selector aliases");

    const auto logical = MakeLogicalAudioPlaybackId(123, 2);
    const auto cue_family = MakeAudioCueFamilyIdentity(7);
    expect(IsAudioCueFamilyIdentity(cue_family)
            && AudioCueFamilyFromIdentity(cue_family) == 7
            && !IsAudioCueFamilyIdentity(7),
        "authored audio cue families are distinct from process-local CRI slots");
    expect(playback.BeginEpoch(7)
            && playback.Insert(7, class_player, logical, 0x1234),
        "audio playback map admits one logical-to-native binding");
    std::uint32_t mapped{};
    expect(playback.LogicalForNative(7, class_player, 0x1234, mapped)
            && mapped == logical
            && playback.NativeForLogical(7, class_player, logical, mapped)
            && mapped == 0x1234,
        "audio playback mapping is reversible for one stable owner");
    expect(!playback.Insert(7, class_player, logical, 0x1235)
            && !playback.LogicalForNative(8, class_player, 0x1234, mapped),
        "audio playback map rejects aliases and stale epochs");
    AudioOwnerResolver same;
    expect(same.BeginEpoch(8) && same.Bind(8, 0x2000, shared)
            && same.Bind(8, 0x1000, class_player) && same.Seal(8)
            && owners.SameBindings(same),
        "audio owner graph equality ignores epoch and insertion order");
    expect(playback.TransitionEpoch(7, 8,
                [&](AudioOwnerSelector selector) noexcept
                {
                    std::uintptr_t before{};
                    std::uintptr_t after{};
                    return owners.ResolveOwner(7, selector, before)
                        && same.ResolveOwner(8, selector, after)
                        && before == after;
                })
            && playback.NativeForLogical(
                8, class_player, logical, mapped)
            && mapped == 0x1234
            && !playback.NativeForLogical(
                7, class_player, logical, mapped),
        "audio playback map preserves only stable-owner mappings across a provenance epoch");
    AudioOwnerResolver changed;
    AudioPlaybackMap changed_playback;
    expect(changed.BeginEpoch(9)
            && changed.Bind(9, 0x3000, class_player) && changed.Seal(9)
            && changed_playback.BeginEpoch(8)
            && changed_playback.Insert(8, class_player, logical, 0x1234)
            && changed_playback.TransitionEpoch(8, 9,
                [&](AudioOwnerSelector selector) noexcept
                {
                    std::uintptr_t before{};
                    std::uintptr_t after{};
                    return same.ResolveOwner(8, selector, before)
                        && changed.ResolveOwner(9, selector, after)
                        && before == after;
                })
            && !changed_playback.NativeForLogical(
                9, class_player, logical, mapped),
        "audio playback map retires a mapping when its native owner changes");
    expect(playback.RemoveOne(8, class_player, logical)
            && !playback.NativeForLogical(8, class_player, logical, mapped),
        "audio playback map retires an exact stopped voice");
    const auto inactive_logical = MakeLogicalAudioPlaybackId(124, 0);
    const auto active_logical = MakeLogicalAudioPlaybackId(124, 1);
    expect(playback.Insert(8, class_player, inactive_logical, 0x1235)
            && playback.Insert(8, shared, active_logical, 0x1236)
            && playback.PruneInactive(8,
                [](AudioOwnerSelector, std::uint32_t native_id) noexcept
                { return native_id == 0x1236; }) == 1
            && !playback.NativeForLogical(
                8, class_player, inactive_logical, mapped)
            && playback.NativeForLogical(8, shared, active_logical, mapped)
            && mapped == 0x1236,
        "audio playback map prunes only native-lifecycle-inactive voices");

    AudioTerminalEvent terminal{};
    terminal.operation = AudioTerminalOperation::Create;
    terminal.owner = class_player;
    terminal.logical_playback_id = logical;
    terminal.cue_sheet_id = cue_family;
    terminal.cue_id = 77;
    terminal.value = 0x3f000000u;
    PresentationEvent encoded{};
    expect(EncodeAudioPresentation({7, 123}, 9, terminal, encoded).ok()
            && encoded.kind == Schema::audio_presentation_event_kind
            && encoded.payload_size == Schema::audio_presentation_payload_size,
        "audio terminal encodes as a bounded versioned presentation value");
    AudioTerminalEvent decoded{};
    expect(DecodeAudioPresentation(encoded, decoded).ok()
            && decoded == terminal,
        "audio presentation round-trips without native owner pointers");
    const auto identity = encoded.identity;
    expect(EncodeAudioPresentation({7, 123}, 10, terminal, encoded).ok()
            && encoded.identity != identity,
        "authored cross-family order contributes to stable audio identity");
    encoded.identity = identity;
    expect(DecodeAudioPresentation(encoded, decoded).code
            == FailureCode::ProtocolMismatch,
        "audio decoding rejects an identity copied across authored ordinals");

    AudioBlueprintPresentationValue blueprint{};
    blueprint.handler_slot = 2;
    blueprint.direct = true;
    blueprint.semantic[0] = std::byte{0x34};
    blueprint.semantic[23] = std::byte{0x91};
    expect(EncodeAudioBlueprintPresentation(
            {7, 123}, 11, blueprint, encoded).ok(),
        "audio Blueprint publication encodes as a pointer-free value");
    AudioBlueprintPresentationValue decoded_blueprint{};
    expect(DecodeAudioBlueprintPresentation(encoded, decoded_blueprint).ok()
            && decoded_blueprint == blueprint,
        "audio Blueprint publication round-trips its exact 24-byte semantic record");
    encoded.payload[3] = std::byte{2};
    expect(DecodeAudioBlueprintPresentation(encoded, decoded_blueprint).code
            == FailureCode::ProtocolMismatch,
        "audio Blueprint decoding rejects noncanonical direct flags");
}

void test_stage_presentation_is_pointer_free_and_composite()
{
    StagePresentationValue value{};
    value.coordinate = {9, 44};
    value.source_ordinal = 7;
    value.operation = StagePresentationOperation::BarrierHit;
    value.owner_logical_id = 0x1122334455667788ull;
    const std::int32_t actor_id = 4;
    const std::int32_t hit_count = 2;
    std::memcpy(value.source_semantic.data(), &actor_id, sizeof(actor_id));
    const std::array<float, 3> direction{1.0f, -2.0f, 3.0f};
    std::memcpy(value.source_semantic.data() + 4, direction.data(), 12);
    std::memcpy(value.canonical_before.data(), &hit_count, sizeof(hit_count));
    value.source_payload_size = 12;
    value.canonical_before_size = 4;
    value.particle_count = 1;
    auto& particle = value.particles[0].semantic;
    particle[0] = std::byte{1};
    const std::uint64_t owner = value.owner_logical_id;
    const std::uint64_t asset = 0x8877665544332211ull;
    std::memcpy(particle.data() + 1, &owner, sizeof(owner));
    std::memcpy(particle.data() + 9, &asset, sizeof(asset));
    const std::array<float, 9> transform{
        10.0f, 20.0f, 30.0f, 0.0f, 90.0f, 0.0f, 1.0f, 1.0f, 1.0f};
    std::memcpy(particle.data() + 17, transform.data(), sizeof(transform));
    particle[53] = std::byte{1};

    PresentationEvent encoded{};
    StagePresentationValue decoded{};
    expect(EncodeStagePresentation(value, encoded).ok()
            && encoded.kind == Schema::stage_presentation_event_kind
            && DecodeStagePresentation(encoded, decoded).ok()
            && decoded == value,
        "stage presentation round-trips canonical pre-state and nested particles");
    encoded.identity ^= 1;
    expect(DecodeStagePresentation(encoded, decoded).code
            == FailureCode::ProtocolMismatch,
        "stage presentation rejects copied or corrupted event identity");
    NativeBatchEnvelope batch{};
    batch.entry_coordinate = {9, 43};
    batch.exit_coordinate = {9, 44};
    batch.stage_barrier_calls = 1;
    batch.stage_barrier_journal_count = 1;
    auto& stage = batch.stage_barrier_journal[0];
    stage.owner_logical_id = value.owner_logical_id;
    stage.semantic = value.source_semantic;
    stage.canonical_before = value.canonical_before;
    stage.payload_size = value.source_payload_size;
    stage.canonical_before_size = value.canonical_before_size;
    stage.first_particle = 0;
    stage.particle_count = 1;
    batch.particle_spawn_calls = 1;
    batch.particle_spawn_journal_count = 1;
    batch.particle_spawn_journal[0].semantic = particle;
    batch.presentation_order_journal_count = 2;
    batch.presentation_order_journal[0] = {
        PresentationEventFamily::StageBarrier, 0, 1};
    batch.presentation_order_journal[1] = {
        PresentationEventFamily::ParticleSpawn, 0, 1};
    std::array<PresentationEvent, 2> built{};
    std::size_t built_count{};
    auto built_value = value;
    built_value.source_ordinal = 1;
    expect(BuildNativeAudioPresentation(batch, built, built_count).ok()
            && built_count == 1
            && DecodeStagePresentation(built[0], decoded).ok()
            && decoded == built_value,
        "native stage event atomically owns its nested particle publication");
    batch.stage_barrier_journal[0].particle_count = 0;
    expect(BuildNativeAudioPresentation(batch, built, built_count).code
            == FailureCode::UnsupportedContent,
        "unowned particle publication fails the native journal closed");
    value.particle_count = 3;
    expect(EncodeStagePresentation(value, encoded).code
            == FailureCode::InvalidConfiguration,
        "stage presentation fails closed above its static particle bound");
}
}

int main()
{
    test_online_qualification_metrics_are_bounded_and_resettable();
    test_authoritative_input_gate_is_transactional_and_fail_closed();
    test_aborted_outer_tick_reaches_post_completion_callback();
    test_canonical_hash_timeline_is_immutable_and_bounded();
    test_round_transition_selects_the_last_canonicalized_fencepost();
    test_round_rearm_clears_prediction_before_checkpoint_reservation();
    test_public_config_contract();
    test_input_replacement_and_invalidation();
    test_native_batch_timeline_is_exact_and_bounded();
    test_snapshot_capacity_is_atomic();
    test_checkpoint_memory_matches_capture_cadence();
    test_resimulation_base_planning_respects_batch_width();
    test_owned_gekko_retention_boundary_fails_before_history_discard();
    test_batch_aware_replay_seek_planning();
    test_presentation_exactly_once();
    test_native_audio_presentation_preserves_cross_family_order();
    test_native_audio_presentation_correction_is_atomic();
    test_callsite_qualified_particle_values();
    test_replay_checkpoint_seek_and_resume();
    test_cross_generation_seek_materializes_before_restore();
    test_cross_generation_identity_mismatch_fails_before_restore();
    test_native_replay_materializer_requires_state4_fencepost();
    test_sc6_replay_bridge_transaction_and_undo();
    test_transactional_restore_failures_undo();
    test_floating_point_environment_capture_is_raw_and_non_mutating();
    test_ucrt_broker_is_callsite_and_thread_bound();
    test_audio_presentation_identities_are_epoch_bound();
    test_stage_presentation_is_pointer_free_and_composite();
    if (failures == 0)
    {
        std::cout << "DeterministicCoreSelfTest passed\n";
    }
    return failures == 0 ? 0 : 1;
}
