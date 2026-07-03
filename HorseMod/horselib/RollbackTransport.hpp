// ============================================================================
// Horse::RollbackTransport
//
// Phase-8 protocol/model groundwork for rollback netcode. This does not hook
// SC6's live transport yet. It defines the packet shape and ordering rules that
// a guarded online adapter must satisfy before it is allowed to feed rollback.
// ============================================================================

#pragma once

#include "RollbackInputHistory.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace Horse
{
    static constexpr uint32_t kRollbackTransportMagic = 0x31425248u; // "HRB1"
    static constexpr uint16_t kRollbackTransportVersion = 1;
    static constexpr size_t kRollbackTransportWireBytes = 48;
    static constexpr uint32_t kRollbackTransportNoFrame = 0xFFFFFFFFu;

    enum RollbackTransportFlags : uint32_t
    {
        RollbackTransportFlag_InputPresent = 1u << 0,
        RollbackTransportFlag_StateHashPresent = 1u << 1,
        RollbackTransportFlag_ResendRangePresent = 1u << 2,
    };

    enum class RollbackTransportAcceptStatus : uint8_t
    {
        Accepted,
        Duplicate,
        Conflict,
        OverWindowLate,
        InvalidPacket,
    };

    enum class RollbackCacheOrderingMode : uint8_t
    {
        StockDrainBeforePrediction,
        DrainBypass,
    };

    enum class RollbackHashPolicy : uint8_t
    {
        Disabled,
        WarnOnly,
        Enforced,
    };

    enum class RollbackHandshakeStatus : uint8_t
    {
        Accepted,
        InvalidLocal,
        InvalidRemote,
        ProtocolMismatch,
        BuildMismatch,
        HorseModMismatch,
        ManifestMismatch,
        WindowMismatch,
        HashPolicyMismatch,
    };

    enum class RollbackStateHashStatus : uint8_t
    {
        NotPresent,
        Match,
        WarningMismatch,
        EnforcedMismatch,
        RequiredButMissing,
    };

    struct RollbackTransportPacket
    {
        uint32_t flags {RollbackTransportFlag_InputPresent};
        uint32_t local_frame {0};
        uint32_t last_confirmed_remote_frame {kRollbackTransportNoFrame};
        uint64_t local_input {0};
        uint64_t state_hash {0};
        uint32_t resend_base_frame {0};
        uint16_t prediction_age_frames {0};
        uint16_t rollback_depth_frames {0};
        uint16_t resend_count {0};
    };

    struct RollbackPeerHandshake
    {
        uint32_t sc6_build_id {0};
        uint32_t horsemod_version {0};
        uint16_t protocol_version {kRollbackTransportVersion};
        uint16_t rollback_window_frames {0};
        uint64_t gameplay_manifest_hash {0};
        RollbackHashPolicy hash_policy {RollbackHashPolicy::WarnOnly};
    };

    struct RollbackHandshakeDecision
    {
        bool accepted {false};
        RollbackHandshakeStatus status {RollbackHandshakeStatus::InvalidLocal};
        const char* reason {"not-run"};
        uint16_t agreed_rollback_window {0};
        RollbackHashPolicy agreed_hash_policy {RollbackHashPolicy::WarnOnly};
    };

    struct RollbackStateHashDecision
    {
        bool ok {false};
        bool warning {false};
        bool mismatch {false};
        RollbackStateHashStatus status {
            RollbackStateHashStatus::RequiredButMissing};
        const char* reason {"not-run"};
    };

    struct RollbackTransportWirePacket
    {
        std::array<uint8_t, kRollbackTransportWireBytes> bytes {};
        size_t size {0};
    };

    struct RollbackTransportAcceptReport
    {
        RollbackTransportAcceptStatus status {
            RollbackTransportAcceptStatus::InvalidPacket};
        bool accepted {false};
        bool duplicate {false};
        bool reordered {false};
        bool confirmation_advanced {false};
        bool peer_confirmation_advanced {false};
        bool peer_confirmation_regressed {false};
        uint32_t contiguous_remote_frame {0xFFFFFFFFu};
        uint32_t prediction_age_frames {0};
        uint32_t rollback_depth_frames {0};
        const char* failure {"not-run"};
    };

    struct RollbackTransportMetrics
    {
        uint32_t packets_received {0};
        uint32_t packets_accepted {0};
        uint32_t duplicates {0};
        uint32_t conflicts {0};
        uint32_t reordered {0};
        uint32_t over_window_late {0};
        uint32_t confirmation_regressions {0};
        uint32_t max_prediction_age {0};
        uint32_t max_rollback_depth {0};
        uint32_t contiguous_remote_frame {0xFFFFFFFFu};
        uint32_t highest_remote_frame {0xFFFFFFFFu};
        uint32_t last_peer_confirmed_frame {0xFFFFFFFFu};
        bool peer_confirmation_known {false};
    };

    struct RollbackCacheOrderingDecision
    {
        bool ok {false};
        bool may_write_live_cache {false};
        bool requires_game_thread {true};
        const char* reason {"not-run"};
    };

    struct RollbackTransportSelfTestReport
    {
        bool ok {false};
        bool absolute_frame_roundtrip {false};
        bool duplicate_detected {false};
        bool reorder_detected {false};
        bool conflict_detected {false};
        bool over_window_rejected {false};
        bool over_window_duplicate_detected {false};
        bool over_window_conflict_detected {false};
        bool network_thread_cache_write_rejected {false};
        bool stock_drain_ordering_ok {false};
        bool ack_monotonic {false};
        bool invalid_packets_rejected {false};
        bool handshake_accepts_match {false};
        bool handshake_rejects_mismatch {false};
        bool handshake_rejects_invalid_policy {false};
        bool state_hash_policy_ok {false};
        bool loopback_delay_reorder_ok {false};
        RollbackTransportMetrics metrics {};
        const char* failure {"not-run"};
    };

    static inline bool RollbackHashPolicyValid(
        RollbackHashPolicy policy) noexcept
    {
        return policy == RollbackHashPolicy::Disabled
            || policy == RollbackHashPolicy::WarnOnly
            || policy == RollbackHashPolicy::Enforced;
    }

    static inline bool RollbackHandshakeValid(
        const RollbackPeerHandshake& h) noexcept
    {
        return h.sc6_build_id != 0
            && h.horsemod_version != 0
            && h.protocol_version != 0
            && h.rollback_window_frames != 0
            && h.rollback_window_frames <= 60
            && h.gameplay_manifest_hash != 0
            && RollbackHashPolicyValid(h.hash_policy);
    }

    static inline RollbackHandshakeDecision ValidateRollbackHandshake(
        const RollbackPeerHandshake& local,
        const RollbackPeerHandshake& remote) noexcept
    {
        RollbackHandshakeDecision out {};
        if (!RollbackHandshakeValid(local))
        {
            out.status = RollbackHandshakeStatus::InvalidLocal;
            out.reason = "invalid-local-handshake";
            return out;
        }
        if (!RollbackHandshakeValid(remote))
        {
            out.status = RollbackHandshakeStatus::InvalidRemote;
            out.reason = "invalid-remote-handshake";
            return out;
        }
        if (local.protocol_version != remote.protocol_version)
        {
            out.status = RollbackHandshakeStatus::ProtocolMismatch;
            out.reason = "rollback-protocol-mismatch";
            return out;
        }
        if (local.sc6_build_id != remote.sc6_build_id)
        {
            out.status = RollbackHandshakeStatus::BuildMismatch;
            out.reason = "sc6-build-mismatch";
            return out;
        }
        if (local.horsemod_version != remote.horsemod_version)
        {
            out.status = RollbackHandshakeStatus::HorseModMismatch;
            out.reason = "horsemod-version-mismatch";
            return out;
        }
        if (local.gameplay_manifest_hash != remote.gameplay_manifest_hash)
        {
            out.status = RollbackHandshakeStatus::ManifestMismatch;
            out.reason = "gameplay-manifest-mismatch";
            return out;
        }
        if (local.rollback_window_frames != remote.rollback_window_frames)
        {
            out.status = RollbackHandshakeStatus::WindowMismatch;
            out.reason = "rollback-window-mismatch";
            return out;
        }
        if (local.hash_policy != remote.hash_policy)
        {
            out.status = RollbackHandshakeStatus::HashPolicyMismatch;
            out.reason = "hash-policy-mismatch";
            return out;
        }

        out.accepted = true;
        out.status = RollbackHandshakeStatus::Accepted;
        out.reason = "accepted";
        out.agreed_rollback_window = local.rollback_window_frames;
        out.agreed_hash_policy = local.hash_policy;
        return out;
    }

    static inline RollbackStateHashDecision CheckRollbackStateHash(
        const RollbackTransportPacket& packet,
        uint64_t expected_state_hash,
        RollbackHashPolicy policy) noexcept
    {
        RollbackStateHashDecision out {};
        if (policy == RollbackHashPolicy::Disabled)
        {
            out.ok = true;
            out.status = RollbackStateHashStatus::NotPresent;
            out.reason = "state-hash-disabled";
            return out;
        }

        const bool hash_present =
            (packet.flags & RollbackTransportFlag_StateHashPresent) != 0;
        if (!hash_present)
        {
            out.ok = policy != RollbackHashPolicy::Enforced;
            out.status = out.ok
                ? RollbackStateHashStatus::NotPresent
                : RollbackStateHashStatus::RequiredButMissing;
            out.reason = out.ok
                ? "state-hash-not-present"
                : "state-hash-required";
            return out;
        }
        if (packet.state_hash == expected_state_hash)
        {
            out.ok = true;
            out.status = RollbackStateHashStatus::Match;
            out.reason = "state-hash-match";
            return out;
        }
        out.mismatch = true;
        if (policy == RollbackHashPolicy::WarnOnly)
        {
            out.ok = true;
            out.warning = true;
            out.status = RollbackStateHashStatus::WarningMismatch;
            out.reason = "state-hash-warning-mismatch";
            return out;
        }
        out.status = RollbackStateHashStatus::EnforcedMismatch;
        out.reason = "state-hash-enforced-mismatch";
        return out;
    }

    static inline void RollbackTransportWrite16(
        uint8_t* dst,
        uint16_t v) noexcept
    {
        dst[0] = static_cast<uint8_t>(v & 0xffu);
        dst[1] = static_cast<uint8_t>((v >> 8) & 0xffu);
    }

    static inline void RollbackTransportWrite32(
        uint8_t* dst,
        uint32_t v) noexcept
    {
        for (size_t i = 0; i < 4; ++i)
            dst[i] = static_cast<uint8_t>((v >> (i * 8)) & 0xffu);
    }

    static inline void RollbackTransportWrite64(
        uint8_t* dst,
        uint64_t v) noexcept
    {
        for (size_t i = 0; i < 8; ++i)
            dst[i] = static_cast<uint8_t>((v >> (i * 8)) & 0xffu);
    }

    static inline uint16_t RollbackTransportRead16(
        const uint8_t* src) noexcept
    {
        return static_cast<uint16_t>(src[0])
            | (static_cast<uint16_t>(src[1]) << 8);
    }

    static inline uint32_t RollbackTransportRead32(
        const uint8_t* src) noexcept
    {
        uint32_t v = 0;
        for (size_t i = 0; i < 4; ++i)
            v |= static_cast<uint32_t>(src[i]) << (i * 8);
        return v;
    }

    static inline uint64_t RollbackTransportRead64(
        const uint8_t* src) noexcept
    {
        uint64_t v = 0;
        for (size_t i = 0; i < 8; ++i)
            v |= static_cast<uint64_t>(src[i]) << (i * 8);
        return v;
    }

    static inline bool EncodeRollbackTransportPacket(
        const RollbackTransportPacket& packet,
        RollbackTransportWirePacket& out) noexcept
    {
        out.bytes = {};
        out.size = kRollbackTransportWireBytes;

        RollbackTransportWrite32(out.bytes.data() + 0x00,
                                 kRollbackTransportMagic);
        RollbackTransportWrite16(out.bytes.data() + 0x04,
                                 kRollbackTransportVersion);
        RollbackTransportWrite16(out.bytes.data() + 0x06,
                                 static_cast<uint16_t>(
                                     kRollbackTransportWireBytes));
        RollbackTransportWrite32(out.bytes.data() + 0x08, packet.flags);
        RollbackTransportWrite32(out.bytes.data() + 0x0C,
                                 packet.local_frame);
        RollbackTransportWrite32(out.bytes.data() + 0x10,
                                 packet.last_confirmed_remote_frame);
        RollbackTransportWrite64(out.bytes.data() + 0x14,
                                 packet.local_input);
        RollbackTransportWrite64(out.bytes.data() + 0x1C,
                                 packet.state_hash);
        RollbackTransportWrite32(out.bytes.data() + 0x24,
                                 packet.resend_base_frame);
        RollbackTransportWrite16(out.bytes.data() + 0x28,
                                 packet.prediction_age_frames);
        RollbackTransportWrite16(out.bytes.data() + 0x2A,
                                 packet.rollback_depth_frames);
        RollbackTransportWrite16(out.bytes.data() + 0x2C,
                                 packet.resend_count);
        RollbackTransportWrite16(out.bytes.data() + 0x2E, 0);
        return true;
    }

    static inline bool DecodeRollbackTransportPacket(
        const uint8_t* bytes,
        size_t size,
        RollbackTransportPacket& out) noexcept
    {
        out = {};
        if (!bytes || size < kRollbackTransportWireBytes)
            return false;
        if (RollbackTransportRead32(bytes + 0x00)
            != kRollbackTransportMagic)
            return false;
        if (RollbackTransportRead16(bytes + 0x04)
            != kRollbackTransportVersion)
            return false;
        if (RollbackTransportRead16(bytes + 0x06)
            != kRollbackTransportWireBytes)
            return false;

        out.flags = RollbackTransportRead32(bytes + 0x08);
        out.local_frame = RollbackTransportRead32(bytes + 0x0C);
        out.last_confirmed_remote_frame =
            RollbackTransportRead32(bytes + 0x10);
        out.local_input = RollbackTransportRead64(bytes + 0x14);
        out.state_hash = RollbackTransportRead64(bytes + 0x1C);
        out.resend_base_frame = RollbackTransportRead32(bytes + 0x24);
        out.prediction_age_frames = RollbackTransportRead16(bytes + 0x28);
        out.rollback_depth_frames = RollbackTransportRead16(bytes + 0x2A);
        out.resend_count = RollbackTransportRead16(bytes + 0x2C);
        return (out.flags & RollbackTransportFlag_InputPresent) != 0;
    }

    static inline RollbackCacheOrderingDecision
    ValidateRollbackCacheOrdering(
        RollbackCacheOrderingMode mode,
        bool on_game_thread,
        bool stock_drain_complete,
        bool network_thread_wants_live_cache_write) noexcept
    {
        RollbackCacheOrderingDecision out {};
        if (network_thread_wants_live_cache_write)
        {
            out.reason = "network-thread-live-cache-write-forbidden";
            return out;
        }
        if (!on_game_thread)
        {
            out.reason = "not-game-thread";
            return out;
        }
        if (mode == RollbackCacheOrderingMode::StockDrainBeforePrediction
            && !stock_drain_complete)
        {
            out.reason = "stock-drain-not-complete";
            return out;
        }
        out.ok = true;
        out.may_write_live_cache = false;
        out.reason = mode == RollbackCacheOrderingMode::DrainBypass
            ? "drain-bypass-adapter-owns-history"
            : "stock-drain-before-prediction";
        return out;
    }

    template<size_t N>
    class RollbackTransportPeerModel
    {
    public:
        static_assert((N & (N - 1)) == 0, "history size must be power of two");

        void clear() noexcept
        {
            m_history.clear();
            m_metrics = {};
        }

        const RollbackTransportMetrics& metrics() const noexcept
        {
            return m_metrics;
        }

        RollbackTransportAcceptReport accept_remote_input(
            const RollbackTransportPacket& packet,
            uint32_t local_sim_frame,
            uint32_t max_rollback_window) noexcept
        {
            RollbackTransportAcceptReport report {};
            report.failure = "ok";
            ++m_metrics.packets_received;

            if ((packet.flags & RollbackTransportFlag_InputPresent) == 0)
            {
                report.status = RollbackTransportAcceptStatus::InvalidPacket;
                report.failure = "input-not-present";
                return report;
            }

            RollbackInputFrame* existing =
                m_history.find_mutable(
                    static_cast<int32_t>(packet.local_frame));
            if (existing && existing->p2_confirmed)
            {
                if (existing->input.p2 == packet.local_input)
                {
                    report.status = RollbackTransportAcceptStatus::Duplicate;
                    report.duplicate = true;
                    report.failure = "duplicate";
                    ++m_metrics.duplicates;
                    return report;
                }
                report.status = RollbackTransportAcceptStatus::Conflict;
                report.failure = "conflicting-confirmed-input";
                ++m_metrics.conflicts;
                return report;
            }

            if (packet.local_frame < local_sim_frame
                && local_sim_frame - packet.local_frame
                    > max_rollback_window)
            {
                report.status =
                    RollbackTransportAcceptStatus::OverWindowLate;
                report.failure = "over-window-late-input";
                ++m_metrics.over_window_late;
                return report;
            }

            if (m_metrics.highest_remote_frame != 0xFFFFFFFFu
                && packet.local_frame < m_metrics.highest_remote_frame)
            {
                report.reordered = true;
                ++m_metrics.reordered;
            }

            RollbackInputFrame& frame =
                m_history.write(static_cast<int32_t>(packet.local_frame));
            frame.input.p2 = packet.local_input;
            frame.p2_confirmed = true;
            frame.p2_predicted = false;

            if (m_metrics.highest_remote_frame == 0xFFFFFFFFu
                || packet.local_frame > m_metrics.highest_remote_frame)
            {
                m_metrics.highest_remote_frame = packet.local_frame;
            }

            const uint32_t previous_contiguous =
                m_metrics.contiguous_remote_frame;
            for (;;)
            {
                const uint32_t next =
                    m_metrics.contiguous_remote_frame == 0xFFFFFFFFu
                    ? 0
                    : m_metrics.contiguous_remote_frame + 1;
                const RollbackInputFrame* next_frame =
                    m_history.find(static_cast<int32_t>(next));
                if (!next_frame || !next_frame->p2_confirmed)
                    break;
                m_metrics.contiguous_remote_frame = next;
            }

            const bool has_peer_confirmation =
                packet.last_confirmed_remote_frame
                    != kRollbackTransportNoFrame;
            if (has_peer_confirmation
                && m_metrics.peer_confirmation_known
                && packet.last_confirmed_remote_frame
                    < m_metrics.last_peer_confirmed_frame)
            {
                ++m_metrics.confirmation_regressions;
                report.peer_confirmation_regressed = true;
            }
            if (has_peer_confirmation
                && (!m_metrics.peer_confirmation_known
                    || packet.last_confirmed_remote_frame
                        > m_metrics.last_peer_confirmed_frame))
            {
                m_metrics.last_peer_confirmed_frame =
                    packet.last_confirmed_remote_frame;
                m_metrics.peer_confirmation_known = true;
                report.peer_confirmation_advanced = true;
            }

            if (packet.prediction_age_frames > m_metrics.max_prediction_age)
                m_metrics.max_prediction_age =
                    packet.prediction_age_frames;
            if (packet.rollback_depth_frames > m_metrics.max_rollback_depth)
                m_metrics.max_rollback_depth =
                    packet.rollback_depth_frames;

            ++m_metrics.packets_accepted;
            report.status = RollbackTransportAcceptStatus::Accepted;
            report.accepted = true;
            report.confirmation_advanced =
                m_metrics.contiguous_remote_frame != previous_contiguous;
            report.contiguous_remote_frame =
                m_metrics.contiguous_remote_frame;
            report.prediction_age_frames = packet.prediction_age_frames;
            report.rollback_depth_frames = packet.rollback_depth_frames;
            return report;
        }

    private:
        RollbackInputHistory<N> m_history {};
        RollbackTransportMetrics m_metrics {};
    };

    static inline RollbackTransportSelfTestReport
    RunRollbackTransportModelSelfTest() noexcept
    {
        RollbackTransportSelfTestReport report {};
        report.failure = "ok";

        RollbackTransportPacket packet {};
        packet.flags = RollbackTransportFlag_InputPresent
            | RollbackTransportFlag_StateHashPresent
            | RollbackTransportFlag_ResendRangePresent;
        packet.local_frame = 0x12345678u;
        packet.last_confirmed_remote_frame = 17;
        packet.local_input = 0x1122334455667788ull;
        packet.state_hash = 0x8877665544332211ull;
        packet.resend_base_frame = 0x12345670u;
        packet.prediction_age_frames = 4;
        packet.rollback_depth_frames = 6;
        packet.resend_count = 8;

        RollbackTransportWirePacket wire {};
        RollbackTransportPacket decoded {};
        report.absolute_frame_roundtrip =
            EncodeRollbackTransportPacket(packet, wire)
            && DecodeRollbackTransportPacket(
                wire.bytes.data(), wire.size, decoded)
            && decoded.flags == packet.flags
            && decoded.local_frame == packet.local_frame
            && decoded.local_input == packet.local_input
            && decoded.last_confirmed_remote_frame
                == packet.last_confirmed_remote_frame
            && decoded.state_hash == packet.state_hash
            && decoded.resend_base_frame == packet.resend_base_frame
            && decoded.prediction_age_frames
                == packet.prediction_age_frames
            && decoded.rollback_depth_frames
                == packet.rollback_depth_frames
            && decoded.resend_count == packet.resend_count;
        if (!report.absolute_frame_roundtrip)
        {
            report.failure = "absolute-frame-roundtrip-failed";
            return report;
        }

        RollbackTransportWirePacket bad_wire = wire;
        bad_wire.bytes[0] ^= 0xFFu;
        RollbackTransportPacket bad_decoded {};
        const bool bad_magic_rejected =
            !DecodeRollbackTransportPacket(
                bad_wire.bytes.data(), bad_wire.size, bad_decoded);
        bad_wire = wire;
        bad_wire.bytes[4] ^= 0x01u;
        const bool bad_version_rejected =
            !DecodeRollbackTransportPacket(
                bad_wire.bytes.data(), bad_wire.size, bad_decoded);
        bad_wire = wire;
        bad_wire.bytes[6] ^= 0x01u;
        const bool bad_size_field_rejected =
            !DecodeRollbackTransportPacket(
                bad_wire.bytes.data(), bad_wire.size, bad_decoded);
        RollbackTransportPacket no_input = packet;
        no_input.flags &= ~RollbackTransportFlag_InputPresent;
        RollbackTransportWirePacket no_input_wire {};
        const bool no_input_rejected =
            EncodeRollbackTransportPacket(no_input, no_input_wire)
            && !DecodeRollbackTransportPacket(
                no_input_wire.bytes.data(),
                no_input_wire.size,
                bad_decoded);
        const bool short_packet_rejected =
            !DecodeRollbackTransportPacket(
                wire.bytes.data(),
                kRollbackTransportWireBytes - 1,
                bad_decoded);
        report.invalid_packets_rejected =
            bad_magic_rejected
            && bad_version_rejected
            && bad_size_field_rejected
            && no_input_rejected
            && short_packet_rejected;
        if (!report.invalid_packets_rejected)
        {
            report.failure = "invalid-packet-rejection-failed";
            return report;
        }

        RollbackTransportPeerModel<128> peer {};
        RollbackTransportPacket p0 {};
        p0.local_frame = 0;
        p0.local_input = 0x10;
        p0.last_confirmed_remote_frame = kRollbackTransportNoFrame;
        p0.prediction_age_frames = 1;
        p0.rollback_depth_frames = 1;
        auto r0 = peer.accept_remote_input(p0, 4, 60);

        RollbackTransportPacket p2 = p0;
        p2.local_frame = 2;
        p2.local_input = 0x12;
        auto r2 = peer.accept_remote_input(p2, 4, 60);

        RollbackTransportPacket p1 = p0;
        p1.local_frame = 1;
        p1.local_input = 0x11;
        auto r1 = peer.accept_remote_input(p1, 4, 60);

        auto dup = peer.accept_remote_input(p1, 4, 60);
        RollbackTransportPacket conflict = p1;
        conflict.local_input = 0x99;
        auto bad = peer.accept_remote_input(conflict, 4, 60);
        auto late_dup = peer.accept_remote_input(p1, 100, 60);
        RollbackTransportPacket late_conflict = p1;
        late_conflict.local_input = 0x98;
        auto late_bad = peer.accept_remote_input(late_conflict, 100, 60);

        RollbackTransportPacket too_late = p0;
        too_late.local_frame = 30;
        auto late = peer.accept_remote_input(too_late, 100, 60);

        RollbackTransportPeerModel<128> ack_peer {};
        RollbackTransportPacket ack0 {};
        ack0.local_frame = 0;
        ack0.local_input = 0x20;
        ack0.last_confirmed_remote_frame = 5;
        auto ack_advance = ack_peer.accept_remote_input(ack0, 0, 60);

        RollbackTransportPacket ack_sentinel = ack0;
        ack_sentinel.local_frame = 1;
        ack_sentinel.local_input = 0x21;
        ack_sentinel.last_confirmed_remote_frame = kRollbackTransportNoFrame;
        auto ack_noop = ack_peer.accept_remote_input(
            ack_sentinel, 1, 60);

        RollbackTransportPacket ack_lower = ack0;
        ack_lower.local_frame = 2;
        ack_lower.local_input = 0x22;
        ack_lower.last_confirmed_remote_frame = 3;
        auto ack_regress = ack_peer.accept_remote_input(
            ack_lower, 2, 60);

        RollbackTransportPacket ack_higher = ack0;
        ack_higher.local_frame = 3;
        ack_higher.local_input = 0x23;
        ack_higher.last_confirmed_remote_frame = 9;
        auto ack_recover = ack_peer.accept_remote_input(
            ack_higher, 3, 60);
        const RollbackTransportMetrics& ack_metrics =
            ack_peer.metrics();

        report.duplicate_detected =
            dup.status == RollbackTransportAcceptStatus::Duplicate;
        report.reorder_detected = r1.reordered;
        report.conflict_detected =
            bad.status == RollbackTransportAcceptStatus::Conflict;
        report.over_window_duplicate_detected =
            late_dup.status == RollbackTransportAcceptStatus::Duplicate;
        report.over_window_conflict_detected =
            late_bad.status == RollbackTransportAcceptStatus::Conflict;
        report.over_window_rejected =
            late.status == RollbackTransportAcceptStatus::OverWindowLate;
        report.metrics = peer.metrics();
        report.ack_monotonic =
            ack_advance.accepted
            && ack_advance.peer_confirmation_advanced
            && ack_noop.accepted
            && !ack_noop.peer_confirmation_advanced
            && !ack_noop.peer_confirmation_regressed
            && ack_regress.accepted
            && ack_regress.peer_confirmation_regressed
            && !ack_regress.peer_confirmation_advanced
            && ack_recover.accepted
            && ack_recover.peer_confirmation_advanced
            && ack_metrics.peer_confirmation_known
            && ack_metrics.last_peer_confirmed_frame == 9
            && ack_metrics.confirmation_regressions == 1;

        RollbackPeerHandshake local_handshake {};
        local_handshake.sc6_build_id = 0x53433601u;
        local_handshake.horsemod_version = 0x00010000u;
        local_handshake.protocol_version = kRollbackTransportVersion;
        local_handshake.rollback_window_frames = 60;
        local_handshake.gameplay_manifest_hash = 0xA55A12345678FEDCull;
        local_handshake.hash_policy = RollbackHashPolicy::WarnOnly;

        RollbackPeerHandshake remote_handshake = local_handshake;
        const RollbackHandshakeDecision handshake_ok =
            ValidateRollbackHandshake(local_handshake, remote_handshake);
        RollbackPeerHandshake protocol_mismatch = remote_handshake;
        protocol_mismatch.protocol_version =
            static_cast<uint16_t>(kRollbackTransportVersion + 1);
        const RollbackHandshakeDecision handshake_protocol_bad =
            ValidateRollbackHandshake(local_handshake, protocol_mismatch);
        RollbackPeerHandshake manifest_mismatch = remote_handshake;
        manifest_mismatch.gameplay_manifest_hash ^= 0x10ull;
        const RollbackHandshakeDecision handshake_manifest_bad =
            ValidateRollbackHandshake(local_handshake, manifest_mismatch);
        RollbackPeerHandshake window_mismatch = remote_handshake;
        window_mismatch.rollback_window_frames = 30;
        const RollbackHandshakeDecision handshake_window_bad =
            ValidateRollbackHandshake(local_handshake, window_mismatch);
        RollbackPeerHandshake hash_policy_mismatch = remote_handshake;
        hash_policy_mismatch.hash_policy = RollbackHashPolicy::Enforced;
        const RollbackHandshakeDecision handshake_hash_policy_bad =
            ValidateRollbackHandshake(local_handshake, hash_policy_mismatch);
        RollbackPeerHandshake invalid_local_policy = local_handshake;
        invalid_local_policy.hash_policy =
            static_cast<RollbackHashPolicy>(0xFFu);
        const RollbackHandshakeDecision handshake_invalid_local_policy =
            ValidateRollbackHandshake(invalid_local_policy, remote_handshake);
        RollbackPeerHandshake invalid_remote_policy = remote_handshake;
        invalid_remote_policy.hash_policy =
            static_cast<RollbackHashPolicy>(0xFEu);
        const RollbackHandshakeDecision handshake_invalid_remote_policy =
            ValidateRollbackHandshake(local_handshake, invalid_remote_policy);
        RollbackPeerHandshake invalid_both_local = local_handshake;
        invalid_both_local.hash_policy =
            static_cast<RollbackHashPolicy>(0xFDu);
        RollbackPeerHandshake invalid_both_remote = invalid_both_local;
        const RollbackHandshakeDecision handshake_invalid_both_policy =
            ValidateRollbackHandshake(
                invalid_both_local, invalid_both_remote);

        report.handshake_accepts_match =
            handshake_ok.accepted
            && handshake_ok.agreed_rollback_window == 60
            && handshake_ok.agreed_hash_policy == RollbackHashPolicy::WarnOnly;
        report.handshake_rejects_mismatch =
            !handshake_protocol_bad.accepted
            && handshake_protocol_bad.status
                == RollbackHandshakeStatus::ProtocolMismatch
            && !handshake_manifest_bad.accepted
            && handshake_manifest_bad.status
                == RollbackHandshakeStatus::ManifestMismatch
            && !handshake_window_bad.accepted
            && handshake_window_bad.status
                == RollbackHandshakeStatus::WindowMismatch
            && !handshake_hash_policy_bad.accepted
            && handshake_hash_policy_bad.status
                == RollbackHandshakeStatus::HashPolicyMismatch;
        report.handshake_rejects_invalid_policy =
            !handshake_invalid_local_policy.accepted
            && handshake_invalid_local_policy.status
                == RollbackHandshakeStatus::InvalidLocal
            && !handshake_invalid_remote_policy.accepted
            && handshake_invalid_remote_policy.status
                == RollbackHandshakeStatus::InvalidRemote
            && !handshake_invalid_both_policy.accepted
            && handshake_invalid_both_policy.status
                == RollbackHandshakeStatus::InvalidLocal;

        RollbackTransportPacket hash_packet = packet;
        hash_packet.state_hash = 0x1111222233334444ull;
        const RollbackStateHashDecision hash_match =
            CheckRollbackStateHash(
                hash_packet,
                hash_packet.state_hash,
                RollbackHashPolicy::Enforced);
        const RollbackStateHashDecision hash_warn =
            CheckRollbackStateHash(
                hash_packet,
                hash_packet.state_hash ^ 1ull,
                RollbackHashPolicy::WarnOnly);
        const RollbackStateHashDecision hash_enforced =
            CheckRollbackStateHash(
                hash_packet,
                hash_packet.state_hash ^ 1ull,
                RollbackHashPolicy::Enforced);
        RollbackTransportPacket no_hash_packet = hash_packet;
        no_hash_packet.flags &= ~RollbackTransportFlag_StateHashPresent;
        const RollbackStateHashDecision hash_required_missing =
            CheckRollbackStateHash(
                no_hash_packet,
                hash_packet.state_hash,
                RollbackHashPolicy::Enforced);
        report.state_hash_policy_ok =
            hash_match.ok
            && hash_match.status == RollbackStateHashStatus::Match
            && hash_warn.ok
            && hash_warn.warning
            && hash_warn.status == RollbackStateHashStatus::WarningMismatch
            && !hash_enforced.ok
            && hash_enforced.mismatch
            && hash_enforced.status
                == RollbackStateHashStatus::EnforcedMismatch
            && !hash_required_missing.ok
            && hash_required_missing.status
                == RollbackStateHashStatus::RequiredButMissing;

        RollbackTransportPeerModel<128> loopback_peer {};
        RollbackTransportPacket loop0 {};
        loop0.flags = RollbackTransportFlag_InputPresent
            | RollbackTransportFlag_StateHashPresent;
        loop0.local_frame = 0;
        loop0.local_input = 0x30;
        loop0.last_confirmed_remote_frame = kRollbackTransportNoFrame;
        loop0.state_hash = 0x3000;
        auto loop_r0 = loopback_peer.accept_remote_input(loop0, 2, 60);
        RollbackTransportPacket loop2 = loop0;
        loop2.local_frame = 2;
        loop2.local_input = 0x32;
        loop2.state_hash = 0x3002;
        loop2.prediction_age_frames = 2;
        loop2.rollback_depth_frames = 2;
        auto loop_r2 = loopback_peer.accept_remote_input(loop2, 2, 60);
        RollbackTransportPacket loop1 = loop0;
        loop1.local_frame = 1;
        loop1.local_input = 0x31;
        loop1.state_hash = 0x3001;
        loop1.prediction_age_frames = 1;
        loop1.rollback_depth_frames = 1;
        auto loop_r1 = loopback_peer.accept_remote_input(loop1, 2, 60);
        const RollbackTransportMetrics& loop_metrics =
            loopback_peer.metrics();
        report.loopback_delay_reorder_ok =
            loop_r0.accepted
            && loop_r2.accepted
            && loop_r1.accepted
            && loop_r1.reordered
            && loop_metrics.contiguous_remote_frame == 2
            && loop_metrics.reordered == 1
            && loop_metrics.max_prediction_age == 2
            && loop_metrics.max_rollback_depth == 2;

        const RollbackCacheOrderingDecision net_thread =
            ValidateRollbackCacheOrdering(
                RollbackCacheOrderingMode::StockDrainBeforePrediction,
                false, false, true);
        report.network_thread_cache_write_rejected =
            !net_thread.ok
            && !net_thread.may_write_live_cache
            && net_thread.reason
            && net_thread.reason[0] != '\0';

        const RollbackCacheOrderingDecision game_thread =
            ValidateRollbackCacheOrdering(
                RollbackCacheOrderingMode::StockDrainBeforePrediction,
                true, true, false);
        report.stock_drain_ordering_ok =
            game_thread.ok && !game_thread.may_write_live_cache;

        report.ok =
            r0.accepted
            && r2.accepted
            && r1.accepted
            && report.metrics.contiguous_remote_frame == 2
            && report.absolute_frame_roundtrip
            && report.duplicate_detected
            && report.reorder_detected
            && report.conflict_detected
            && report.over_window_duplicate_detected
            && report.over_window_conflict_detected
            && report.over_window_rejected
            && report.ack_monotonic
            && report.invalid_packets_rejected
            && report.handshake_accepts_match
            && report.handshake_rejects_mismatch
            && report.handshake_rejects_invalid_policy
            && report.state_hash_policy_ok
            && report.loopback_delay_reorder_ok
            && report.network_thread_cache_write_rejected
            && report.stock_drain_ordering_ok;
        if (!report.ok && report.failure && report.failure[0] == 'o')
            report.failure = "transport-model-selftest-failed";
        return report;
    }
}
