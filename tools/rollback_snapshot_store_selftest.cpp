#include "../HorseMod/horselib/RollbackSnapshotStore.hpp"
#include "../HorseMod/horselib/RollbackPreallocatedCaptureGate.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
    struct TestState
    {
        uint32_t frame {0};
        uint32_t value {0};

        void clear() noexcept
        {
            frame = 0;
            value = 0;
        }
    };

    struct ArenaState
    {
        inline static uint32_t copies = 0;
        inline static uint32_t capacity_preserves = 0;
        std::vector<uint32_t> values;

        ArenaState() = default;
        ArenaState(const ArenaState& other) : values(other.values)
        {
            ++copies;
        }
        ArenaState& operator=(const ArenaState& other)
        {
            values = other.values;
            ++copies;
            return *this;
        }
        ArenaState(ArenaState&&) noexcept = default;
        ArenaState& operator=(ArenaState&&) noexcept = default;
        void preserve_capacities_from(const ArenaState& other)
        {
            values.reserve(other.values.capacity());
            ++capacity_preserves;
        }
        void clear() noexcept { values.clear(); }
    };

    struct ArenaCapacity
    {
        size_t value {0};
        bool valid {false};
        bool operator==(const ArenaCapacity& other) const noexcept
        {
            return valid && other.valid && value == other.value;
        }
    };

    struct NestedArenaState
    {
        uint32_t marker {0};
        std::vector<std::vector<uint8_t>> nodes;

        void preserve_capacities_from(const NestedArenaState& other)
        {
            nodes.reserve(other.nodes.capacity());
            if (nodes.size() < other.nodes.size())
                nodes.resize(other.nodes.size());
            for (size_t i = 0; i < other.nodes.size(); ++i)
                nodes[i].reserve(other.nodes[i].capacity());
        }

        void clear() noexcept
        {
            marker = 0;
            for (auto& node : nodes) node.clear();
        }

        bool copy_preallocated_from(
            const NestedArenaState& other) noexcept
        {
            if (nodes.capacity() < other.nodes.size()
                || nodes.size() < other.nodes.size())
            {
                return false;
            }
            for (size_t i = 0; i < other.nodes.size(); ++i)
            {
                if (nodes[i].capacity() < other.nodes[i].size())
                    return false;
            }
            const size_t outer_capacity = nodes.capacity();
            std::vector<size_t> inner_capacities;
            inner_capacities.reserve(nodes.size());
            for (const auto& node : nodes)
                inner_capacities.push_back(node.capacity());
            try
            {
                *this = other;
            }
            catch (...)
            {
                return false;
            }
            if (nodes.capacity() != outer_capacity
                || nodes.size() > inner_capacities.size())
            {
                return false;
            }
            for (size_t i = 0; i < nodes.size(); ++i)
            {
                if (nodes[i].capacity() != inner_capacities[i])
                    return false;
            }
            return true;
        }
    };
}

