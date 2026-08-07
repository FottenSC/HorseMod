#pragma once

#include <atomic>
#include <cstdint>
#include <thread>

namespace Horse
{
    // Admission gate for process-lifetime native detours. Closing the gate
    // never removes a hook: new callbacks run their immutable trampoline,
    // while callers that already entered are allowed to finish.
    class RollbackInstallOnceCallbackGate
    {
    public:
        bool enter(bool& admitted) noexcept
        {
            ++s_current_thread_depth;
            admitted = false;
            uint64_t state = m_state.load(std::memory_order_acquire);
            while ((state & kAcceptingBit) != 0)
            {
                if ((state & kInflightMask) == kInflightMask)
                    return true;
                if (m_state.compare_exchange_weak(
                        state, state + 1,
                        std::memory_order_acq_rel,
                        std::memory_order_acquire))
                {
                    admitted = true;
                    break;
                }
            }
            return true;
        }

        void leave(bool admitted) noexcept
        {
            if (admitted)
                m_state.fetch_sub(1, std::memory_order_acq_rel);
            if (s_current_thread_depth != 0)
                --s_current_thread_depth;
        }

        void open() noexcept
        {
            m_state.fetch_or(kAcceptingBit, std::memory_order_release);
        }

        void close_and_drain() noexcept
        {
            m_state.fetch_and(kInflightMask, std::memory_order_acq_rel);
            while ((m_state.load(std::memory_order_acquire)
                    & kInflightMask) != 0)
                std::this_thread::yield();
        }

        bool accepting() const noexcept
        {
            return (m_state.load(std::memory_order_acquire)
                & kAcceptingBit) != 0;
        }

        uint32_t inflight() const noexcept
        {
            return static_cast<uint32_t>(
                m_state.load(std::memory_order_acquire) & kInflightMask);
        }

        static bool current_thread_inside_callback() noexcept
        {
            return s_current_thread_depth != 0;
        }

    private:
        static constexpr uint64_t kAcceptingBit = 1ull << 63;
        static constexpr uint64_t kInflightMask = kAcceptingBit - 1;
        std::atomic<uint64_t> m_state {0};
        inline static thread_local uint32_t s_current_thread_depth {0};
    };
}
