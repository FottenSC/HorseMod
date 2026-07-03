// ============================================================================
// Horse::RollbackLiveTransportQueue
//
// Guarded seam between validated network-side HRG1 packets and the game-thread
// rollback online-session model. This does not hook Steam or SC6 packet I/O.
// ============================================================================

#pragma once

#include "RollbackGekkoTransportBridge.hpp"
#include "RollbackOnlineSession.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace Horse
{
    enum class RollbackLiveTransportDrainStatus : uint8_t
    {
        NoPacket,
        AcceptedNoCorrection,
        AcceptedCorrectionRequired,
        Duplicate,
        Conflict,
        OverWindowLate,
        InvalidPacket,
        HashRejected,
        CacheOrderingRejected,
    };

    struct RollbackLiveTransportDrainReport
    {
        bool ok {false};
        bool has_packet {false};
        bool drained {false};
        bool left_queued {false};
        RollbackLiveTransportDrainStatus status {
            RollbackLiveTransportDrainStatus::NoPacket};
        RollbackOnlineReceiveReport receive {};
        RollbackTransportPacket metadata {};
        uint8_t source_peer {0};
        uint8_t dest_peer {0};
        uint64_t session_id {0};
        uint32_t sequence {0};
        uint64_t payload_hash {0};
        uint32_t payload_size {0};
        uint32_t queue_count {0};
        const char* failure {"not-run"};
    };

    struct RollbackLiveQueuedPacket
    {
        uint8_t source_peer {0};
        uint8_t dest_peer {0};
        uint64_t session_id {0};
        uint32_t sequence {0};
        uint64_t payload_hash {0};
        uint32_t payload_size {0};
        RollbackTransportPacket metadata {};
    };

    struct RollbackLiveTransportQueueSelfTestReport
    {
        bool ok {false};
        bool bridge_enqueue_ok {false};
        bool bad_bridge_rejected {false};
        bool wrong_source_rejected {false};
        bool wrong_destination_rejected {false};
        bool wrong_session_rejected {false};
        bool network_receive_queued_only {false};
        bool stock_drain_required {false};
        bool game_thread_drain_accepts {false};
        bool correction_required {false};
        bool duplicate_drained {false};
        bool over_window_rejected {false};
        bool drain_bypass_ok {false};
        bool capacity_guard {false};
        uint32_t enqueued_packets {0};
        uint32_t drained_packets {0};
        uint32_t rejected_packets {0};
        uint32_t queue_count {0};
        const char* failure {"not-run"};
    };

    template<size_t N>
    class RollbackLiveTransportQueue
    {
    public:
        static_assert(N > 0, "queue must have at least one slot");

        void clear() noexcept
        {
            m_head = 0;
            m_count = 0;
            m_enqueued = 0;
            m_drained = 0;
            m_rejected = 0;
            for (RollbackLiveQueuedPacket& packet : m_packets)
                packet = {};
        }

        uint32_t count() const noexcept
        {
            return m_count;
        }

        uint32_t enqueued_packets() const noexcept
        {
            return m_enqueued;
        }

        uint32_t drained_packets() const noexcept
        {
            return m_drained;
        }

        uint32_t rejected_packets() const noexcept
        {
            return m_rejected;
        }

        bool peek_state_hash(uint64_t& out) const noexcept
        {
            if (m_count == 0)
                return false;
            out = m_packets[m_head].metadata.state_hash;
            return true;
        }

        bool enqueue_bridge_wire(
            const uint8_t* bytes,
            size_t size,
            uint8_t expected_dest_peer) noexcept
        {
            RollbackGekkoBridgeDecodePolicy policy {};
            policy.expected_dest_peer = expected_dest_peer;
            return enqueue_bridge_wire(bytes, size, policy);
        }

        bool enqueue_bridge_wire(
            const uint8_t* bytes,
            size_t size,
            uint8_t expected_source_peer,
            uint8_t expected_dest_peer,
            uint64_t expected_session_id) noexcept
        {
            RollbackGekkoBridgeDecodePolicy policy {};
            policy.expected_source_peer = expected_source_peer;
            policy.expected_dest_peer = expected_dest_peer;
            policy.expected_session_id = expected_session_id;
            policy.require_source_peer = true;
            policy.require_session_id = true;
            return enqueue_bridge_wire(bytes, size, policy);
        }

        bool enqueue_bridge_wire(
            const uint8_t* bytes,
            size_t size,
            const RollbackGekkoBridgeDecodePolicy& policy) noexcept
        {
            if (m_count >= N)
            {
                ++m_rejected;
                return false;
            }

            RollbackGekkoBridgePacket bridge {};
            if (!DecodeRollbackGekkoBridgePacket(
                    bytes, size, policy, bridge))
            {
                ++m_rejected;
                return false;
            }

            const size_t tail = (m_head + m_count) % N;
            RollbackLiveQueuedPacket& out = m_packets[tail];
            out.source_peer = bridge.source_peer;
            out.dest_peer = bridge.dest_peer;
            out.session_id = bridge.session_id;
            out.sequence = bridge.sequence;
            out.payload_hash = bridge.payload_hash;
            out.payload_size = bridge.payload_size;
            out.metadata = bridge.metadata;
            ++m_count;
            ++m_enqueued;
            return true;
        }

        template<size_t HistoryN>
        RollbackLiveTransportDrainReport drain_one_to_session(
            RollbackOnlineSessionModel<HistoryN>& session,
            uint32_t local_sim_frame,
            uint64_t expected_state_hash,
            bool stock_drain_complete,
            RollbackCacheOrderingMode cache_mode =
                RollbackCacheOrderingMode::StockDrainBeforePrediction) noexcept
        {
            RollbackLiveTransportDrainReport out {};
            out.queue_count = m_count;
            out.failure = "ok";
            if (m_count == 0)
            {
                out.ok = true;
                out.status = RollbackLiveTransportDrainStatus::NoPacket;
                out.failure = "no-packet";
                return out;
            }

            out.has_packet = true;
            RollbackLiveQueuedPacket& packet = m_packets[m_head];
            out.metadata = packet.metadata;
            out.source_peer = packet.source_peer;
            out.dest_peer = packet.dest_peer;
            out.session_id = packet.session_id;
            out.sequence = packet.sequence;
            out.payload_hash = packet.payload_hash;
            out.payload_size = packet.payload_size;
            out.receive = session.receive_remote_packet(
                packet.metadata,
                local_sim_frame,
                expected_state_hash,
                true,
                stock_drain_complete,
                false,
                cache_mode);
            out.status = map_status(out.receive.status);
            out.ok = out.receive.ok;

            if (out.receive.status
                == RollbackOnlineAdapterStatus::CacheOrderingRejected)
            {
                out.left_queued = true;
                out.failure = out.receive.failure;
                out.queue_count = m_count;
                return out;
            }

            pop_front();
            out.drained = true;
            out.queue_count = m_count;
            out.failure = out.receive.failure;
            return out;
        }

    private:
        static RollbackLiveTransportDrainStatus map_status(
            RollbackOnlineAdapterStatus status) noexcept
        {
            switch (status)
            {
            case RollbackOnlineAdapterStatus::AcceptedNoCorrection:
                return RollbackLiveTransportDrainStatus::AcceptedNoCorrection;
            case RollbackOnlineAdapterStatus::AcceptedCorrectionRequired:
                return RollbackLiveTransportDrainStatus::AcceptedCorrectionRequired;
            case RollbackOnlineAdapterStatus::Duplicate:
                return RollbackLiveTransportDrainStatus::Duplicate;
            case RollbackOnlineAdapterStatus::Conflict:
                return RollbackLiveTransportDrainStatus::Conflict;
            case RollbackOnlineAdapterStatus::OverWindowLate:
                return RollbackLiveTransportDrainStatus::OverWindowLate;
            case RollbackOnlineAdapterStatus::HashRejected:
                return RollbackLiveTransportDrainStatus::HashRejected;
            case RollbackOnlineAdapterStatus::CacheOrderingRejected:
                return RollbackLiveTransportDrainStatus::CacheOrderingRejected;
            case RollbackOnlineAdapterStatus::InvalidPacket:
            default:
                return RollbackLiveTransportDrainStatus::InvalidPacket;
            }
        }

        void pop_front() noexcept
        {
            if (m_count == 0)
                return;
            m_packets[m_head] = {};
            m_head = (m_head + 1) % N;
            --m_count;
            ++m_drained;
        }

        std::array<RollbackLiveQueuedPacket, N> m_packets {};
        uint32_t m_head {0};
        uint32_t m_count {0};
        uint32_t m_enqueued {0};
        uint32_t m_drained {0};
        uint32_t m_rejected {0};
    };

    static inline bool MakeRollbackLiveTransportTestWire(
        uint8_t source_peer,
        uint8_t dest_peer,
        uint64_t session_id,
        uint32_t sequence,
        uint32_t last_confirmed_remote_sequence,
        const void* payload,
        size_t payload_size,
        RollbackGekkoBridgeWirePacket& out) noexcept
    {
        const RollbackTransportPacket metadata =
            MakeRollbackGekkoBridgeMetadata(
                sequence,
                last_confirmed_remote_sequence,
                payload,
                payload_size);
        return EncodeRollbackGekkoBridgePacketWithSession(
            source_peer,
            dest_peer,
            session_id,
            sequence,
            metadata,
            payload,
            payload_size,
            out);
    }

    static inline bool MakeRollbackLiveTransportTestWire(
        uint8_t source_peer,
        uint8_t dest_peer,
        uint32_t sequence,
        uint32_t last_confirmed_remote_sequence,
        const void* payload,
        size_t payload_size,
        RollbackGekkoBridgeWirePacket& out) noexcept
    {
        return MakeRollbackLiveTransportTestWire(
            source_peer,
            dest_peer,
            0,
            sequence,
            last_confirmed_remote_sequence,
            payload,
            payload_size,
            out);
    }

    static inline RollbackLiveTransportQueueSelfTestReport
    RunRollbackLiveTransportQueueSelfTest() noexcept
    {
        RollbackLiveTransportQueueSelfTestReport report {};
        report.failure = "ok";

        RollbackLiveTransportQueue<4> queue {};
        RollbackOnlineSessionModel<128> session {};
        session.reset(12, RollbackHashPolicy::Enforced);
        const uint64_t session_id = 0x4C4956455452414Eull;

        std::array<uint8_t, 4> payload0 {0x10, 0x20, 0x30, 0x40};
        RollbackGekkoBridgeWirePacket wire0 {};
        report.bridge_enqueue_ok =
            MakeRollbackLiveTransportTestWire(
                0xA0,
                0xB0,
                session_id,
                0,
                kRollbackTransportNoFrame,
                payload0.data(),
                payload0.size(),
                wire0)
            && queue.enqueue_bridge_wire(
                wire0.bytes.data(), wire0.size, 0xA0, 0xB0, session_id)
            && queue.count() == 1;
        report.network_receive_queued_only =
            report.bridge_enqueue_ok
            && session.metrics().packets_received == 0
            && session.metrics().packets_accepted == 0;

        uint64_t expected_hash = 0;
        (void)queue.peek_state_hash(expected_hash);
        const RollbackLiveTransportDrainReport blocked =
            queue.drain_one_to_session(
                session,
                0,
                expected_hash,
                false,
                RollbackCacheOrderingMode::StockDrainBeforePrediction);
        report.stock_drain_required =
            blocked.left_queued
            && !blocked.drained
            && blocked.status
                == RollbackLiveTransportDrainStatus::CacheOrderingRejected
            && queue.count() == 1
            && session.metrics().packets_received == 0;

        const RollbackLiveTransportDrainReport drained0 =
            queue.drain_one_to_session(
                session,
                0,
                expected_hash,
                true,
                RollbackCacheOrderingMode::StockDrainBeforePrediction);
        report.game_thread_drain_accepts =
            drained0.ok
            && drained0.drained
            && drained0.receive.accepted
            && drained0.status
                == RollbackLiveTransportDrainStatus::AcceptedNoCorrection
            && queue.count() == 0
            && session.metrics().packets_accepted == 1;

        (void)session.predict_remote_input(1);
        std::array<uint8_t, 4> payload1 {0x11, 0x21, 0x31, 0x41};
        RollbackGekkoBridgeWirePacket wire1 {};
        const bool enqueued1 =
            MakeRollbackLiveTransportTestWire(
                0xA0,
                0xB0,
                session_id,
                1,
                0,
                payload1.data(),
                payload1.size(),
                wire1)
            && queue.enqueue_bridge_wire(
                wire1.bytes.data(), wire1.size, 0xA0, 0xB0, session_id)
            && queue.peek_state_hash(expected_hash);
        const RollbackLiveTransportDrainReport drained1 =
            queue.drain_one_to_session(
                session,
                4,
                expected_hash,
                true,
                RollbackCacheOrderingMode::StockDrainBeforePrediction);
        report.correction_required =
            enqueued1
            && drained1.ok
            && drained1.drained
            && drained1.receive.requires_correction
            && drained1.status
                == RollbackLiveTransportDrainStatus::AcceptedCorrectionRequired;

        const bool enqueued_duplicate =
            queue.enqueue_bridge_wire(
                wire1.bytes.data(), wire1.size, 0xA0, 0xB0, session_id)
            && queue.peek_state_hash(expected_hash);
        const RollbackLiveTransportDrainReport duplicate =
            queue.drain_one_to_session(
                session,
                4,
                expected_hash,
                true,
                RollbackCacheOrderingMode::StockDrainBeforePrediction);
        report.duplicate_drained =
            enqueued_duplicate
            && duplicate.ok
            && duplicate.drained
            && !duplicate.receive.accepted
            && duplicate.status == RollbackLiveTransportDrainStatus::Duplicate;

        RollbackLiveTransportQueue<2> late_queue {};
        RollbackOnlineSessionModel<128> late_session {};
        late_session.reset(2, RollbackHashPolicy::Enforced);
        const bool enqueued_late =
            late_queue.enqueue_bridge_wire(
                wire0.bytes.data(), wire0.size, 0xA0, 0xB0, session_id)
            && late_queue.peek_state_hash(expected_hash);
        const RollbackLiveTransportDrainReport late =
            late_queue.drain_one_to_session(
                late_session,
                10,
                expected_hash,
                true,
                RollbackCacheOrderingMode::StockDrainBeforePrediction);
        report.over_window_rejected =
            enqueued_late
            && late.drained
            && !late.receive.accepted
            && late.status
                == RollbackLiveTransportDrainStatus::OverWindowLate;

        RollbackLiveTransportQueue<2> bypass_queue {};
        RollbackOnlineSessionModel<128> bypass_session {};
        bypass_session.reset(12, RollbackHashPolicy::Enforced);
        const bool enqueued_bypass =
            bypass_queue.enqueue_bridge_wire(
                wire0.bytes.data(), wire0.size, 0xA0, 0xB0, session_id)
            && bypass_queue.peek_state_hash(expected_hash);
        const RollbackLiveTransportDrainReport bypass =
            bypass_queue.drain_one_to_session(
                bypass_session,
                0,
                expected_hash,
                false,
                RollbackCacheOrderingMode::DrainBypass);
        report.drain_bypass_ok =
            enqueued_bypass
            && bypass.ok
            && bypass.drained
            && bypass.receive.accepted
            && bypass.status
                == RollbackLiveTransportDrainStatus::AcceptedNoCorrection;

        RollbackTransportPacket bad_metadata =
            MakeRollbackGekkoBridgeMetadata(
                2,
                1,
                payload1.data(),
                payload1.size());
        bad_metadata.local_input ^= 0x01ull;
        RollbackGekkoBridgeWirePacket bad_wire {};
        report.bad_bridge_rejected =
            EncodeRollbackGekkoBridgePacketWithSession(
                0xA0,
                0xB0,
                session_id,
                2,
                bad_metadata,
                payload1.data(),
                payload1.size(),
                bad_wire)
            && !queue.enqueue_bridge_wire(
                bad_wire.bytes.data(), bad_wire.size,
                0xA0, 0xB0, session_id);
        report.wrong_source_rejected =
            !queue.enqueue_bridge_wire(
                wire0.bytes.data(), wire0.size,
                0xA1, 0xB0, session_id);
        report.wrong_destination_rejected =
            !queue.enqueue_bridge_wire(
                wire0.bytes.data(), wire0.size,
                0xA0, 0xC0, session_id);
        report.wrong_session_rejected =
            !queue.enqueue_bridge_wire(
                wire0.bytes.data(), wire0.size,
                0xA0, 0xB0, session_id ^ 0x100ull);

        RollbackLiveTransportQueue<2> full_queue {};
        std::array<uint8_t, 4> payload2 {0x12, 0x22, 0x32, 0x42};
        std::array<uint8_t, 4> payload3 {0x13, 0x23, 0x33, 0x43};
        RollbackGekkoBridgeWirePacket wire2 {};
        RollbackGekkoBridgeWirePacket wire3 {};
        report.capacity_guard =
            MakeRollbackLiveTransportTestWire(
                0xA0, 0xB0, session_id, 2, 1,
                payload2.data(), payload2.size(), wire2)
            && MakeRollbackLiveTransportTestWire(
                0xA0, 0xB0, session_id, 3, 1,
                payload3.data(), payload3.size(), wire3)
            && full_queue.enqueue_bridge_wire(
                wire0.bytes.data(), wire0.size, 0xA0, 0xB0, session_id)
            && full_queue.enqueue_bridge_wire(
                wire1.bytes.data(), wire1.size, 0xA0, 0xB0, session_id)
            && !full_queue.enqueue_bridge_wire(
                wire2.bytes.data(), wire2.size, 0xA0, 0xB0, session_id)
            && full_queue.count() == 2
            && full_queue.rejected_packets() == 1;

        report.enqueued_packets =
            queue.enqueued_packets()
            + late_queue.enqueued_packets()
            + bypass_queue.enqueued_packets()
            + full_queue.enqueued_packets();
        report.drained_packets =
            queue.drained_packets()
            + late_queue.drained_packets()
            + bypass_queue.drained_packets()
            + full_queue.drained_packets();
        report.rejected_packets =
            queue.rejected_packets()
            + late_queue.rejected_packets()
            + bypass_queue.rejected_packets()
            + full_queue.rejected_packets();
        report.queue_count =
            queue.count()
            + late_queue.count()
            + bypass_queue.count()
            + full_queue.count();

        report.ok =
            report.bridge_enqueue_ok
            && report.bad_bridge_rejected
            && report.wrong_source_rejected
            && report.wrong_destination_rejected
            && report.wrong_session_rejected
            && report.network_receive_queued_only
            && report.stock_drain_required
            && report.game_thread_drain_accepts
            && report.correction_required
            && report.duplicate_drained
            && report.over_window_rejected
            && report.drain_bypass_ok
            && report.capacity_guard;
        if (!report.ok)
            report.failure = "live-transport-queue-selftest-failed";
        return report;
    }
}
