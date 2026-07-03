// ============================================================================
// Horse::RollbackInputHistory
//
// Fixed-size absolute-frame input history for the local rollback lab. This is a
// pure data container for now; it deliberately does not write SC6 input caches.
// ============================================================================

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Horse
{
    struct RollbackInputPair
    {
        uint64_t p1 {0};
        uint64_t p2 {0};
    };

    struct RollbackInputFrame
    {
        int32_t frame {-1};
        RollbackInputPair input {};
        bool p1_confirmed {false};
        bool p2_confirmed {false};
        bool p1_predicted {false};
        bool p2_predicted {false};
    };

    template<size_t N>
    class RollbackInputHistory
    {
    public:
        static_assert((N & (N - 1)) == 0, "history size must be power of two");

        void clear() noexcept
        {
            for (auto& f : m_frames) f = {};
        }

        RollbackInputFrame& write(int32_t frame) noexcept
        {
            RollbackInputFrame& slot = m_frames[slot_for(frame)];
            if (slot.frame != frame)
            {
                slot = {};
                slot.frame = frame;
            }
            return slot;
        }

        const RollbackInputFrame* find(int32_t frame) const noexcept
        {
            const RollbackInputFrame& slot = m_frames[slot_for(frame)];
            return slot.frame == frame ? &slot : nullptr;
        }

        RollbackInputFrame* find_mutable(int32_t frame) noexcept
        {
            RollbackInputFrame& slot = m_frames[slot_for(frame)];
            return slot.frame == frame ? &slot : nullptr;
        }

        template<typename Fn>
        void for_each(Fn&& fn) const noexcept
        {
            for (const RollbackInputFrame& frame : m_frames)
            {
                if (frame.frame >= 0)
                    fn(frame);
            }
        }

    private:
        static constexpr size_t slot_for(int32_t frame) noexcept
        {
            return static_cast<size_t>(frame) & (N - 1);
        }

        std::array<RollbackInputFrame, N> m_frames {};
    };
}
