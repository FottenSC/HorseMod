#include "deterministic/InputTimeline.hpp"
#include "deterministic/Config.hpp"
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
    expect(replay.Begin(context(), {1, 0}).ok(), "begin replay capture");
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
    test_transactional_restore_failures_undo();
    if (failures == 0)
    {
        std::cout << "DeterministicCoreSelfTest passed\n";
    }
    return failures == 0 ? 0 : 1;
}
