// ============================================================================
// Horse::RollbackInputHistory
//
// Fixed-size absolute-frame input history for the local rollback lab. This is a
// pure data container for now; it deliberately does not write SC6 input caches.
// ============================================================================

#pragma once

#include "RollbackFrameStamp.hpp"

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
        RollbackFrameStamp frame {};
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

        RollbackInputFrame& write(uint32_t frame) noexcept
        {
            RollbackInputFrame& slot = m_frames[slot_for(frame)];
            if (!slot.frame.valid || slot.frame.value != frame)
            {
                slot = {};
                slot.frame = RollbackFrameStamp::From(frame);
            }
            return slot;
        }

        const RollbackInputFrame* find(uint32_t frame) const noexcept
        {
            const RollbackInputFrame& slot = m_frames[slot_for(frame)];
            return slot.frame.valid && slot.frame.value == frame
                ? &slot : nullptr;
        }

        RollbackInputFrame* find_mutable(uint32_t frame) noexcept
        {
            RollbackInputFrame& slot = m_frames[slot_for(frame)];
            return slot.frame.valid && slot.frame.value == frame
                ? &slot : nullptr;
        }

        template<typename Fn>
        void for_each(Fn&& fn) const noexcept
        {
            for (const RollbackInputFrame& frame : m_frames)
            {
                if (frame.frame.valid)
                    fn(frame);
            }
        }

    private:
        static constexpr size_t slot_for(uint32_t frame) noexcept
        {
            return static_cast<size_t>(frame) & (N - 1);
        }

        std::array<RollbackInputFrame, N> m_frames {};
    };
}
