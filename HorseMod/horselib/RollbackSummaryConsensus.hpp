// ============================================================================
// Horse::RollbackSummaryConsensus
//
// Bounded application-level confirmation window. A frame becomes consumable
// only after this client matched the peer summary and received the peer's
// echoed ACK proving the peer matched this client's summary. Consumption is
// strictly contiguous; later matches cannot skip a missing predecessor.
// ============================================================================

#pragma once

#include "RollbackFrameStamp.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace Horse
{
    template <size_t N = 128>
    class RollbackSummaryMatchAckWindow
    {
    public:
        static_assert(N == 128,
            "summary ACK wire bitmap currently covers 128 frames");

        void clear() noexcept
        {
            m_next.clear();
            m_slots = {};
        }

        void start(uint32_t first_frame) noexcept
        {
            if (!m_next.valid) m_next = first_frame;
        }

        bool observe(uint32_t frame) noexcept
        {
            if (!m_next.valid) return false;
            if (RollbackFrameIsAfter(m_next.value, frame)) return true;
            if (RollbackFrameDistance(frame, m_next.value) >= N)
                return false;
            Slot& slot = m_slots[frame % N];
            if (slot.matched && slot.frame != frame) return false;
            slot = {frame, true};
            while (true)
            {
                Slot& next = m_slots[m_next.value % N];
                if (!next.matched || next.frame != m_next.value) break;
                next = {};
                m_next = m_next.value + 1u;
            }
            return true;
        }

        uint32_t next_unmatched_frame() const noexcept
        {
            return m_next.valid ? m_next.value : 0;
        }

        bool valid() const noexcept { return m_next.valid; }

        bool observed(uint32_t frame) const noexcept
        {
            if (!m_next.valid) return false;
            if (RollbackFrameIsAfter(m_next.value, frame)) return true;
            if (RollbackFrameDistance(frame, m_next.value) >= N)
                return false;
            const Slot& slot = m_slots[frame % N];
            return slot.matched && slot.frame == frame;
        }

        void selective(uint64_t (&out)[2]) const noexcept
        {
            out[0] = 0;
            out[1] = 0;
            if (!m_next.valid) return;
            for (size_t bit = 0; bit < N; ++bit)
            {
                const uint32_t frame =
                    m_next.value + static_cast<uint32_t>(bit);
                const Slot& slot = m_slots[frame % N];
                if (slot.matched && slot.frame == frame)
                    out[bit / 64u] |= uint64_t {1} << (bit % 64u);
            }
        }

    private:
        struct Slot
        {
            uint32_t frame {0};
            bool matched {false};
        };

        RollbackFrameStamp m_next {};
        std::array<Slot, N> m_slots {};
    };

    enum class RollbackSummaryFrameClass : uint8_t
    {
        InWindow,
        Stale,
        TooFarAhead,
    };

    template <size_t N = 128>
    class RollbackSummaryConsensusWindow
    {
    public:
        static_assert(N >= 2 && N < 0x80000000u,
            "summary window must be RFC-1982-safe");

        void clear() noexcept
        {
            m_expected.clear();
            m_slots = {};
        }

        void start(uint32_t first_frame) noexcept
        {
            if (!m_expected.valid) m_expected = first_frame;
        }

        RollbackSummaryFrameClass classify(uint32_t frame) const noexcept
        {
            if (!m_expected.valid)
                return RollbackSummaryFrameClass::InWindow;
            if (RollbackFrameIsAfter(m_expected.value, frame))
                return RollbackSummaryFrameClass::Stale;
            if (RollbackFrameIsAfter(frame, m_expected.value)
                && RollbackFrameDistance(frame, m_expected.value) >= N)
            {
                return RollbackSummaryFrameClass::TooFarAhead;
            }
            return RollbackSummaryFrameClass::InWindow;
        }

        bool observe_local_match(uint32_t frame) noexcept
        {
            Slot* slot = claim(frame);
            if (!slot) return false;
            slot->local_match = true;
            return true;
        }

        bool observe_peer_ack(uint32_t frame) noexcept
        {
            Slot* slot = claim(frame);
            if (!slot) return false;
            slot->peer_ack = true;
            return true;
        }

        bool is_peer_acked(uint32_t frame) const noexcept
        {
            const Slot& slot = m_slots[frame % N];
            return slot.occupied && slot.frame == frame && slot.peer_ack;
        }

        bool pop_contiguous(uint32_t& frame) noexcept
        {
            if (!m_expected.valid) return false;
            Slot& slot = m_slots[m_expected.value % N];
            if (!slot.occupied || slot.frame != m_expected.value
                || !slot.local_match || !slot.peer_ack)
            {
                return false;
            }
            frame = m_expected.value;
            slot = {};
            m_expected = frame + 1u;
            return true;
        }

        const RollbackFrameStamp& expected() const noexcept
        {
            return m_expected;
        }

    private:
        struct Slot
        {
            uint32_t frame {0};
            bool occupied {false};
            bool local_match {false};
            bool peer_ack {false};
        };

        Slot* claim(uint32_t frame) noexcept
        {
            if (classify(frame) != RollbackSummaryFrameClass::InWindow)
                return nullptr;
            Slot& slot = m_slots[frame % N];
            if (slot.occupied && slot.frame != frame) return nullptr;
            slot.frame = frame;
            slot.occupied = true;
            return &slot;
        }

        RollbackFrameStamp m_expected {};
        std::array<Slot, N> m_slots {};
    };

    struct RollbackSummaryConsensusSelfTestReport
    {
        bool ok {false};
        bool peer_summary_first {false};
        bool ack_first {false};
        bool later_frame_cannot_skip {false};
        bool bilateral_ack_required {false};
        bool reordered_drains_contiguously {false};
        bool duplicates_idempotent {false};
        bool stale_after_reuse_safe {false};
        bool collision_rejected {false};
        bool too_far_rejected {false};
        bool wrap_aware {false};
        bool cumulative_ack_recovers_loss {false};
        bool selective_ack_reports_gap {false};
        bool receipt_query_distinguishes_gap {false};
        const char* failure {"not-run"};
    };

    static inline RollbackSummaryConsensusSelfTestReport
    RunRollbackSummaryConsensusSelfTest() noexcept
    {
        RollbackSummaryConsensusSelfTestReport report {};
        RollbackSummaryConsensusWindow<> window {};
        window.start(0);

        report.peer_summary_first = window.observe_local_match(0);
        uint32_t consumed = UINT32_MAX;
        report.bilateral_ack_required = !window.pop_contiguous(consumed);
        report.ack_first = window.observe_peer_ack(1)
            && window.observe_local_match(1);
        report.later_frame_cannot_skip = !window.pop_contiguous(consumed);
        if (!window.observe_peer_ack(0)
            || !window.pop_contiguous(consumed) || consumed != 0)
        {
            report.failure = "frame-zero-bilateral-proof-failed";
            return report;
        }
        uint32_t consumed_second = UINT32_MAX;
        report.reordered_drains_contiguously =
            window.pop_contiguous(consumed_second)
            && consumed_second == 1;
        report.stale_after_reuse_safe =
            window.classify(0) == RollbackSummaryFrameClass::Stale;
        report.too_far_rejected = window.classify(130)
            == RollbackSummaryFrameClass::TooFarAhead;

        RollbackSummaryConsensusWindow<> collision {};
        collision.start(20);
        report.collision_rejected = collision.observe_local_match(20)
            && !collision.observe_peer_ack(148);

        RollbackSummaryConsensusWindow<> duplicate {};
        duplicate.start(5);
        uint32_t duplicate_frame = 0;
        report.duplicates_idempotent = duplicate.observe_local_match(5)
            && duplicate.observe_local_match(5)
            && duplicate.observe_peer_ack(5)
            && duplicate.observe_peer_ack(5)
            && duplicate.pop_contiguous(duplicate_frame)
            && duplicate_frame == 5
            && !duplicate.pop_contiguous(duplicate_frame);

        RollbackSummaryConsensusWindow<> wrap {};
        wrap.start(UINT32_MAX - 1u);
        uint32_t wrap_a = 0;
        uint32_t wrap_b = 0;
        report.wrap_aware = wrap.observe_peer_ack(UINT32_MAX)
            && wrap.observe_local_match(UINT32_MAX)
            && wrap.observe_local_match(UINT32_MAX - 1u)
            && wrap.observe_peer_ack(UINT32_MAX - 1u)
            && wrap.pop_contiguous(wrap_a)
            && wrap_a == UINT32_MAX - 1u
            && wrap.pop_contiguous(wrap_b)
            && wrap_b == UINT32_MAX;

        RollbackSummaryMatchAckWindow<> ack_window {};
        ack_window.start(20);
        uint64_t selective[2] {};
        const bool observed_gap =
            ack_window.observe(22) && ack_window.observe(23);
        ack_window.selective(selective);
        report.selective_ack_reports_gap = observed_gap
            && ack_window.next_unmatched_frame() == 20
            && (selective[0] & (uint64_t {1} << 2u)) != 0
            && (selective[0] & (uint64_t {1} << 3u)) != 0;
        report.receipt_query_distinguishes_gap =
            !ack_window.observed(20)
            && ack_window.observed(22)
            && !ack_window.observed(24);
        report.cumulative_ack_recovers_loss =
            ack_window.observe(20) && ack_window.observe(21)
            && ack_window.next_unmatched_frame() == 24
            && ack_window.observed(20)
            && ack_window.observed(23)
            && !ack_window.observed(24);

        report.ok = report.peer_summary_first
            && report.ack_first
            && report.later_frame_cannot_skip
            && report.bilateral_ack_required
            && report.reordered_drains_contiguously
            && report.duplicates_idempotent
            && report.stale_after_reuse_safe
            && report.collision_rejected
            && report.too_far_rejected
            && report.wrap_aware
            && report.cumulative_ack_recovers_loss
            && report.selective_ack_reports_gap
            && report.receipt_query_distinguishes_gap;
        report.failure = report.ok ? "ok" : "consensus-selftest-failed";
        return report;
    }
}
