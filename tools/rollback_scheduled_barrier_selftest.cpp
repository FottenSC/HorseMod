#include "RollbackScheduledBarrier.hpp"

#include <cstdint>
#include <iostream>
#include <limits>
#include <atomic>
#include <thread>

int main()
{
    uint64_t target = 0;
    if (!Horse::ParseRollbackScheduledTarget("  12345\r\n", target)
        || target != 12345)
        return 1;
    if (Horse::ParseRollbackScheduledTarget("", target)
        || Horse::ParseRollbackScheduledTarget("0", target)
        || Horse::ParseRollbackScheduledTarget("12x", target)
        || Horse::ParseRollbackScheduledTarget(
            "18446744073709551616", target))
        return 2;

    constexpr uint64_t generation = 7;
    constexpr uint64_t scheduled = 1000;
    if (Horse::RollbackScheduledReleaseDue(
            generation, generation, true, scheduled,
            false, false, scheduled - 1))
        return 3;
    if (!Horse::RollbackScheduledReleaseDue(
            generation, generation, true, scheduled,
            false, false, scheduled))
        return 4;
    if (Horse::RollbackScheduledReleaseDue(
            generation + 1, generation, true, scheduled,
            false, false, scheduled)
        || Horse::RollbackScheduledReleaseDue(
            generation, generation, false, scheduled,
            false, false, scheduled)
        || Horse::RollbackScheduledReleaseDue(
            generation, generation, true, scheduled,
            true, false, scheduled)
        || Horse::RollbackScheduledReleaseDue(
            generation, generation, true, scheduled,
            false, true, scheduled))
        return 5;

    if (!Horse::RollbackStockBattleAssetReleaseEligible(
            true, true, true, true, false))
        return 6;
    for (unsigned missing = 0; missing < 5; ++missing)
    {
        bool values[5] {true, true, true, true, false};
        values[missing] = !values[missing];
        if (Horse::RollbackStockBattleAssetReleaseEligible(
                values[0], values[1], values[2], values[3], values[4]))
            return 7;
    }

    Horse::RollbackScheduledBarrierCoordinator peer0;
    Horse::RollbackScheduledBarrierCoordinator peer1;
    peer0.reset(generation);
    peer1.reset(generation);
    if (!peer0.latch_target("1000") || !peer1.latch_target("1000"))
        return 8;

    // Heavy observation can happen at unrelated phases, but has no release
    // transition. A missing readiness gate stays closed even after the target.
    const uint64_t peer0_heavy_tick = scheduled + 300;
    const uint64_t peer1_heavy_tick = scheduled + 900;
    if (peer0.release_applied() || peer1.release_applied()
        || Horse::RollbackScheduledReleaseAllowed(
            generation, generation, true, scheduled, false, false,
            peer0_heavy_tick, true, true, true, false, false)
        || Horse::RollbackScheduledReleaseAllowed(
            generation, generation, true, scheduled, false, false,
            peer1_heavy_tick, true, true, false, true, false))
        return 9;

    // The native stock predicate poll is the authoritative release edge. It
    // can cross the target while an unrelated engine-tick callback is paused.
    const uint64_t peer0_tick = scheduled;
    const uint64_t peer1_tick = scheduled + 1;
    if (!peer0.release_due(generation, false, peer0_tick)
        || !peer1.release_due(generation, false, peer1_tick)
        || !Horse::RollbackScheduledReleaseAllowed(
            generation, generation, true, scheduled, false, false,
            peer0_tick, true, true, true, true, false)
        || !Horse::RollbackScheduledReleaseAllowed(
            generation, generation, true, scheduled, false, false,
            peer1_tick, true, true, true, true, false))
        return 10;
    peer0.mark_released(peer0_tick);
    peer1.mark_released(peer1_tick);
    if (!peer0.release_applied() || !peer1.release_applied()
        || peer0.actual_qpc() != peer0_tick
        || peer1.actual_qpc() != peer1_tick
        || peer1.actual_qpc() - peer0.actual_qpc() > 1
        || peer0.release_due(generation, false, peer0_tick + 1))
        return 11;

    Horse::RollbackScheduledBarrierCoordinator paused_engine_tick;
    paused_engine_tick.reset(generation);
    if (!paused_engine_tick.latch_target("1000")) return 12;
    if (Horse::RollbackScheduledReleaseAllowed(
            generation, generation, true, scheduled, false, false,
            scheduled - 1, true, true, true, true, false))
        return 13;
    const uint64_t first_native_poll_after_target = scheduled + 4;
    if (!Horse::RollbackScheduledReleaseAllowed(
            generation, generation, true, scheduled, false, false,
            first_native_poll_after_target,
            true, true, true, true, false))
        return 14;
    paused_engine_tick.mark_released(first_native_poll_after_target);
    const uint64_t resumed_engine_tick = scheduled + 60;
    if (paused_engine_tick.actual_qpc() != first_native_poll_after_target
        || paused_engine_tick.actual_qpc() == resumed_engine_tick)
        return 15;

    Horse::RollbackScheduledReleaseClaim claim;
    claim.publish(generation, scheduled);
    std::atomic<bool> stale_running {true};
    std::thread stale_poll([&]() {
        while (stale_running.load(std::memory_order_acquire))
        {
            (void)claim.try_release(
                generation, generation, scheduled, false, false,
                scheduled, true, true, true, true);
        }
    });
    for (uint64_t next_generation = generation + 1;
         next_generation < generation + 1000; ++next_generation)
    {
        claim.reset();
        claim.publish(next_generation, scheduled + next_generation);
    }
    stale_running.store(false, std::memory_order_release);
    stale_poll.join();

    constexpr uint64_t final_generation = 9001;
    constexpr uint64_t final_target = 1200;
    claim.reset();
    claim.publish(final_generation, final_target);
    if (claim.try_release(
            generation, generation, scheduled, false, false,
            final_target, true, true, true, true)
        || claim.released() || claim.actual_qpc() != 0
        || claim.generation() != final_generation
        || claim.target_qpc() != final_target)
        return 16;
    if (!claim.try_release(
            final_generation, final_generation, final_target, false, false,
            final_target + 1, true, true, true, true)
        || !claim.released() || claim.actual_qpc() != final_target + 1)
        return 17;
    const Horse::RollbackScheduledReleaseObservation final_observation =
        claim.observe();
    if (final_observation.generation != final_generation
        || final_observation.target_qpc != final_target
        || final_observation.actual_qpc != final_target + 1
        || !final_observation.released)
        return 18;

    Horse::RollbackScheduledBarrierCoordinator malformed;
    malformed.reset(generation);
    if (malformed.latch_target("1000junk")
        || !malformed.marker_invalid()
        || malformed.latch_target("1000"))
        return 19;

    std::cout << "rollback scheduled barrier selftest passed\n";
    return 0;
}
