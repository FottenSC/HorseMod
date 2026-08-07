#pragma once

#include <cstdint>

namespace Horse
{
    enum class RollbackPeerLivenessResult : uint8_t
    {
        Healthy = 0,
        TransportUnavailable,
        AuthenticatedTrafficTimeout,
    };

    class RollbackPeerLiveness
    {
    public:
        static constexpr uint64_t kAuthenticatedTrafficTimeoutUs =
            5'000'000;
        static constexpr uint32_t kMaximumStalledObservations = 300;

        void reset() noexcept
        {
            m_observed = false;
            m_last_authenticated_packets = 0;
            m_last_progress_us = 0;
            m_stalled_observations = 0;
        }

        RollbackPeerLivenessResult observe(
            bool transport_ready,
            uint64_t authenticated_packets,
            uint64_t now_us) noexcept
        {
            if (!transport_ready)
                return RollbackPeerLivenessResult::TransportUnavailable;

            if (!m_observed
                || authenticated_packets != m_last_authenticated_packets
                || now_us < m_last_progress_us)
            {
                m_observed = true;
                m_last_authenticated_packets = authenticated_packets;
                m_last_progress_us = now_us;
                m_stalled_observations = 0;
                return RollbackPeerLivenessResult::Healthy;
            }

            ++m_stalled_observations;
            if (now_us - m_last_progress_us
                    >= kAuthenticatedTrafficTimeoutUs
                || m_stalled_observations >= kMaximumStalledObservations)
            {
                return RollbackPeerLivenessResult::
                    AuthenticatedTrafficTimeout;
            }
            return RollbackPeerLivenessResult::Healthy;
        }

        uint64_t last_authenticated_packets() const noexcept
        {
            return m_last_authenticated_packets;
        }

        uint64_t last_progress_us() const noexcept
        {
            return m_last_progress_us;
        }

        uint32_t stalled_observations() const noexcept
        {
            return m_stalled_observations;
        }

    private:
        bool m_observed {false};
        uint64_t m_last_authenticated_packets {0};
        uint64_t m_last_progress_us {0};
        uint32_t m_stalled_observations {0};
    };
}
