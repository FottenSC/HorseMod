#include "deterministic/ReplayDiagnosticTrace.hpp"
#include "deterministic/ReplayTraceWeakReference.hpp"
#include "deterministic/ReplayHudPlayback.hpp"
#include "deterministic/ReplayHudWidgetAdmission.hpp"
#include "replay_qualification_mod/ReplayResumeRateWindow.hpp"
#include "deterministic/ReplayOcclusionHistory.hpp"
#include "deterministic/ReplayStreamableDomain.hpp"
#include "deterministic/CanonicalHashTimeline.hpp"
#include "deterministic/AudioPresentation.hpp"
#include "deterministic/CandidateGameStateAdapter.hpp"
#include "deterministic/DeterministicHookSet.hpp"
#include "deterministic/InputTimeline.hpp"
#include "deterministic/Config.hpp"
#include "deterministic/FloatingPointEnvironment.hpp"
#include "deterministic/NativeReplayMaterializer.hpp"
#include "deterministic/NativeBatchTimeline.hpp"
#include "deterministic/NativeAudioPresentationController.hpp"
#include "deterministic/NativePresentationJournal.hpp"
#include "deterministic/ParticlePresentation.hpp"
#include "deterministic/PresentationJournal.hpp"
#include "deterministic/ReplayCoordinator.hpp"
#include "deterministic/ReplaySeekPlanner.hpp"
#include "deterministic/Sc6ReplayNativeBridge.hpp"
#include "deterministic/InputProducer.hpp"
#include "deterministic/ReplaySourceRegistration.hpp"
#include "deterministic/Sc6ReplayRuntime.hpp"
#include "deterministic/Sc6ReplaySchedulerState.hpp"
#include "deterministic/Sc6ReplayVfxState.hpp"
#include "deterministic/ReplayLightingBinding.hpp"
#include "deterministic/SnapshotStore.hpp"
#include "deterministic/StagePresentation.hpp"
#include "deterministic/UcrtRandBroker.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <cstdlib>
#include <xmmintrin.h>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <thread>
#endif

#include "replay_seek_state_selftest.inl"
#include "replay_capture_accounting_selftest.inl"
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

