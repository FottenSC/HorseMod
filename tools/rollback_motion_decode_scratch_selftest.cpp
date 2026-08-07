#include "RollbackMotionDecodeScratch.hpp"

#include <array>
#include <cassert>
#include <cstdint>

int main()
{
    using namespace Horse;

    RollbackMotionDecodeScratch scratch;
    std::array<uint8_t, kRollbackMotionDecodeScratchPairBytes> p1 {};
    std::array<uint8_t, kRollbackMotionDecodeScratchPairBytes> p2 {};
    for (size_t i = 0; i < p1.size(); ++i)
    {
        p1[i] = static_cast<uint8_t>((i * 3u + 7u) & 0xffu);
        p2[i] = static_cast<uint8_t>((i * 5u + 11u) & 0xffu);
    }

    // The native SolveBonePose stack is reused at one call depth. P2 starts
    // with the pair left by P1, and the next frame's P1 starts with the pair
    // left by P2. This is one sequential native state, not per-player state.
    const auto bootstrap = scratch.before_sample(0, p1.data());
    assert(bootstrap.seeded);
    assert(bootstrap.bootstrapped);
    assert(bootstrap.changed);
    assert(bootstrap.verified);
    assert(p1 == decltype(p1) {});
    for (size_t i = 0; i < p1.size(); ++i)
        p1[i] = static_cast<uint8_t>((i * 3u + 7u) & 0xffu);
    assert(scratch.after_sample(0, p1.data()));
    assert(scratch.after_sample(1, p2.data()));
    assert(scratch.observe_count(0) == 1);
    assert(scratch.observe_count(1) == 1);

    const RollbackMotionDecodeScratchSnapshot saved = scratch.capture();
    assert(saved.valid == 1);
    assert(saved.last_player == 1);
    assert(saved.pair == p2);
    assert(ValidateRollbackMotionDecodeScratchSnapshot(saved));
    const uint64_t saved_hash =
        HashRollbackMotionDecodeScratchSnapshot(saved);

    p1.fill(0xA5);
    p2.fill(0x5A);
    assert(scratch.after_sample(0, p1.data()));
    assert(scratch.after_sample(1, p2.data()));
    const RollbackMotionDecodeScratchSnapshot future = scratch.capture();
    assert(future.pair != saved.pair);

    assert(scratch.restore(saved));
    assert(scratch.pending());
    assert(scratch.capture().pair == saved.pair);
    assert(HashRollbackMotionDecodeScratchSnapshot(scratch.capture())
        == saved_hash);
    scratch.cancel_pending_restore();
    assert(!scratch.pending());
    assert(scratch.capture().pair == saved.pair);
    const auto cancelled_seed = scratch.before_sample(0, p1.data());
    assert(cancelled_seed.seeded);
    assert(cancelled_seed.carried);
    assert(p1 == saved.pair);

    assert(scratch.restore(saved));
    assert(scratch.pending());
    p1.fill(0xa5);

    // The first sample after restore is P1, but the correct incoming stack
    // state is the P2 tail captured at the end of the saved frame.
    const auto p1_seed = scratch.before_sample(0, p1.data());
    assert(p1_seed.seeded);
    assert(p1_seed.restored);
    assert(p1_seed.changed);
    assert(p1_seed.verified);
    assert(p1_seed.expected_hash == p1_seed.after_hash);
    assert(p1 == saved.pair);
    assert(!scratch.pending());
    assert(scratch.seed_count() == 3);

    // Later samples and the P2 solve explicitly materialize the tracked
    // carry, so unrelated stack reuse cannot alter retained decoder words.
    p1[0] ^= 0xffu;
    const auto duplicate_seed = scratch.before_sample(0, p1.data());
    assert(duplicate_seed.seeded);
    assert(duplicate_seed.carried);
    assert(p1 == saved.pair);
    p1[0] ^= 0xffu;
    assert(scratch.after_sample(0, p1.data()));

    p2.fill(0x5a);
    const auto p2_seed = scratch.before_sample(1, p2.data());
    assert(p2_seed.seeded);
    assert(p2_seed.carried);
    assert(p2 == p1);
    assert(scratch.after_sample(1, p2.data()));
    assert(scratch.seed_count() == 5);

    RollbackMotionDecodeScratchSnapshot invalid {};
    assert(scratch.restore(invalid));
    assert(!scratch.pending());
    const auto invalid_bootstrap = scratch.before_sample(0, p1.data());
    assert(invalid_bootstrap.seeded);
    assert(invalid_bootstrap.bootstrapped);
    assert(p1 == decltype(p1) {});
    assert(!scratch.before_sample(-1, p1.data()).seeded);
    assert(!scratch.before_sample(2, p1.data()).seeded);
    assert(!scratch.after_sample(-1, p1.data()));
    assert(!scratch.after_sample(2, p1.data()));

    // The saved decoder tail is deterministic state: mutating it must be
    // visible even though it is only materialized into the native stack on
    // the next sample.
    RollbackMotionDecodeScratchSnapshot corrupted = saved;
    corrupted.pair[0x24] ^= 0x01u;
    assert(HashRollbackMotionDecodeScratchSnapshot(corrupted) != saved_hash);
    corrupted = saved;
    corrupted.last_player = 2;
    assert(!ValidateRollbackMotionDecodeScratchSnapshot(corrupted));
    assert(!scratch.restore(corrupted));
    assert(!scratch.pending());
    return 0;
}
