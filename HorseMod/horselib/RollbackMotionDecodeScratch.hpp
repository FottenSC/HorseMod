#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Horse
{
    static constexpr size_t kRollbackMotionDecodedWordBufferBytes = 0x250;
    static constexpr size_t kRollbackMotionDecodeScratchPairBytes =
        2 * kRollbackMotionDecodedWordBufferBytes;
    struct RollbackMotionDecodeScratchSnapshot
    {
        // Exact bytes are sufficient in-process: the next native decoder
        // overwrites its stream-defined prefix and consumes the retained tail
        // directly. A separate prefix count is needed only by an offline
        // decoder that reconstructs that overwrite itself.
        std::array<uint8_t,
            kRollbackMotionDecodeScratchPairBytes> pair {};
        uint8_t valid {0};
        int8_t last_player {-1};
    };

    static_assert(sizeof(RollbackMotionDecodeScratchSnapshot)
                  == kRollbackMotionDecodeScratchPairBytes + 2);

    static inline bool ValidateRollbackMotionDecodeScratchSnapshot(
        const RollbackMotionDecodeScratchSnapshot& snapshot) noexcept
    {
        return snapshot.valid == 0
            ? snapshot.last_player == -1
            : snapshot.valid == 1
                && snapshot.last_player >= 0
                && snapshot.last_player < 2;
    }

    static inline uint64_t HashRollbackMotionDecodeScratchSnapshot(
        const RollbackMotionDecodeScratchSnapshot& snapshot) noexcept
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
        add(snapshot.pair.data(), snapshot.pair.size());
        add(&snapshot.valid, sizeof(snapshot.valid));
        add(&snapshot.last_player, sizeof(snapshot.last_player));
        return hash ? hash : 1;
    }

    struct RollbackMotionDecodeScratchSeedReport
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

    class RollbackMotionDecodeScratch
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

        RollbackMotionDecodeScratchSnapshot capture() const noexcept
        {
            return m_state;
        }

        bool restore(
            const RollbackMotionDecodeScratchSnapshot& snapshot) noexcept
        {
            if (!ValidateRollbackMotionDecodeScratchSnapshot(snapshot))
            {
                m_pending = false;
                return false;
            }
            // The tracker is part of the saved deterministic state. The
            // native bytes themselves live in SampleKeyframeTransforms'
            // reused stack frame, so arm one exact seed for the first sample
            // after Load while making immediate restore verification observe
            // the saved tracker state rather than the future frame's tail.
            m_state = snapshot;
            m_restore = snapshot;
            m_pending = snapshot.valid != 0;
            return true;
        }

        void cancel_pending_restore() noexcept
        {
            m_pending = false;
        }

        RollbackMotionDecodeScratchSeedReport before_sample(
            int player,
            void* scratch_pair) noexcept
        {
            RollbackMotionDecodeScratchSeedReport report {};
            if (!valid_player(player) || !scratch_pair)
            {
                return report;
            }

            RollbackMotionDecodeScratchSnapshot expected {};
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
                // SolveBonePose does not initialize its caller-owned pair,
                // and shorter decoded streams can expose the retained tail.
                // Bootstrap that otherwise undefined first tail to zero on
                // every process, then carry the exact native result between
                // all later P1/P2 samples.
                expected.valid = 1;
                expected.last_player = static_cast<int8_t>(player);
                report.bootstrapped = true;
            }
            uint8_t* live = static_cast<uint8_t*>(scratch_pair);
            report.expected_hash = hash_bytes(
                expected.pair.data(), expected.pair.size());
            report.before_hash = hash_bytes(live, expected.pair.size());
            report.changed =
                std::memcmp(live, expected.pair.data(),
                    expected.pair.size()) != 0;
            std::memcpy(live, expected.pair.data(), expected.pair.size());
            report.after_hash = hash_bytes(live, expected.pair.size());
            report.verified =
                std::memcmp(live, expected.pair.data(),
                    expected.pair.size()) == 0;
            report.seeded = true;
            m_pending = false;
            ++m_seed_count;
            return report;
        }

        bool after_sample(int player, const void* scratch_pair) noexcept
        {
            if (!valid_player(player) || !scratch_pair)
                return false;
            const size_t index = static_cast<size_t>(player);
            std::memcpy(
                m_state.pair.data(),
                scratch_pair,
                m_state.pair.size());
            m_state.valid = 1;
            m_state.last_player = static_cast<int8_t>(player);
            ++m_observe_count[index];
            return true;
        }

        bool pending() const noexcept
        {
            return m_pending;
        }

        uint64_t seed_count() const noexcept
        {
            return m_seed_count;
        }

        uint64_t observe_count(int player) const noexcept
        {
            return valid_player(player)
                ? m_observe_count[static_cast<size_t>(player)] : 0;
        }

    private:
        static bool valid_player(int player) noexcept
        {
            return player >= 0
                && static_cast<size_t>(player) < 2;
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

        RollbackMotionDecodeScratchSnapshot m_state {};
        RollbackMotionDecodeScratchSnapshot m_restore {};
        bool m_pending {false};
        uint64_t m_seed_count {0};
        std::array<uint64_t, 2> m_observe_count {};
    };

    inline RollbackMotionDecodeScratch&
    rollback_motion_decode_scratch() noexcept
    {
        static RollbackMotionDecodeScratch state;
        return state;
    }
}
