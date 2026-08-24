#include "deterministic/InputTimeline.hpp"
#include "deterministic/Config.hpp"
#include "deterministic/NativeReplayMaterializer.hpp"
#include "deterministic/PresentationJournal.hpp"
#include "deterministic/ReplayCoordinator.hpp"
#include "deterministic/SnapshotStore.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

using namespace Horse::Deterministic;

namespace
{
int failures = 0;

void expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
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
        value += static_cast<int>(inputs.players[0].buttons);
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
    input.players[0].buttons = 1;
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
               << "legacy_transport=udp\n"
               << "legacy_mode=lab\n";
    }
    const ConfigLoadResult loaded = LoadConfig(path);
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    expect(loaded.status.ok(), "load public deterministic config");
    expect(loaded.config.enabled, "parse deterministic enabled flag");
    expect(loaded.diagnostics.size() == 1, "legacy config emits one diagnostic");
}

void test_input_replacement_and_invalidation()
{
    InputTimeline timeline{2};
    expect(timeline.AppendAuthoritative({1, 0}, one_input(false)).ok(), "append predicted input");
    PlayerInput remote;
    remote.buttons = 7;
    expect(timeline.ReplacePredicted({1, 0}, 1, remote).ok(), "replace predicted input");
    expect(timeline.GetExact({1, 0})->players[1].buttons == 7, "confirmed input stored");
    remote.buttons = 8;
    expect(
        timeline.ReplacePredicted({1, 0}, 1, remote).code == FailureCode::IdentityMismatch,
        "confirmed input cannot be rewritten");
    timeline.InvalidateGeneration(1);
    expect(!timeline.GetExact({1, 0}).has_value(), "input generation invalidated");
}

void test_snapshot_capacity_is_atomic()
{
    SnapshotStore store{sizeof(Snapshot) + 4, 1, CapacityPolicy::RejectNew};
    Snapshot first{{1, 0}, 1, {}, std::vector<std::byte>(4)};
    Snapshot second{{1, 1}, 1, {}, std::vector<std::byte>(4)};
    expect(store.Save(first).ok(), "save first snapshot");
    const auto bytes_before = store.BytesUsed();
    expect(store.Save(second).code == FailureCode::CapacityExceeded, "reject full snapshot store");
    expect(store.BytesUsed() == bytes_before, "capacity rejection does not mutate store");
    expect(store.Load({1, 0}).has_value(), "original snapshot survives rejection");
}

void test_presentation_exactly_once()
{
    PresentationJournal journal{4, 64};
    CountingSink sink;
    PresentationEvent event{{1, 4}, 3, 44, {std::byte{1}}};
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
        event.identity = frame;
        expect(bounded.Record(event).ok(), "record after prior event committed");
        expect(bounded.CommitThrough(event.coordinate, bounded_sink).ok(), "commit bounded event");
    }
    expect(bounded_sink.count == 100, "committed-event dedup metadata stays bounded");
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
}

int main()
{
    test_public_config_contract();
    test_input_replacement_and_invalidation();
    test_snapshot_capacity_is_atomic();
    test_presentation_exactly_once();
    test_replay_checkpoint_seek_and_resume();
    test_cross_generation_seek_materializes_before_restore();
    test_cross_generation_identity_mismatch_fails_before_restore();
    test_native_replay_materializer_requires_state4_fencepost();
    test_transactional_restore_failures_undo();
    if (failures == 0)
    {
        std::cout << "DeterministicCoreSelfTest passed\n";
    }
    return failures == 0 ? 0 : 1;
}
