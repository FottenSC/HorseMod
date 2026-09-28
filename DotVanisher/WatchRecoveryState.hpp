#pragma once

#include <cstdint>

namespace DotVanisher
{
    // Value-only state. Native pointers are identities, never retained references.
    // The adapter serializes access and only operates on a currently executing owner.
    struct WatchAttemptIdentity
    {
        uintptr_t owner{};
        uint64_t session{};
        uintptr_t request_list{};
        uint32_t thread{};
        uint8_t role{}, state{}, substate{};

        bool operator==(const WatchAttemptIdentity&) const = default;
    };

    struct WatchAssignment
    {
        uint16_t peer_id{};
        uintptr_t peer{};
        uint64_t route_tag{};
        bool operator==(const WatchAssignment&) const = default;
    };

    class WatchRecoveryState
    {
    public:
        static constexpr uint64_t maximum_wait_ms = 20'000;
        enum class Hold { admitted, duplicate, rejected };

        void clear() noexcept
        {
            ++m_generation;
            m_active = m_pending = m_acknowledged = m_force_native = false;
            m_identity = {};
            m_assignment = {};
        }

        // Admission is deliberately limited to a single request on an empty queue.
        void begin(WatchAttemptIdentity identity, uint64_t now, uint64_t budget) noexcept
        {
            clear();
            m_identity = identity;
            m_started = now;
            m_budget = budget < maximum_wait_ms ? budget : maximum_wait_ms;
            m_active = budget != 0;
        }

        bool matches(const WatchAttemptIdentity& identity) const noexcept
        {
            return m_active && m_identity == identity;
        }

        bool expired(uint64_t now) const noexcept
        {
            // A regressed clock also terminates admission rather than granting more time.
            return now < m_started || now - m_started >= m_budget;
        }

        Hold hold(WatchAssignment assignment, uint64_t now) noexcept
        {
            if (!m_active || expired(now)) return Hold::rejected;
            if (m_pending)
                return m_assignment == assignment ? Hold::duplicate : Hold::rejected;
            m_assignment = assignment;
            m_pending = true;
            return Hold::admitted;
        }

        void acknowledge(uint32_t status) noexcept
        {
            if (status != 0) clear();
            else if (m_active) m_acknowledged = true;
        }

        bool active() const noexcept { return m_active; }
        bool pending() const noexcept { return m_active && m_pending; }
        bool acknowledged() const noexcept { return m_acknowledged; }
        void require_native() noexcept { m_force_native = true; }
        bool native_required() const noexcept { return m_force_native; }
        uint64_t generation() const noexcept { return m_generation; }
        const WatchAttemptIdentity& identity() const noexcept { return m_identity; }
        WatchAssignment assignment() const noexcept { return m_assignment; }

        // Consume before invoking native code: reentrant cancellation or another
        // assignment cannot complete this retained assignment twice.
        WatchAssignment consume() noexcept
        {
            const auto result = m_assignment;
            clear();
            return result;
        }

    private:
        WatchAttemptIdentity m_identity{};
        WatchAssignment m_assignment{};
        uint64_t m_generation{}, m_started{}, m_budget{};
        bool m_active{}, m_pending{}, m_acknowledged{}, m_force_native{};
    };
}