int main()
{
    Horse::RollbackSnapshotStore<TestState, 4> store {};
    Horse::RollbackSnapshotHandle h0 {};
    const TestState s0 {0, 0xA0};
    const auto saved0 = store.save(
        7, 0, 0x100, 0x200, s0,
        Horse::RollbackFrameStamp::From(0), 4, h0);

    const TestState* loaded = nullptr;
    const auto loaded0 = store.load(h0, loaded);
    Horse::RollbackSnapshotHandle found0 {};
    const auto found0_report = store.find(7, 0, found0);
    Horse::RollbackSnapshotHandle missing1 {};
    const auto missing1_report = store.find(7, 1, missing1);
    const bool roundtrip = saved0.ok && loaded0.ok && loaded
        && loaded->frame == 0 && loaded->value == 0xA0;
    const bool lookup = found0_report.ok
        && found0.generation == h0.generation
        && found0.canonical_hash == h0.canonical_hash
        && !missing1_report.ok && !missing1.valid();

    Horse::RollbackSnapshotHandle corrected0 {};
    const TestState s0_corrected {0, 0xB0};
    const auto corrected_save = store.save(
        7, 0, 0x110, 0x210, s0_corrected,
        Horse::RollbackFrameStamp::From(0), 4, corrected0);
    const TestState* corrected_loaded = nullptr;
    const auto corrected_load = store.load(corrected0, corrected_loaded);
    const TestState* old_same_frame = nullptr;
    const auto old_same_frame_load = store.load(h0, old_same_frame);
    const bool same_frame_replaced = corrected_save.ok
        && corrected0.generation != h0.generation
        && corrected_load.ok && corrected_loaded
        && corrected_loaded->value == 0xB0
        && !old_same_frame_load.ok
        && old_same_frame_load.status
            == Horse::RollbackSnapshotStoreStatus::StaleHandle;

    Horse::RollbackSnapshotHandle blocked_handle {};
    const TestState s4 {4, 0xA4};
    const auto blocked = store.save(
        7, 4, 0x104, 0x204, s4,
        Horse::RollbackFrameStamp::From(4), 4, blocked_handle);
    const bool protected_slot = !blocked.ok
        && blocked.status == Horse::RollbackSnapshotStoreStatus::ProtectedSlot;

    const auto replaced = store.save(
        7, 4, 0x104, 0x204, s4,
        Horse::RollbackFrameStamp::From(4), 3, blocked_handle);
    const TestState* stale = nullptr;
    const auto stale_report = store.load(h0, stale);
    const bool stale_rejected = replaced.ok && !stale_report.ok
        && stale_report.status
            == Horse::RollbackSnapshotStoreStatus::StaleHandle;

    Horse::RollbackSnapshotHandle wrap_a {};
    Horse::RollbackSnapshotHandle wrap_b {};
    const TestState sw0 {0xFFFFFFFCu, 1};
    const TestState sw1 {0, 2};
    const auto wrap0 = store.save(
        8, sw0.frame, 0x301, 0x401, sw0,
        Horse::RollbackFrameStamp::From(sw0.frame), 3, wrap_a);
    const auto wrap1 = store.save(
        8, sw1.frame, 0x302, 0x402, sw1,
        Horse::RollbackFrameStamp::From(sw1.frame), 3, wrap_b);
    const bool wrap_ok = wrap0.ok && wrap1.ok;

    uint32_t confirmed = 0;
    const bool horizon_waits = !Horse::RollbackTryGetConfirmedFrame(
        104, 100, 4, confirmed);
    const bool horizon_ready = Horse::RollbackTryGetConfirmedFrame(
        105, 100, 4, confirmed) && confirmed == 100;
    const bool horizon_wrap = Horse::RollbackTryGetConfirmedFrame(
        2, 0xFFFFFFFDu, 4, confirmed)
        && confirmed == 0xFFFFFFFDu;
    const bool confirmation_horizon = horizon_waits
        && horizon_ready && horizon_wrap;

    Horse::RollbackSnapshotStore<TestState, 4> baseline_store {};
    Horse::RollbackSnapshotHandle baseline_handle {};
    const TestState baseline_state {
        Horse::kRollbackGekkoBaselineFrameKey, 0xBACE};
    const auto baseline_saved = baseline_store.save(
        10, Horse::kRollbackGekkoBaselineFrameKey, 0x701, 0x801,
        baseline_state,
        Horse::RollbackFrameStamp::From(
            Horse::kRollbackGekkoBaselineFrameKey),
        1, baseline_handle);
    Horse::RollbackSnapshotHandle before_baseline = baseline_handle;
    before_baseline.frame -= 4;
    const TestState* before_baseline_state = nullptr;
    const auto before_baseline_load = baseline_store.load(
        before_baseline, before_baseline_state);
    const bool no_prebaseline_load = baseline_saved.ok
        && !before_baseline_load.ok && !before_baseline_state
        && before_baseline_load.status
            == Horse::RollbackSnapshotStoreStatus::StaleHandle;

    Horse::RollbackSnapshotStore<ArenaState, 64> arena {};
    ArenaState exemplar {};
    exemplar.values.reserve(32);
    exemplar.values.resize(16, 7);
    ArenaState::capacity_preserves = 0;
    const bool arena_prepared = arena.prepare(exemplar, 12)
        && arena.occupied() == 16
        && ArenaState::capacity_preserves == 16;
    const auto arena_capacity_hash = [](const ArenaState& state) noexcept {
        return ArenaCapacity {state.values.capacity(), true};
    };
    const ArenaCapacity expected_arena_capacity = arena_capacity_hash(exemplar);
    const ArenaCapacity wrong_arena_capacity {
        expected_arena_capacity.value + 1u, true};
    const bool preallocated_shape = arena.preallocated_matches(
            12, expected_arena_capacity, arena_capacity_hash)
        && !arena.preallocated_matches(
            2, expected_arena_capacity, arena_capacity_hash)
        && !arena.preallocated_matches(
            12, wrong_arena_capacity, arena_capacity_hash);
    Horse::RollbackSnapshotHandle preflight_handle {};
    const auto preflight_saved = arena.save_preallocated_copy(
        9, 31, 0x411, 0x511, exemplar, preflight_handle);
    const auto* exemplar_pointer = exemplar.values.data();
    const auto exemplar_values = exemplar.values;
    const auto arena_value_hash = [](const ArenaState& state) noexcept {
        uint64_t hash = 1469598103934665603ull;
        for (uint32_t value : state.values)
            hash = (hash ^ value) * 1099511628211ull;
        return hash;
    };
    const uint64_t exemplar_hash = arena_value_hash(exemplar);
    struct GateStage {} gate_stage {};
    struct GateScratch {} gate_scratch {};
    const auto refused_preflight =
        Horse::ValidateRollbackPreallocatedCaptureGate(
            exemplar, &gate_stage, &gate_scratch,
            &expected_arena_capacity,
            [](const auto&, const auto&) noexcept { return false; },
            [](const auto&) noexcept { return true; },
            [](const auto&) noexcept { return "ok"; },
            [](const auto&) noexcept { return "ok"; },
            [](const auto&, const auto&) noexcept { return true; });
    const ArenaState* retained_preflight_state = nullptr;
    const auto retained_preflight_load = arena.load(
        preflight_handle, retained_preflight_state);
    const bool refused_preflight_preserved = preflight_saved.ok
        && !refused_preflight.ok
        && exemplar.values.data() == exemplar_pointer
        && exemplar.values == exemplar_values
        && arena_value_hash(exemplar) == exemplar_hash
        && retained_preflight_load.ok && retained_preflight_state
        && retained_preflight_load.generation
            == preflight_handle.generation
        && retained_preflight_state->values == exemplar_values
        && arena_value_hash(*retained_preflight_state) == exemplar_hash;
    ArenaState provisional = exemplar;
    provisional.preserve_capacities_from(exemplar);
    const size_t capacity_before = provisional.values.capacity();
    ArenaState::copies = 0;
    Horse::RollbackSnapshotHandle arena_handle {};
    const auto arena_saved = arena.save_recycling(
        9, 0, 0x501, 0x601, provisional,
        Horse::RollbackFrameStamp::From(0), 12, arena_handle);
    const bool recycling_no_copy = arena_saved.ok
        && ArenaState::copies == 0
        && provisional.values.capacity() >= capacity_before
        && refused_preflight_preserved;
    const size_t arena_slots_before_rearm = arena.occupied();
    arena.invalidate_recycling();
    const bool preallocated_shape_after_invalidate =
        arena.preallocated_matches(
            12, expected_arena_capacity, arena_capacity_hash);
    const ArenaState* old_round_state = nullptr;
    const auto old_round_load = arena.load(arena_handle, old_round_state);
    provisional.values.resize(16, 8);
    Horse::RollbackSnapshotHandle rearmed_handle {};
    const auto rearmed_save = arena.save_recycling(
        10, 0, 0x502, 0x602, provisional,
        Horse::RollbackFrameStamp::From(0), 12, rearmed_handle);
    const bool rearm_reuses_arena = !old_round_load.ok
        && old_round_load.status
            == Horse::RollbackSnapshotStoreStatus::StaleHandle
        && arena.occupied() == arena_slots_before_rearm
        && rearmed_save.ok
        && rearmed_handle.generation > arena_handle.generation;
    Horse::RollbackSnapshotStore<ArenaState, 2> terminal_arena {};
    const bool terminal_prepared = terminal_arena.prepare(exemplar, 0);
    ArenaState::copies = 0;
    Horse::RollbackSnapshotHandle terminal_handle {};
    const auto terminal_saved = terminal_arena.save_preallocated_copy(
        9, 2337, 0x701, 0x801, exemplar, terminal_handle);
    const ArenaState* terminal_state = nullptr;
    const auto terminal_loaded = terminal_arena.load(
        terminal_handle, terminal_state);
    const bool terminal_checkpoint = terminal_prepared
        && terminal_saved.ok && terminal_loaded.ok && terminal_state
        && !terminal_saved.preallocated_copy_verified
        && ArenaState::copies == 1
        && terminal_state->values == exemplar.values;

    // A speculative pair match must not pin the terminal arena. Later
    // corrected terminal frames still need an exact checkpoint when they
    // become authoritative at the confirmation frontier.
    Horse::RollbackSnapshotStore<TestState, 64> terminal_candidates {};
    const TestState terminal_exemplar {2096, 0xD0};
    const bool candidates_prepared = terminal_candidates.prepare(
        terminal_exemplar, 12);
    auto retain_candidate = [&](uint32_t frame, uint32_t value) {
        Horse::RollbackSnapshotHandle handle {};
        const TestState state {frame, value};
        return terminal_candidates.save_preallocated_copy(
            11, frame, 0x900 + frame, 0xA00 + frame, state, handle).ok;
    };
    bool retained_sequence = candidates_prepared
        && retain_candidate(2096, 0xD0)
        && retain_candidate(2097, 0xD1)
        && retain_candidate(2097, 0xD1) // speculative pair match
        && retain_candidate(2098, 0xD2)
        && retain_candidate(2098, 0xD2) // corrected pair match
        && retain_candidate(2099, 0xD3);
    // Continue through the bounded post-edge neighborhood. Confirmation may
    // arrive later, but these slots stop rotating after this interval.
    for (uint32_t frame = 2100; frame <= 2109; ++frame)
        retained_sequence = retained_sequence
            && retain_candidate(frame, 0xD0 + frame - 2096);
    Horse::RollbackSnapshotHandle authoritative_handle {};
    const auto authoritative_found = terminal_candidates.find(
        11, 2099, authoritative_handle);
    const TestState* authoritative_state = nullptr;
    const auto authoritative_loaded = authoritative_found.ok
        ? terminal_candidates.load(authoritative_handle, authoritative_state)
        : authoritative_found;
    const bool corrected_terminal_retained = retained_sequence
        && authoritative_found.ok && authoritative_loaded.ok
        && authoritative_state && authoritative_state->frame == 2099
        && authoritative_state->value == 0xD3;

    NestedArenaState nested_exemplar {};
    nested_exemplar.nodes.resize(2);
    for (auto& node : nested_exemplar.nodes)
    {
        node.reserve(16);
        node.resize(8, 0x44);
    }
    Horse::RollbackSnapshotStore<NestedArenaState, 2> nested_arena {};
    const bool nested_prepared = nested_arena.prepare(nested_exemplar, 0);
    Horse::RollbackSnapshotHandle nested_handle {};
    const auto nested_saved = nested_arena.save_preallocated_copy(
        12, 0, 0xB01, 0xC01, nested_exemplar, nested_handle);
    const NestedArenaState* nested_loaded = nullptr;
    const auto nested_loaded_report = nested_arena.load(
        nested_handle, nested_loaded);
    const bool nested_preallocated_copy = nested_prepared
        && nested_saved.ok && nested_saved.preallocated_copy_verified
        && nested_loaded_report.ok && nested_loaded
        && nested_loaded->nodes == nested_exemplar.nodes
        && nested_loaded->nodes[0].capacity() == 16
        && nested_loaded->nodes[1].capacity() == 16;
    // A checkpoint can have a smaller logical nested shape than the launch
    // exemplar without allocating. This is not capacity growth and must not
    // be rejected merely because a flattened capacity manifest has fewer
    // entries after the copy.
    NestedArenaState reduced_nested = nested_exemplar;
    reduced_nested.marker = 0x51;
    reduced_nested.nodes.resize(1);
    Horse::RollbackSnapshotHandle reduced_nested_handle {};
    const auto reduced_nested_saved = nested_arena.save_preallocated_copy(
        12, 1, 0xB03, 0xC03, reduced_nested,
        reduced_nested_handle);
    const NestedArenaState* reduced_nested_loaded = nullptr;
    const auto reduced_nested_load = nested_arena.load(
        reduced_nested_handle, reduced_nested_loaded);
    const bool nested_shape_reduction = reduced_nested_saved.ok
        && reduced_nested_saved.preallocated_copy_verified
        && reduced_nested_load.ok && reduced_nested_loaded
        && reduced_nested_loaded->marker == reduced_nested.marker
        && reduced_nested_loaded->nodes == reduced_nested.nodes
        && reduced_nested_loaded->nodes.capacity()
            >= nested_exemplar.nodes.size()
        && reduced_nested_loaded->nodes[0].capacity() == 16;
    NestedArenaState oversized_nested = nested_exemplar;
    oversized_nested.nodes[0].resize(17, 0x55);
    Horse::RollbackSnapshotHandle refused_nested_handle {};
    const auto refused_nested = nested_arena.save_preallocated_copy(
        12, 1, 0xB02, 0xC02, oversized_nested,
        refused_nested_handle);
    const bool nested_growth_refused = !refused_nested.ok
        && !refused_nested.preallocated_copy_verified
        && !refused_nested_handle.valid()
        && std::strcmp(
            refused_nested.failure,
            "snapshot-preallocated-capacity-mismatch") == 0;

    store.clear();
    const bool cleared = store.occupied() == 0 && store.saves() == 0;
    const bool ok = roundtrip && lookup && same_frame_replaced
        && protected_slot && stale_rejected
        && wrap_ok && confirmation_horizon && cleared
        && no_prebaseline_load && arena_prepared && preallocated_shape
        && preallocated_shape_after_invalidate && recycling_no_copy
        && rearm_reuses_arena
        && terminal_checkpoint && corrected_terminal_retained
        && nested_preallocated_copy && nested_shape_reduction
        && nested_growth_refused;
    std::printf(
        "rollback snapshot-store self-test %s roundtrip=%d lookup=%d same_frame=%d "
        "protected=%d stale=%d wrap=%d horizon=%d prebaseline=%d cleared=%d "
        "arena=%d preallocated=%d recycling=%d rearm_reuse=%d terminal=%d "
        "corrected_terminal=%d nested_copy=%d nested_reduced=%d "
        "nested_refusal=%d\n",
        ok ? "passed" : "failed",
        roundtrip ? 1 : 0,
        lookup ? 1 : 0,
        same_frame_replaced ? 1 : 0,
        protected_slot ? 1 : 0,
        stale_rejected ? 1 : 0,
        wrap_ok ? 1 : 0,
        confirmation_horizon ? 1 : 0,
        no_prebaseline_load ? 1 : 0,
        cleared ? 1 : 0,
        arena_prepared ? 1 : 0,
        preallocated_shape && preallocated_shape_after_invalidate ? 1 : 0,
        recycling_no_copy ? 1 : 0,
        rearm_reuses_arena ? 1 : 0,
        terminal_checkpoint ? 1 : 0,
        corrected_terminal_retained ? 1 : 0,
        nested_preallocated_copy ? 1 : 0,
        nested_shape_reduction ? 1 : 0,
        nested_growth_refused ? 1 : 0);
    return ok ? 0 : 1;
}
