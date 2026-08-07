#include "RollbackCarriedStateTransaction.hpp"
#include "RollbackStageWindAuthority.hpp"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <thread>
#include <utility>

namespace
{
    template<typename T>
    void write(uint8_t* address, const T& value)
    {
        std::memcpy(address, &value, sizeof(value));
    }

    void seed_emitter(
        uint8_t* emitter,
        int32_t active,
        int32_t remaining,
        float base_timer,
        float reload_timer,
        float jitter)
    {
        write(emitter + 0x50, active);
        write(emitter + 0x54, remaining);
        write(emitter + 0x58, base_timer);
        write(emitter + 0x5C, reload_timer);
        write(emitter + 0xA4, jitter);
    }
}

int main()
{
    Horse::RollbackStageWindOriginalAllocatorRoutes original_routes {};
    std::atomic<bool> route_reader_ok {false};
    std::thread route_reader([&]() {
        for (uint32_t attempt = 0;
             attempt < 1000000 && !original_routes.ready(); ++attempt)
            std::this_thread::yield();
        route_reader_ok.store(
            original_routes.ready()
                && original_routes.malloc_owner() == 0x1000
                && original_routes.malloc_function() == 0x2000
                && original_routes.free_owner() == 0x3000
                && original_routes.free_function() == 0x4000,
            std::memory_order_release);
    });
    if (!original_routes.publish_once(0x1000, 0x2000, 0x3000, 0x4000))
    {
        route_reader.join();
        return 25;
    }
    route_reader.join();
    if (!route_reader_ok.load(std::memory_order_acquire)
        || !original_routes.publish_once(
            0x1000, 0x2000, 0x3000, 0x4000)
        || original_routes.publish_once(
            0x1001, 0x2000, 0x3000, 0x4000)
        || original_routes.malloc_owner() != 0x1000
        || original_routes.malloc_function() != 0x2000
        || original_routes.free_owner() != 0x3000
        || original_routes.free_function() != 0x4000)
        return 26;

    using Horse::RollbackStageWindAllocationDisposition;
    if (Horse::ClassifyRollbackStageWindAllocation(
            Horse::kRollbackStageWindRingInAllocReturnRva,
            0x1E0, 0, true, true)
            != RollbackStageWindAllocationDisposition::FixedPool
        || Horse::ClassifyRollbackStageWindAllocation(
            Horse::kRollbackStageWindPairParallelAllocReturnRva,
            0x130, 0, true, true)
            != RollbackStageWindAllocationDisposition::FixedPool
        || Horse::ClassifyRollbackStageWindAllocation(
            Horse::kRollbackStageWindPairRingOutAllocReturnRva,
            0x130, 0, true, true)
            != RollbackStageWindAllocationDisposition::FixedPool
        || Horse::ClassifyRollbackStageWindAllocation(
            Horse::kRollbackStageWindShockWaveAllocReturnRva,
            0x180, 0, true, true)
            != RollbackStageWindAllocationDisposition::FixedPool
        || Horse::ClassifyRollbackStageWindAllocation(
            Horse::kRollbackStageWindShockWaveAllocReturnRva,
            0x1E0, 0, true, true)
            != RollbackStageWindAllocationDisposition::PassThrough
        || Horse::ClassifyRollbackStageWindAllocation(
            Horse::kRollbackStageWindRingInAllocReturnRva,
            0x130, 0, true, true)
            != RollbackStageWindAllocationDisposition::PassThrough
        || Horse::ClassifyRollbackStageWindAllocation(
            0xDEADBEEF, 0x1E0, 0, true, true)
            != RollbackStageWindAllocationDisposition::PassThrough
        || Horse::ClassifyRollbackStageWindAllocation(
            Horse::kRollbackStageWindRingInAllocReturnRva,
            0x1E0, 0, false, true)
            != RollbackStageWindAllocationDisposition::PassThrough
        || Horse::ClassifyRollbackStageWindAllocation(
            Horse::kRollbackStageWindRingInAllocReturnRva,
            0x1E0, 16, true, true)
            != RollbackStageWindAllocationDisposition::PassThrough)
        return 16;

    int pool_calls = 0;
    int original_calls = 0;
    int failure_calls = 0;
    auto* const pool_marker = reinterpret_cast<void*>(uintptr_t {0x1000});
    auto* const original_marker =
        reinterpret_cast<void*>(uintptr_t {0x2000});
    void* routed = Horse::RouteRollbackStageWindAllocation(
        RollbackStageWindAllocationDisposition::PassThrough, 0x70, 32,
        [&](size_t) noexcept -> void* {
            ++pool_calls;
            return pool_marker;
        },
        [&](size_t bytes, uint32_t alignment) noexcept -> void* {
            ++original_calls;
            return bytes == 0x70 && alignment == 32
                ? original_marker : nullptr;
        },
        [&](const char*) noexcept { ++failure_calls; });
    if (routed != original_marker || pool_calls != 0
        || original_calls != 1 || failure_calls != 0)
        return 20;

    pool_calls = original_calls = failure_calls = 0;
    routed = Horse::RouteRollbackStageWindAllocation(
        RollbackStageWindAllocationDisposition::FixedPool, 0x130, 0,
        [&](size_t bytes) noexcept -> void* {
            ++pool_calls;
            return bytes == 0x130 ? pool_marker : nullptr;
        },
        [&](size_t, uint32_t) noexcept -> void* {
            ++original_calls;
            return original_marker;
        },
        [&](const char*) noexcept { ++failure_calls; });
    if (routed != pool_marker || pool_calls != 1
        || original_calls != 0 || failure_calls != 0)
        return 21;

    pool_calls = original_calls = failure_calls = 0;
    routed = Horse::RouteRollbackStageWindAllocation(
        RollbackStageWindAllocationDisposition::FixedPool, 0x1E0, 0,
        [&](size_t) noexcept -> void* {
            ++pool_calls;
            return nullptr;
        },
        [&](size_t bytes, uint32_t alignment) noexcept -> void* {
            ++original_calls;
            return bytes == 0x1E0 && alignment == 0
                ? original_marker : nullptr;
        },
        [&](const char* failure) noexcept {
            if (failure && std::strcmp(failure,
                    "stage-wind-allocation-pool-exhausted") == 0)
                ++failure_calls;
        });
    if (routed != original_marker || pool_calls != 1
        || original_calls != 1 || failure_calls != 1)
        return 22;

    using Horse::RollbackStageWindFreeDisposition;
    int null_free_classification_calls = 0;
    int null_free_original_calls = 0;
    const auto exercise_free_route = [&](void* pointer) noexcept {
        if (!Horse::ShouldRouteRollbackStageWindFree(pointer)) return;
        ++null_free_classification_calls;
        if (pointer == original_marker)
        {
            ++null_free_original_calls;
        }
    };
    exercise_free_route(nullptr);
    if (null_free_classification_calls != 0
        || null_free_original_calls != 0)
        return 28;
    exercise_free_route(original_marker);
    if (null_free_classification_calls != 1
        || null_free_original_calls != 1)
        return 29;
    int deferred_release_calls = 0;
    Horse::ReleaseRollbackStageWindDeferredFreesIfNeeded(false,
        [&]() noexcept { ++deferred_release_calls; });
    if (deferred_release_calls != 0) return 30;
    Horse::ReleaseRollbackStageWindDeferredFreesIfNeeded(true,
        [&]() noexcept { ++deferred_release_calls; });
    if (deferred_release_calls != 1) return 31;
    if (Horse::ClassifyRollbackStageWindFree(true, true, true, false)
            != RollbackStageWindFreeDisposition::Intercept
        || Horse::ClassifyRollbackStageWindFree(false, true, true, false)
            != RollbackStageWindFreeDisposition::InterceptAndFail
        || Horse::ClassifyRollbackStageWindFree(true, true, false, false)
            != RollbackStageWindFreeDisposition::PassThrough
        || Horse::ClassifyRollbackStageWindFree(false, false, false, true)
            != RollbackStageWindFreeDisposition::Intercept
        || Horse::ClassifyRollbackStageWindFree(false, false, false, false)
            != RollbackStageWindFreeDisposition::PassThrough)
        return 23;

    // A logical session boundary must preserve pool-backed nodes because the
    // native parallel wind node can remain linked indefinitely. Only external
    // tracking and the seal are session-local.
    Horse::RollbackStageWindAllocationPool persistent_pool;
    const uintptr_t initial_external = 0x12345000;
    if (!persistent_pool.track_initial_node(initial_external)) return 55;
    persistent_pool.seal();
    auto* persistent_node = static_cast<uint8_t*>(
        persistent_pool.allocate(0x130));
    if (!persistent_node) return 56;
    persistent_node[0] = 0x5a;
    persistent_node[0x12f] = 0xa5;
    const uintptr_t persistent_address =
        reinterpret_cast<uintptr_t>(persistent_node);
    if (!persistent_pool.begin_session_preserving_live_nodes()) return 57;
    if (persistent_pool.sealed()
        || persistent_pool.allocated_count() != 1
        || persistent_pool.external_count() != 0
        || persistent_pool.pool_slot_address(0) != persistent_address
        || persistent_node[0] != 0x5a
        || persistent_node[0x12f] != 0xa5)
        return 58;
    if (!persistent_pool.track_initial_node(initial_external + 0x1000))
        return 59;
    persistent_pool.seal();
    if (!persistent_pool.intercept_free(persistent_node)
        || persistent_pool.allocated_count() != 0)
        return 60;

    Horse::RollbackStageWindAllocationPool deferred_pool;
    const uintptr_t deferred_external = 0x22345000;
    if (!deferred_pool.track_initial_node(deferred_external)) return 61;
    deferred_pool.seal();
    if (!deferred_pool.intercept_free(
            reinterpret_cast<void*>(deferred_external)))
        return 62;
    if (deferred_pool.begin_session_preserving_live_nodes()) return 63;
    uintptr_t released_external = 0;
    if (!deferred_pool.take_deferred_external_free(released_external)
        || released_external != deferred_external
        || !deferred_pool.begin_session_preserving_live_nodes())
        return 64;

    constexpr size_t region_bytes = 0x4850000;
    auto* base = static_cast<uint8_t*>(::VirtualAlloc(
        nullptr, region_bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!base) return 1;

    auto release = [&]() { ::VirtualFree(base, 0, MEM_RELEASE); };
    uint8_t* sentinel = base + 0x1000;
    uint8_t* node0 = base + 0x1100;
    uint8_t* node1 = base + 0x1200;
    uint8_t* emitter0 = base + 0x2000;
    uint8_t* emitter1 = base + 0x2200;
    const std::array<uint32_t, 6> expected_combined_rng {
        0x10203040u, 0x55667788u, 0x90ABCDEFu,
        0x13579BDFu, 0x2468ACE0u, 0x0BADF00Du,
    };
    write(base + Horse::kRollbackStageWindOutputActiveRva, uint32_t {1});
    std::memcpy(base + Horse::kRollbackStageWindCombinedRngStateRva,
        expected_combined_rng.data(), sizeof(expected_combined_rng));
    write(base + Horse::kRollbackStageWindEmitterListRva,
          reinterpret_cast<uintptr_t>(sentinel));
    write(base + Horse::kRollbackStageWindEmitterCountRva, uint64_t {2});
    write(sentinel, reinterpret_cast<uintptr_t>(node0));
    write(node0, reinterpret_cast<uintptr_t>(node1));
    write(node1, reinterpret_cast<uintptr_t>(sentinel));
    write(node0 + 0x10, reinterpret_cast<uintptr_t>(emitter0));
    write(node1 + 0x10, reinterpret_cast<uintptr_t>(emitter1));
    seed_emitter(emitter0, 1, 4, 0.25f, 0.75f, 1.5f);
    seed_emitter(emitter1, 0, 7, 0.5f, 1.25f, 2.0f);
    write(emitter0 + 0x70, uint32_t {0xA1B2C3D4});
    write(emitter0 + 0xA8, uint64_t {0x1122334455667788ull});

    Horse::RollbackStageWindSnapshot snapshot {};
    auto report = Horse::CaptureRollbackStageWindSnapshot(
        reinterpret_cast<uintptr_t>(base), snapshot);
    if (!report.ok || report.count != 2
        || snapshot.output_active != 1
        || snapshot.combined_rng_state != expected_combined_rng
        || snapshot.canonical_hash == 0 || snapshot.integrity_hash == 0)
    {
        release();
        return 2;
    }

    Horse::RollbackStageWindSnapshot relocated = snapshot;
    relocated.sentinel += 0x100;
    relocated.emitters[0].list_node += 0x100;
    relocated.emitters[0].emitter += 0x100;
    relocated.integrity_hash =
        Horse::HashRollbackStageWindIntegrity(relocated);
    if (Horse::HashRollbackStageWindCanonical(relocated)
            != snapshot.canonical_hash
        || relocated.integrity_hash == snapshot.integrity_hash)
    {
        release();
        return 3;
    }
    Horse::RollbackStageWindSnapshot corrupted_emitter = snapshot;
    corrupted_emitter.emitters[0].data[0x20] ^= 1;
    if (Horse::HashRollbackStageWindIntegrity(corrupted_emitter)
            == snapshot.integrity_hash)
    {
        release();
        return 15;
    }
    Horse::RollbackStageWindSnapshot emitter_local_tail = snapshot;
    emitter_local_tail.emitters[0].data[
        Horse::kRollbackStageWindEmitterSemanticBytes
            - Horse::kRollbackStageWindEmitterMutableOffset] ^= 1;
    if (Horse::HashRollbackStageWindCanonical(emitter_local_tail)
            != snapshot.canonical_hash
        || Horse::HashRollbackStageWindIntegrity(emitter_local_tail)
            == snapshot.integrity_hash)
    {
        release();
        return 64;
    }
    Horse::RollbackStageWindSnapshot changed_output_active = snapshot;
    changed_output_active.output_active = 0;
    if (Horse::HashRollbackStageWindCanonical(changed_output_active)
            == snapshot.canonical_hash
        || Horse::HashRollbackStageWindIntegrity(changed_output_active)
            == snapshot.integrity_hash)
    {
        release();
        return 40;
    }
    Horse::RollbackStageWindSnapshot changed_combined_rng = snapshot;
    changed_combined_rng.combined_rng_state[3] ^= 1;
    if (Horse::HashRollbackStageWindCanonical(changed_combined_rng)
            == snapshot.canonical_hash
        || Horse::HashRollbackStageWindIntegrity(changed_combined_rng)
            == snapshot.integrity_hash)
    {
        release();
        return 41;
    }

    write(base + Horse::kRollbackStageWindOutputActiveRva, uint32_t {0});
    std::memset(base + Horse::kRollbackStageWindCombinedRngStateRva, 0,
        sizeof(expected_combined_rng));
    seed_emitter(emitter0, 0, 99, 8.0f, 9.0f, 10.0f);
    write(emitter0 + 0x70, uint32_t {0});
    report = Horse::RestoreRollbackStageWindSnapshot(
        reinterpret_cast<uintptr_t>(base), snapshot);
    if (!report.ok)
    {
        release();
        return 4;
    }
    Horse::RollbackStageWindSnapshot verified {};
    report = Horse::CaptureRollbackStageWindSnapshot(
        reinterpret_cast<uintptr_t>(base), verified);
    uint32_t restored_emitter_value = 0;
    std::memcpy(&restored_emitter_value, emitter0 + 0x70,
                sizeof(restored_emitter_value));
    if (!report.ok || restored_emitter_value != 0xA1B2C3D4
        || verified.output_active != 1
        || verified.combined_rng_state != expected_combined_rng
        || verified.integrity_hash != snapshot.integrity_hash
        || verified.canonical_hash != snapshot.canonical_hash)
    {
        release();
        return 5;
    }

    write(node0 + 0x10, reinterpret_cast<uintptr_t>(emitter1));
    report = Horse::RestoreRollbackStageWindSnapshot(
        reinterpret_cast<uintptr_t>(base), snapshot);
    if (report.ok
        || std::strcmp(report.failure,
            "stage-wind-emitter-ownership-changed") != 0)
    {
        release();
        return 6;
    }

    // Production wind ownership keeps native graph addresses stable while
    // redirecting new node allocations into a fixed arena. Prove exact graph
    // bytes, pool state, and a logically freed node all restore together.
    write(node0 + 0x10, reinterpret_cast<uintptr_t>(emitter0));
    uint8_t* root = base + 0x3000;
    uint8_t* graph0 = base + 0x3400;
    uint8_t* graph1 = base + 0x3800;
    write(base + Horse::kRollbackStageWindRootRva,
          reinterpret_cast<uintptr_t>(root));
    write(root, reinterpret_cast<uintptr_t>(graph0));
    write(root + 0x18, reinterpret_cast<uintptr_t>(base + 0x334430));
    // One live callback in bank 0 participates in the peer hash. A stale
    // inactive-bank value is retained for local restore but must not make
    // equivalent peers disagree.
    write(root + 0x58, uintptr_t {0x1234});
    write(root + 0x9C, int32_t {1});
    write(root + 0x08, 1.25f);
    write(root + 0x0C, 2.5f);
    write(root + 0xA0, int32_t {3});
    write(root + 0xA4, int32_t {1});
    write(root + 0xB0, 4.5f);
    write(root + 0xC0, 6.5f);
    write(graph0, reinterpret_cast<uintptr_t>(base
        + Horse::kRollbackStageWindParallelVtableRva));
    write(graph0 + 0x10, reinterpret_cast<uintptr_t>(graph1));
    write(graph0 + 0x18, uintptr_t {0});
    write(graph0 + 0x28, reinterpret_cast<uintptr_t>(root));
    write(graph1, reinterpret_cast<uintptr_t>(base
        + Horse::kRollbackStageWindRingOutVtableRva));
    write(graph1 + 0x10, uintptr_t {0});
    write(graph1 + 0x18, reinterpret_cast<uintptr_t>(graph0));
    write(graph1 + 0x08, uint64_t {0x1122334455667788ull});
    write(graph1 + 0x20, uint64_t {0x8877665544332211ull});
    write(graph1 + 0x28, reinterpret_cast<uintptr_t>(root));
    for (uint32_t offset = 0x34; offset < 0x40; ++offset)
    {
        graph0[offset] = static_cast<uint8_t>(0x80u + offset);
        graph1[offset] = static_cast<uint8_t>(0x40u + offset);
    }
    write(graph0 + 0x40, uint32_t {0x11223344});
    write(graph1 + 0x40, uint32_t {0x55667788});

    // Observational checkpointing must not freeze the production ownership
    // set before the actual rollback boundary. This is a behavioral
    // regression contract for the live startup failure where animation
    // tracing sealed an earlier stock wind graph.
    Horse::RollbackStageWindAllocationPool diagnostic_authority {};
    Horse::RollbackStageWindSnapshot diagnostic_snapshot {};
    report = Horse::CaptureRollbackStageWindSnapshotForDiagnostics(
        reinterpret_cast<uintptr_t>(base), diagnostic_snapshot,
        diagnostic_authority);
    if (!report.ok || diagnostic_authority.sealed()
        || diagnostic_authority.external_count() != 0
        || diagnostic_snapshot.graph.count != 2
        || diagnostic_snapshot.graph.canonical_hash == 0)
    {
        release();
        return 65;
    }

    Horse::RollbackStageWindAllocationPool pool {};
    Horse::RollbackStageWindSnapshot graph_snapshot {};
    report = Horse::CaptureRollbackStageWindSnapshot(
        reinterpret_cast<uintptr_t>(base), graph_snapshot, &pool);
    if (!report.ok || !pool.sealed() || pool.external_count() != 2
        || graph_snapshot.graph.count != 2
        || graph_snapshot.graph.integrity_hash == 0)
    {
        release();
        return 7;
    }

    // A sealed production pool owns nodes by the addresses of its fixed
    // slots. Diagnostic capture after a native allocation must preserve that
    // address identity; copying the pool would move the slots and falsely
    // report stage-wind-node-ownership-unproven.
    auto* pooled_shock_wave =
        static_cast<uint8_t*>(pool.allocate(0x180));
    if (!pooled_shock_wave)
    {
        release();
        return 66;
    }
    write(pooled_shock_wave,
        reinterpret_cast<uintptr_t>(base
            + Horse::kRollbackStageWindShockWaveVtableRva));
    write(pooled_shock_wave + 0x10,
        reinterpret_cast<uintptr_t>(graph0));
    write(pooled_shock_wave + 0x18, uintptr_t {0});
    write(pooled_shock_wave + 0x28,
        reinterpret_cast<uintptr_t>(root));
    write(graph0 + 0x18,
        reinterpret_cast<uintptr_t>(pooled_shock_wave));
    write(root, reinterpret_cast<uintptr_t>(pooled_shock_wave));
    const Horse::RollbackStageWindPoolState pool_before_diagnostic =
        pool.capture_state();
    Horse::RollbackStageWindSnapshot pooled_diagnostic {};
    report = Horse::CaptureRollbackStageWindSnapshotForDiagnostics(
        reinterpret_cast<uintptr_t>(base), pooled_diagnostic, pool);
    const Horse::RollbackStageWindPoolState pool_after_diagnostic =
        pool.capture_state();
    if (!report.ok
        || pooled_diagnostic.graph.count != 3
        || pooled_diagnostic.graph.nodes[0].address
            != reinterpret_cast<uintptr_t>(pooled_shock_wave)
        || pool_before_diagnostic.allocated_mask
            != pool_after_diagnostic.allocated_mask
        || pool_before_diagnostic.external_freed_mask
            != pool_after_diagnostic.external_freed_mask
        || pool_before_diagnostic.sizes
            != pool_after_diagnostic.sizes)
    {
        release();
        return 67;
    }
    write(root, reinterpret_cast<uintptr_t>(graph0));
    write(graph0 + 0x18, uintptr_t {0});
    if (!pool.intercept_free(pooled_shock_wave))
    {
        release();
        return 68;
    }

    Horse::RollbackRngTuple checkpoint_rng {};
    checkpoint_rng.lcg_state = 0x12345678;
    checkpoint_rng.lfsr_state[0] = 0x5A;
    checkpoint_rng.lfsr_hash =
        Horse::RollbackRngTuple::hash_lfsr(
            checkpoint_rng.lfsr_state);
    checkpoint_rng.lfsr_index = 5;
    const std::array<Horse::RollbackStageWindRngCallerCheckpoint, 3>
        checkpoint_callers {{
            {0x33371F, 1},
            {0x3337A5, 2},
            {0x3337F2, 2},
        }};
    const Horse::RollbackStageWindCheckpoint checkpoint =
        Horse::BuildRollbackStageWindCheckpoint(
            graph_snapshot, 3, 0x1122334455667788ull, 9, 0,
            Horse::RollbackStageWindCapturePhase::PreAdvanceZero,
            true, 1, 0x3F800000, 0x3F000000,
            &checkpoint_rng, checkpoint_callers.data(),
            static_cast<uint32_t>(checkpoint_callers.size()), 5, 0);
    if (checkpoint.round_generation != 3
        || checkpoint.round_epoch != 0x1122334455667788ull
        || checkpoint.native_coordinate != 9
        || checkpoint.logical_frame != 0
        || checkpoint.capture_phase
            != Horse::RollbackStageWindCapturePhase::PreAdvanceZero
        || !checkpoint.owned_simulation
        || !checkpoint.rng_valid
        || checkpoint.rng != checkpoint_rng
        || checkpoint.rng_total_calls != 5
        || checkpoint.rng_overflow_calls != 0
        || checkpoint.rng_caller_count != 3
        || checkpoint.rng_callers[0].rva != 0x33371F
        || checkpoint.rng_callers[2].count != 2
        || checkpoint.emitter_count != graph_snapshot.count
        || checkpoint.node_count != graph_snapshot.graph.count)
    {
        release();
        return 46;
    }
    Horse::RollbackStageWindSnapshot allocator_residue =
        graph_snapshot;
    for (uint32_t offset = 0x34; offset < 0x40; ++offset)
        allocator_residue.graph.nodes[0].data[offset] ^= 0x5Au;
    // The parallel-family current-angle output is a float3. +0x12C is the
    // unwritten fourth lane and must behave exactly like allocator residue.
    allocator_residue.graph.nodes[0].data[0x12C] ^= 0x5Au;
    if (Horse::HashRollbackStageWindGraphCanonical(
            allocator_residue.graph)
            != graph_snapshot.graph.canonical_hash
        || Horse::HashRollbackStageWindCanonical(allocator_residue)
            != graph_snapshot.canonical_hash
        || Horse::HashRollbackStageWindGraphIntegrity(
            allocator_residue.graph)
            == graph_snapshot.graph.integrity_hash
        || Horse::HashRollbackStageWindIntegrity(allocator_residue)
            == graph_snapshot.integrity_hash)
    {
        release();
        return 42;
    }
    Horse::RollbackStageWindGraphSnapshot parallel_angle =
        graph_snapshot.graph;
    parallel_angle.nodes[0].data[0x128] ^= 1;
    if (Horse::HashRollbackStageWindGraphCanonical(parallel_angle)
            == graph_snapshot.graph.canonical_hash)
    {
        release();
        return 50;
    }
    Horse::RollbackStageWindGraphSnapshot shock_graph {};
    shock_graph.valid = true;
    shock_graph.root = 0x220000;
    shock_graph.count = 1;
    shock_graph.nodes[0].address = 0x330000;
    shock_graph.nodes[0].vtable =
        0x140000000ull + Horse::kRollbackStageWindShockWaveVtableRva;
    shock_graph.nodes[0].vtable_rva =
        Horse::kRollbackStageWindShockWaveVtableRva;
    shock_graph.nodes[0].bytes = 0x180;
    shock_graph.nodes[0].data.fill(0x5A);
    Horse::StampRollbackStageWindGraphNodeHeader(
        shock_graph.nodes[0], shock_graph.root, 0, 0);
    const uint64_t shock_canonical =
        Horse::HashRollbackStageWindGraphCanonical(shock_graph);
    const uint64_t shock_integrity =
        Horse::HashRollbackStageWindGraphIntegrity(shock_graph);
    if (!shock_canonical || !shock_integrity)
    {
        release();
        return 51;
    }
    for (const auto [begin, end] :
        std::array<std::pair<uint32_t, uint32_t>, 3> {{
            {0xE4, 0xF0},
            {0x110, 0x120},
            {0x12C, 0x130},
        }})
    {
        for (uint32_t offset = begin; offset < end; ++offset)
        {
            Horse::RollbackStageWindGraphSnapshot changed = shock_graph;
            changed.nodes[0].data[offset] ^= 0xA5u;
            if (Horse::HashRollbackStageWindGraphCanonical(changed)
                    != shock_canonical
                || Horse::HashRollbackStageWindGraphIntegrity(changed)
                    == shock_integrity)
            {
                release();
                return 52;
            }
        }
    }
    for (uint32_t offset :
        std::array<uint32_t, 6> {
            0xE3, 0xF0, 0x10F, 0x120, 0x128, 0x17F})
    {
        Horse::RollbackStageWindGraphSnapshot changed = shock_graph;
        changed.nodes[0].data[offset] ^= 1;
        if (Horse::HashRollbackStageWindGraphCanonical(changed)
            == shock_canonical)
        {
            release();
            return 53;
        }
    }
    Horse::RollbackStageWindSnapshot emitter_residue =
        graph_snapshot;
    for (uint32_t native_offset :
        std::array<uint32_t, 2> {0x6C, 0x7C})
    {
        emitter_residue.emitters[0].data[
            native_offset
                - Horse::kRollbackStageWindEmitterMutableOffset] ^= 0x5Au;
    }
    if (Horse::HashRollbackStageWindCanonical(emitter_residue)
            != graph_snapshot.canonical_hash
        || Horse::HashRollbackStageWindIntegrity(emitter_residue)
            == graph_snapshot.integrity_hash)
    {
        release();
        return 47;
    }
    Horse::RollbackStageWindSnapshot emitter_semantic =
        graph_snapshot;
    emitter_semantic.emitters[0].data[0] ^= 1;
    if (Horse::HashRollbackStageWindCanonical(emitter_semantic)
            == graph_snapshot.canonical_hash)
    {
        release();
        return 48;
    }
    for (uint32_t offset :
        std::array<uint32_t, 4> {0x30, 0x60, 0x68, 0x6C})
    {
        Horse::RollbackStageWindGraphSnapshot semantic_change =
            graph_snapshot.graph;
        semantic_change.nodes[0].data[offset] ^= 1;
        if (Horse::HashRollbackStageWindGraphCanonical(semantic_change)
            == graph_snapshot.graph.canonical_hash)
        {
            release();
            return 43;
        }
    }
    const uint64_t presentation_hash =
        Horse::HashRollbackStageWindPresentation(graph_snapshot);
    const uint64_t gameplay_hash =
        Horse::HashRollbackStageWindGameplay(graph_snapshot);
    Horse::RollbackStageWindSnapshot node_force = graph_snapshot;
    node_force.graph.nodes[0].data[0x40] ^= 1;
    if (!presentation_hash || !gameplay_hash
        || Horse::HashRollbackStageWindCanonical(node_force)
            != graph_snapshot.canonical_hash
        || Horse::HashRollbackStageWindGameplay(node_force)
            != gameplay_hash
        || Horse::HashRollbackStageWindGraphCanonical(node_force.graph)
            != graph_snapshot.graph.canonical_hash
        || Horse::HashRollbackStageWindIntegrity(node_force)
            == graph_snapshot.integrity_hash
        || Horse::HashRollbackStageWindPresentation(node_force)
            == presentation_hash)
    {
        release();
        return 54;
    }
    Horse::RollbackStageWindSnapshot oscillator_phase = graph_snapshot;
    oscillator_phase.graph.nodes[0].data[0x70] ^= 1;
    if (Horse::HashRollbackStageWindGameplay(oscillator_phase)
            != gameplay_hash
        || Horse::HashRollbackStageWindPresentation(oscillator_phase)
            == presentation_hash)
    {
        release();
        return 55;
    }
    Horse::RollbackStageWindSnapshot lifecycle = graph_snapshot;
    lifecycle.graph.nodes[0].data[0x30] ^= 1;
    if (Horse::HashRollbackStageWindGameplay(lifecycle)
            == gameplay_hash)
    {
        release();
        return 56;
    }
    Horse::RollbackStageWindSnapshot emitter_presentation = graph_snapshot;
    emitter_presentation.emitters[0].data[0x10] ^= 1;
    if (Horse::HashRollbackStageWindGameplay(emitter_presentation)
            != gameplay_hash
        || Horse::HashRollbackStageWindPresentation(emitter_presentation)
            == presentation_hash)
    {
        release();
        return 57;
    }
    Horse::RollbackStageWindSnapshot emitter_scheduler = graph_snapshot;
    emitter_scheduler.emitters[0].data[0x00] ^= 1;
    if (Horse::HashRollbackStageWindGameplay(emitter_scheduler)
            == gameplay_hash)
    {
        release();
        return 58;
    }
    Horse::RollbackStageWindSnapshot root_strength = graph_snapshot;
    root_strength.graph.root_state.strength += 1.0f;
    if (Horse::HashRollbackStageWindGameplay(root_strength)
            != gameplay_hash
        || Horse::HashRollbackStageWindPresentation(root_strength)
            == presentation_hash)
    {
        release();
        return 59;
    }
    Horse::RollbackStageWindGraphNode ring_in {};
    ring_in.address = 1;
    ring_in.vtable_rva = Horse::kRollbackStageWindRingInVtableRva;
    ring_in.bytes = 0x1E0;
    ring_in.data.fill(0x33);
    const uint64_t ring_in_hash =
        Horse::BuildRollbackStageWindNodeCanonicalBreakdown(
            ring_in).combined;
    for (uint32_t offset : std::array<uint32_t, 8> {
            0x70, 0xF0, 0xF8, 0x110,
            0x130, 0x148, 0x150, 0x15B})
    {
        Horse::RollbackStageWindGraphNode changed = ring_in;
        changed.data[offset] ^= 1;
        if (!ring_in_hash
            || Horse::BuildRollbackStageWindNodeCanonicalBreakdown(
                    changed).combined == ring_in_hash)
        {
            std::cerr << "ring-in semantic mutation was ignored offset=0x"
                      << std::hex << offset
                      << " baseline=0x" << ring_in_hash
                      << " changed=0x"
                      << Horse::BuildRollbackStageWindNodeCanonicalBreakdown(
                            changed).combined
                      << std::dec << '\n';
            release();
            return 44;
        }
    }
    for (uint32_t offset : std::array<uint32_t, 10> {
            0xF4, 0x10C, 0x11C, 0x120, 0x134,
            0x138, 0x144, 0x14C, 0x15C, 0x160})
    {
        Horse::RollbackStageWindGraphNode changed = ring_in;
        changed.data[offset] ^= 1;
        if (Horse::BuildRollbackStageWindNodeCanonicalBreakdown(
                changed).combined != ring_in_hash)
        {
            release();
            return 49;
        }
    }
    std::array<uint8_t, 12> captured_residue {};
    std::memcpy(captured_residue.data(),
        graph_snapshot.graph.nodes[0].data.data() + 0x34,
        captured_residue.size());
    std::memset(graph0 + 0x34, 0xEE, captured_residue.size());
    report = Horse::RestoreRollbackStageWindGraph(
        reinterpret_cast<uintptr_t>(base), graph_snapshot.graph, pool);
    if (!report.ok
        || std::memcmp(graph0 + 0x34, captured_residue.data(),
            captured_residue.size()) != 0)
    {
        release();
        return 45;
    }
    const Horse::RollbackStageWindCanonicalBreakdown breakdown =
        Horse::BuildRollbackStageWindCanonicalBreakdown(graph_snapshot);
    if (breakdown.output_active != 1 || !breakdown.combined_rng
        || !breakdown.emitters
        || !breakdown.root_scheduler
        || !breakdown.root_derived_outputs || !breakdown.graph_nodes
        || !breakdown.graph || breakdown.combined
            != graph_snapshot.canonical_hash)
    {
        release();
        return 32;
    }
    Horse::RollbackStageWindGraphSnapshot empty_bank_zero =
        graph_snapshot.graph;
    empty_bank_zero.root_state.pending_count = 0;
    empty_bank_zero.root_state.active_bank = 0;
    Horse::RollbackStageWindGraphSnapshot empty_bank_one = empty_bank_zero;
    empty_bank_one.root_state.active_bank = 1;
    if (Horse::HashRollbackStageWindGraphCanonical(empty_bank_zero)
            != Horse::HashRollbackStageWindGraphCanonical(empty_bank_one)
        || Horse::HashRollbackStageWindGraphIntegrity(empty_bank_zero)
            == Horse::HashRollbackStageWindGraphIntegrity(empty_bank_one))
    {
        release();
        return 36;
    }
    Horse::RollbackStageWindGraphSnapshot equivalent_selected_bank =
        graph_snapshot.graph;
    equivalent_selected_bank.root_state.active_bank = 1;
    equivalent_selected_bank.root_state.callback_rvas[8] =
        graph_snapshot.graph.root_state.callback_rvas[0];
    if (Horse::HashRollbackStageWindGraphCanonical(equivalent_selected_bank)
            != graph_snapshot.graph.canonical_hash)
    {
        release();
        return 37;
    }
    equivalent_selected_bank.root_state.callback_rvas[8] ^= 1;
    if (Horse::HashRollbackStageWindGraphCanonical(equivalent_selected_bank)
            == graph_snapshot.graph.canonical_hash)
    {
        release();
        return 38;
    }
    const auto node_breakdown =
        Horse::BuildRollbackStageWindNodeCanonicalBreakdown(
            graph_snapshot.graph.nodes[0]);
    if (!node_breakdown.valid || !node_breakdown.common
        || !node_breakdown.body || !node_breakdown.tail
        || !node_breakdown.combined)
    {
        release();
        return 39;
    }
    Horse::RollbackStageWindSnapshot changed_derived = graph_snapshot;
    changed_derived.graph.root_state.scene_tick += 10.0f;
    changed_derived.graph.canonical_hash =
        Horse::HashRollbackStageWindGraphCanonical(changed_derived.graph);
    if (Horse::HashRollbackStageWindCanonical(changed_derived)
            == graph_snapshot.canonical_hash
        || Horse::HashRollbackStageWindGraphIntegrity(changed_derived.graph)
            == graph_snapshot.graph.integrity_hash
        || Horse::BuildRollbackStageWindCanonicalBreakdown(changed_derived)
                .root_derived_outputs
            != breakdown.root_derived_outputs)
    {
        release();
        return 33;
    }
    Horse::RollbackStageWindSnapshot changed_output = graph_snapshot;
    changed_output.graph.root_state.output_forces[0] += 20.0f;
    changed_output.graph.canonical_hash =
        Horse::HashRollbackStageWindGraphCanonical(changed_output.graph);
    if (Horse::HashRollbackStageWindCanonical(changed_output)
            != graph_snapshot.canonical_hash
        || Horse::HashRollbackStageWindGraphIntegrity(changed_output.graph)
            == graph_snapshot.graph.integrity_hash
        || Horse::HashRollbackStageWindPresentation(changed_output)
            == presentation_hash)
    {
        release();
        return 34;
    }
    Horse::RollbackStageWindSnapshot changed_scheduler = graph_snapshot;
    changed_scheduler.graph.root_state.schedule_state += 1;
    changed_scheduler.graph.canonical_hash =
        Horse::HashRollbackStageWindGraphCanonical(changed_scheduler.graph);
    if (Horse::HashRollbackStageWindCanonical(changed_scheduler)
            == graph_snapshot.canonical_hash
        || Horse::HashRollbackStageWindGameplay(changed_scheduler)
            == gameplay_hash)
    {
        release();
        return 35;
    }
    Horse::RollbackStageWindGraphSnapshot relocated_graph =
        graph_snapshot.graph;
    relocated_graph.root += 0x100;
    for (uint32_t index = 0; index < relocated_graph.count; ++index)
    {
        relocated_graph.nodes[index].address += 0x100;
        relocated_graph.nodes[index].vtable += 0x100;
    }
    if (Horse::HashRollbackStageWindGraphCanonical(relocated_graph)
            != graph_snapshot.graph.canonical_hash)
    {
        release();
        return 17;
    }
    Horse::RollbackStageWindGraphSnapshot relocated_embedded_pointer =
        graph_snapshot.graph;
    const uintptr_t process_local_gap_value =
        reinterpret_cast<uintptr_t>(base + 0x4400);
    std::memcpy(relocated_embedded_pointer.nodes[0].data.data() + 0xE8,
        &process_local_gap_value, sizeof(process_local_gap_value));
    if (Horse::HashRollbackStageWindGraphCanonical(
            relocated_embedded_pointer)
            != graph_snapshot.graph.canonical_hash
        || Horse::HashRollbackStageWindGraphIntegrity(
            relocated_embedded_pointer)
            == graph_snapshot.graph.integrity_hash)
    {
        release();
        return 27;
    }
    Horse::RollbackStageWindGraphSnapshot missing_active_callback =
        graph_snapshot.graph;
    missing_active_callback.root_state.callback_rvas[0] = 0;
    if (Horse::HashRollbackStageWindGraphCanonical(
            missing_active_callback) != 0)
    {
        release();
        return 24;
    }
    Horse::RollbackStageWindSnapshot corrupted_graph = graph_snapshot;
    corrupted_graph.graph.nodes[0].data[0x40] ^= 1;
    if (Horse::HashRollbackStageWindIntegrity(corrupted_graph) != 0
        || Horse::HashRollbackStageWindGraphCanonical(
            corrupted_graph.graph)
            != graph_snapshot.graph.canonical_hash
        || Horse::HashRollbackStageWindPresentation(corrupted_graph)
            == presentation_hash)
    {
        release();
        return 14;
    }

    uint32_t output_before_missing_pool = 0;
    std::array<uint8_t, 16> rng_before_missing_pool {};
    std::memcpy(&output_before_missing_pool,
        base + Horse::kRollbackStageWindOutputActiveRva,
        sizeof(output_before_missing_pool));
    std::memcpy(rng_before_missing_pool.data(),
        base + Horse::kRollbackStageWindCombinedRngStateRva,
        rng_before_missing_pool.size());
    report = Horse::RestoreRollbackStageWindSnapshot(
        reinterpret_cast<uintptr_t>(base), graph_snapshot, nullptr);
    uint32_t output_after_missing_pool = 0;
    std::array<uint8_t, 16> rng_after_missing_pool {};
    std::memcpy(&output_after_missing_pool,
        base + Horse::kRollbackStageWindOutputActiveRva,
        sizeof(output_after_missing_pool));
    std::memcpy(rng_after_missing_pool.data(),
        base + Horse::kRollbackStageWindCombinedRngStateRva,
        rng_after_missing_pool.size());
    if (report.ok
        || std::strcmp(report.failure, "stage-wind-graph-pool-missing") != 0
        || output_after_missing_pool != output_before_missing_pool
        || rng_after_missing_pool != rng_before_missing_pool)
    {
        release();
        return 50;
    }

    write(root + 0x98, uint32_t {1});
    write(root + 0x18, uintptr_t {0});
    write(root + 0xA0, int32_t {99});
    write(root + 0xC0, -9.0f);
    write(graph0 + 0x40, uint32_t {0});
    report = Horse::RestoreRollbackStageWindSnapshot(
        reinterpret_cast<uintptr_t>(base), graph_snapshot, &pool);
    uint32_t restored_graph_value = 0;
    uint32_t restored_root_value = 1;
    uintptr_t restored_callback = 0;
    int32_t restored_schedule_state = 0;
    float restored_force = 0.0f;
    std::memcpy(&restored_graph_value, graph0 + 0x40,
                sizeof(restored_graph_value));
    std::memcpy(&restored_root_value, root + 0x98,
                sizeof(restored_root_value));
    std::memcpy(&restored_callback, root + 0x18,
                sizeof(restored_callback));
    std::memcpy(&restored_schedule_state, root + 0xA0,
                sizeof(restored_schedule_state));
    std::memcpy(&restored_force, root + 0xC0,
                sizeof(restored_force));
    if (!report.ok || restored_graph_value != 0x11223344
        || restored_root_value != 0
        || restored_callback
            != reinterpret_cast<uintptr_t>(base + 0x334430)
        || restored_schedule_state != 3 || restored_force != 6.5f)
    {
        std::cerr << "graph restore failed: " << report.failure
                  << " graph=" << restored_graph_value
                  << " root=" << restored_root_value
                  << " callback=" << restored_callback
                  << " schedule=" << restored_schedule_state
                  << " force=" << restored_force << "\n";
        release();
        return 8;
    }

    auto* pooled = static_cast<uint8_t*>(pool.allocate(0x1E0));
    if (!pooled)
    {
        release();
        return 9;
    }
    write(pooled, reinterpret_cast<uintptr_t>(base
        + Horse::kRollbackStageWindRingInVtableRva));
    write(pooled + 0x10, reinterpret_cast<uintptr_t>(graph0));
    write(pooled + 0x18, uintptr_t {0});
    write(pooled + 0x28, reinterpret_cast<uintptr_t>(root));
    write(graph0 + 0x18, reinterpret_cast<uintptr_t>(pooled));
    write(root, reinterpret_cast<uintptr_t>(pooled));
    Horse::RollbackStageWindSnapshot pooled_snapshot {};
    report = Horse::CaptureRollbackStageWindSnapshot(
        reinterpret_cast<uintptr_t>(base), pooled_snapshot, &pool);
    if (!report.ok || pooled_snapshot.graph.count != 3
        || pool.allocated_count() != 1
        || !pool.intercept_free(pooled))
    {
        release();
        return 10;
    }
    write(root, reinterpret_cast<uintptr_t>(graph0));
    report = Horse::RestoreRollbackStageWindSnapshot(
        reinterpret_cast<uintptr_t>(base), pooled_snapshot, &pool);
    uintptr_t restored_head = 0;
    uintptr_t restored_next = 0;
    uintptr_t restored_root = 0;
    std::memcpy(&restored_head, root, sizeof(restored_head));
    std::memcpy(&restored_next, pooled + 0x10, sizeof(restored_next));
    std::memcpy(&restored_root, pooled + 0x28, sizeof(restored_root));
    if (!report.ok || restored_head != reinterpret_cast<uintptr_t>(pooled)
        || restored_next != reinterpret_cast<uintptr_t>(graph0)
        || restored_root != reinterpret_cast<uintptr_t>(root)
        || pool.allocated_count() != 1)
    {
        release();
        return 11;
    }

    // A peer with no live pool node may safely retire our extra constructed
    // node. Exercise the complete pure-plan -> preflight -> restore ->
    // recapture path, then force validation failure and prove that the
    // original topology and pool state are recoverable after mutation.
    Horse::RollbackStageWindAllocationPool owner_pool {};
    if (!owner_pool.track_initial_node(
            reinterpret_cast<uintptr_t>(graph0))
        || !owner_pool.track_initial_node(
            reinterpret_cast<uintptr_t>(graph1)))
    {
        release();
        return 42;
    }
    owner_pool.seal();
    Horse::RollbackStageWindSnapshot owner_authority_snapshot = graph_snapshot;
    for (uint32_t emitter_index = 0;
         emitter_index < owner_authority_snapshot.count; ++emitter_index)
    {
        auto& data = owner_authority_snapshot.emitters[emitter_index].data;
        for (size_t byte_index = 0; byte_index < data.size(); ++byte_index)
            data[byte_index] ^= static_cast<uint8_t>(
                0x51u + emitter_index + byte_index);
    }
    owner_authority_snapshot.graph.root_state.scene_tick += 1.0f;
    for (float& output :
         owner_authority_snapshot.graph.root_state.output_forces)
        output += 2.0f;
    owner_authority_snapshot.graph.canonical_hash =
        Horse::HashRollbackStageWindGraphCanonical(
            owner_authority_snapshot.graph);
    owner_authority_snapshot.graph.integrity_hash =
        Horse::HashRollbackStageWindGraphIntegrity(
            owner_authority_snapshot.graph);
    owner_authority_snapshot.canonical_hash =
        Horse::HashRollbackStageWindCanonical(owner_authority_snapshot);
    owner_authority_snapshot.integrity_hash =
        Horse::HashRollbackStageWindIntegrity(owner_authority_snapshot);
    Horse::RollbackStageWindAuthorityImage authority_image {};
    if (!Horse::RollbackBuildStageWindAuthorityImage(
            owner_authority_snapshot, owner_pool, authority_image))
    {
        release();
        return 43;
    }
    const uintptr_t fighter0 = reinterpret_cast<uintptr_t>(base + 0x5000);
    const uintptr_t fighter1 = reinterpret_cast<uintptr_t>(base + 0x35000);
    std::memcpy(reinterpret_cast<void*>(fighter0
            + Horse::kRollbackStageWindFighterSliceOffset),
        graph_snapshot.graph.root_state.output_forces.data() + 4,
        4 * sizeof(float));
    std::memcpy(reinterpret_cast<void*>(fighter1
            + Horse::kRollbackStageWindFighterSliceOffset),
        graph_snapshot.graph.root_state.output_forces.data() + 8,
        4 * sizeof(float));
    Horse::RollbackStageWindSnapshot authority_plan {};
    const auto authority_report =
        Horse::RollbackApplyStageWindAuthorityImage(
            authority_image, pooled_snapshot,
            reinterpret_cast<uintptr_t>(base), pool,
            fighter0, fighter1, authority_plan);
    auto expected_authorized_emitter =
        pooled_snapshot.emitters[0].data;
    const bool expected_emitter_valid =
        Horse::CopyRollbackStageWindEmitterSemanticBytes(
            owner_authority_snapshot.emitters[0].data,
            expected_authorized_emitter);
    if (!authority_report.ok || !authority_report.topology_rebuilt
        || authority_plan.graph.count != 2
        || authority_plan.graph.pool.allocated_mask != 0
        || pool.allocated_count() != 1
        || authority_plan.canonical_hash
            != owner_authority_snapshot.canonical_hash
        || Horse::BuildRollbackStageWindCanonicalBreakdown(authority_plan)
                .emitters
            != Horse::BuildRollbackStageWindCanonicalBreakdown(
                owner_authority_snapshot).emitters
        || !expected_emitter_valid
        || authority_plan.emitters[0].data
            != expected_authorized_emitter
        || authority_plan.emitters[0].list_node
            != pooled_snapshot.emitters[0].list_node
        || authority_plan.emitters[0].emitter
            != pooled_snapshot.emitters[0].emitter
        || authority_plan.graph.root_state.scene_tick
            != owner_authority_snapshot.graph.root_state.scene_tick
        || authority_plan.graph.root_state.output_forces
            != pooled_snapshot.graph.root_state.output_forces)
    {
        release();
        return 44;
    }
    const auto preflight = [&](
        const Horse::RollbackStageWindSnapshot& expected) noexcept {
        return Horse::PreflightRollbackStageWindSnapshotRestore(
                reinterpret_cast<uintptr_t>(base), expected, &pool).ok
            && Horse::RollbackStageWindFighterOutputsReadable(
                fighter0, fighter1);
    };
    const auto restore = [&](
        const Horse::RollbackStageWindSnapshot& expected) noexcept {
        return Horse::RestoreRollbackStageWindSnapshot(
                reinterpret_cast<uintptr_t>(base), expected, &pool).ok
            && Horse::RestoreRollbackStageWindFighterOutputs(
                expected, fighter0, fighter1);
    };
    bool reject_authorized_once = true;
    const auto validate = [&](
        const Horse::RollbackStageWindSnapshot& expected) noexcept {
        Horse::RollbackStageWindSnapshot observed {};
        const auto observed_report =
            Horse::CaptureRollbackStageWindSnapshot(
                reinterpret_cast<uintptr_t>(base), observed, &pool);
        const bool matches = observed_report.ok
            && observed.integrity_hash == expected.integrity_hash
            && Horse::RollbackStageWindTopologyAndOwnershipMatch(
                expected, observed)
            && Horse::RollbackStageWindRootFighterOutputsConsistent(
                observed, fighter0, fighter1);
        if (&expected == &authority_plan && reject_authorized_once)
        {
            reject_authorized_once = false;
            return false;
        }
        return matches;
    };
    Horse::RollbackStageWindSnapshot unrestorable_plan = authority_plan;
    unrestorable_plan.output_active =
        pooled_snapshot.output_active == 0 ? 1u : 0u;
    unrestorable_plan.graph.nodes[0].address =
        reinterpret_cast<uintptr_t>(base + 0x7000);
    unrestorable_plan.graph.canonical_hash =
        Horse::HashRollbackStageWindGraphCanonical(
            unrestorable_plan.graph);
    unrestorable_plan.graph.integrity_hash =
        Horse::HashRollbackStageWindGraphIntegrity(
            unrestorable_plan.graph);
    unrestorable_plan.canonical_hash =
        Horse::HashRollbackStageWindCanonical(unrestorable_plan);
    unrestorable_plan.integrity_hash =
        Horse::HashRollbackStageWindIntegrity(unrestorable_plan);
    uint32_t control_before_rejection = 0;
    std::memcpy(&control_before_rejection,
        base + Horse::kRollbackStageWindOutputActiveRva,
        sizeof(control_before_rejection));
    const auto rejected = Horse::RollbackExecuteCarriedStateTransaction(
        unrestorable_plan, pooled_snapshot, preflight, restore, validate);
    uint32_t control_after_rejection = 0;
    std::memcpy(&control_after_rejection,
        base + Horse::kRollbackStageWindOutputActiveRva,
        sizeof(control_after_rejection));
    if (rejected != Horse::RollbackCarriedStateTransactionResult::
            RejectedBeforeMutation
        || control_after_rejection != control_before_rejection
        || pool.allocated_count() != 1)
    {
        release();
        return 47;
    }
    bool partial_authorized_restore = true;
    const auto partial_restore = [&](
        const Horse::RollbackStageWindSnapshot& expected) noexcept {
        if (&expected == &authority_plan && partial_authorized_restore)
        {
            partial_authorized_restore = false;
            std::memcpy(root + 0x0C,
                &expected.graph.root_state.scene_tick, sizeof(float));
            std::memcpy(root + 0xC0,
                expected.graph.root_state.output_forces.data(),
                sizeof(float));
            std::memcpy(reinterpret_cast<void*>(fighter0
                    + Horse::kRollbackStageWindFighterSliceOffset),
                expected.graph.root_state.output_forces.data() + 4,
                4 * sizeof(float));
            return false;
        }
        return restore(expected);
    };
    const auto partial_recovered =
        Horse::RollbackExecuteCarriedStateTransaction(
            authority_plan, pooled_snapshot, preflight,
            partial_restore, validate);
    Horse::RollbackStageWindSnapshot partial_recovered_snapshot {};
    report = Horse::CaptureRollbackStageWindSnapshot(
        reinterpret_cast<uintptr_t>(base), partial_recovered_snapshot, &pool);
    if (partial_authorized_restore
        || partial_recovered
            != Horse::RollbackCarriedStateTransactionResult::FailedRecovered
        || !report.ok
        || partial_recovered_snapshot.integrity_hash
            != pooled_snapshot.integrity_hash
        || !Horse::RollbackStageWindTopologyAndOwnershipMatch(
            pooled_snapshot, partial_recovered_snapshot)
        || !Horse::RollbackStageWindRootFighterOutputsConsistent(
            partial_recovered_snapshot, fighter0, fighter1)
        || pool.allocated_count() != 1)
    {
        release();
        return 48;
    }
    const auto recovered = Horse::RollbackExecuteCarriedStateTransaction(
        authority_plan, pooled_snapshot, preflight, restore, validate);
    Horse::RollbackStageWindSnapshot recovered_snapshot {};
    report = Horse::CaptureRollbackStageWindSnapshot(
        reinterpret_cast<uintptr_t>(base), recovered_snapshot, &pool);
    if (recovered
            != Horse::RollbackCarriedStateTransactionResult::FailedRecovered
        || !report.ok
        || recovered_snapshot.integrity_hash
            != pooled_snapshot.integrity_hash
        || !Horse::RollbackStageWindTopologyAndOwnershipMatch(
            pooled_snapshot, recovered_snapshot)
        || !Horse::RollbackStageWindRootFighterOutputsConsistent(
            recovered_snapshot, fighter0, fighter1)
        || pool.allocated_count() != 1)
    {
        release();
        return 45;
    }
    const auto applied_authority =
        Horse::RollbackExecuteCarriedStateTransaction(
            authority_plan, pooled_snapshot, preflight, restore, validate);
    Horse::RollbackStageWindSnapshot applied_snapshot {};
    report = Horse::CaptureRollbackStageWindSnapshot(
        reinterpret_cast<uintptr_t>(base), applied_snapshot, &pool);
    if (applied_authority
            != Horse::RollbackCarriedStateTransactionResult::Applied
        || !report.ok
        || applied_snapshot.integrity_hash != authority_plan.integrity_hash
        || Horse::BuildRollbackStageWindCanonicalBreakdown(applied_snapshot)
                .emitters
            != Horse::BuildRollbackStageWindCanonicalBreakdown(
                owner_authority_snapshot).emitters
        || applied_snapshot.emitters[0].data
            != expected_authorized_emitter
        || applied_snapshot.emitters[0].list_node
            != pooled_snapshot.emitters[0].list_node
        || applied_snapshot.emitters[0].emitter
            != pooled_snapshot.emitters[0].emitter
        || !Horse::RollbackStageWindTopologyAndOwnershipMatch(
            authority_plan, applied_snapshot)
        || !Horse::RollbackStageWindRootFighterOutputsConsistent(
            applied_snapshot, fighter0, fighter1)
        || pool.allocated_count() != 0
        || !restore(pooled_snapshot) || pool.allocated_count() != 1)
    {
        release();
        return 46;
    }

    // The live second-round failure had one fewer fixed-pool graph node on
    // the guest. Exercise the opposite topology direction end to end:
    // owner image (3 nodes) -> guest plan (2 nodes) -> materialized pool
    // node -> restore -> recapture.
    if (!restore(graph_snapshot) || pool.allocated_count() != 0)
    {
        release();
        return 49;
    }
    auto* materialized_owner_node =
        static_cast<uint8_t*>(pool.allocate(0x130));
    if (!materialized_owner_node)
    {
        release();
        return 49;
    }
    write(materialized_owner_node, reinterpret_cast<uintptr_t>(base
        + Horse::kRollbackStageWindRingOutVtableRva));
    write(materialized_owner_node + 0x10, uintptr_t {0});
    write(materialized_owner_node + 0x18,
        reinterpret_cast<uintptr_t>(graph1));
    write(materialized_owner_node + 0x28,
        reinterpret_cast<uintptr_t>(root));
    write(materialized_owner_node + 0x40, uint32_t {0x99AABBCC});
    write(graph1 + 0x10,
        reinterpret_cast<uintptr_t>(materialized_owner_node));
    Horse::RollbackStageWindSnapshot materialized_owner {};
    report = Horse::CaptureRollbackStageWindSnapshot(
        reinterpret_cast<uintptr_t>(base), materialized_owner, &pool);
    Horse::RollbackStageWindAuthorityImage larger_authority_image {};
    if (!report.ok || materialized_owner.graph.count != 3
        || pool.allocated_count() != 1
        || !Horse::RollbackBuildStageWindAuthorityImage(
            materialized_owner, pool, larger_authority_image)
        || !restore(graph_snapshot)
        || pool.allocated_count() != 0)
    {
        release();
        return 49;
    }
    Horse::RollbackStageWindSnapshot materialized_plan {};
    const auto materialized_report =
        Horse::RollbackApplyStageWindAuthorityImage(
            larger_authority_image, graph_snapshot,
            reinterpret_cast<uintptr_t>(base), pool, fighter0, fighter1,
            materialized_plan);
    if (!materialized_report.ok || !materialized_report.topology_rebuilt
        || materialized_report.owner_node_count != 3
        || materialized_report.local_node_count != 2
        || materialized_plan.graph.count != 3
        || materialized_plan.graph.pool.allocated_mask != 1u
        || materialized_plan.graph.nodes[2].address
            != pool.pool_slot_address(0)
        || materialized_plan.graph.nodes[2].address
            == graph_snapshot.graph.nodes[0].address
        || materialized_plan.graph.nodes[2].address
            == graph_snapshot.graph.nodes[1].address
        || pool.allocated_count() != 0
        || !preflight(materialized_plan)
        || !restore(materialized_plan))
    {
        release();
        return 49;
    }
    Horse::RollbackStageWindSnapshot materialized_observed {};
    report = Horse::CaptureRollbackStageWindSnapshot(
        reinterpret_cast<uintptr_t>(base), materialized_observed, &pool);
    if (!report.ok
        || materialized_observed.integrity_hash
            != materialized_plan.integrity_hash
        || materialized_observed.canonical_hash
            != materialized_plan.canonical_hash
        || !Horse::RollbackStageWindTopologyAndOwnershipMatch(
            materialized_plan, materialized_observed)
        || !Horse::RollbackStageWindRootFighterOutputsConsistent(
            materialized_observed, fighter0, fighter1)
        || pool.allocated_count() != 1
        || !restore(pooled_snapshot) || pool.allocated_count() != 1)
    {
        release();
        return 49;
    }

    void* unreachable = pool.allocate(0x130);
    Horse::RollbackStageWindSnapshot unreachable_snapshot {};
    report = Horse::CaptureRollbackStageWindSnapshot(
        reinterpret_cast<uintptr_t>(base), unreachable_snapshot, &pool);
    if (!unreachable || report.ok
        || std::strcmp(report.failure,
            "stage-wind-pool-reachability-mismatch") != 0
        || !pool.intercept_free(unreachable))
    {
        release();
        return 18;
    }

    if (Horse::kRollbackStageWindGraphMaxNodes
            != Horse::kRollbackStageWindPoolMaxNodes
                + Horse::kRollbackStageWindExternalMaxNodes)
    {
        release();
        return 12;
    }
    if (Horse::kRollbackStageWindPoolMaxNodes != 32
        || Horse::kRollbackStageWindExternalMaxNodes != 16
        || Horse::kRollbackStageWindGraphMaxNodes != 48)
    {
        release();
        return 12;
    }
    for (size_t index = 1;
         index < Horse::kRollbackStageWindPoolMaxNodes; ++index)
    {
        if (!pool.allocate(0x70))
        {
            release();
            return 12;
        }
    }
    if (pool.allocate(0x70) != nullptr)
    {
        release();
        return 13;
    }

    uintptr_t deferred = 0;
    if (!pool.intercept_free(graph0)
        || pool.intercept_free(graph0)
        || !pool.has_deferred_external_frees()
        || !pool.take_deferred_external_free(deferred)
        || deferred != reinterpret_cast<uintptr_t>(graph0)
        || pool.has_deferred_external_frees()
        || pool.take_deferred_external_free(deferred))
    {
        release();
        return 19;
    }

    release();
    std::cout << "rollback stage wind snapshot self-test passed\n";
    return 0;
}
