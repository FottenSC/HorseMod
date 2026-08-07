#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <atomic>
#include <string_view>

namespace Horse
{
    struct RollbackScheduledReleaseObservation
    {
        uint64_t generation {0};
        uint64_t target_qpc {0};
        uint64_t actual_qpc {0};
        bool released {false};
    };

    inline bool ParseRollbackScheduledTarget(
        std::string_view text, uint64_t& target) noexcept
    {
        target = 0;
        std::size_t cursor = 0;
        while (cursor < text.size()
            && (text[cursor] == ' ' || text[cursor] == '\t'
                || text[cursor] == '\r' || text[cursor] == '\n'))
        {
            ++cursor;
        }
        if (cursor == text.size()) return false;

        uint64_t value = 0;
        bool digit_seen = false;
        for (; cursor < text.size(); ++cursor)
        {
            const char ch = text[cursor];
            if (ch < '0' || ch > '9') break;
            digit_seen = true;
            const uint64_t digit = static_cast<uint64_t>(ch - '0');
            if (value > (std::numeric_limits<uint64_t>::max() - digit) / 10)
                return false;
            value = value * 10 + digit;
        }
        while (cursor < text.size()
            && (text[cursor] == ' ' || text[cursor] == '\t'
                || text[cursor] == '\r' || text[cursor] == '\n'))
        {
            ++cursor;
        }
        if (!digit_seen || value == 0 || cursor != text.size()) return false;
        target = value;
        return true;
    }

    inline constexpr bool RollbackScheduledReleaseDue(
        uint64_t request_generation,
        uint64_t expected_generation,
        bool target_latched,
        uint64_t target_qpc,
        bool release_applied,
        bool native_battle_started,
        uint64_t now_qpc) noexcept
    {
        return request_generation != 0
            && request_generation == expected_generation
            && target_latched && target_qpc != 0
            && !release_applied && !native_battle_started
            && now_qpc >= target_qpc;
    }

    inline constexpr bool RollbackStockBattleAssetReleaseEligible(
        bool armed,
        bool ready,
        bool completion_withheld,
        bool ready_callback_ok,
        bool released) noexcept
    {
        return armed && ready && completion_withheld
            && ready_callback_ok && !released;
    }

    inline constexpr bool RollbackScheduledReleaseAllowed(
        uint64_t request_generation,
        uint64_t expected_generation,
        bool target_latched,
        uint64_t target_qpc,
        bool release_applied,
        bool native_battle_started,
        uint64_t now_qpc,
        bool armed,
        bool ready,
        bool completion_withheld,
        bool ready_callback_ok,
        bool released) noexcept
    {
        return RollbackScheduledReleaseDue(
                request_generation, expected_generation, target_latched,
                target_qpc, release_applied, native_battle_started, now_qpc)
            && RollbackStockBattleAssetReleaseEligible(
                armed, ready, completion_withheld, ready_callback_ok,
                released);
    }

    class RollbackScheduledBarrierCoordinator
    {
    public:
        void reset(uint64_t request_generation) noexcept
        {
            m_request_generation = request_generation;
            m_target_qpc = 0;
            m_actual_qpc = 0;
            m_target_latched = false;
            m_marker_invalid = false;
            m_release_applied = false;
        }

        bool latch_target(std::string_view text) noexcept
        {
            if (m_target_latched || m_marker_invalid) return false;
            uint64_t target = 0;
            if (!ParseRollbackScheduledTarget(text, target))
            {
                m_marker_invalid = true;
                return false;
            }
            m_target_qpc = target;
            m_target_latched = true;
            return true;
        }

        void reject_marker() noexcept { m_marker_invalid = true; }

        bool release_due(uint64_t expected_generation,
                         bool native_battle_started,
                         uint64_t now_qpc) const noexcept
        {
            return RollbackScheduledReleaseDue(
                m_request_generation, expected_generation,
                m_target_latched, m_target_qpc, m_release_applied,
                native_battle_started, now_qpc);
        }

        void mark_released(uint64_t actual_qpc) noexcept
        {
            m_release_applied = true;
            m_actual_qpc = actual_qpc;
        }

