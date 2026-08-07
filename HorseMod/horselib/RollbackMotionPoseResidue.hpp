#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Horse
{
    // LuxBattleChara_SolveBonePose owns 62 temporary FTransform48 records.
    // Native code initializes only each record's 0x20..0x2f scale lanes at
    // entry. Rotation and translation (0x00..0x1f) retain the prior stack
    // invocation and are observable when a clip does not author every lane.
    static constexpr size_t kRollbackMotionPoseTransformCount = 62;
    static constexpr size_t kRollbackMotionPoseBaseTransformCount = 32;
    static constexpr size_t kRollbackMotionPoseTransformBytes = 0x30;
    static constexpr size_t kRollbackMotionPoseRetainedBytesPerTransform =
        0x20;
    static constexpr size_t kRollbackMotionPoseResidueBytes =
        kRollbackMotionPoseTransformCount
        * kRollbackMotionPoseRetainedBytesPerTransform;
    static constexpr size_t kRollbackMotionPoseExtraBoneFlagCount = 9;
    // sampledPoseScratch begins at RBP+0xDF0. The final unrolled cache
    // exchange reads flags for bones 23..31 at RBP+0x3721..0x3729.
    static constexpr size_t kRollbackMotionPoseExtraBoneFlagsOffset = 0x2931;

    struct RollbackMotionPoseResidueSnapshot
    {
        std::array<uint8_t, kRollbackMotionPoseResidueBytes> retained {};
        std::array<uint8_t, kRollbackMotionPoseExtraBoneFlagCount>
            extra_bone_cache_reuse {};
        uint8_t valid {0};
        int8_t last_player {-1};
    };

    static_assert(sizeof(RollbackMotionPoseResidueSnapshot)
                  == kRollbackMotionPoseResidueBytes
                    + kRollbackMotionPoseExtraBoneFlagCount + 2);

    static inline bool ValidateRollbackMotionPoseResidueSnapshot(
        const RollbackMotionPoseResidueSnapshot& snapshot) noexcept
    {
        return snapshot.valid == 0
            ? snapshot.last_player == -1
            : snapshot.valid == 1
                && snapshot.last_player >= 0
                && snapshot.last_player < 2;
    }

    static inline uint64_t HashRollbackMotionPoseResidueSnapshot(
        const RollbackMotionPoseResidueSnapshot& snapshot) noexcept
    {
        // Replay-input sidecars predate the recovered extra-bone decision
        // bytes and persist only retained + valid + last_player. Keep this
        // hash wire-compatible with those fixed-layout artifacts.
        uint64_t hash = 1469598103934665603ull;
        const auto add = [&hash](const void* data, size_t size) noexcept {
            const auto* bytes = static_cast<const uint8_t*>(data);
            for (size_t i = 0; i < size; ++i)
            {
                hash ^= bytes[i];
                hash *= 1099511628211ull;
            }
        };
        add(snapshot.retained.data(), snapshot.retained.size());
        add(&snapshot.valid, sizeof(snapshot.valid));
        add(&snapshot.last_player, sizeof(snapshot.last_player));
        return hash ? hash : 1;
    }

    static inline uint64_t HashRollbackMotionPoseResidueRollbackState(
        const RollbackMotionPoseResidueSnapshot& snapshot) noexcept
    {
        uint64_t hash = 1469598103934665603ull;
        const auto add = [&hash](const void* data, size_t size) noexcept {
            const auto* bytes = static_cast<const uint8_t*>(data);
            for (size_t i = 0; i < size; ++i)
            {
                hash ^= bytes[i];
                hash *= 1099511628211ull;
            }
        };
        add(snapshot.retained.data(), snapshot.retained.size());
        add(snapshot.extra_bone_cache_reuse.data(),
            snapshot.extra_bone_cache_reuse.size());
        add(&snapshot.valid, sizeof(snapshot.valid));
        add(&snapshot.last_player, sizeof(snapshot.last_player));
        return hash ? hash : 1;
    }

    struct RollbackMotionPoseResidueSeedReport
    {
        bool seeded {false};
        bool restored {false};
        bool bootstrapped {false};
        bool carried {false};
        bool changed {false};
        bool verified {false};
        uint64_t expected_hash {0};
        uint64_t before_hash {0};
        uint64_t after_hash {0};
    };

    class RollbackMotionPoseResidue
    {
    public:
        void reset() noexcept
        {
            m_state = {};
            m_restore = {};
            m_pending = false;
            m_seed_count = 0;
            m_observe_count = {};
        }

        RollbackMotionPoseResidueSnapshot capture() const noexcept
        {
            return m_state;
        }

        bool restore(
            const RollbackMotionPoseResidueSnapshot& snapshot) noexcept
        {
            if (!ValidateRollbackMotionPoseResidueSnapshot(snapshot))
            {
                m_pending = false;
                return false;
            }
            m_state = snapshot;
            m_restore = snapshot;
            m_pending = snapshot.valid != 0;
            return true;
        }

        void cancel_pending_restore() noexcept
        {
            m_pending = false;
        }

        RollbackMotionPoseResidueSeedReport before_sample(
            int player,
            void* pose,
            const void* base_pose) noexcept
        {
            RollbackMotionPoseResidueSeedReport report {};
            if (!valid_player(player) || !pose)
            {
                return report;
            }

            RollbackMotionPoseResidueSnapshot expected {};
            if (m_pending && m_restore.valid != 0)
            {
                expected = m_restore;
                report.restored = true;
            }
            else if (m_state.valid != 0)
            {
                expected = m_state;
                report.carried = true;
            }
            else
            {
                if (!base_pose) return report;
                make_deterministic_bootstrap(player, base_pose, expected);
                report.bootstrapped = true;
            }

            std::array<uint8_t, kRollbackMotionPoseResidueBytes> before {};
            copy_from_pose(pose, before);
            std::array<uint8_t, kRollbackMotionPoseExtraBoneFlagCount>
                before_flags {};
            copy_flags_from_pose(pose, before_flags);
            report.expected_hash = hash_payload(
                expected.retained, expected.extra_bone_cache_reuse);
            report.before_hash = hash_payload(before, before_flags);
            report.changed = before != expected.retained
                || before_flags != expected.extra_bone_cache_reuse;
            copy_to_pose(expected.retained,
                expected.extra_bone_cache_reuse, pose);

            std::array<uint8_t, kRollbackMotionPoseResidueBytes> after {};
            copy_from_pose(pose, after);
            std::array<uint8_t, kRollbackMotionPoseExtraBoneFlagCount>
                after_flags {};
            copy_flags_from_pose(pose, after_flags);
            report.after_hash = hash_payload(after, after_flags);
            report.verified = after == expected.retained
                && after_flags == expected.extra_bone_cache_reuse;
            report.seeded = true;
            m_pending = false;
            ++m_seed_count;
            return report;
        }

        bool capture_after_solve(int player, const void* pose) noexcept
        {
            if (!valid_player(player) || !pose) return false;
            copy_from_pose(pose, m_state.retained);
            copy_flags_from_pose(
                pose, m_state.extra_bone_cache_reuse);
            m_state.valid = 1;
            m_state.last_player = static_cast<int8_t>(player);
            ++m_observe_count[static_cast<size_t>(player)];
            return true;
        }

        bool pending() const noexcept { return m_pending; }
        uint64_t seed_count() const noexcept { return m_seed_count; }
        uint64_t observe_count(int player) const noexcept
        {
            return valid_player(player)
                ? m_observe_count[static_cast<size_t>(player)] : 0;
        }

    private:
        static bool valid_player(int player) noexcept
        {
            return player >= 0 && player < 2;
        }

        static void copy_from_pose(
            const void* pose,
            std::array<uint8_t, kRollbackMotionPoseResidueBytes>& out) noexcept
        {
            const auto* source = static_cast<const uint8_t*>(pose);
            for (size_t i = 0; i < kRollbackMotionPoseTransformCount; ++i)
            {
                std::memcpy(
                    out.data()
                        + i * kRollbackMotionPoseRetainedBytesPerTransform,
                    source + i * kRollbackMotionPoseTransformBytes,
                    kRollbackMotionPoseRetainedBytesPerTransform);
            }
        }

        static void copy_to_pose(
            const std::array<uint8_t, kRollbackMotionPoseResidueBytes>& in,
            const std::array<uint8_t,
                kRollbackMotionPoseExtraBoneFlagCount>& flags,
            void* pose) noexcept
        {
            auto* destination = static_cast<uint8_t*>(pose);
            for (size_t i = 0; i < kRollbackMotionPoseTransformCount; ++i)
            {
                std::memcpy(
                    destination + i * kRollbackMotionPoseTransformBytes,
                    in.data()
                        + i * kRollbackMotionPoseRetainedBytesPerTransform,
                    kRollbackMotionPoseRetainedBytesPerTransform);
            }
            std::memcpy(
                destination + kRollbackMotionPoseExtraBoneFlagsOffset,
                flags.data(), flags.size());
        }

        static void copy_flags_from_pose(
            const void* pose,
            std::array<uint8_t,
                kRollbackMotionPoseExtraBoneFlagCount>& out) noexcept
        {
            const auto* source = static_cast<const uint8_t*>(pose);
            std::memcpy(out.data(),
                source + kRollbackMotionPoseExtraBoneFlagsOffset,
                out.size());
        }

        static uint64_t hash_bytes(
            const uint8_t* bytes,
            size_t size) noexcept
        {
            uint64_t hash = 1469598103934665603ull;
            for (size_t i = 0; i < size; ++i)
            {
                hash ^= bytes[i];
                hash *= 1099511628211ull;
            }
            return hash;
        }

        static uint64_t hash_payload(
            const std::array<uint8_t,
                kRollbackMotionPoseResidueBytes>& retained,
            const std::array<uint8_t,
                kRollbackMotionPoseExtraBoneFlagCount>& flags) noexcept
        {
            uint64_t hash = hash_bytes(retained.data(), retained.size());
            for (const uint8_t value : flags)
            {
                hash ^= value;
                hash *= 1099511628211ull;
            }
            return hash;
        }

        static void make_deterministic_bootstrap(
            int player,
            const void* base_pose,
            RollbackMotionPoseResidueSnapshot& out) noexcept
        {
            out = {};
            // Identity rotation plus zero translation is the safe semantic
            // default for transforms outside the verified 32-entry base.
            constexpr uint32_t one_bits = 0x3f800000u;
            for (size_t i = 0; i < kRollbackMotionPoseTransformCount; ++i)
            {
                std::memcpy(
                    out.retained.data()
                        + i * kRollbackMotionPoseRetainedBytesPerTransform
                        + 0x0c,
                    &one_bits, sizeof(one_bits));
            }

            const auto* base = static_cast<const uint8_t*>(base_pose);
            for (size_t i = 0;
                 i < kRollbackMotionPoseBaseTransformCount; ++i)
            {
                std::memcpy(
                    out.retained.data()
                        + i * kRollbackMotionPoseRetainedBytesPerTransform,
                    base + i * kRollbackMotionPoseTransformBytes,
                    kRollbackMotionPoseRetainedBytesPerTransform);
            }
            // With no authored extra-bone source, native's intended branch
            // retains the initialized persistent matrix cache. Conditional
            // selector processing overwrites each byte when it has evidence
            // to refresh instead.
            out.extra_bone_cache_reuse.fill(1);
            out.valid = 1;
            out.last_player = static_cast<int8_t>(player);
        }

        RollbackMotionPoseResidueSnapshot m_state {};
        RollbackMotionPoseResidueSnapshot m_restore {};
        bool m_pending {false};
        uint64_t m_seed_count {0};
        std::array<uint64_t, 2> m_observe_count {};
    };

    inline RollbackMotionPoseResidue& rollback_motion_pose_residue() noexcept
    {
        static RollbackMotionPoseResidue state;
        return state;
    }
}