void test_replay_resume_wall_clock() {
    Horse::Qualification::ReplayResumeRateWindow rate;
    expect(!rate.Sample(0,0),"rate requires an explicit start");
    rate.Begin(170,0,120);
    expect(rate.Sample(170,100'000) && rate.Sample(170,180'000)
        && rate.Sample(171,200'000) && rate.elapsed_us()==200'000 && rate.ticks()==1,
        "zero-progress polls preserve the entire initial stall");
    expect(rate.Sample(289,2'180'000) && rate.Sample(289,2'280'000)
        && rate.Sample(290,2'300'000) && rate.elapsed_us()==2'300'000 && rate.ticks()==120,
        "120-tick wall window includes intermediate stalls");
    expect(rate.ticks()*1'000'000'000ull/rate.elapsed_us()<58'000,
        "stalled playback cannot qualify through discarded poll time");
    expect(rate.Sample(310,4'000'000) && rate.elapsed_us()==2'300'000 && rate.ticks()==120,
        "completed rate window excludes later observation time");
    expect(!rate.Sample(309,4'100'000) && !rate.Sample(311,3'999'999)
        && rate.ticks()==120 && rate.elapsed_us()==2'300'000,
        "tick and clock regressions reject without changing the measurement");
    rate.Begin(100,10,120);
    expect(rate.Sample(223,2'050'010) && rate.ticks()==123 && rate.elapsed_us()==2'050'000
        && rate.ticks()*1'000'000'000ull/rate.elapsed_us()==60'000,
        "multi-tick final poll uses actual observed count and full wall time");
    rate.Begin(0,0,120);
    expect(rate.Sample(120,2'000'000) && rate.ticks()*1'000'000'000ull/rate.elapsed_us()==60'000,
        "ordinary 60 TPS and zero epoch are measured correctly");
}

void test_particle_tile_partition_rejects_equal_count_corruption()
{
    std::array<std::uint64_t, 1024> owned{};
    owned[0] = 1; // Tile zero belongs to an emitter.
    std::vector<std::uint32_t> free;
    for (std::uint32_t tile = 1; tile < 65536; ++tile) free.push_back(tile);
    expect(Sc6ReplayVfxState::IsCompleteTilePartition(owned, free), "full disjoint tile partition accepted");
    free.back() = 0;
    expect(!Sc6ReplayVfxState::IsCompleteTilePartition(owned, free), "equal counts cannot hide free/owned overlap");
    free.back() = 1;
    expect(!Sc6ReplayVfxState::IsCompleteTilePartition(owned, free), "duplicate free ID and missing tile rejected");
    free.back() = 65536;
    expect(!Sc6ReplayVfxState::IsCompleteTilePartition(owned, free), "out-of-range free ID rejected");
    free.pop_back();
    expect(!Sc6ReplayVfxState::IsCompleteTilePartition(owned, free), "unaccounted tile rejected");
}

#ifdef _WIN32
void test_particle_tile_prefix_recovery_and_lock_exclusion()
{
    struct PoolFixture
    {
        std::byte* data = static_cast<std::byte*>(VirtualAlloc(nullptr, 0x41000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        CRITICAL_SECTION* lock{};
        PoolFixture()
        {
            if (data) { lock = reinterpret_cast<CRITICAL_SECTION*>(data + 0x40198); InitializeCriticalSectionAndSpinCount(lock, 733); }
        }
        ~PoolFixture() { if (lock) DeleteCriticalSection(lock); if (data) VirtualFree(data, 0, MEM_RELEASE); }
    } pool;
    expect(pool.data != nullptr, "tile-prefix fixture allocation");
    if (!pool.data) return;
    const auto address = reinterpret_cast<std::uintptr_t>(pool.data);
    auto* count = reinterpret_cast<int*>(pool.data + 0x40190);
    auto* prefix = reinterpret_cast<std::uint32_t*>(pool.data + 0x190);
    const auto spin = pool.lock->SpinCount;
    const auto debug = pool.lock->DebugInfo;
    std::vector<std::uint32_t> b(2048), a(2048);
    for (std::uint32_t i = 0; i < b.size(); ++i) { b[i] = i; a[i] = static_cast<std::uint32_t>(b.size() - i - 1); }
    std::memcpy(prefix, b.data(), b.size() * 4); *count = static_cast<int>(b.size());
    bool dirty{};
    auto status = Sc6ReplayVfxState::ReplaceNativeTilePrefix(address, a, a, true, dirty);
    expect(status.code == FailureCode::GenerationMismatch && !dirty && *count == b.size()
        && !std::memcmp(prefix, b.data(), b.size() * 4), "stale B prefix rejects before mutation");

    HANDLE held = CreateEventW(nullptr, TRUE, FALSE, nullptr), release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    expect(held && release, "tile-prefix contention events");
    if (held && release)
    {
        std::thread worker([&] {
            EnterCriticalSection(pool.lock); SetEvent(held);
            WaitForSingleObject(release, INFINITE); LeaveCriticalSection(pool.lock);
        });
        const auto acquired = WaitForSingleObject(held, 5000) == WAIT_OBJECT_0;
        expect(acquired, "fixture worker owns native pool lock");
        if (acquired)
        {
            status = Sc6ReplayVfxState::ReplaceNativeTilePrefix(address, b, a, true, dirty);
            expect(status.code == FailureCode::RestorePreflightFailed && !dirty
                && !std::memcmp(prefix, b.data(), b.size() * 4), "contended native lock does not block or write");
        }
        SetEvent(release); worker.join();
    }
    if (held) CloseHandle(held);
    if (release) CloseHandle(release);

    DWORD previous{};
    const bool protected_page = VirtualProtect(pool.data + 0x1000, 0x1000, PAGE_READONLY, &previous) != FALSE;
    expect(protected_page, "inject tile-prefix write fault after a writable prefix");
    if (protected_page)
    {
        status = Sc6ReplayVfxState::ReplaceNativeTilePrefix(address, b, a, true, dirty);
        expect(status.code == FailureCode::RestoreWriteFailed && dirty, "partial-write fault preserves dirty undo disposition");
        DWORD ignored{};
        const bool writable = VirtualProtect(pool.data + 0x1000, 0x1000, previous, &ignored) != FALSE;
        expect(writable, "restore fixture page before undo");
        if (!writable) return;
        status = Sc6ReplayVfxState::ReplaceNativeTilePrefix(address, a, b, false, dirty);
        expect(status.ok() && dirty && *count == b.size() && !std::memcmp(prefix, b.data(), b.size() * 4),
            "undo recovers B after a partial A write and requires enclosing acknowledgment");
        dirty = false; // Independent B readback above is the fixture's acknowledgment.
    }
    status = Sc6ReplayVfxState::ReplaceNativeTilePrefix(address, b, {}, true, dirty);
    expect(status.ok() && dirty && *count == 0, "zero free tiles is a valid installed prefix");
    status = Sc6ReplayVfxState::ReplaceNativeTilePrefix(address, {}, b, false, dirty);
    expect(status.ok() && *count == b.size() && !std::memcmp(prefix, b.data(), b.size() * 4), "undo restores a nonempty free list");
    expect(pool.lock->SpinCount == spin && pool.lock->DebugInfo == debug && pool.lock->RecursionCount == 0,
        "publication/failure/undo preserve and release the original native lock");
}
#endif

void test_native_tick_epoch_rebase_preserves_signed_comparison()
{
#ifdef _WIN32
    Sc6ReplaySchedulerState current, original;
    expect(current.Capture(1,reinterpret_cast<void*>(1),1024,{},&current).code==FailureCode::IllegalTransition,
        "continuation capture rejects self alias before touching native addresses");
    expect(current.Capture(1,reinterpret_cast<void*>(1),1024,{},&original).code==FailureCode::GenerationMismatch,
        "continuation capture rejects an unowned B scheduler before native reads");
    expect(current.tick_count()==0 && original.tick_count()==0 && current.owned_bytes()==0,
        "rejected continuation capture leaves current and original storage unchanged");
#endif
    std::int32_t output{};
    expect(Sc6ReplaySchedulerState::RebaseEpochStamp(99, 100, 200, output) && output == 199,
        "unadmitted native ticks must stay older than the live epoch");
    expect(Sc6ReplaySchedulerState::RebaseEpochStamp(100, 100, 200, output) && output == 200,
        "admitted native ticks must remain admitted without rewinding the engine epoch");
    expect(Sc6ReplaySchedulerState::RebaseEpochStamp(-1, 0xffffffffULL, 200, output) && output == 199,
        "native sign extension must not be replaced by a 32-bit epoch comparison");
    expect(!Sc6ReplaySchedulerState::RebaseEpochStamp(100, 100, 0x80000000ULL, output),
        "an admitted state must fail when the live epoch cannot be represented by a signed stamp");
    expect(Sc6ReplaySchedulerState::RebaseEpochStamp(-1, 0xffffffffffffffffULL, 200, output) && output == 200,
        "native comparison uses the complete sign-extended bit pattern");
}

void test_canonical_hash_timeline_is_immutable_and_bounded()
{
    CanonicalHashTimeline timeline{2};
    expect(timeline.allocated_bytes() == 0, "unused canonical history owns no reserved backing");
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
    const auto global_range = timeline.Range();
    const auto latest_range = timeline.LatestGenerationRange();
    expect(global_range.has_value()
            && global_range->first == FrameCoordinate{2, 358}
            && global_range->second == FrameCoordinate{3, 361}
            && latest_range.has_value()
            && latest_range->first == FrameCoordinate{3, 361}
            && latest_range->second == FrameCoordinate{3, 361},
        "seekable range selects the latest generation without discarding older history");
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
        write(replay, 0x390, std::uintptr_t{0x3290d20});
        write(replay, 0x3b8, round_images.data());
        write(replay, 0x3c0, std::int32_t{2});
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
    std::array<std::byte, 0x3d0> replay{};
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
    expect(timeline.allocated_bytes() == 0, "unused input history owns no reserved backing");
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
    expect(timeline.allocated_bytes() == 0, "unused native batch history owns no reserved backing");
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
    zero_width.consumers_before_count = 2;
    zero_width.producers_before_count = 2;
    for (std::uint8_t index = 0; index < 2; ++index)
    {
        auto& producer = zero_width.producers_before[index];
        producer.valid = true;
        producer.native_frame_before = 2;
        producer.native_frame_after = 2;
        producer.preceding_consumers = index + 1;
    }
    expect(zero_width_timeline.Append(zero_width, {}).ok(),
        "batch storage accepts offset zero presentation at a zero-width entry");
    auto reordered_producers = zero_width;
    reordered_producers.producers_before[1].preceding_consumers = 0;
    expect(zero_width_timeline.ReplaceBatch(0, zero_width, reordered_producers).code
            == FailureCode::IdentityMismatch
            && zero_width_timeline.GetBatch(0)->producers_before[1].preceding_consumers == 2,
        "zero-coordinate calls retain producer order and reject reordered replacement atomically");
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

void test_native_interval_suffix_rebuilds_coordinate_index()
{
    NativeBatchTimeline timeline{4, 6};
    const auto batch = [](std::uint64_t id, std::uint64_t first,
                         std::uint64_t last, std::uint32_t count) {
        NativeBatchEnvelope value{};
        value.batch_id = id;
        value.entry_coordinate = {9, first};
        value.exit_coordinate = {9, last};
        value.coordinate_count = count;
        value.completed_identity_valid = true;
        value.completed_hash[0] = std::byte{1};
        return value;
    };
    const std::array prefix_coordinates{FrameCoordinate{9, 359}};
    expect(timeline.Append(batch(50, 358, 359, 1), prefix_coordinates).ok(),
        "retain an unchanged interval prefix");
    const std::array expected{
        batch(51, 359, 359, 0), batch(52, 359, 361, 2), batch(53, 361, 362, 1)};
    const std::array old_coordinates{
        FrameCoordinate{9, 360}, FrameCoordinate{9, 361}, FrameCoordinate{9, 362}};
    expect(timeline.Append(expected[0], {}).ok()
            && timeline.Append(expected[1], std::span{old_coordinates}.first(2)).ok()
            && timeline.Append(expected[2], std::span{old_coordinates}.last(1)).ok(),
        "retain zero, repeated, and ordinary native intervals");
    const auto allocated = timeline.allocated_bytes();
    auto incomplete = expected;
    incomplete[1].exit_coordinate.frame = 362;
    incomplete[2].entry_coordinate.frame = 362;
    incomplete[2].exit_coordinate.frame = 363;
    const std::array skipped_coordinates{
        FrameCoordinate{9, 360}, FrameCoordinate{9, 362}, FrameCoordinate{9, 363}};
    expect(timeline.ValidateSuffixReplacement(expected, incomplete, skipped_coordinates).code
            == FailureCode::IdentityMismatch && timeline.coordinate_count() == 4,
        "a complete interval cannot omit an interior native fencepost");
    auto replacements = std::array{
        batch(51, 359, 360, 1), batch(52, 360, 360, 0), batch(53, 360, 363, 3)};
    const std::array corrected_coordinates{
        FrameCoordinate{9, 360}, FrameCoordinate{9, 361},
        FrameCoordinate{9, 362}, FrameCoordinate{9, 363}};
    const auto admitted = timeline.ValidateSuffixReplacement(
        expected, replacements, corrected_coordinates);
    expect(admitted.ok(), "admit an entire corrected suffix with changed native geometry");
    if (admitted.ok()) timeline.CommitValidatedSuffixReplacement(replacements, corrected_coordinates);
    const auto changed_membership = timeline.FindCoordinate({9, 360});
    expect(changed_membership.has_value() && changed_membership->batch_index == 1
            && timeline.GetBatchCoordinate(2, 0) == nullptr
            && timeline.GetBatchCoordinate(3, 2)->coordinate == FrameCoordinate{9, 363}
            && timeline.FindCoordinate({9, 359})->batch_index == 0
            && timeline.coordinate_count() == 5 && timeline.allocated_bytes() == allocated,
        "rebuild native membership while preserving interval IDs, prefix and allocation baseline");
    expect(timeline.ValidateSuffixReplacement(expected, replacements, corrected_coordinates).code
            == FailureCode::IdentityMismatch,
        "stale suffix evidence cannot overwrite corrected history");
    const std::array<FrameCoordinate, 6> excessive{};
    expect(timeline.ValidateSuffixReplacement(replacements, replacements, excessive).code
            == FailureCode::CapacityExceeded && timeline.coordinate_count() == 5,
        "insufficient coordinate capacity rejects the whole replacement before mutation");
    auto crossing = replacements;
    crossing.back().input_generation_changed = true;
    expect(timeline.ValidateSuffixReplacement(replacements, crossing, corrected_coordinates).code
            == FailureCode::IdentityMismatch,
        "native ownership transitions require a barrier instead of suffix replacement");
    expect(timeline.ValidateSuffixReplacement(std::span{replacements}.first(2),
                std::span{replacements}.first(2), std::span{corrected_coordinates}.first(1)).code
            == FailureCode::InvalidConfiguration,
        "partial geometry replacement cannot leave an incompatible later suffix");
    timeline.DiscardBefore({9, 360});
    expect(timeline.GetBatch(0)->batch_id == 51
            && timeline.ValidateSuffixReplacement(replacements, expected, old_coordinates).ok(),
        "immutable batch IDs resolve the suffix after retained-prefix compaction");
    timeline.CommitValidatedSuffixReplacement(expected, old_coordinates);
    expect(timeline.FindCoordinate({9, 360})->batch_index == 1
            && !timeline.FindCoordinate({9, 363}).has_value()
            && timeline.allocated_bytes() == allocated,
        "suffix shrink removes obsolete coordinates without reallocating");
}

void test_interval_checkpoint_identity_survives_native_geometry_changes()
{
    SnapshotStore intervals{4 * 1024 * 1024, 4, CapacityPolicy::EvictOldest,
        SnapshotIndex::NativeInterval};
    Snapshot baseline{};
    baseline.coordinate = {7, 359};
    baseline.context_identity = 91;
    baseline.bytes.resize(32, std::byte{1});
    baseline.canonical_hash[0] = std::byte{1};
    expect(intervals.SaveIntervalCopyPrewarmed({12, 0}, baseline).code
            == FailureCode::CapacityExceeded,
        "interval capture cannot allocate its storage on the first owned save");
    expect(intervals.PrewarmCaptureSlots(baseline).ok(), "admit interval capture storage");
    const auto retained_bytes = intervals.BytesUsed();
    expect(intervals.SaveIntervalCopyPrewarmed({12, 0}, baseline).ok(), "save baseline interval");
    auto maintenance = baseline;
    maintenance.bytes[0] = std::byte{2};
    maintenance.canonical_hash[0] = std::byte{2};
    expect(intervals.SaveIntervalCopyPrewarmed({12, 0}, maintenance).code
            == FailureCode::IdentityMismatch
            && intervals.FindInterval({12, 0})->bytes == baseline.bytes,
        "ordinary interval save cannot overwrite history without validating its prior hash");
    expect(intervals.SaveIntervalCopyPrewarmed({12, 1}, maintenance).ok(),
        "zero-coordinate interval has its own retained state");
    auto repeated = maintenance;
    repeated.coordinate.frame = 361;
    repeated.bytes[0] = std::byte{3};
    repeated.canonical_hash[0] = std::byte{3};
    expect(intervals.SaveIntervalCopyPrewarmed({12, 2}, repeated).ok()
            && intervals.FindInterval({12, 0})->bytes == baseline.bytes
            && intervals.FindInterval({12, 1})->bytes == maintenance.bytes,
        "repeated native coordinates do not overwrite earlier interval states");
    expect(intervals.FindExact({7, 359}) == nullptr
            && intervals.FindInterval({13, 1}) == nullptr
            && intervals.SaveCopyPrewarmed(baseline).code == FailureCode::InvalidConfiguration,
        "interval ownership cannot be confused with affine native coordinate lookup");
    std::array replacements{maintenance, repeated};
    replacements[0].coordinate.frame = 360;
    replacements[1].coordinate.frame = 362;
    replacements[1].canonical_hash[0] = std::byte{4};
    const std::array ids{NativeIntervalId{12, 1}, NativeIntervalId{12, 2}};
    const std::array expected{maintenance.canonical_hash, repeated.canonical_hash};
    const auto admitted = intervals.ValidateIntervalReplacement(ids, replacements, expected);
    expect(admitted.ok(), "admit corrected interval geometry using immutable interval identities");
    if (admitted.ok()) intervals.CommitValidatedIntervalReplacement(ids, replacements);
    expect(intervals.FindInterval({12, 1})->coordinate.frame == 360
            && intervals.FindInterval({12, 2})->coordinate.frame == 362
            && intervals.BytesUsed() == retained_bytes,
        "commit changed native geometry in existing preallocated slots");
    expect(intervals.ValidateIntervalReplacement(ids, replacements, expected).code
            == FailureCode::IdentityMismatch,
        "stale saved hashes cannot replace an already-corrected suffix");
    intervals.DiscardIntervalsBeforeRetainingNearest({12, 1});
    expect(intervals.FindInterval({12, 0}) == nullptr
            && intervals.FindInterval({12, 1})->coordinate.frame == 360,
        "prefix retirement keeps interval token identity independent of index compaction");
    auto oversized = repeated;
    oversized.bytes.resize(candidate_checkpoint_capture_byte_capacity + 1);
    expect(intervals.SaveIntervalCopyPrewarmed({12, 3}, oversized).code
            == FailureCode::CapacityExceeded
            && intervals.FindInterval({12, 3}) == nullptr
            && intervals.FindInterval({12, 2})->coordinate.frame == 362
            && intervals.BytesUsed() == retained_bytes,
        "partial capacity failure preserves retained history and allocation baseline");
    expect(intervals.SaveIntervalCopyPrewarmed({12, 3}, repeated).ok(),
        "freed preallocated slot remains usable after rejected capture");
    expect(intervals.SaveIntervalCopyPrewarmed({12, 4}, repeated).ok(),
        "fill the admitted interval store");
    Snapshot extracted{};
    expect(!intervals.TakeOldestIfFull(extracted)
            && intervals.entry_count() == 4
            && intervals.BytesUsed() == retained_bytes,
        "legacy eviction cannot extract an owned interval storage envelope");
    intervals.DiscardIntervalsBeforeRetainingNearest({12, 5});
    expect(intervals.entry_count() == 1 && intervals.FindInterval({12, 4}) != nullptr,
        "retirement retains the nearest anchor within the same ownership epoch");
    intervals.DiscardIntervalsBeforeRetainingNearest({13, 0});
    expect(intervals.entry_count() == 0 && intervals.BytesUsed() == retained_bytes
            && intervals.SaveIntervalCopyPrewarmed({13, 0}, baseline).ok(),
        "new ownership cannot retain a previous epoch anchor and reuses admitted buffers");
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
    prototype.bytes.reserve(256);
    prototype.local_images.resize(1);
    prototype.local_images[0].bytes.resize(32, std::byte{0x42});
    prototype.local_images[0].bytes.reserve(128);
    expect(prewarmed.PrewarmCopySlots(prototype).ok(),
        "prewarm bounded checkpoint copy slots from native shape");
    const auto prewarmed_bytes = prewarmed.BytesUsed();
    expect(prewarmed.SaveCopyPrewarmed(prototype).ok()
            && prewarmed.BytesUsed() == prewarmed_bytes,
        "prewarmed checkpoint save does not grow allocator-accounted storage");
    const auto* capacity_copy = prewarmed.FindExact({5, 1});
    expect(capacity_copy != nullptr
            && capacity_copy->bytes.capacity() >= prototype.bytes.capacity()
            && capacity_copy->local_images[0].bytes.capacity()
                >= prototype.local_images[0].bytes.capacity(),
        "checkpoint prewarm preserves serializer capacities, not only sizes");
    Snapshot correction_scratch{};
    expect(PrepareSnapshotCopyStorage(correction_scratch, prototype).ok()
            && correction_scratch.bytes.capacity()
                >= prototype.bytes.capacity()
            && correction_scratch.local_images[0].bytes.capacity()
                >= prototype.local_images[0].bytes.capacity(),
        "owned correction scratch prewarms the complete serializer envelope");
    std::vector<LocalReconstructionImage> adapter_exchange;
    expect(PrepareLocalReconstructionCopyStorage(
                adapter_exchange, prototype.local_images).ok(),
        "transient adapter exchange prewarms the same serializer envelope");
    const auto correction_local_capacity = [](
        const std::vector<LocalReconstructionImage>& locals) {
        std::size_t bytes = locals.capacity()
            * sizeof(LocalReconstructionImage);
        for (const auto& local : locals) bytes += local.bytes.capacity();
        return bytes;
    };
    const auto local_before = correction_local_capacity(
        correction_scratch.local_images);
    correction_scratch.local_images.swap(adapter_exchange);
    expect(correction_local_capacity(correction_scratch.local_images)
            >= local_before,
        "transient capture exchange cannot shrink prewarmed correction scratch");
    Snapshot capture_scratch{};
    expect(PrepareSnapshotCaptureStorage(capture_scratch, prototype).ok()
            && capture_scratch.bytes.capacity()
                >= candidate_checkpoint_capture_byte_capacity
            && capture_scratch.local_images.capacity()
                >= maximum_local_reconstruction_images,
        "reusable checkpoint capture owns the full variable encoded envelope");
    const auto capture_capacity = capture_scratch.bytes.capacity();
    capture_scratch.bytes.resize(
        candidate_checkpoint_capture_byte_capacity, std::byte{0x5a});
    expect(capture_scratch.bytes.capacity() == capture_capacity,
        "maximum encoded checkpoint capture cannot grow after ownership");
    Snapshot later_content_capture{};
    expect(PrepareSnapshotCaptureStorage(
               later_content_capture, prototype).ok(),
        "online correction capture prewarms beyond the status-4 prototype");
    const auto later_content_capacity = later_content_capture.bytes.capacity();
    later_content_capture.bytes.resize(
        prototype.bytes.capacity() + 1476, std::byte{0x3c});
    expect(later_content_capture.bytes.capacity() == later_content_capacity,
        "content-specific correction snapshots cannot grow owned storage");
    SnapshotStore online_history{4 * 1024 * 1024, 8,
        CapacityPolicy::RejectNew};
    expect(online_history.PrewarmCopySlots(prototype).ok()
            && online_history.SaveCopyPrewarmed(prototype).ok(),
        "seed online history before ownership");
    expect(online_history.PrewarmCaptureSlots(prototype).ok(),
        "status 4 prewarms history to the bounded capture envelope");
    const auto online_history_bytes = online_history.BytesUsed();
    auto later_history = prototype;
    later_history.coordinate = {5, 2};
    later_history.bytes.resize(
        prototype.bytes.capacity() + 65536, std::byte{0x6d});
    expect(online_history.SaveCopyPrewarmed(later_history).ok()
            && online_history.BytesUsed() == online_history_bytes
            && online_history.FindExact({5, 1}) != nullptr,
        "post-ownership content growth uses preowned history storage");
    auto outside_envelope = later_history;
    outside_envelope.coordinate = {5, 3};
    outside_envelope.bytes.resize(candidate_checkpoint_capture_byte_capacity + 1);
    expect(!online_history.SaveCopyPrewarmed(outside_envelope).ok()
            && online_history.BytesUsed() == online_history_bytes
            && online_history.FindExact({5, 1})->coordinate == FrameCoordinate{5, 1}
            && online_history.FindExact({5, 2})->bytes == later_history.bytes
            && online_history.FindExact({5, 3}) == nullptr,
        "owned envelope exhaustion preserves allocation and required checkpoint history");
    SnapshotStore partial{64 * 1024, 4, CapacityPolicy::RejectNew};
    expect(partial.PrewarmCopySlots(prototype).ok()
            && partial.SaveCopyPrewarmed(prototype).ok(),
        "partial preparation fixture retains a real checkpoint");
    const auto before_partial = partial.BytesUsed();
    expect(!partial.PrewarmCaptureSlots(prototype).ok()
            && partial.BytesUsed() > before_partial
            && partial.FindExact(prototype.coordinate) != nullptr
            && partial.FindExact(prototype.coordinate)->canonical_hash == prototype.canonical_hash,
        "failed full-envelope admission accounts partial allocation and retains identity");
    partial.ReleasePrewarmedCopySlots();
    expect(partial.FindExact(prototype.coordinate) == nullptr
            && partial.BytesUsed() < before_partial,
        "failed preparation can release its entire retained envelope");
    std::vector<LocalReconstructionImage> movable_capture_exchange;
    expect(PrepareLocalReconstructionCopyStorage(
                movable_capture_exchange, prototype.local_images).ok(),
        "movable checkpoint image prewarms local reconstruction payloads");
    movable_capture_exchange.reserve(maximum_local_reconstruction_images);
    const auto capture_local_capacity = correction_local_capacity(
        capture_scratch.local_images);
    expect(correction_local_capacity(movable_capture_exchange)
            == capture_local_capacity,
        "movable image and reusable capture own symmetric exchange capacity");
    capture_scratch.local_images.swap(movable_capture_exchange);
    expect(correction_local_capacity(capture_scratch.local_images)
            == capture_local_capacity,
        "captured local-image exchange cannot grow owned snapshot storage");
    class NullNativeMemory final : public INativeMemory
    {
    public:
        bool Read(std::uintptr_t, std::span<std::byte>) noexcept override
        {
            return false;
        }
        bool Write(std::uintptr_t,
            std::span<const std::byte>) noexcept override
        {
            return false;
        }
    } memory;
    NativeCandidateRegions regions{memory};
    HgCpuStreamShim hgcpu;
    CandidateGameStateAdapter adapter{regions, hgcpu};
    const auto adapter_owned_before = adapter.owned_scratch_bytes();
    expect(adapter.PrepareTransientCaptureStorage(prototype).ok(),
        "adapter prewarms its movable capture exchange");
    std::vector<LocalReconstructionImage> admitted_adapter_exchange;
    expect(PrepareLocalReconstructionCopyStorage(
                admitted_adapter_exchange, prototype.local_images).ok(),
        "adapter accounting fixture preserves serializer capacities");
    admitted_adapter_exchange.reserve(maximum_local_reconstruction_images);
    expect(adapter.owned_scratch_bytes()
            == adapter_owned_before
                + correction_local_capacity(admitted_adapter_exchange),
        "aggregate adapter ownership includes the admitted movable envelope");
    const auto adapter_owned_prepared = adapter.owned_scratch_bytes();
    auto full_envelope_prototype = prototype;
    full_envelope_prototype.local_images.reserve(
        maximum_local_reconstruction_images);
    expect(adapter.PrepareTransientCaptureStorage(full_envelope_prototype).ok()
            && adapter.owned_scratch_bytes() == adapter_owned_prepared,
        "later full-envelope prototypes cannot grow prepared adapter storage");
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
            && prewarmed.FindExact({5, 3}) != nullptr
            && prewarmed.FindExact({5, 1}) != nullptr
            && prewarmed.FindExact({5, 1})->coordinate
                == FrameCoordinate{5, 1}
            && prewarmed.FindNearestAtOrBefore({5, 2}) != nullptr
            && prewarmed.FindNearestAtOrBefore({5, 2})->coordinate
                == FrameCoordinate{5, 2},
        "checkpoint shape may rewarm the bounded pool without replacing "
        "retained payload identities");
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

void test_confirmed_online_history_retirement_is_bounded()
{
    InputTimeline inputs{96};
    CanonicalHashTimeline canonical{96};
    NativeBatchTimeline batches{96, 96};
    SnapshotStore checkpoints{
        1024 * 1024, 4, CapacityPolicy::RejectNew};
    Snapshot prototype{};
    prototype.coordinate = {3, 0};
    prototype.bytes.resize(64, std::byte{0x41});
    expect(checkpoints.PrewarmCopySlots(prototype).ok(),
        "prewarm bounded online checkpoint history");

    for (std::uint64_t frame = 1; frame <= 96; ++frame)
    {
        InputPair input{};
        input.players[0].held = static_cast<std::uint32_t>(frame);
        expect(inputs.AppendAuthoritative({3, frame}, input).ok(),
            "append online input before confirmed-prefix retirement");
        CanonicalHash hash{};
        hash[0] = std::byte{static_cast<unsigned char>(frame)};
        expect(canonical.Append({3, frame}, hash, {}, {}, {}, {}, {}, {}, {},
                {}, {}).ok(),
            "append online canonical state before confirmed-prefix retirement");
        NativeBatchEnvelope batch{};
        batch.batch_id = frame;
        batch.entry_coordinate = {3, frame - 1};
        batch.exit_coordinate = {3, frame};
        batch.coordinate_count = 1;
        const std::array coordinates{FrameCoordinate{3, frame}};
        expect(batches.Append(batch, coordinates).ok(),
            "append online batch before confirmed-prefix retirement");
        if (frame % 24 == 0)
        {
            auto checkpoint = prototype;
            checkpoint.coordinate = {3, frame};
            expect(checkpoints.SaveCopyPrewarmed(checkpoint).ok(),
                "append online checkpoint before confirmed-prefix retirement");
        }
    }

    const FrameCoordinate minimum{3, 64};
    inputs.DiscardBefore(minimum);
    canonical.DiscardBefore(minimum);
    batches.DiscardBefore(minimum);
    checkpoints.DiscardBeforeRetainingNearest(minimum);
    expect(!inputs.GetExact({3, 63}).has_value()
            && inputs.GetExact(minimum).has_value()
            && !canonical.GetExact({3, 63}).has_value()
            && canonical.GetExact(minimum).has_value(),
        "confirmed retirement removes only the immutable input/hash prefix");
    expect(!batches.FindCoordinate({3, 63}).has_value()
            && batches.FindCoordinate(minimum).has_value()
            && batches.FindCoordinate({3, 96}).has_value(),
        "confirmed retirement reindexes retained native batches");
    expect(checkpoints.FindExact({3, 24}) == nullptr
            && checkpoints.FindExact({3, 48}) != nullptr
            && checkpoints.FindExact({3, 72}) != nullptr,
        "confirmed retirement preserves the nearest checkpoint anchor");
    ReplayCorrectionPlan correction{};
    expect(PlanReplayCorrection({3, 90}, {3, 96}, batches, checkpoints,
            Schema::checkpoint_interval - 1, correction).ok()
            && correction.resimulation_base == FrameCoordinate{3, 72},
        "retained confirmed window still plans the exact correction base");

    for (std::uint64_t frame = 97; frame <= 192; ++frame)
    {
        if (frame % 24 == 0)
        {
            checkpoints.DiscardBeforeRetainingNearest({3, frame - 64});
            auto checkpoint = prototype;
            checkpoint.coordinate = {3, frame};
            expect(checkpoints.SaveCopyPrewarmed(checkpoint).ok(),
                "confirmed-prefix retirement crosses the old fixed-store boundary");
        }
    }
    expect(checkpoints.BytesUsed() != 0
            && checkpoints.FindExact({3, 192}) != nullptr,
        "long online history remains bounded without losing the live suffix");
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
    expect(!ShouldReleaseCorrectionScratchOnRebaseline(true, false),
        "acknowledged round replacement retains prewarmed correction scratch");
    expect(ShouldReleaseCorrectionScratchOnRebaseline(false, false)
            && !ShouldReleaseCorrectionScratchOnRebaseline(false, true),
        "only stock-owned non-preserving rebaseline releases correction scratch");
    expect(OnlineScratchRequiresTransientPayload(
                OnlineScratchRole::CorrectionUndo, false)
            && OnlineScratchRequiresTransientPayload(
                OnlineScratchRole::CorrectionVerified, false)
            && OnlineScratchRequiresTransientPayload(
                OnlineScratchRole::Diagnostic, false)
            && !OnlineScratchRequiresTransientPayload(
                OnlineScratchRole::CorrectionCanonical, true)
            && !OnlineScratchRequiresTransientPayload(
                OnlineScratchRole::TimelineCanonical, false)
            && OnlineScratchRequiresTransientPayload(
                OnlineScratchRole::TimelineCanonical, true),
        "online prewarm reserves local payloads only for transient scratch roles");

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

    NativeBatchTimeline generation_transition_batches{4, 8};
    NativeBatchEnvelope generation_transition{};
    generation_transition.batch_id = 1;
    generation_transition.entry_coordinate = {1, 3};
    generation_transition.exit_coordinate = {2, 1};
    generation_transition.coordinate_count = 1;
    generation_transition.input_generation_changed = true;
    const std::array generation_transition_coordinates{
        FrameCoordinate{2, 1}};
    expect(generation_transition_batches.Append(generation_transition,
               generation_transition_coordinates).ok(),
        "retain a generation-transition batch as diagnostic history");
    SnapshotStore transition_entries{
        1024 * 1024, 8, CapacityPolicy::RejectNew};
    expect(transition_entries.Save({{1, 3}, 1, {}, {}}).ok(),
        "save the retired-generation transition entry");
    expect(PlanReplaySeek({2, 1}, generation_transition_batches,
               transition_entries, 29, plan).code
            == FailureCode::GenerationMismatch
            && plan.failure_stage == 5
            && plan.failure_base == FrameCoordinate{1, 3}
            && plan.failure_entry == FrameCoordinate{1, 3}
            && plan.failure_exit == FrameCoordinate{2, 1},
        "seek planning identifies a retired-generation checkpoint before "
        "attempting reconstruction");

    NativeBatchEnvelope current_generation{};
    current_generation.batch_id = 2;
    current_generation.entry_coordinate = {2, 1};
    current_generation.exit_coordinate = {2, 2};
    current_generation.coordinate_count = 1;
    const std::array current_generation_coordinates{FrameCoordinate{2, 2}};
    expect(generation_transition_batches.Append(current_generation,
               current_generation_coordinates).ok(),
        "append the first replayable batch in the replacement generation");
    expect(transition_entries.Save({{2, 1}, 1, {}, {}}).ok(),
        "save a replacement-generation entry after the transition");
    expect(PlanReplaySeek({2, 2}, generation_transition_batches,
               transition_entries, 29, plan).ok()
            && plan.resimulation_base == FrameCoordinate{2, 1}
            && plan.first_batch_index == 1,
        "seek planning starts after the generation transition when a valid "
        "replacement-generation entry exists");

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

    PresentationJournal corrected{8, 64};
    CountingSink corrected_sink;
    event.coordinate = {2, 10};
    expect(corrected.RecordPresented(event).ok()
            && corrected.ReplaceFrom({2, 10}, std::span{&event, 1}).ok()
            && corrected.CommitThrough({2, 10}, corrected_sink).ok(),
        "correction reuses an already-presented event");
    auto local = corrected.TakeDrainedCorrection();
    expect(local && local->reused_events == 1 && local->published_events == 0
            && local->changed_published_events == 0 && local->final_drain,
        "positive commit totals do not imply changed publication");
    event.coordinate = {2, 11};
    expect(corrected.ReplaceFrom({2, 11}, std::span{&event, 1}).ok()
            && corrected.CommitThrough({2, 11}, corrected_sink).ok(),
        "new correction event publishes through the real sink");
    local = corrected.TakeDrainedCorrection();
    expect(local && local->changed_published_events == 1 && corrected_sink.count == 1,
        "correction-local observation records actual changed publication");
    event.coordinate = {2, 12};
    expect(corrected.ReplaceFrom({2, 12}, std::span{&event, 1}).ok(),
        "prepare an unpublished correction before generation exit");
    corrected.InvalidateGeneration(2);
    expect(corrected.CommitThrough({2, 12}, corrected_sink).ok(), "drain after generation exit");
    local = corrected.TakeDrainedCorrection();
    expect(local && local->discarded_events == 1 && !local->final_drain,
        "generation exit cannot relabel discarded publication as a successful drain");
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

void test_replay_source_registration_transaction()
{
    using R=ReplaySourceRegistration;
    struct Fixture {
        std::array<std::byte,0x5000> input{};
        std::array<std::byte,0x500> source{};
        unsigned allocations{},frees{},destroys{};
        bool bound{true},fail_write{};
        R::Ops ops() {
            return {this,0x140000000,
                [](void*,std::uintptr_t p,void* out,std::size_t n){std::memcpy(out,reinterpret_cast<void*>(p),n);return true;},
                [](void* u,std::uintptr_t p,const void* in,std::size_t n){auto& f=*static_cast<Fixture*>(u);if(f.fail_write){f.fail_write=false;return false;}std::memcpy(reinterpret_cast<void*>(p),in,n);return true;},
                [](void* u){return static_cast<Fixture*>(u)->bound;},
                [](void* u,std::size_t n)->void*{++static_cast<Fixture*>(u)->allocations;return new std::byte[n];},
                [](void* u,void* p){++static_cast<Fixture*>(u)->frees;delete[] static_cast<std::byte*>(p);},
                [](void*,std::uintptr_t owner,std::uint64_t token,void* p){auto* b=static_cast<std::byte*>(p);const std::uintptr_t v=0x143298810;const std::int32_t units=2;std::memcpy(b,&v,8);std::memcpy(b+8,&owner,8);std::memcpy(b+0x18,&token,8);std::memcpy(b+0x30,&units,4);return true;},
                [](void* u,void*){++static_cast<Fixture*>(u)->destroys;}};
        }
        R::Image read(){R::Image i;expect(R::Capture(ops(),reinterpret_cast<std::uintptr_t>(input.data()),reinterpret_cast<std::uintptr_t>(source.data()),i),"capture source fixture");return i;}
    };
    for(unsigned scenario=0;scenario<8;++scenario) {
        Fixture f;auto b=f.read();auto a=b;a.storage.count=1;a.handle=17;
        R::Prepared p;expect(p.Prepare(f.ops(),a,b,32768).ok(),"prepare source ownership");
        if(scenario==0){expect(p.Undo().ok() && f.read()==b && f.frees==1,"cancel before publication retains B");continue;}
        if(scenario==1){f.fail_write=true;expect(!p.Publish().ok(),"partial publication rejects");expect(p.Undo().ok() && f.read()==b && f.frees==1,"partial publication recovers both B roots");continue;}
        expect(p.Publish().ok(),"publish retained replay receiver");
        expect(p.BeginExecution(32768).ok(),"source execution retains B");
        expect(!p.Undo().ok(),"unsettled source cannot undo");
        f.bound=false;expect(!p.ReopenExecution().ok(),"unsettled reopen rejects invalid bindings");f.bound=true;
        expect(p.ReopenExecution().ok() && p.ReopenExecution().ok() && !p.Undo().ok(),
            "early participant failure and reopening retry retain native ownership");
        if(scenario==5) {
            auto native=f.read();f.ops().destroy_wrapper(&f,reinterpret_cast<void*>(native.storage.data));f.ops().free(&f,reinterpret_cast<void*>(native.storage.data));
            std::memset(f.input.data()+0x43d0,0,16);std::memset(f.source.data()+0x3c8,0,8);
            expect(p.ReopenExecution().ok() && p.ReopenExecution().ok(),"reopening native removal never reads stale A storage");
        }
        if(scenario==7) {
            auto native=f.read();auto replacement=native;
            auto* allocation=f.ops().allocate(&f,4*R::stride);std::memset(allocation,0,4*R::stride);
            replacement.storage.data=reinterpret_cast<std::uintptr_t>(allocation);replacement.handle=31;
            expect(f.ops().copy_wrapper(&f,replacement.source,replacement.handle,allocation),"native C receiver reallocation");
            f.ops().destroy_wrapper(&f,reinterpret_cast<void*>(native.storage.data));f.ops().free(&f,reinterpret_cast<void*>(native.storage.data));
            std::memcpy(f.input.data()+0x43d0,&replacement.storage,16);std::memcpy(f.source.data()+0x3c8,&replacement.handle,8);
            expect(p.ReopenExecution().ok() && p.ReopenExecution().ok() && !p.Undo().ok(),
                "reallocated native C reopens without adopting stale A or permitting direct undo");
        }
        expect(p.SettleExecution().ok(),"settle current source owner");
        if(scenario==2){f.bound=false;expect(!p.Undo().ok() && !f.frees,"invalid bindings preserve allocations");f.bound=true;expect(p.Undo().ok() && f.read()==b && f.frees==1,"settled source restores exact B");}
        if(scenario==3){expect(p.ReopenExecution().ok() && p.ReopenExecution().ok() && !p.Undo().ok()
            && p.SettleExecution().ok() && p.Undo().ok() && f.read()==b,"reopened source retry restores B after fresh settlement");}
        if(scenario==4){expect(p.Commit().ok() && p.Commit().ok() && !f.frees,"commit leaves native current allocation alive");auto c=f.read();f.ops().destroy_wrapper(&f,reinterpret_cast<void*>(c.storage.data));f.ops().free(&f,reinterpret_cast<void*>(c.storage.data));}
        if(scenario==5){expect(p.Undo().ok() && f.read()==b && f.frees==1,"native removal never retires stale A twice");}
        if(scenario==7){expect(p.Undo().ok() && f.read()==b && f.frees==2 && f.destroys==2,
            "fresh C settlement recovers B and retires reallocated receiver exactly once");}
        if(scenario==6){
            expect(p.Commit().ok(),"establish registered B");auto registered=f.read();auto target=registered;target.handle=23;
            R::Prepared next;expect(next.Prepare(f.ops(),target,registered,32768).ok() && next.Publish().ok(),"private A keeps registered B storage");
            expect(next.BeginExecution(32768).ok(),"registered B execution begins");
            auto* token=reinterpret_cast<std::uint64_t*>(registered.storage.data+0x18);
            const auto saved_token=*token;*token=999;
            expect(!next.ReopenExecution().ok(),"unsettled reopen rejects corrupt private B");*token=saved_token;
            expect(next.ReopenExecution().ok() && next.SettleExecution().ok()
                && next.ReopenExecution().ok() && next.ReopenExecution().ok() && !next.Undo().ok()
                && next.SettleExecution().ok() && next.Undo().ok(),"registered B recovery after early and repeated reopening");
            expect(f.read()==registered && f.frees==1,"B allocation and handle recovered exactly");
            f.ops().destroy_wrapper(&f,reinterpret_cast<void*>(registered.storage.data));f.ops().free(&f,reinterpret_cast<void*>(registered.storage.data));
        }
    }
}

void test_replay_source_route_admission()
{
    std::array<std::byte,0x7000> storage{};
    const auto first=reinterpret_cast<std::uintptr_t>(storage.data());
    const auto manager=first, input=first+0x1000, replay=first+0x6000, entries=first+0x6800;
    constexpr std::uintptr_t base=0x140000000;
    const auto write=[](std::uintptr_t address,const auto& value){std::memcpy(reinterpret_cast<void*>(address),&value,sizeof(value));};
    const auto read=[&](std::uintptr_t address,auto& value){
        if(address<first || address-first>storage.size() || sizeof(value)>storage.size()-(address-first))return false;
        std::memcpy(&value,reinterpret_cast<void*>(address),sizeof(value));return true;};
    write(manager+0x478,input);write(input+0x43d0,entries);write(input+0x43d8,std::int32_t{1});write(input+0x43dc,std::int32_t{2});
    write(replay+0x3c8,std::uintptr_t{17});write(entries,base+0x3298810);write(entries+8,replay);
    write(entries+0x18,std::uintptr_t{17});write(entries+0x30,std::int32_t{2});
    const auto original=storage;
    auto route=InspectReplayInputRoute(read,base,manager,replay);
    expect(route.valid && route.registered && route.matches==1 && storage==original,"native source registration admission is read-only");
    write(entries+0x18,std::uintptr_t{18});
    expect(!InspectReplayInputRoute(read,base,manager,replay).valid,"source handle mismatch rejects");
    storage=original;std::memcpy(reinterpret_cast<void*>(entries+0x40),reinterpret_cast<void*>(entries),0x40);
    write(input+0x43d8,std::int32_t{2});
    expect(!InspectReplayInputRoute(read,base,manager,replay).valid,"duplicate replay source subscriptions reject");
    storage=original;write(input+0x43d8,std::int32_t{});write(replay+0x3c8,std::uintptr_t{});
    route=InspectReplayInputRoute(read,base,manager,replay);
    expect(route.valid && !route.registered,"completed replay can have a valid absent subscription but cannot feed restored input");
    write(input+0x43d8,std::int32_t{65});
    expect(!InspectReplayInputRoute(read,base,manager,replay).valid,"source registry inventory remains bounded");
}

void test_sc6_replay_source_round_transaction()
{
    RawReplayBridgeFixture f;
    std::array<std::byte,32> rounds{};
    std::array<std::array<std::byte,48>,2> recorders{};
    std::array<std::array<std::byte,32>,4> objects{};
    std::array<std::array<std::byte,16>,4> inputs{};
    f.write(f.replay,0x3b8,rounds.data());f.write(f.replay,0x3c4,std::int32_t{2});
    for(unsigned round=0;round<2;++round) {
        f.write(rounds,round*16,recorders[round].data());
        f.write(rounds,round*16+8,std::int32_t{2});f.write(rounds,round*16+12,std::int32_t{2});
        for(unsigned player=0;player<2;++player) {
            auto& object=objects[round*2+player];auto& recorder=recorders[round];
            f.write(recorder,player*24,std::uint32_t{16});f.write(recorder,player*24+4,std::uint32_t{4});
            f.write(recorder,player*24+16,object.data());
            f.write(object,0,std::uintptr_t{0x328e948});f.write(object,8,inputs[round*2+player].data());
            f.write(object,16,std::int32_t{16});f.write(object,20,std::int32_t{16});f.write(object,24,std::int32_t{4});
        }
    }
    Sc6ReplayNativeBridge bridge(f.resolvers());ReplaySourceState a{},b{},actual{};
    f.write(f.replay,0x3a0,std::int32_t{2});expect(bridge.CapturePlaybackSource(a,true).ok(),"capture round A source");
    f.write(f.replay,0x398,std::uint8_t{0});f.write(f.replay,0x39c,std::int32_t{1});f.write(f.replay,0x3a0,std::int32_t{7});
    expect(bridge.CapturePlaybackSource(b,true).ok(),"capture inactive round B source beyond input end");
    const auto original=f.replay;const auto original_objects=objects;const auto original_recorders=recorders;
    using Scope=Sc6ReplayNativeBridge::SourceRestoreScope;
    expect(!bridge.RestorePlaybackSource(b,a,true).ok() && f.replay==original,"legacy source scope cannot cross rounds");
    expect(bridge.RestorePlaybackSource(b,a,true,Scope::RetainedReplay).ok()
        && bridge.CapturePlaybackSource(actual,true).ok() && actual==a,"retained replay restores active round and cursor");
    expect(bridge.RestorePlaybackSource(a,b,true,Scope::RetainedReplay).ok() && f.replay==original,
        "source B undo preserves every tracker byte including padding");
    expect(objects==original_objects && recorders==original_recorders,"round restore never refreshes or overwrites authored metrics");
    f.write(objects[0],24,std::int32_t{3});
    expect(bridge.RestorePlaybackSource(b,a,true,Scope::RetainedReplay).code==FailureCode::UnsupportedContent
        && f.replay==original,"noncanonical target recorder rejects before source publication");
    objects=original_objects;
#if defined(_WIN32)
    auto* pages=static_cast<std::byte*>(VirtualAlloc(nullptr,8192,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    expect(pages!=nullptr,"allocate interrupted source publication fixture");
    if(pages) {
        auto* replay=pages+4096-0x3a0;std::memcpy(replay,f.replay.data(),f.replay.size());
        auto resolvers=f.resolvers();resolvers.user=replay;resolvers.replay_player=[](void* p) noexcept {return p;};
        Sc6ReplayNativeBridge interrupted(resolvers);ReplaySourceState before{};
        expect(interrupted.CapturePlaybackSource(before,true).ok(),"capture protected B source");
        auto target=a;target.owner=reinterpret_cast<std::uintptr_t>(replay);
        DWORD old{};expect(VirtualProtect(pages+4096,4096,PAGE_READONLY,&old)!=0,"protect cursor after writable round prefix");
        expect(interrupted.RestorePlaybackSource(before,target,true,Scope::RetainedReplay).code==FailureCode::RestoreVerificationFailed
            && interrupted.CapturePlaybackSource(actual,true).ok() && actual==before
            && std::memcmp(replay,original.data(),original.size())==0,"interrupted round publication independently verifies complete B recovery");
        DWORD ignored{};VirtualProtect(pages+4096,4096,old,&ignored);VirtualFree(pages,0,MEM_RELEASE);
    }
#endif
}

void test_hud_playback_retains_logical_time_without_player_ownership()
{
    std::array<std::byte,0x780> native{};
    const auto write=[&](std::size_t offset,const auto& value) {
        std::memcpy(native.data()+offset,&value,sizeof(value));
    };
    // Native UMG clock is a double even though evaluation converts it to float.
    // This input is not imported from an expected gameplay observation.
    const double time=1.0/3.0, upper=0.35;
    write(0x6a0,time);write(0x6d0,upper);
    write(0x6b8,std::uint8_t{1});write(0x6c8,std::uint8_t{1});
    write(0x6d8,std::int32_t{1});write(0x750,std::int32_t{1});
    write(0x758,1.0f);write(0x760,std::uint8_t{1});
    ReplayHudPlayback a;
    const auto original=native;
    expect(ReplayHudPlayback::Read(native,a) && a.forward_first_loop(),"HUD logical first-loop shape");
    expect(native==original && a.time==time && a.time!=double(float(time)),"HUD read preserves native double and storage");
    write(0x6a0,upper);write(0x6d8,std::int32_t{0});write(0x754,std::int32_t{1});
    ReplayHudPlayback completed;
    expect(ReplayHudPlayback::Read(native,completed) && !completed.forward_first_loop()
        && a.time==time && a.status==1 && a.completed_loops==0,"HUD capture survives original player completion");
    auto rejected=a;
    expect(!ReplayHudPlayback::Read(std::span(native).first(0x779),rejected)
        && rejected.time==a.time && rejected.status==a.status,"HUD short read leaves prior logical value intact");
    for(unsigned variant=0;variant<8;++variant) {
        auto other=a;
        switch(variant) {
        case 0:other.mode=1;other.forward=0;break;
        case 1:other.mode=2;break;
        case 2:other.completed_loops=1;break;
        case 3:other.offset=std::numeric_limits<double>::infinity();break;
        case 4:other.time=std::numeric_limits<double>::quiet_NaN();break;
        case 5:other.rate=std::numeric_limits<float>::infinity();break;
        case 6:other.lower_kind=2;break;
        case 7:other.time=upper+0.01;break;
        }
        expect(!other.forward_first_loop(),"HUD unsupported playback is not normalized into reconstruction");
    }
    const float asset_start=-9.313226e-10f,asset_end=0.35000002f;
    auto shifted=a;shifted.offset=double(asset_start);shifted.upper=double(float(asset_end-asset_start));
    expect(shifted.forward_first_loop() && shifted.matches_asset_range(asset_start,asset_end),"HUD authored nonzero offset retains native range arithmetic");
    shifted.offset=0;
    expect(!shifted.matches_asset_range(asset_start,asset_end),"HUD offset cannot be normalized away");
}

void test_hud_player_undo_survives_completion_and_partial_start()
{
    using Journal=ReplayHudPlayerOperation;
    unsigned finishes{},starts{};
    auto finish=[&]{++finishes;return true;};
    auto start=[&]{++starts;return true;};
    Journal untouched;
    expect(!untouched.Publish(finish,start) && !starts && !finishes,"HUD publication requires preparation");
    expect(untouched.Prepare() && untouched.Undo(finish) && !finishes,"HUD prepublication cancellation leaves native B untouched");
    expect(!untouched.Commit(),"HUD cancelled journal cannot commit");

    Journal partial;
    bool evaluation_owned{},native_playing{};
    expect(partial.Prepare() && !partial.Publish(finish,[&]{
        ++starts;evaluation_owned=true;native_playing=true;return false;
    }) && partial.phase()==Journal::Phase::Starting,"HUD failed start retains publication ownership");
    expect(!partial.Commit(),"HUD incomplete start cannot commit B away");
    expect(!partial.Undo([&]{native_playing=false;return false;})
        && partial.phase()==Journal::Phase::FinishingUndo && evaluation_owned,"HUD failed undo retains evaluation ownership");
    expect(partial.Undo([&]{evaluation_owned=false;return true;})
        && partial.phase()==Journal::Phase::Retired,"HUD undo retry reaches completed retirement");
    expect(partial.Undo([&]{expect(false,"HUD completed undo must not repeat native work");return false;}),"HUD repeated undo is completed");

    Journal completed;
    expect(completed.Prepare() && completed.Publish(finish,[&]{evaluation_owned=true;native_playing=true;return true;}),"HUD player publication completes");
    // Native141826F90 changes status/membership on natural completion without
    // finishing the evaluation root. Journal ownership must not follow status.
    native_playing=false;
    expect(evaluation_owned && completed.Undo([&]{++finishes;evaluation_owned=false;return true;})
        && !native_playing && !evaluation_owned,"HUD undo finishes a naturally stopped player missing from C membership");

    Journal committed;
    expect(committed.Prepare() && committed.Publish(finish,start) && committed.Commit()
        && committed.Commit() && !committed.Undo(finish),"HUD native ownership follows explicit enclosing commit only");
    Journal early_failure;
    expect(early_failure.Prepare() && !early_failure.Publish([]{return false;},start)
        && early_failure.phase()==Journal::Phase::FinishingOriginal
        && early_failure.Undo(finish),"HUD original evaluation cleanup failure remains recoverable");
}

void test_hud_private_B_publication_and_retirement()
{
    using J=ReplayHudPrivatePlayer;
    std::array<std::byte,0x780> b{};b[0x6d8]=std::byte{1};
    const auto original=b;
    unsigned finishes{},attachments{};
    J untouched;
    expect(untouched.Prepare(b) && untouched.Recover(b,[&]{++attachments;return true;})
        && !attachments && !untouched.Commit(b,[&]{++finishes;return true;}),"private HUD prepublication cancellation cannot retire B");
    J partial;
    expect(partial.Prepare(b) && !partial.Park(b,[]{return false;}) && partial.phase()==J::Phase::Private,
        "private HUD journals detachment before allocation/publication failure");
    expect(!partial.Recover(b,[]{return false;}) && partial.phase()==J::Phase::Private,
        "private HUD failed membership recovery retains B");
    b[0x6a0]=std::byte{1};
    expect(!partial.Recover(b,[&]{++attachments;return true;}) && !attachments,
        "private HUD rejects changed B instead of installing witness bytes");
    b=original;
    expect(partial.Recover(b,[&]{++attachments;return true;}) && attachments==1
        && partial.Recover(b,[&]{++attachments;return false;}) && attachments==1,
        "private HUD complete B recovery is idempotent");
    expect(!partial.Commit(b,[&]{++finishes;return true;}) && !finishes,"recovered B cannot be discarded by commit");
    J committed;
    expect(committed.Prepare(b) && committed.Park(b,[]{return true;}),"private HUD retains original evaluation during C execution");
    expect(!committed.Commit(b,[&]{++finishes;b[0x6d8]=std::byte{};return false;})
        && committed.phase()==J::Phase::Retiring,"private HUD failed native retirement retains explicit ownership");
    expect(!committed.Recover(b,[]{return true;}),"irreversible retirement cannot claim B recovery");
    expect(committed.Commit(b,[&]{++finishes;return true;}) && finishes==2
        && committed.Commit(b,[&]{++finishes;return false;}) && finishes==2,
        "private HUD retirement retry completes once without stale witness installation");
    J invalid;
    expect(!invalid.Prepare(std::span<const std::byte>(b).first(0x779)) && invalid.phase()==J::Phase::Empty,
        "private HUD rejects incomplete native player witness");
}

void test_streamable_domain_ignores_only_retirement_eligible_records()
{
    using D=ReplayStreamableDomain;using M=ReplayOcclusionHistory;
    std::array<std::byte,0xc0> manager{};
    std::array<std::byte,0x20> entry{};
    std::array<std::byte,0x30> record{};
    std::array<std::byte,0x20> handle{};
    std::array<std::byte,0x10> control{},pair{};
    M::Write(manager.data(),8,entry.data());M::Write(manager.data(),0x10,1);M::Write(manager.data(),0x14,1);
    M::Write(manager.data(),0x18,1u);M::Write(manager.data(),0x30,1);M::Write(manager.data(),0x34,32);
    M::Write(entry.data(),0x10,record.data());M::Write(record.data(),0,std::uintptr_t(0x12340000));
    M::Write(record.data(),0x20,pair.data());M::Write(record.data(),0x28,1);M::Write(record.data(),0x2c,1);
    M::Write(pair.data(),0,std::uintptr_t(1));M::Write(pair.data(),8,control.data());M::Write(control.data(),12,1);
    bool relevant{};D::Diagnostic diagnostic{};
    auto digest=[&]() {
        std::uint64_t hash=14695981039346656037ull;
        const bool ok=D::Manager(manager.data(),hash,relevant,diagnostic);
        return std::pair{ok,relevant?hash:std::uint64_t(0)};
    };
    const auto original=manager;const auto original_record=record;const auto original_control=control;
    const auto expired=digest();
    expect(expired.first && !expired.second && manager==original && record==original_record && control==original_control,
        "expired weak payload is never dereferenced and retirement-eligible cache is read-only");
    M::Write(manager.data(),0x10,0);M::Write(manager.data(),0x30,0);M::Write(manager.data(),0x18,0u);
    expect(digest()==expired,"native removal of expired-only records preserves callback lease identity");manager=original;
    M::Write(control.data(),12,0);expect(!digest().first,"malformed expired controller still rejects");control=original_control;
    M::Write(record.data(),8,static_cast<unsigned char>(1));expect(!digest().first,"in-flight request cannot be normalized away");record=original_record;
    M::Write(record.data(),0x18,1);expect(!digest().first,"pending strong waiter rejects before retirement normalization");record=original_record;
    M::Write(pair.data(),0,handle.data());M::Write(control.data(),8,1);
    expect(!digest().first,"unfinished live callback handle rejects");
    M::Write(handle.data(),0x10,static_cast<unsigned char>(1));const auto live=digest();
    expect(live.first && live.second,"completed live handle keeps its full resource lease");
    M::Write(record.data(),0,std::uintptr_t(0x12340001));
    expect(digest().second!=live.second,"changed live resource binding invalidates callback lease");record=original_record;
    M::Write(manager.data(),0x30,2);expect(!digest().first,"malformed sparse membership cannot be treated as empty");
}

void test_occlusion_private_history_reconstruction()
{
    using H=ReplayOcclusionHistory;
    std::array<std::byte,0x50> header{};
    std::vector<std::byte> entries(4*0x50),flags(4),hashes(4);
    H::Write(header.data(),8,4);H::Write(header.data(),0xc,4);
    H::Write(header.data(),0x28,4);H::Write(header.data(),0x2c,32);
    H::Write(header.data(),0x30,3);H::Write(header.data(),0x34,1);H::Write(header.data(),0x48,1);
    H::Write(flags.data(),0,7u);H::Write(hashes.data(),0,0);
    auto* q=reinterpret_cast<void*>(0x1000);auto* kept=reinterpret_cast<void*>(0x2000);
    for(int i=0;i<3;++i) {
        auto* row=entries.data()+i*0x50;H::Write(row,0,unsigned(770+i));
        H::Write(row,8,i==1?kept:q);H::Write(row,0x20,2);H::Write(row,0x24,2);
        H::Write(row,0x48,i==2?-1:i+1);H::Write(row,0x4c,0u);
    }
    H::Write(entries.data()+3*0x50,0,-1);H::Write(entries.data()+3*0x50,4,-1);
    // Independent complete B and immutable checkpoint A remain byte-identical.
    const auto original_header=header;const auto original_entries=entries;
    const auto original_flags=flags;const auto original_hashes=hashes;
    std::vector<void*> queries{q,kept,q};std::size_t leases=3;
    unsigned callbacks{},q_refs=5;
    const auto erase=[&](void* map,int slot) noexcept {
        ++callbacks;
        auto* rows=H::Read<std::byte*>(map);auto* f=H::Read<std::byte*>(map,0x20);
        auto* buckets=H::Read<std::byte*>(map,0x40);
        expect(rows==entries.data() && f==flags.data() && buckets==hashes.data(),"native erase only sees private A storage");
        auto* link=buckets;
        while(H::Read<int>(link)!=slot) link=rows+H::Read<int>(link)*0x50+0x48;
        auto* row=rows+slot*0x50;H::Write(link,0,H::Read<int>(row,0x48));
        expect(H::Read<void*>(row,8)==q,"only orphan query lease is released");--q_refs;
        const int old=H::Read<int>(map,0x30);H::Write(rows+old*0x50,0,slot);
        H::Write(row,0,-1);H::Write(row,4,old);H::Write(map,0x30,slot);
        H::Write(map,0x34,H::Read<int>(map,0x34)+1);
        H::Write(f,0,H::Read<unsigned>(f)&~(1u<<slot));
    };
    auto broken=hashes;H::Write(broken.data(),0,9);
    expect(!H::Prune(header,entries,flags,broken,queries,leases,[](unsigned,unsigned){return true;},erase).ok
        && !callbacks && q_refs==5 && entries==original_entries,"invalid hash chain rejects before native release");
    auto result=H::Prune(header,entries,flags,hashes,queries,leases,
        [](unsigned id,unsigned subquery){return id!=771 && !subquery;},erase);
    expect(result.ok && result.removed==2 && result.queries_released==2 && callbacks==2 && q_refs==3,
        "two orphan records release exactly their operation-owned references");
    expect(leases==1 && queries==std::vector<void*>{kept} && H::Read<int>(hashes.data())==1
        && H::Read<int>(header.data(),0x34)==3,"retained active history and native sparse map survive pruning");
    expect(H::Read<unsigned>(header.data(),0x10)==2 && H::Read<int>(header.data(),0x38)==1,
        "inline native flags/hash are synchronized without historical pointers");
    result=H::Prune(header,entries,flags,hashes,queries,leases,[](unsigned,unsigned){return false;},erase);
    expect(result.ok && !result.removed && callbacks==2,"repeated preparation does not double-release");
    expect(original_header!=header && H::Read<unsigned>(original_flags.data())==7
        && H::Read<int>(original_hashes.data())==0 && H::Read<unsigned>(original_entries.data())==770,
        "complete B/checkpoint copies remain unchanged for cancellation and future seeks");
}

void test_lighting_lod_binding_correspondence()
{
    ReplayLightingBinding a{};
    a.mesh=100;a.render_data=200;a.count=2;
    a.lods[0]={300,400,500};a.lods[1]={600,700,800};
    auto b=a;
    expect(a.Matches(b,3),"same mesh LOD and light-map domain admits relocated LCI slots");
    std::swap(b.lods[0],b.lods[1]);
    expect(!a.Matches(b,3),"equal LCI counts cannot hide changed LOD ordering");
    b=a;b.mesh++;
    expect(!a.Matches(b,3),"replacement mesh rejects historical LCI correspondence");
    b=a;b.render_data++;
    expect(!a.Matches(b,3),"replacement render data rejects historical LCI correspondence");
    b=a;b.lods[1][1]++;
    expect(!a.Matches(b,3),"changed light-map binding rejects historical uniform mapping");
    b=a;b.lods[1][2]++;
    expect(!a.Matches(b,3),"changed shadow-map binding rejects historical uniform mapping");
    expect(!a.Matches(a,2),"missing enumerated LCI rejects correspondence");
    b=a;b.count=17;
    expect(!b.Matches(b,18),"out-of-range LOD count rejects before access");
    b=a;b.lods[0][0]=0;
    expect(!b.Matches(b,3),"null mesh LOD rejects mapping");

    struct ReopenControl {int strong{},weak{},added{},removed{};bool leased{};
        bool operator==(const ReopenControl&) const=default;};
    const auto has_lease=[](const auto& c){return c.leased;};
    std::array<ReopenControl,2> bookkeeping{{{0,3,1,0,true},{0,1,1,0,true}}};
    const auto unchanged=bookkeeping;
    expect(!ReplayTraceReopenControls(bookkeeping,2,2,has_lease) && bookkeeping==unchanged
        && !ReplayTraceReopenControls(bookkeeping,2,2,has_lease) && bookkeeping==unchanged,
        "later weak retirement rejection preserves all metadata across retry");
    expect(ReplayTraceReopenControls(bookkeeping,2,1,has_lease)
        && bookkeeping[0].weak==2 && bookkeeping[1]==ReopenControl{},
        "C-only expired bookkeeping is forgotten while original B bookkeeping survives");
    bookkeeping=unchanged;bookkeeping[1].removed=1;const auto private_b_ref=bookkeeping;
    expect(!ReplayTraceReopenControls(bookkeeping,2,1,has_lease) && bookkeeping==private_b_ref,
        "C-only metadata cannot discard a displaced B weak reference");
    bookkeeping=unchanged;bookkeeping[1].leased=false;
    expect(!ReplayTraceReopenControls(bookkeeping,2,1,has_lease),"unleased C controller remains rejected");
    bookkeeping={{{1,1,1,0,false},{0,1,1,0,true}}};
    const auto retained_live=[](const auto& c){return c.strong==1;};
    expect(ReplayTraceReopenControls(bookkeeping,2,0,has_lease,retained_live)
        && bookkeeping[0]==ReopenControl{} && bookkeeping[1]==ReopenControl{},
        "live A/B owner newly referenced by C and C-expired controller relinquish bookkeeping together");
    bookkeeping={{{1,1,1,0,false},{}}};const auto live_before=bookkeeping;
    expect(!ReplayTraceReopenControls(bookkeeping,1,0,has_lease) && bookkeeping==live_before,
        "newly tracked live controller without exact A/B ownership rejects atomically");
    bookkeeping[0].removed=1;const auto live_b_reference=bookkeeping;
    expect(!ReplayTraceReopenControls(bookkeeping,1,0,has_lease,retained_live) && bookkeeping==live_b_reference,
        "retained live ownership never permits dropping displaced B reference bookkeeping");
    ReplayTraceWeakController current_expired{0x1234,0,1};
    expect(!current_expired.MatchesOwnership(0x1234,false,false,false),
        "expired C-only controller without an image lease rejects");
    expect(current_expired.RetainExpired(0x1234)
        && current_expired.MatchesOwnership(0x1234,true,false,false),
        "independent C weak lease admits destroyed state without A/B membership");
    const auto drop_controller=+[](void*) {};
    expect(current_expired.ReleaseExpired(0x1234,drop_controller)
        && current_expired.weak==1 && current_expired.strong==0,
        "C lease retirement preserves native weak ownership without resurrection");
    for(auto bad: {ReplayTraceWeakController{0x9999,0,1},ReplayTraceWeakController{0x1234,0,0},
                  ReplayTraceWeakController{0x1234,-1,1},ReplayTraceWeakController{0x1234,1,1}})
        expect(!bad.MatchesOwnership(0x1234,true,false,false),
            "C expired admission rejects wrong type, dead count, corruption and live C-only state");
    ReplayTraceWeakController live_trace{0x1234,1,1};
    expect(live_trace.MatchesOwnership(0x1234,false,true,true)
        && !live_trace.MatchesOwnership(0x1234,true,true,false),
        "live trace settlement still requires both A and B membership");
    ReplayTraceWeakController expired{0x1234,0,1};
    expect(expired.RetainExpired(0x1234) && expired.strong==0 && expired.weak==2,
        "captured expired weak reference pins controller without resurrecting state");
    // Native removes its original weak entry after capture; the image survives.
    --expired.weak;
    static int controller_deletions{};controller_deletions=0;
    const auto dispose=+[](void*) {++controller_deletions;};
    // C-only handoff keeps no controller pointer after C's lease is released.
    // Native can subsequently remove its last entry; the next image starts
    // from the retained original controls, never the destroyed C controller.
    ReplayTraceWeakController c_only{0x1234,0,2};
    bookkeeping={{{0,1,1,0,true},{}}};
    expect(ReplayTraceReopenControls(bookkeeping,1,0,has_lease) && bookkeeping[0]==ReopenControl{}
        && c_only.weak==2,"C-only reopening changes no native weak counts");
    expect(c_only.ReleaseExpired(0x1234,dispose) && controller_deletions==0
        && c_only.ReleaseExpired(0x1234,dispose) && controller_deletions==1,
        "C lease then final native entry retire controller exactly once after handoff");
    expect(ReplayTraceReopenControls(bookkeeping,0,0,has_lease) && controller_deletions==1,
        "repeated handoff has no stale C controller to inspect");
    controller_deletions=0;
    expect(expired.ReleaseExpired(0x1234,dispose) && controller_deletions==1 && expired.strong==0,
        "last expired weak owner deletes only the controller exactly once");
    expect(!expired.ReleaseExpired(0x1234,dispose) && controller_deletions==1,
        "double expired-controller release rejects");
    expired={0x1234,0,1};
    expect(expired.RetainExpired(0x1234) && expired.RetainExpired(0x1234),
        "A clone and complete B lease may coexist");
    expect(expired.ReleaseExpired(0x1234,dispose) && expired.ReleaseExpired(0x1234,dispose)
        && expired.weak==1 && expired.strong==0 && controller_deletions==1,
        "cancellation drops private owners while preserving original B weak reference");
    expired={0x1234,1,1};expect(!expired.RetainExpired(0x1234),"live trace cannot enter expired ownership");
    expired={0x1234,-1,1};expect(!expired.RetainExpired(0x1234),"corrupt strong count rejects");
    expired={0x1234,0,INT_MAX};expect(!expired.RetainExpired(0x1234),"weak overflow rejects");
    expired={0x1234,0,1};expect(!expired.RetainExpired(0x9999),"unknown controller type rejects");
    ReplayCreationRenderOwner visible{};
    visible.component=100;visible.owner=200;visible.storage=300;visible.parent=400;
    visible.mesh=500;visible.animation=600;visible.weak={7,8};visible.owner_weak={9,10};
    visible.slot=1;visible.count=2;visible.capacity=4;visible.flags=0x2e4e0e03;visible.visibility=0x411;
    expect(visible.ValidMembership(300,2,4,100),"creation membership validates exact native backing");
    auto weapon=visible;weapon.membership=ReplayCreationRenderOwner::Membership::WeaponSlot;
    weapon.storage=weapon.owner+0x390;weapon.slot=0;weapon.count=weapon.capacity=1;
    expect(weapon.ValidMembership(weapon.owner+0x390,1,1,weapon.component),"weapon requires exact inline fighter slot");
    expect(!weapon.ValidMembership(weapon.owner+0x390,1,1,weapon.component+1),"replaced weapon rejects before reconstruction");
    expect(!weapon.ValidMembership(300,2,4,weapon.component),"creation array cannot substitute for weapon slot");
    unsigned membership_reads{};
    const auto weapon_reader=[&](std::uintptr_t address,auto& value){
        ++membership_reads;
        if(address!=weapon.owner+0x390 || sizeof(value)!=sizeof(weapon.component))return false;
        std::memcpy(&value,&weapon.component,sizeof(value));return true;};
    expect(weapon.LiveMembership(weapon_reader) && membership_reads==1,"live weapon undo reads inline slot without creation-array substitution");
    expect(!visible.LiveMembership(weapon_reader),"creation owner cannot borrow weapon membership");
    expect(!weapon.LiveMembership([](std::uintptr_t,auto&){return false;}),"unreadable live weapon rejects undo");
    auto corrupt=weapon;corrupt.slot=1;
    expect(!corrupt.ValidMembership(corrupt.storage,1,1,corrupt.component),"out of range weapon membership rejects");
    corrupt=weapon;corrupt.storage++;
    expect(!corrupt.ValidMembership(corrupt.storage,1,1,corrupt.component),"weapon backing cannot relocate away from fighter slot");
    corrupt=weapon;corrupt.membership=ReplayCreationRenderOwner::Membership::CreationArray;
    expect(!weapon.SameBinding(corrupt),"membership kind remains immutable across undo");
    auto hidden=visible;hidden.visibility^=0x10;
    expect(visible!=hidden && visible.SameBinding(hidden),"captured visibility changes state without changing native component identity");
    auto invalid=hidden;invalid.visibility^=0x20;
    expect(!visible.SameBinding(invalid),"visibility admission cannot mask unrelated scene flags");
    invalid=hidden;invalid.mesh++;
    expect(!visible.SameBinding(invalid),"hidden variant cannot authorize replacement mesh assets");
    invalid=hidden;invalid.animation++;
    expect(!visible.SameBinding(invalid),"hidden variant cannot authorize replacement animation instances");
    invalid=hidden;invalid.weak[1]++;
    expect(!visible.SameBinding(invalid),"component generation remains an exact binding");
    invalid=hidden;invalid.primitive_id++;
    expect(!visible.SameBinding(invalid),"cached visibility cannot borrow a different primitive generation");
    invalid=hidden;invalid.slot++;
    expect(!visible.SameBinding(invalid),"component array membership remains an exact binding");
    invalid=hidden;invalid.flags^=0x20000000;
    expect(!visible.SameBinding(invalid),"visibility admission cannot mask scheduling registration");
}

void test_sc6_replay_bridge_transaction_and_undo()
{
    RawReplayBridgeFixture fixture;
    Sc6ReplayNativeBridge bridge{fixture.resolvers()};
    ReplaySourceState target_source{};
    fixture.write(fixture.replay, 0x3a0, std::int32_t{120});
    expect(bridge.CapturePlaybackSource(target_source).ok()
        && target_source.cursor == 120, "capture actual replay producer continuation");
    fixture.write(fixture.replay, 0x3a0, std::int32_t{480});
    ReplaySourceState frontier_source{};
    expect(bridge.CapturePlaybackSource(frontier_source).ok(), "capture source frontier");
    expect(bridge.RestorePlaybackSource(frontier_source, target_source).ok(),
        "rewind source cursor with simulation, before normal playback resumes");
    ReplaySourceState restored_source{};
    expect(bridge.CapturePlaybackSource(restored_source).ok()
        && restored_source == target_source, "source continuation restored exactly");
    expect(bridge.RestorePlaybackSource(frontier_source, target_source).code
        == FailureCode::RestorePreflightFailed, "reject stale producer frontier");
    const auto source_before_rejection = fixture.replay;
    auto replacement_source = target_source;
    ++replacement_source.recordings;
    expect(bridge.RestorePlaybackSource(target_source, replacement_source).code
        == FailureCode::RestorePreflightFailed
        && fixture.replay == source_before_rejection,
        "replacement recording identity cannot inherit another replay cursor");
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
    const auto thread = GetCurrentThreadId();
    constexpr unsigned seed = 0x01234500u; // Compatible zero-warmup native seed.
    std::array<int, 4> expected{};
    std::srand(seed);
    for (auto& value : expected) value = std::rand();

    UcrtRandBroker broker;
    expect(broker.Start().ok(), "start UCRT broker before its native stream exists");
    expect(broker.BindNative(&std::rand, &std::srand).ok(), "bind verified native CRT state access");
    broker.HandleSrand(thread, Schema::Sc6UcrtLayout::rng_init_srand_return_rva,
        seed, &std::srand);
    expect(broker.owner_thread_id() == thread,
        "allowlisted native seed binds the broker's simulation thread");
    expect(broker.AcquireOwnership(thread).ok(),
        "UCRT broker acquires only after an allowlisted seed");
    expect(broker.EnsureOwnership(thread).ok(),
        "UCRT ownership transition is idempotent for its owner");
    expect(broker.EnsureOwnership(thread + 1).code == FailureCode::WrongThread,
        "UCRT ownership transition rejects a different thread");
    for (const auto value : expected)
    {
        expect(broker.HandleRand(thread,
                Schema::Sc6UcrtLayout::movevm_rand_return_rva, &std::rand)
                == value,
            "owned calls retain the imported CRT sequence");
    }

    UcrtRandBrokerImage saved{};
    expect(broker.Capture(thread, saved).ok() && saved.draws == expected.size() && saved.native_draws == 0
            && saved.state == seed && saved.combat_state != seed,
        "capture value-only UCRT state and draw count");
    const int advanced = broker.HandleRand(thread,
        Schema::Sc6UcrtLayout::movevm_rand_return_rva, &std::rand);
    expect(broker.Restore(thread, saved).ok()
            && broker.HandleRand(thread,
                Schema::Sc6UcrtLayout::movevm_rand_return_rva, &std::rand)
                == advanced,
        "restored private MoveVM state reproduces the exact next draw");
    // A call bypassing the game IAT must be included in the actual CRT image.
    std::rand();
    UcrtRandBrokerImage external{};
    expect(broker.Capture(thread, external).ok(), "capture native state after an unobserved caller");
    const auto native_next = std::rand();
    expect(broker.Restore(thread, external).ok() && std::rand() == native_next,
        "restoration reproduces direct native calls without a private replacement result");
    expect(broker.ReleaseOwnership(thread).ok()
            && broker.mode() == UcrtRandBrokerMode::Observing,
        "qualification cleanup releases UCRT ownership without disabling observation");
    expect(broker.EnsureOwnership(thread).ok()
            && broker.mode() == UcrtRandBrokerMode::Owned,
        "a later qualification cycle can reacquire the same synchronized stream");

    std::srand(seed);
    const int forwarded = broker.HandleRand(thread + 2, 0x1111, &std::rand);
    std::srand(seed);
    expect(forwarded == std::rand() && broker.mode() == UcrtRandBrokerMode::Owned,
        "non-allowlisted calls on other threads forward without broker mutation");
    broker.HandleRand(thread + 2, Schema::Sc6UcrtLayout::movevm_rand_return_rva,
        &std::rand);
    expect(broker.mode() == UcrtRandBrokerMode::Failed
            && broker.failure() == FailureCode::WrongThread,
        "allowlisted callsite migration fails the broker terminally");
}

void test_ucrt_broker_separates_movevm_and_native_presentation_lanes()
{
#ifdef _WIN32
    const auto thread = GetCurrentThreadId();
    constexpr unsigned battle_seed = 0x12345007u;
    // LuxBattle_InitRngAndHashPrimes: srand(seed >> 4), then seed & 0xFFF
    // CRT warm-up draws. These are return RVAs, not CALL instruction RVAs.
    constexpr std::uintptr_t seed_return = 0x34f634;
    constexpr std::uintptr_t warmup_return = 0x34f658;
    constexpr std::uintptr_t movevm_return = 0x366ff4; // Opcode 0x50006.
    // Ground yaw/jitter, tile-pool emitter, and audio: retained native callers
    // in docs/evidence/rollback-ground-ucrt-live-shared-audio-2026-09-22.json.
    constexpr std::array<std::uintptr_t, 4> presentation_returns{
        0x895d6e, 0x896105, 0x1f9bd5c, 0x54f91e};

    // Observe the actual owner-thread PTD independently of the owned broker's
    // image; checking only its diagnostic state/draw count could hide forwarding.
    UcrtRandBroker observer;
    std::uint32_t original_native_state{};
    const bool observing = observer.Start().ok()
        && observer.BindNative(&std::rand, &std::srand).ok()
        && observer.ObserveNative(thread, original_native_state).ok();
    expect(observing, "lane regression binds the real owner-thread UCRT PTD");
    if (!observing) return;
    struct RestoreNativeState
    {
        std::uint32_t state;
        ~RestoreNativeState() { std::srand(state); }
    } restore_native{original_native_state};

    const auto initialize = [&](UcrtRandBroker& broker) {
        if (!broker.Start().ok() || !broker.BindNative(&std::rand, &std::srand).ok())
            return false;
        broker.HandleSrand(thread, seed_return, battle_seed >> 4, &std::srand);
        for (unsigned i = 0; i < (battle_seed & 0xfffu); ++i)
            broker.HandleRand(thread, warmup_return, &std::rand);
        return broker.AcquireOwnership(thread).ok();
    };
    struct Draws
    {
        std::array<int, 8> movevm{};
        std::array<int, 32> presentation{};
        bool movevm_preserved_native{true};
        std::uint32_t native_end{};
    };
    const auto draw_interleaved = [&](UcrtRandBroker& broker, Draws& draws) {
        for (std::size_t i = 0; i < draws.movevm.size(); ++i)
        {
            for (std::size_t j = 0; j < presentation_returns.size(); ++j)
                draws.presentation[i * presentation_returns.size() + j] =
                    broker.HandleRand(thread, presentation_returns[j], &std::rand);
            std::uint32_t before{}, after{};
            if (!observer.ObserveNative(thread, before).ok()) return false;
            draws.movevm[i] = broker.HandleRand(thread, movevm_return, &std::rand);
            if (!observer.ObserveNative(thread, after).ok()) return false;
            draws.movevm_preserved_native &= before == after;
        }
        return observer.ObserveNative(thread, draws.native_end).ok();
    };

    // Independent modified control: same seed/warm-up and MoveVM inputs,
    // without presentation calls. No saved/expected image is imported.
    UcrtRandBroker control;
    const bool control_ready = initialize(control);
    expect(control_ready, "initialize independently seeded MoveVM control");
    if (!control_ready) return;
    std::array<int, 8> expected_movevm{};
    for (auto& value : expected_movevm)
        value = control.HandleRand(thread, movevm_return, &std::rand);
    control.Stop();

    // Native presentation reference uses the actual CRT, not a fixture LCG.
    std::srand(battle_seed >> 4);
    for (unsigned i = 0; i < (battle_seed & 0xfffu); ++i) std::rand();
    std::array<int, 32> expected_presentation{};
    for (auto& value : expected_presentation) value = std::rand();
    std::uint32_t expected_native_end{};
    const bool reference_observed = observer.ObserveNative(thread, expected_native_end).ok();
    expect(reference_observed, "observe independently advanced native presentation reference");
    if (!reference_observed) return;

    UcrtRandBroker candidate;
    const bool candidate_ready = initialize(candidate);
    expect(candidate_ready, "initialize independently seeded interleaved candidate");
    if (!candidate_ready) return;
    Draws interleaved{};
    const bool interleaved_observed = draw_interleaved(candidate, interleaved);
    expect(interleaved_observed, "observe native PTD around every candidate MoveVM draw");
    if (!interleaved_observed) return;
    expect(interleaved.movevm == expected_movevm,
        "native presentation rand calls do not change MoveVM 0x50006 outputs");
    expect(interleaved.movevm_preserved_native,
        "MoveVM draws do not advance the actual owner-thread native UCRT PTD stream");
    expect(interleaved.presentation == expected_presentation
            && interleaved.native_end == expected_native_end,
        "presentation calls retain the independently advanced native UCRT sequence");

    // Both lanes have advanced, by different amounts (8 MoveVM / 32 native).
    // Replaying the suffix must restore each lane's own cursor, not copy one
    // cursor to both or reconstruct a stream from diagnostic draw counts.
    UcrtRandBrokerImage checkpoint{};
    const bool captured = candidate.Capture(thread, checkpoint).ok();
    expect(captured, "capture both RNG lanes after unequal draw counts");
    if (!captured) return;
    Draws continued{};
    const bool continuation_observed = draw_interleaved(candidate, continued);
    expect(continuation_observed, "observe both RNG lane continuations before restore");
    if (!continuation_observed) return;
    const int direct_native_next = std::rand(); // Bypass the game IAT too.
    const bool restored = candidate.Restore(thread, checkpoint).ok();
    expect(restored, "restore broker checkpoint after advancing both RNG lanes");
    if (!restored) return;
    std::uint32_t restored_native{};
    expect(observer.ObserveNative(thread, restored_native).ok()
            && restored_native == interleaved.native_end,
        "broker restore rewinds the actual native PTD to its captured cursor");
    Draws replayed{};
    const bool replay_observed = draw_interleaved(candidate, replayed);
    expect(replay_observed, "observe both RNG lane continuations after restore");
    if (!replay_observed) return;
    expect(replayed.movevm == continued.movevm,
        "broker capture/restore reproduces the MoveVM lane continuation");
    expect(replayed.presentation == continued.presentation
            && replayed.native_end == continued.native_end
            && std::rand() == direct_native_next,
        "broker capture/restore reproduces presentation and direct native UCRT continuation");
    expect(continued.movevm_preserved_native && replayed.movevm_preserved_native,
        "MoveVM preserves native PTD before and after broker restore");
#endif
}

void test_ucrt_split_preflight_reset_and_complete_b()
{
#ifdef _WIN32
    const auto thread = GetCurrentThreadId();
    constexpr auto seed_rva = Schema::Sc6UcrtLayout::rng_init_srand_return_rva;
    constexpr auto warmup_rva = Schema::Sc6UcrtLayout::rng_init_rand_return_rva;
    constexpr auto movevm_rva = Schema::Sc6UcrtLayout::movevm_rand_return_rva;
    constexpr auto complete_rva = Schema::Sc6UcrtLayout::rng_init_xorshift_return_rva;
    constexpr unsigned seed = 0x01234500u;
    UcrtRandBroker broker;
    const auto start = [&] {
        return broker.Start().ok() && broker.BindNative(&std::rand, &std::srand).ok();
    };
    const auto capture = [&] {
        UcrtRandBrokerImage result{};
        expect(broker.Capture(thread, result).ok(), "capture production split image");
        return result;
    };
    expect(start(), "bind native broker for split preflight and recovery");
    expect(!broker.AcquireOwnership(thread).ok(), "ownership before native seed rejects");
    broker.HandleSrand(thread, seed_rva, seed, &std::srand);
    UcrtRandBrokerImage incomplete{};
    expect(!broker.EnsureOwnership(thread).ok() && !broker.Capture(thread, incomplete).ok(),
        "production admission and capture cannot complete a seed implicitly");
    expect(!broker.ObserveInitializationComplete(thread, complete_rva + 1).ok(),
        "wrong completion callsite does not grant ownership");
    expect(broker.ObserveInitializationComplete(thread, complete_rva).ok()
            && broker.mode() == UcrtRandBrokerMode::Observing,
        "zero-warmup native boundary establishes split before correction ownership");
    const auto initial = capture();
    expect(initial.state == seed && initial.combat_state == seed
            && initial.warmup_draws == 0 && initial.draws == 0,
        "zero warmup clones actual seed to both cursors");
    // The modified independent control has the same routing while Observing.
    broker.HandleRand(thread, movevm_rva, &std::rand);
    const auto observed = capture();
    expect(observed.state == seed && observed.draws == 1,
        "observing modified control routes MoveVM privately");
    expect(broker.EnsureOwnership(thread).ok(), "production admission after init succeeds");
    const auto a = capture();
    const auto expected_move = broker.HandleRand(thread, movevm_rva, &std::rand);
    const auto expected_native = broker.HandleRand(thread, 0x895d6e, &std::rand);
    const auto b = capture();
    expect(b.draws == a.draws + 1 && b.native_draws == a.native_draws + 1
            && b.combat_state != a.combat_state && b.state != a.state,
        "omitted or extra draw in either lane changes the complete image");
    expect(broker.Restore(thread, a).ok(), "publish A with complete B retained by caller");
    broker.HandleRand(thread, 0x1111, &std::rand);
    const auto unknown = capture();
    expect(unknown.combat_state == a.combat_state && unknown.draws == a.draws
            && unknown.state != a.state && unknown.unknown_draws == a.unknown_draws + 1
            && unknown.native_draws == a.native_draws + 1,
        "unknown owner calls stay native and remain in exact authoritative image");
    broker.HandleRand(thread, movevm_rva, &std::rand);
    expect(broker.Restore(thread, b).ok() && capture() == b,
        "cancel corrected suffix restores complete B in both lanes and counters");
    const auto b_next_move = broker.HandleRand(thread, movevm_rva, &std::rand);
    const auto b_next_native = std::rand();
    expect(broker.Restore(thread, b).ok()
            && broker.HandleRand(thread, movevm_rva, &std::rand) == b_next_move
            && std::rand() == b_next_native,
        "B cancellation reproduces private and direct-native continuation");
    expect(broker.Restore(thread, a).ok()
            && broker.HandleRand(thread, movevm_rva, &std::rand) == expected_move
            && broker.HandleRand(thread, 0x895d6e, &std::rand) == expected_native,
        "independent traversal from A replays both lane outputs");
    const auto retained = capture();
    for (unsigned mutation = 0; mutation < 7; ++mutation)
    {
        auto invalid = retained;
        switch (mutation) {
        case 0: --invalid.algorithm_version; break;
        case 1: --invalid.allowlist_version; break;
        case 2: ++invalid.epoch; break;
        case 3: invalid.combat_ready = false; break;
        case 4: ++invalid.seed_state; break;
        case 5: ++invalid.warmup_draws; break;
        case 6: invalid.unknown_draws = invalid.native_draws + 1; break;
        }
        expect(!broker.Restore(thread, invalid).ok() && capture() == retained
                && broker.Restore(thread, retained).ok(),
            "rejected target leaves both lanes untouched and complete B restorable");
    }
    expect(!broker.Restore(thread + 1, a).ok() && capture() == retained,
        "wrong-thread restore preflight has no state or mode side effects");
    expect(broker.ReleaseOwnership(thread).ok(), "release checkpoint permission");
    broker.HandleRand(thread, 0x54f91e, &std::rand);
    const auto release_native = capture().state;
    broker.HandleRand(thread, movevm_rva, &std::rand);
    expect(capture().state == release_native && !broker.Restore(thread, a).ok(),
        "release keeps split routing but rejects restore until reacquired");
    const auto released = capture();
    expect(broker.EnsureOwnership(thread).ok() && capture() == released,
        "reacquire never reclones over advanced combat state");

    broker.HandleSrand(thread, seed_rva, seed, &std::srand); // Native new round.
    expect(!broker.EnsureOwnership(thread).ok() && !broker.Restore(thread, retained).ok(),
        "new round revokes old epoch and rejects premature correction");
    expect(broker.ObserveInitializationComplete(thread, complete_rva).ok()
            && broker.EnsureOwnership(thread).ok(), "complete new-round split");
    const auto reset = capture();
    expect(reset.epoch != retained.epoch && reset.draws == 0 && reset.native_draws == 0
            && reset.unknown_draws == 0 && reset.state == seed && reset.combat_state == seed
            && !broker.Restore(thread, retained).ok() && capture() == reset,
        "same seed still starts a new epoch and cannot revive old checkpoint");
    expect(start(), "restart broker without reviving old epochs");
    broker.HandleSrand(thread, seed_rva, seed, &std::srand);
    expect(broker.AcquireOwnership(thread).ok() && !broker.Restore(thread, reset).ok(),
        "Stop/Start invalidates prior seed epoch even for same seed and thread");

    expect(start(), "start premature MoveVM case");
    broker.HandleSrand(thread, seed_rva, seed, &std::srand);
    broker.HandleRand(thread, movevm_rva, &std::rand);
    expect(broker.mode() == UcrtRandBrokerMode::Failed,
        "MoveVM before completed initialization fails admission");
    expect(start(), "start incomplete warmup case");
    broker.HandleSrand(thread, seed_rva, seed + 1, &std::srand); // At least 16 draws.
    broker.HandleRand(thread, warmup_rva, &std::rand);
    expect(!broker.AcquireOwnership(thread).ok(), "detect incomplete native warmup");
    for (unsigned i = 1; i < 16; ++i) broker.HandleRand(thread, warmup_rva, &std::rand);
    expect(broker.ObserveInitializationComplete(thread, complete_rva).ok(),
        "warmup bound and native completion agree");
    broker.HandleRand(thread, warmup_rva, &std::rand);
    expect(broker.mode() == UcrtRandBrokerMode::Failed,
        "extra initialization call after completed clone fails admission");
    expect(start(), "start unknown reseed case");
    broker.HandleSrand(thread, seed_rva, seed, &std::srand);
    expect(broker.AcquireOwnership(thread).ok(), "complete unknown reseed setup");
    broker.HandleSrand(thread, 0x1111, seed, &std::srand);
    expect(broker.mode() == UcrtRandBrokerMode::Failed,
        "unresolved same-thread reseed cannot silently retain stale combat epoch");
    expect(start(), "start unobserved initialization draw case");
    broker.HandleSrand(thread, seed_rva, seed, &std::srand);
    std::rand();
    expect(!broker.ObserveInitializationComplete(thread, complete_rva).ok(),
        "unobserved CRT mutation before clone cannot manufacture warmup agreement");
    expect(start(), "start extra warmup count case");
    broker.HandleSrand(thread, seed_rva, seed, &std::srand);
    for (unsigned i = 0; i < 16; ++i) broker.HandleRand(thread, warmup_rva, &std::rand);
    expect(broker.mode() == UcrtRandBrokerMode::Failed,
        "warmup exceeding every possible low seed nibble fails before admission");
    expect(start(), "start genuine foreign-thread MoveVM case");
    broker.HandleSrand(thread, seed_rva, seed, &std::srand);
    expect(broker.AcquireOwnership(thread).ok(), "initialize foreign-thread case");
    std::thread foreign([&] {
        broker.HandleRand(GetCurrentThreadId(), movevm_rva, &std::rand);
    });
    foreign.join();
    expect(broker.mode() == UcrtRandBrokerMode::Failed
            && broker.failure() == FailureCode::WrongThread,
        "actual foreign TLS cannot consume the private MoveVM stream");
#endif
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

void test_replay_diagnostic_budget()
{
    using Horse::Deterministic::ReplayDiagnosticTrace;
    ReplayDiagnosticTrace trace;
    expect(trace.Admit(ReplayDiagnosticTrace::Audio, 418)==0,"diagnostics default disabled");
    const auto path=std::filesystem::temp_directory_path()/("horse-diagnostic-"+std::to_string(GetCurrentProcessId())+".ini");
    for(unsigned depth:{0u,8u,16u}) {
        {std::ofstream out(path);out<<"stack_depth="<<depth<<"\nmask=3\nfirst_tick=416\nlast_tick=426\nbyte_limit=4096\n";}
        expect(trace.Load(path) && trace.stack_depth==depth,"runtime diagnostic stack depth");
        expect(trace.Admit(ReplayDiagnosticTrace::Particles,418)==0 && trace.Admit(ReplayDiagnosticTrace::Audio,415)==0,"subsystem and tick admission");
        expect(trace.Admit(ReplayDiagnosticTrace::Audio,418)==1 && trace.Admit(ReplayDiagnosticTrace::Rng,418)==1,"bounded diagnostic records");
        expect(trace.Admit(ReplayDiagnosticTrace::Audio,418)==-1 && trace.Admit(ReplayDiagnosticTrace::Audio,418)==0 && trace.Overflowed(),"overflow reported once without further capture");
    }
    {std::ofstream out(path);out<<"stack_depth=24\nmask=3\nfirst_tick=416\nlast_tick=426\nbyte_limit=4096\n";}
    expect(!trace.Load(path) && trace.Admit(1,418)==0,"invalid diagnostics fail closed");
    std::filesystem::remove(path);
}

int main(int argc,char** argv)
{
    test_replay_diagnostic_budget();
    if(argc==2 && std::string_view(argv[1])=="--diagnostic-trace") return failures?1:0;
    if(argc==2 && std::string_view(argv[1])=="--seek-advance-recovery")
        return ReplaySeekStateTest::advance_failure_recovery_contract()?0:1;
    test_replay_resume_wall_clock();
    {
        using I=ReplayTickIndex;
        const auto prepare=[](I& index) {
            I::Entry row{};
            expect(index.Begin(row,sizeof(row)*8,7,I::EndPolicy::RetainedSourceStop).ok(),"retained policy begins explicitly");
            row.tick=row.native_tick=row.interval=1;row.round_state=5;
            expect(index.Append(row).ok() && index.CompleteInterval(1,1,false).ok()
                && index.ObserveRetainedSourceStop(1).ok(),"native source stop recorded at complete interval");
        };
        I index;prepare(index);
        expect(!index.witness().native_finish && !index.witness().final_tail
            && !index.witness().unsupported_native_tail,"source stop is not native replay completion");
        I::Entry row=index.entries().back();row.tick=row.native_tick=row.interval=2;
        expect(index.Append(row).ok() && index.CompleteInterval(2,2,false).ok()
            && index.FinishRetainedSourceStop(2).ok(),"completed retained application includes every actual traversal");
        expect(index.witness().unsupported_native_tail && !index.AllowsPlaybackFrom(2)
            && index.AllowsPlaybackFrom(1),"unsupported tail cannot escape retained session");
        index.SuspendSourceRevision(8);index.SettleSourceRevision(7);
        expect(index.witness().phase==I::Phase::Complete && index.witness().unsupported_native_tail,
            "B recovery preserves retained range policy");
        index.ObserveNativeFinish();
        expect(index.witness().phase==I::Phase::Failed,"native finish invalidates retained session admission");
        I cancelled;prepare(cancelled);expect(cancelled.Cancel() && !cancelled.FinishRetainedSourceStop(2).ok(),"cancel cannot publish shortened index");
        I premature;prepare(premature);expect(!premature.FinishRetainedSourceStop(1).ok(),"source stop alone cannot replace application tail");
        I changed;prepare(changed);row.source_active=1;
        expect(!changed.Append(row).ok(),"reactivated source rejects retained endpoint");
        I finish;prepare(finish);row.source_active=0;row.round_state=10;
        expect(!finish.Append(row).ok(),"native finish transition rejects pre-teardown hold");
    }
    {
        ReplayTickIndex index;
        ReplayTickIndex::Entry row{};
        expect(index.Begin(row, sizeof(row) * 8).ok(), "index reserves bounded baseline storage");
        expect(!index.ReleaseAfterOwners(true).ok() && index.witness().phase==ReplayTickIndex::Phase::Recording,
            "index release cannot bypass recording cancellation");
        expect(!index.FinishAtApplicationBoundary(0).ok(), "index cannot finish without native witnesses");
        expect(index.CompleteInterval(1, 0, false).ok(), "index retains zero-tick interval");
        row.tick = row.native_tick = 1; row.interval = 2; row.publications = 1;
        expect(index.Append(row).ok(), "index records first traversal");
        row.tick = row.native_tick = 2; // Two ticks sharing one publication.
        expect(index.Append(row).ok() && index.CompleteInterval(2, 2, true).ok(), "index records repeat and final tail");
        expect(!index.FinishAtApplicationBoundary(2).ok(), "final tail alone does not complete index");
        index.ObserveNativeFinish();
        for(unsigned tick=3;tick<=4;++tick) {
            row.tick=row.native_tick=tick;row.interval=tick;row.round_state=10;
            expect(index.Append(row).ok() && index.CompleteInterval(tick,tick,true).ok(),
                "native source tail remains indexed after finish notification");
        }
        expect(index.source_end_tick()==2 && index.witness().phase==ReplayTickIndex::Phase::Recording,
            "source endpoint stays distinct from the retained boundary");
        expect(index.FinishAtApplicationBoundary(4).ok() && index.entries().size() == 5
            && index.witness().zero_tick_intervals == 1 && index.witness().multi_tick_intervals == 1,
            "native finish and complete tail seal exact contiguous timeline");
        expect(!index.Append(row).ok() && index.entries().size() == 5, "finished index rejects further native traversal admission");
        expect(index.AllowsPlaybackFrom(0) && index.AllowsPlaybackFrom(3)
            && !index.AllowsPlaybackFrom(4) && !index.AllowsPlaybackFrom(5) && !index.AllowsPlaybackFrom(UINT64_MAX),
            "retained endpoint blocks playback escape but permits backward continuation");
        const auto indexed_bytes=index.storage_bytes();
        index.InvalidateSourceRevision();
        expect(index.witness().phase==ReplayTickIndex::Phase::Failed
            && index.witness().failure==FailureCode::GenerationMismatch
            && index.entries().size()==5 && index.storage_bytes()==indexed_bytes
            && !index.FinishAtApplicationBoundary(4).ok(),
            "changed authored history invalidates completion without discarding bounded evidence or replaying expected entries");
        expect(index.ReleaseAfterOwners(false).ok() && index.witness().phase==ReplayTickIndex::Phase::Releasing
            && index.entries().size()==5 && index.storage_bytes()==indexed_bytes,
            "index retains map and budget while checkpoint owners retire");
        expect(index.ReleaseAfterOwners(false).ok() && index.storage_bytes()==indexed_bytes
            && !index.Begin({},sizeof(row)*4).ok(), "pending retirement cannot restart or silently release index");
        expect(index.ReleaseAfterOwners(true).ok() && index.witness().phase==ReplayTickIndex::Phase::Empty
            && !index.storage_bytes() && index.entries().empty(), "completed owner retirement releases map exactly once");
        row = {};
        expect(index.Begin(row, sizeof(row) * 2).ok(), "index restarts after explicit clear");
        row.tick = row.native_tick = 1;
        expect(index.Append(row).ok(), "last reserved index slot admitted");
        row.tick = row.native_tick = 2;
        expect(index.Append(row).code == FailureCode::CapacityExceeded
            && index.witness().phase == ReplayTickIndex::Phase::Failed && index.entries().size() == 2,
            "capacity failure preserves partial evidence without success");
        for(unsigned defect=0;defect<5;++defect) {
            index.Clear();row={};
            expect(index.Begin(row,sizeof(row)*(defect==4?2:4)).ok(),"tail failure fixture begins");
            row.tick=row.native_tick=1;row.interval=1;row.round_state=10;
            expect(index.Append(row).ok() && index.CompleteInterval(1,1,true).ok(),"tail fixture reaches source endpoint");
            index.ObserveNativeFinish();
            if(defect==0) {
                expect(!index.FinishAtApplicationBoundary(2).ok() && index.witness().phase==ReplayTickIndex::Phase::Failed,
                    "completion cannot omit an unindexed native tick");
                continue;
            }
            row.tick=row.native_tick=2;row.interval=2;
            if(defect==1)row.source_active=1;
            if(defect==2)row.round=1;
            if(defect==3)row.round_state=2;
            const auto failure=index.Append(row);
            expect(!failure.ok() && index.witness().phase==ReplayTickIndex::Phase::Failed && index.entries().size()==2
                && index.source_end_tick()==1,"tail replacement or exhaustion preserves incomplete evidence without success");
            if(defect==4)expect(failure.code==FailureCode::CapacityExceeded,"tail capacity remains part of the index budget");
        }
        index.Clear(); row = {};
        expect(index.Begin(row, sizeof(row) * 4).ok() && index.Cancel()
            && !index.FinishAtApplicationBoundary(0).ok(), "cancelled index cannot become complete");
        index.Clear();
        expect(index.Begin(row, sizeof(row) * 4).ok(), "index starts continuity check");
        row.tick = row.native_tick = 2;
        expect(index.Append(row).code == FailureCode::AdvanceFailed && index.entries().size() == 1,
            "missing interior boundary fails indexing");
    }
    ReplaySeekStateTest::run();
    ReplayCaptureAccountingTest::run();
    test_hud_playback_retains_logical_time_without_player_ownership();
    test_hud_player_undo_survives_completion_and_partial_start();
    test_hud_private_B_publication_and_retirement();
    {
        ReplayHudWidgetAdmission admission;
        int transaction{},foreign{},b1{},b2{},c{};
        std::array<const void*,2> b{&b1,&b2};
        expect(admission.Acquire(&transaction,b) && admission.Excludes(&b1) && admission.Excludes(&b2)
            && !admission.Excludes(&c),"private B excludes all widget work without excluding C");
        expect(admission.Acquire(&transaction,b),"private B acquisition retry is idempotent");
        expect(!admission.Acquire(&foreign,b) && !admission.Release(&foreign)
            && admission.Excludes(&b1),"foreign cancellation cannot re-admit B");
        std::array<const void*,2> changed{&b1,&c};
        expect(!admission.Acquire(&transaction,changed) && !admission.Excludes(&c),"retry cannot replace private membership");
        expect(admission.Release(&transaction) && admission.empty() && !admission.Excludes(&b1)
            && admission.Release(&transaction),"undo or completed retirement re-admits once");
        std::array<const void*,2> duplicate{&b1,&b1},missing{&b1,nullptr};
        expect(!admission.Acquire(&transaction,duplicate) && !admission.Acquire(&transaction,missing)
            && admission.empty(),"invalid membership rejects atomically before publication");
        std::array<const void*,17> overflow{};
        expect(!admission.Acquire(&transaction,overflow) && !admission.Acquire(nullptr,b)
            && admission.empty(),"bounded admission rejects capacity and absent owner");
    }
    test_streamable_domain_ignores_only_retirement_eligible_records();
    test_occlusion_private_history_reconstruction();
    test_lighting_lod_binding_correspondence();
    test_particle_tile_partition_rejects_equal_count_corruption();
#ifdef _WIN32
    test_particle_tile_prefix_recovery_and_lock_exclusion();
#endif
    test_native_tick_epoch_rebase_preserves_signed_comparison();
    test_canonical_hash_timeline_is_immutable_and_bounded();
    test_round_transition_selects_the_last_canonicalized_fencepost();
    test_round_rearm_clears_prediction_before_checkpoint_reservation();
    test_public_config_contract();
    test_input_replacement_and_invalidation();
    test_native_batch_timeline_is_exact_and_bounded();
    test_native_interval_suffix_rebuilds_coordinate_index();
    test_snapshot_capacity_is_atomic();
    test_interval_checkpoint_identity_survives_native_geometry_changes();
    test_confirmed_online_history_retirement_is_bounded();
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
    test_replay_source_route_admission();
    test_replay_source_registration_transaction();
    test_sc6_replay_source_round_transaction();
    test_transactional_restore_failures_undo();
    test_floating_point_environment_capture_is_raw_and_non_mutating();
    test_ucrt_broker_is_callsite_and_thread_bound();
    test_ucrt_broker_separates_movevm_and_native_presentation_lanes();
    test_ucrt_split_preflight_reset_and_complete_b();
    test_audio_presentation_identities_are_epoch_bound();
    test_stage_presentation_is_pointer_free_and_composite();
    if (failures == 0)
    {
        std::cout << "DeterministicCoreSelfTest passed\n";
    }
    return failures == 0 ? 0 : 1;
}