        uint64_t request_generation() const noexcept
        { return m_request_generation; }
        uint64_t target_qpc() const noexcept { return m_target_qpc; }
        uint64_t actual_qpc() const noexcept { return m_actual_qpc; }
        bool target_latched() const noexcept { return m_target_latched; }
        bool marker_invalid() const noexcept { return m_marker_invalid; }
        bool release_applied() const noexcept { return m_release_applied; }

    private:
        uint64_t m_request_generation {0};
        uint64_t m_target_qpc {0};
        uint64_t m_actual_qpc {0};
        bool m_target_latched {false};
        bool m_marker_invalid {false};
        bool m_release_applied {false};
    };

    class RollbackScheduledReleaseClaim
    {
    public:
        void reset() noexcept
        {
            lock();
            m_generation.store(0, std::memory_order_relaxed);
            m_target_qpc.store(0, std::memory_order_relaxed);
            m_actual_qpc.store(0, std::memory_order_relaxed);
            m_released.store(false, std::memory_order_release);
            unlock();
        }

        void publish(uint64_t generation, uint64_t target_qpc) noexcept
        {
            lock();
            m_actual_qpc.store(0, std::memory_order_relaxed);
            m_target_qpc.store(target_qpc, std::memory_order_relaxed);
            m_generation.store(generation, std::memory_order_relaxed);
            m_released.store(false, std::memory_order_release);
            unlock();
        }

        void set_released() noexcept
        {
            lock();
            m_released.store(true, std::memory_order_release);
            unlock();
        }

        bool try_release(
            uint64_t request_generation,
            uint64_t expected_generation,
            uint64_t target_qpc,
            bool release_applied,
            bool native_battle_started,
            uint64_t now_qpc,
            bool armed,
            bool ready,
            bool completion_withheld,
            bool ready_callback_ok) noexcept
        {
            if (m_guard.test_and_set(std::memory_order_acquire)) return false;

            const uint64_t live_generation =
                m_generation.load(std::memory_order_relaxed);
            const uint64_t live_target =
                m_target_qpc.load(std::memory_order_relaxed);
            const bool released =
                m_released.load(std::memory_order_relaxed);
            const bool allowed = live_generation == request_generation
                && live_target == target_qpc
                && RollbackScheduledReleaseAllowed(
                    request_generation, expected_generation,
                    live_generation != 0, live_target,
                    release_applied, native_battle_started, now_qpc,
                    armed, ready, completion_withheld, ready_callback_ok,
                    released);
            if (allowed)
            {
                m_actual_qpc.store(now_qpc, std::memory_order_relaxed);
                m_released.store(true, std::memory_order_release);
            }
            unlock();
            return allowed;
        }

        uint64_t generation() const noexcept
        { return m_generation.load(std::memory_order_acquire); }
        uint64_t target_qpc() const noexcept
        { return m_target_qpc.load(std::memory_order_acquire); }
        uint64_t actual_qpc() const noexcept
        { return m_actual_qpc.load(std::memory_order_acquire); }
        bool released() const noexcept
        { return m_released.load(std::memory_order_acquire); }

        RollbackScheduledReleaseObservation observe() noexcept
        {
            lock();
            RollbackScheduledReleaseObservation observation {
                m_generation.load(std::memory_order_relaxed),
                m_target_qpc.load(std::memory_order_relaxed),
                m_actual_qpc.load(std::memory_order_relaxed),
                m_released.load(std::memory_order_relaxed),
            };
            unlock();
            return observation;
        }

    private:
        void lock() noexcept
        {
            while (m_guard.test_and_set(std::memory_order_acquire)) {}
        }

        void unlock() noexcept
        {
            m_guard.clear(std::memory_order_release);
        }

        std::atomic_flag m_guard = ATOMIC_FLAG_INIT;
        std::atomic<uint64_t> m_generation {0};
        std::atomic<uint64_t> m_target_qpc {0};
        std::atomic<uint64_t> m_actual_qpc {0};
        std::atomic<bool> m_released {false};
    };
}
