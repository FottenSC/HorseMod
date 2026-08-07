#include "RollbackMotionPoseResidue.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

int main()
{
    using namespace Horse;

    std::array<uint8_t,
        kRollbackMotionPoseExtraBoneFlagsOffset
            + kRollbackMotionPoseExtraBoneFlagCount> pose_storage {};
    for (size_t i = 0; i < pose_storage.size(); ++i)
        pose_storage[i] = static_cast<uint8_t>((i * 17u + 3u) & 0xffu);
    auto* const pose = pose_storage.data();

    std::array<uint8_t,
        kRollbackMotionPoseTransformCount
            * kRollbackMotionPoseTransformBytes> base {};
    for (size_t i = 0; i < base.size(); ++i)
        base[i] = static_cast<uint8_t>((i * 7u + 11u) & 0xffu);

    RollbackMotionPoseResidue residue;
    const auto bootstrap = residue.before_sample(
        0, pose, base.data());
    assert(bootstrap.seeded && bootstrap.bootstrapped);
    assert(!bootstrap.restored && !bootstrap.carried);
    for (size_t i = 0; i < kRollbackMotionPoseBaseTransformCount; ++i)
    {
        const auto* transform = pose
            + i * kRollbackMotionPoseTransformBytes;
        const auto* source = base.data()
            + i * kRollbackMotionPoseTransformBytes;
        for (size_t j = 0;
             j < kRollbackMotionPoseRetainedBytesPerTransform; ++j)
            assert(transform[j] == source[j]);
    }
    for (size_t i = 0; i < kRollbackMotionPoseExtraBoneFlagCount; ++i)
        assert(pose[kRollbackMotionPoseExtraBoneFlagsOffset + i] == 1);

    assert(residue.capture_after_solve(1, pose));
    const auto saved = residue.capture();
    assert(ValidateRollbackMotionPoseResidueSnapshot(saved));
    assert(saved.valid == 1);
    assert(saved.last_player == 1);

    for (size_t i = 0; i < kRollbackMotionPoseTransformCount; ++i)
    {
        auto* transform = pose
            + i * kRollbackMotionPoseTransformBytes;
        for (size_t j = 0;
             j < kRollbackMotionPoseRetainedBytesPerTransform; ++j)
            transform[j] ^= 0x5a;
        for (size_t j = kRollbackMotionPoseRetainedBytesPerTransform;
             j < kRollbackMotionPoseTransformBytes; ++j)
            transform[j] = 0x7f;
    }
    for (size_t i = 0; i < kRollbackMotionPoseExtraBoneFlagCount; ++i)
        pose[kRollbackMotionPoseExtraBoneFlagsOffset + i] =
            static_cast<uint8_t>(i & 1u);

    assert(residue.restore(saved));
    const auto report = residue.before_sample(
        0, pose, base.data());
    assert(report.seeded && report.restored);
    assert(!report.bootstrapped && !report.carried);
    assert(report.changed && report.verified);
    assert(!residue.pending());
    for (size_t i = 0; i < kRollbackMotionPoseTransformCount; ++i)
    {
        const auto* transform = pose
            + i * kRollbackMotionPoseTransformBytes;
        for (size_t j = 0;
             j < kRollbackMotionPoseRetainedBytesPerTransform; ++j)
        {
            assert(transform[j] == saved.retained[
                i * kRollbackMotionPoseRetainedBytesPerTransform + j]);
        }
        for (size_t j = kRollbackMotionPoseRetainedBytesPerTransform;
             j < kRollbackMotionPoseTransformBytes; ++j)
            assert(transform[j] == 0x7f);
    }
    for (size_t i = 0; i < kRollbackMotionPoseExtraBoneFlagCount; ++i)
    {
        assert(pose[kRollbackMotionPoseExtraBoneFlagsOffset + i]
            == saved.extra_bone_cache_reuse[i]);
    }

    const auto carry = residue.before_sample(0, pose, base.data());
    assert(carry.seeded && carry.carried && carry.verified);
    assert(!carry.restored && !carry.bootstrapped);

    // Lux mutates sampledPoseScratch after the final sampler returns. The
    // transactional carry must therefore be captured at SolveBonePose exit,
    // not at sampler exit.
    pose[0x11] ^= 0x33;
    pose[kRollbackMotionPoseExtraBoneFlagsOffset + 2] ^= 1;
    assert(residue.capture_after_solve(0, pose));
    const auto solve_exit = residue.capture();
    assert(solve_exit.last_player == 0);
    assert(solve_exit.retained[0x11] == pose[0x11]);
    assert(solve_exit.retained[0x11] != saved.retained[0x11]);
    assert(solve_exit.extra_bone_cache_reuse[2]
        == pose[kRollbackMotionPoseExtraBoneFlagsOffset + 2]);

    auto changed = saved;
    changed.retained[17] ^= 1;
    assert(HashRollbackMotionPoseResidueRollbackState(changed)
           != HashRollbackMotionPoseResidueRollbackState(saved));
    auto invalid = saved;
    invalid.last_player = 2;
    assert(!residue.restore(invalid));
    assert(!residue.pending());
    return 0;
}
