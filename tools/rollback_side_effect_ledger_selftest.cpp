#include "../HorseMod/horselib/RollbackSideEffectLedger.hpp"

#include <cstdio>

namespace
{
    void commit(const Horse::RollbackSideEffectEvent&, void* context) noexcept
    {
        ++*static_cast<uint32_t*>(context);
    }
}

int main()
{
    Horse::RollbackSideEffectLedger<8, 16> ledger {};
    const uint32_t payload = 0x1234;
    const bool q0 = ledger.enqueue(
        9, 10, Horse::RollbackSideEffectType::Audio, 1,
        &payload, sizeof(payload));
    const bool duplicate = ledger.enqueue(
        9, 10, Horse::RollbackSideEffectType::Audio, 1,
        &payload, sizeof(payload));
    const bool q1 = ledger.enqueue(
        9, 11, Horse::RollbackSideEffectType::Vfx, 2,
        &payload, sizeof(payload));
    ledger.rollback_from(9, 11);
    const bool discarded = ledger.pending() == 1;

    uint32_t commits = 0;
    const bool confirmed = ledger.confirm_through(9, 10, &commit, &commits);
    const bool once = confirmed && commits == 1 && ledger.pending() == 0;
    const bool replay_duplicate = ledger.enqueue(
        9, 10, Horse::RollbackSideEffectType::Audio, 1,
        &payload, sizeof(payload));
    (void)ledger.confirm_through(9, 10, &commit, &commits);
    const bool still_once = replay_duplicate && commits == 1;

    Horse::RollbackSideEffectLedger<8, 16> load_ledger {};
    const bool load_q0 = load_ledger.enqueue(
        10, 20, Horse::RollbackSideEffectType::Audio, 3,
        &payload, sizeof(payload));
    const bool load_q1 = load_ledger.enqueue(
        10, 21, Horse::RollbackSideEffectType::Vfx, 4,
        &payload, sizeof(payload));
    load_ledger.rollback_after(10, 20);
    uint32_t load_commits = 0;
    const bool loaded_frame_kept = load_q0 && load_q1
        && load_ledger.pending() == 1
        && load_ledger.confirm_through(
            10, 20, &commit, &load_commits)
        && load_commits == 1;

    bool scope_ok = false;
    {
        Horse::RollbackResimScope scope(9, 12);
        const auto context = Horse::CurrentRollbackResimContext();
        scope_ok = context.active && context.epoch == 9 && context.frame == 12;
    }
    scope_ok = scope_ok && !Horse::CurrentRollbackResimContext().active;

    const bool ok = q0 && duplicate && q1 && discarded && once
        && still_once && loaded_frame_kept && scope_ok
        && ledger.report().duplicates >= 2;
    std::printf(
        "rollback side-effect ledger self-test %s discarded=%d once=%d "
        "loaded_frame=%d scope=%d duplicates=%llu\n",
        ok ? "passed" : "failed", discarded ? 1 : 0, once ? 1 : 0,
        loaded_frame_kept ? 1 : 0, scope_ok ? 1 : 0,
        static_cast<unsigned long long>(ledger.report().duplicates));
    return ok ? 0 : 1;
}
