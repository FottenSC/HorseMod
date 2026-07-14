#include "../HorseMod/horselib/RollbackSnapshotStore.hpp"

#include <cstdio>

namespace
{
    struct TestState
    {
        uint32_t frame {0};
        uint32_t value {0};
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
    const bool roundtrip = saved0.ok && loaded0.ok && loaded
        && loaded->frame == 0 && loaded->value == 0xA0;

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

    store.clear();
    const bool cleared = store.occupied() == 0 && store.saves() == 0;
    const bool ok = roundtrip && same_frame_replaced
        && protected_slot && stale_rejected
        && wrap_ok && confirmation_horizon && cleared;
    std::printf(
        "rollback snapshot-store self-test %s roundtrip=%d same_frame=%d "
        "protected=%d stale=%d wrap=%d horizon=%d cleared=%d\n",
        ok ? "passed" : "failed",
        roundtrip ? 1 : 0,
        same_frame_replaced ? 1 : 0,
        protected_slot ? 1 : 0,
        stale_rejected ? 1 : 0,
        wrap_ok ? 1 : 0,
        confirmation_horizon ? 1 : 0,
        cleared ? 1 : 0);
    return ok ? 0 : 1;
}
