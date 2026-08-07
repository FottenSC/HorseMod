// ============================================================================
// Horse::RollbackFaultInject
//
// Deterministic fault-injection metadata for the rollback lab. The injector is
// inactive until the lab backend can own a proven frame boundary.
// ============================================================================

#pragma once

#include "RollbackTransport.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace Horse
{
    enum class RollbackFaultKind : uint8_t
    {
        None,
        DelayedRemoteInput,
        DroppedInput,
        ReorderedInput,
        DuplicateInput,
        StalePrediction,
        WrongFrameId,
        CorruptedInputByte,
        CorruptedCacheTag,
        MasterClockJump,
        SkippedManualTick,
        DoubleManualTick,
        RngPerturbation,
        SideEffectLeak,
        RoundBoundaryRefusal,
    };

    struct RollbackFaultConfig
    {
        RollbackFaultKind kind {RollbackFaultKind::None};
        uint32_t seed {0x5C6B0001u};
        int32_t frame {-1};
        uint8_t player_slot {1};
        uint32_t delay_frames {0};
        uint64_t mutation {0};
    };

    class RollbackDeterministicRng
    {
    public:
        explicit RollbackDeterministicRng(uint32_t seed) noexcept
            : m_state(seed ? seed : 0x5C6B0001u)
        {
        }

        uint32_t next() noexcept
        {
            m_state = m_state * 1664525u + 1013904223u;
            return m_state;
        }

    private:
        uint32_t m_state;
    };

    enum class RollbackNetworkProfileKind : uint8_t
    {
        Clean0ms,
        Wifi50msJitter,
        BadWifi120ms5PctLoss,
        Overseas180ms2PctLoss,
        WiredIntercontinental200msRtt,
        SpikeEvery10s,
        BurstLoss500ms,
        CorruptProbe,
        DuplicateOnly,
    };

    static inline const char* RollbackNetworkProfileName(
        RollbackNetworkProfileKind kind) noexcept
    {
        switch (kind)
        {
        case RollbackNetworkProfileKind::Clean0ms:
            return "clean_0ms";
        case RollbackNetworkProfileKind::Wifi50msJitter:
            return "wifi_50ms_jitter";
        case RollbackNetworkProfileKind::BadWifi120ms5PctLoss:
            return "bad_wifi_120ms_5pct_loss";
        case RollbackNetworkProfileKind::Overseas180ms2PctLoss:
            return "overseas_180ms_2pct_loss";
        case RollbackNetworkProfileKind::WiredIntercontinental200msRtt:
            return "wired_intercontinental_200ms_rtt";
        case RollbackNetworkProfileKind::SpikeEvery10s:
            return "spike_every_10s";
        case RollbackNetworkProfileKind::BurstLoss500ms:
            return "burst_loss_500ms";
        case RollbackNetworkProfileKind::CorruptProbe:
            return "corrupt_probe";
        case RollbackNetworkProfileKind::DuplicateOnly:
            return "duplicate_only";
        }
        return "invalid";
    }

    static inline bool TryParseRollbackNetworkProfile(
        const std::string& text,
        RollbackNetworkProfileKind& out) noexcept
    {
        for (uint8_t raw = 0;
             raw <= static_cast<uint8_t>(
                 RollbackNetworkProfileKind::DuplicateOnly);
             ++raw)
        {
            const auto kind = static_cast<RollbackNetworkProfileKind>(raw);
            if (text == RollbackNetworkProfileName(kind))
            {
                out = kind;
                return true;
            }
        }
        return false;
    }

    struct RollbackNetworkProfile
    {
        RollbackNetworkProfileKind kind {RollbackNetworkProfileKind::Clean0ms};
        const char* name {"clean_0ms"};
        uint32_t seed {0x5C6B0001u};
        uint32_t base_latency_frames {0};
        uint32_t jitter_frames {0};
        uint32_t loss_per_mille {0};
        uint32_t duplicate_per_mille {0};
        uint32_t reorder_per_mille {0};
        uint32_t corrupt_per_mille {0};
        uint32_t spike_period_frames {0};
        uint32_t spike_extra_latency_frames {0};
        uint32_t burst_period_frames {0};
        uint32_t burst_duration_frames {0};
        uint32_t resend_interval_frames {4};
        uint32_t resend_window_frames {24};
        uint32_t rollback_window_frames {60};
        uint32_t frame_count {180};
        uint32_t max_ticks {540};
    };

    struct RollbackFaultInjectionStats
    {
        uint32_t packets_submitted {0};
        uint32_t packets_queued {0};
        uint32_t packets_delivered {0};
        uint32_t packets_dropped {0};
        uint32_t packets_duplicated {0};
        uint32_t packets_reordered {0};
        uint32_t packets_corrupted {0};
        uint32_t packets_rejected {0};
        uint32_t resend_packets {0};
        uint32_t queue_overflow {0};
        uint32_t max_queue_depth {0};
        uint32_t max_latency_frames {0};
        bool spike_applied {false};
        bool burst_applied {false};
    };

    struct RollbackFaultSendReport
    {
        bool ok {false};
        bool queued {false};
        bool dropped {false};
        bool corrupted {false};
        bool duplicated {false};
        bool resend {false};
        uint32_t frame {kRollbackTransportNoFrame};
    };

    static constexpr uint32_t kRollbackFaultMaxTrackedFrames = 1024;

    static inline uint64_t RollbackFaultInputForPeer(
        uint8_t peer,
        uint32_t frame) noexcept;

    struct RollbackFaultAcceptedInputTracker
    {
        std::array<uint8_t, kRollbackFaultMaxTrackedFrames> seen {};
        std::array<uint64_t, kRollbackFaultMaxTrackedFrames> inputs {};
        uint32_t unique_accepted {0};
        uint32_t payload_mismatches {0};
        uint32_t out_of_range {0};
        uint32_t recovered_first_send_faults {0};

        bool record(
            const RollbackTransportPacket& packet,
            uint8_t expected_source_peer,
            bool resend,
            const std::array<uint8_t, kRollbackFaultMaxTrackedFrames>*
                first_send_faulted) noexcept
        {
            if (packet.local_frame >= kRollbackFaultMaxTrackedFrames)
            {
                ++out_of_range;
                return false;
            }

            const uint64_t expected =
                RollbackFaultInputForPeer(
                    expected_source_peer,
                    packet.local_frame);
            if (packet.local_input != expected)
            {
                ++payload_mismatches;
                return false;
            }

            if (seen[packet.local_frame])
                return true;

            seen[packet.local_frame] = 1;
            inputs[packet.local_frame] = packet.local_input;
            ++unique_accepted;
            if (resend
                && first_send_faulted
                && (*first_send_faulted)[packet.local_frame])
            {
                ++recovered_first_send_faults;
            }
            return true;
        }

        bool complete(uint32_t frame_count) const noexcept
        {
            if (frame_count > kRollbackFaultMaxTrackedFrames)
                return false;
            if (unique_accepted != frame_count)
                return false;
            if (payload_mismatches != 0 || out_of_range != 0)
                return false;
            for (uint32_t frame = 0; frame < frame_count; ++frame)
            {
                if (!seen[frame])
                    return false;
            }
            return true;
        }
    };

    struct RollbackFaultProfileRunReport
    {
        bool ok {false};
        bool both_peers_converged {false};
        bool fault_profile_exercised {false};
        bool ack_resend_recovered {false};
        bool no_conflicts {false};
        bool no_over_window_late {false};
        const char* profile_name {"unknown"};
        const char* failure {"not-run"};
        uint32_t ticks {0};
        uint32_t frame_count {0};
        uint32_t peer_a_contiguous {kRollbackTransportNoFrame};
        uint32_t peer_b_contiguous {kRollbackTransportNoFrame};
        uint32_t peer_a_ack_of_local {kRollbackTransportNoFrame};
        uint32_t peer_b_ack_of_local {kRollbackTransportNoFrame};
        uint32_t checksum_a {0};
        uint32_t checksum_b {0};
        uint32_t expected_checksum {0};
        uint32_t peer_a_unique_accepted {0};
        uint32_t peer_b_unique_accepted {0};
        uint32_t peer_a_payload_mismatches {0};
        uint32_t peer_b_payload_mismatches {0};
        uint32_t first_send_faults_a_to_b {0};
        uint32_t first_send_faults_b_to_a {0};
        uint32_t recovered_by_resend_a_to_b {0};
        uint32_t recovered_by_resend_b_to_a {0};
        bool accepted_payloads_match {false};
        bool first_send_faults_observed {false};
        RollbackFaultInjectionStats a_to_b {};
        RollbackFaultInjectionStats b_to_a {};
        RollbackTransportMetrics peer_a_metrics {};
        RollbackTransportMetrics peer_b_metrics {};
    };

    struct RollbackFaultInjectSelfTestReport
    {
        bool ok {false};
        bool clean_profile_ok {false};
        bool wifi_jitter_profile_ok {false};
        bool bad_wifi_profile_ok {false};
        bool overseas_profile_ok {false};
        bool wired_intercontinental_profile_ok {false};
        bool spike_profile_ok {false};
        bool burst_profile_ok {false};
        bool corrupt_probe_ok {false};
        bool duplicate_only_ok {false};
        bool same_machine_profiles_converged {false};
        uint32_t profiles_run {0};
        uint32_t profiles_passed {0};
        RollbackFaultProfileRunReport last_failure {};
        const char* failure {"not-run"};
    };

    static inline RollbackNetworkProfile GetRollbackNetworkProfile(
        RollbackNetworkProfileKind kind) noexcept
    {
        RollbackNetworkProfile p {};
        p.kind = kind;
        p.name = RollbackNetworkProfileName(kind);
        switch (kind)
        {
        case RollbackNetworkProfileKind::Clean0ms:
            p.frame_count = 120;
            p.max_ticks = 240;
            break;
        case RollbackNetworkProfileKind::Wifi50msJitter:
            p.seed = 0x57494649u;
            p.base_latency_frames = 3;
            p.jitter_frames = 4;
            p.duplicate_per_mille = 15;
            p.reorder_per_mille = 120;
            p.frame_count = 180;
            p.max_ticks = 520;
            break;
        case RollbackNetworkProfileKind::BadWifi120ms5PctLoss:
            p.seed = 0xBAD5120u;
            p.base_latency_frames = 7;
            p.jitter_frames = 8;
            p.loss_per_mille = 50;
            p.duplicate_per_mille = 35;
            p.reorder_per_mille = 160;
            p.frame_count = 240;
            p.max_ticks = 840;
            break;
        case RollbackNetworkProfileKind::Overseas180ms2PctLoss:
            p.seed = 0x0180C0DEu;
            p.base_latency_frames = 11;
            p.jitter_frames = 6;
            p.loss_per_mille = 20;
            p.duplicate_per_mille = 20;
            p.reorder_per_mille = 90;
            p.frame_count = 240;
            p.max_ticks = 840;
            break;
        case RollbackNetworkProfileKind::WiredIntercontinental200msRtt:
            // The fault scheduler is directional and advances at 60 Hz.
            // Five to seven frames each way models approximately 167-233 ms
            // RTT: the useful wired corridor from London to California or
            // Japan. Loss/reorder/duplication are deliberately rare rather
            // than Wi-Fi-like bursts.
            p.seed = 0x1C0200u;
            p.base_latency_frames = 5;
            p.jitter_frames = 2;
            p.loss_per_mille = 5;
            p.duplicate_per_mille = 1;
            p.reorder_per_mille = 5;
            p.frame_count = 240;
            p.max_ticks = 720;
            p.resend_window_frames = 36;
            break;
        case RollbackNetworkProfileKind::SpikeEvery10s:
            p.seed = 0x5010E10u;
            p.base_latency_frames = 3;
            p.jitter_frames = 2;
            p.spike_period_frames = 600;
            p.spike_extra_latency_frames = 18;
            p.frame_count = 720;
            p.max_ticks = 1260;
            p.resend_window_frames = 36;
            break;
        case RollbackNetworkProfileKind::BurstLoss500ms:
            p.seed = 0xB0570500u;
            p.base_latency_frames = 4;
            p.jitter_frames = 4;
            p.loss_per_mille = 10;
            p.duplicate_per_mille = 20;
            p.reorder_per_mille = 90;
            p.burst_period_frames = 90;
            p.burst_duration_frames = 30;
            p.frame_count = 240;
            p.max_ticks = 960;
            p.resend_window_frames = 48;
            break;
        case RollbackNetworkProfileKind::CorruptProbe:
            p.seed = 0xC044C7u;
            p.base_latency_frames = 2;
            p.jitter_frames = 2;
            p.corrupt_per_mille = 80;
            p.frame_count = 120;
            p.max_ticks = 480;
            break;
        case RollbackNetworkProfileKind::DuplicateOnly:
            p.seed = 0xD0B1CA7Eu;
            p.duplicate_per_mille = 250;
            p.frame_count = 180;
            p.max_ticks = 420;
            break;
        }
        return p;
    }

    static inline bool RollbackFaultProfileHasFaults(
        const RollbackNetworkProfile& p) noexcept
    {
        return p.base_latency_frames != 0
            || p.jitter_frames != 0
            || p.loss_per_mille != 0
            || p.duplicate_per_mille != 0
            || p.reorder_per_mille != 0
            || p.corrupt_per_mille != 0
            || p.spike_period_frames != 0
            || p.burst_period_frames != 0;
    }

    static inline uint64_t RollbackFaultInputForPeer(
        uint8_t peer,
        uint32_t frame) noexcept
    {
        uint64_t x = static_cast<uint64_t>(peer) << 56;
        x ^= static_cast<uint64_t>(frame) * 0x9E3779B185EBCA87ull;
        x ^= static_cast<uint64_t>(frame + peer) << 17;
        x ^= 0x484F52534552424Cull;
        return x;
    }

    static inline uint32_t RollbackFaultMixChecksum(
        uint32_t checksum,
        uint64_t value) noexcept
    {
        checksum ^= static_cast<uint32_t>(value);
        checksum *= 16777619u;
        checksum ^= static_cast<uint32_t>(value >> 32);
        checksum *= 16777619u;
        return checksum;
    }

    static inline uint32_t RollbackFaultTwoPeerChecksum(
        uint32_t frame_count) noexcept
    {
        uint32_t checksum = 2166136261u;
        for (uint32_t frame = 0; frame < frame_count; ++frame)
        {
            checksum = RollbackFaultMixChecksum(
                checksum,
                RollbackFaultInputForPeer(0xA1, frame));
            checksum = RollbackFaultMixChecksum(
                checksum,
                RollbackFaultInputForPeer(0xB1, frame));
        }
        return checksum;
    }

    static inline uint32_t RollbackFaultAcceptedTwoPeerChecksum(
        uint8_t local_peer,
        uint8_t remote_peer,
        const RollbackFaultAcceptedInputTracker& remote_inputs,
        uint32_t frame_count) noexcept
    {
        if (!remote_inputs.complete(frame_count))
            return 0;
        if (!((local_peer == 0xA1 && remote_peer == 0xB1)
              || (local_peer == 0xB1 && remote_peer == 0xA1)))
        {
            return 0;
        }

        uint32_t checksum = 2166136261u;
        for (uint32_t frame = 0; frame < frame_count; ++frame)
        {
            const uint64_t p0 =
                local_peer == 0xA1
                ? RollbackFaultInputForPeer(0xA1, frame)
                : remote_inputs.inputs[frame];
            const uint64_t p1 =
                local_peer == 0xB1
                ? RollbackFaultInputForPeer(0xB1, frame)
                : remote_inputs.inputs[frame];
            checksum = RollbackFaultMixChecksum(checksum, p0);
            checksum = RollbackFaultMixChecksum(checksum, p1);
        }
        return checksum;
    }

    static inline bool RollbackFaultProfileNeedsResendRecovery(
        const RollbackNetworkProfile& profile) noexcept
    {
        return profile.loss_per_mille != 0
            || profile.corrupt_per_mille != 0
            || profile.burst_period_frames != 0;
    }

    template<size_t Capacity>
    class RollbackFaultInjectedChannel
    {
    public:
        explicit RollbackFaultInjectedChannel(
            uint32_t seed = 0x5C6B0001u) noexcept
            : m_rng(seed)
        {
        }

        void reset(uint32_t seed) noexcept
        {
            m_rng = RollbackDeterministicRng(seed);
            for (QueuedPacket& q : m_queue)
                q = {};
            m_next_order = 0;
            m_stats = {};
        }

        const RollbackFaultInjectionStats& stats() const noexcept
        {
            return m_stats;
        }

        bool send(
            const RollbackNetworkProfile& profile,
            const RollbackTransportPacket& packet,
            uint32_t now,
            bool resend,
            RollbackFaultSendReport* out_report = nullptr) noexcept
        {
            RollbackFaultSendReport send_report {};
            send_report.ok = true;
            send_report.resend = resend;
            send_report.frame = packet.local_frame;

            ++m_stats.packets_submitted;
            if (resend)
                ++m_stats.resend_packets;

            const bool in_burst =
                profile.burst_period_frames != 0
                && profile.burst_duration_frames != 0
                && (now % profile.burst_period_frames)
                    < profile.burst_duration_frames
                && now >= profile.burst_period_frames;
            if (in_burst)
            {
                m_stats.burst_applied = true;
                ++m_stats.packets_dropped;
                send_report.dropped = true;
                if (out_report)
                    *out_report = send_report;
                return true;
            }

            if (chance(profile.loss_per_mille))
            {
                ++m_stats.packets_dropped;
                send_report.dropped = true;
                if (out_report)
                    *out_report = send_report;
                return true;
            }

            const bool corrupted = chance(profile.corrupt_per_mille);
            const bool duplicated = chance(profile.duplicate_per_mille);
            bool ok = enqueue_one(profile, packet, now, corrupted, resend);
            send_report.ok = ok;
            send_report.queued = ok;
            send_report.corrupted = corrupted;
            if (duplicated)
            {
                RollbackTransportPacket duplicate = packet;
                duplicate.prediction_age_frames =
                    static_cast<uint16_t>(
                        duplicate.prediction_age_frames + 1u);
                ok = enqueue_one(
                         profile,
                         duplicate,
                         now + 1,
                         false,
                         resend)
                    && ok;
                send_report.ok = ok;
                send_report.duplicated = true;
                ++m_stats.packets_duplicated;
            }
            if (out_report)
                *out_report = send_report;
            return ok;
        }

        template<size_t HistoryCount>
        void drain_due(
            RollbackTransportPeerModel<HistoryCount>& peer,
            uint32_t now,
            uint32_t local_sim_frame,
            uint32_t rollback_window_frames,
            RollbackFaultAcceptedInputTracker* tracker = nullptr,
            uint8_t expected_source_peer = 0,
            const std::array<uint8_t, kRollbackFaultMaxTrackedFrames>*
                first_send_faulted = nullptr) noexcept
        {
            for (;;)
            {
                size_t best_index = Capacity;
                uint32_t best_deliver_at = 0xFFFFFFFFu;
                uint32_t best_order = 0xFFFFFFFFu;
                for (size_t i = 0; i < Capacity; ++i)
                {
                    const QueuedPacket& q = m_queue[i];
                    if (!q.active || q.deliver_at > now)
                        continue;
                    if (q.deliver_at < best_deliver_at
                        || (q.deliver_at == best_deliver_at
                            && q.order < best_order))
                    {
                        best_index = i;
                        best_deliver_at = q.deliver_at;
                        best_order = q.order;
                    }
                }
                if (best_index == Capacity)
                    break;

                QueuedPacket q = m_queue[best_index];
                m_queue[best_index] = {};
                ++m_stats.packets_delivered;
                const RollbackTransportAcceptReport accept =
                    peer.accept_remote_input(
                        q.packet,
                        local_sim_frame,
                        rollback_window_frames);
                if (accept.accepted && tracker)
                {
                    tracker->record(
                        q.packet,
                        expected_source_peer,
                        q.resend,
                        first_send_faulted);
                }
                if (!accept.accepted
                    && accept.status != RollbackTransportAcceptStatus::Duplicate)
                {
                    ++m_stats.packets_rejected;
                }
            }
        }

    private:
        struct QueuedPacket
        {
            bool active {false};
            bool resend {false};
            uint32_t deliver_at {0};
            uint32_t order {0};
            RollbackTransportPacket packet {};
        };

        bool chance(uint32_t per_mille) noexcept
        {
            return per_mille != 0 && (m_rng.next() % 1000u) < per_mille;
        }

        bool enqueue_one(
            const RollbackNetworkProfile& profile,
            RollbackTransportPacket packet,
            uint32_t now,
            bool corrupted,
            bool resend) noexcept
        {
            size_t slot = Capacity;
            uint32_t queue_depth = 0;
            for (size_t i = 0; i < Capacity; ++i)
            {
                if (m_queue[i].active)
                {
                    ++queue_depth;
                    continue;
                }
                if (slot == Capacity)
                    slot = i;
            }
            if (slot == Capacity)
            {
                ++m_stats.queue_overflow;
                return false;
            }

            uint32_t latency = profile.base_latency_frames;
            if (profile.jitter_frames != 0)
                latency += m_rng.next() % (profile.jitter_frames + 1u);

            if (profile.spike_period_frames != 0
                && now != 0
                && (now % profile.spike_period_frames) == 0)
            {
                latency += profile.spike_extra_latency_frames;
                m_stats.spike_applied = true;
            }

            if (chance(profile.reorder_per_mille))
            {
                latency += profile.jitter_frames + 3u;
                ++m_stats.packets_reordered;
            }

            if (corrupted)
            {
                packet.flags &= ~RollbackTransportFlag_InputPresent;
                ++m_stats.packets_corrupted;
            }

            QueuedPacket& q = m_queue[slot];
            q.active = true;
            q.resend = resend;
            q.deliver_at = now + latency;
            q.order = m_next_order++;
            q.packet = packet;
            ++queue_depth;
            ++m_stats.packets_queued;
            if (queue_depth > m_stats.max_queue_depth)
                m_stats.max_queue_depth = queue_depth;
            if (latency > m_stats.max_latency_frames)
                m_stats.max_latency_frames = latency;
            return true;
        }

        std::array<QueuedPacket, Capacity> m_queue {};
        RollbackDeterministicRng m_rng;
        uint32_t m_next_order {0};
        RollbackFaultInjectionStats m_stats {};
    };

    static inline uint32_t RollbackFaultAckFloor(
        const RollbackTransportMetrics& metrics) noexcept
    {
        if (!metrics.peer_confirmation_known)
            return kRollbackTransportNoFrame;
        return metrics.last_peer_confirmed_frame;
    }

    static inline uint32_t RollbackFaultFirstUnacked(
        const RollbackTransportMetrics& metrics) noexcept
    {
        const uint32_t ack = RollbackFaultAckFloor(metrics);
        return ack == kRollbackTransportNoFrame ? 0u : ack + 1u;
    }

    static inline RollbackTransportPacket MakeRollbackFaultInputPacket(
        uint8_t peer,
        uint32_t frame,
        uint32_t confirmed_remote_frame,
        uint32_t prediction_age,
        uint32_t rollback_depth) noexcept
    {
        RollbackTransportPacket packet {};
        packet.flags = RollbackTransportFlag_InputPresent
            | RollbackTransportFlag_StateHashPresent;
        packet.local_frame = frame;
        packet.last_confirmed_remote_frame = confirmed_remote_frame;
        packet.local_input = RollbackFaultInputForPeer(peer, frame);
        packet.state_hash =
            (static_cast<uint64_t>(frame) << 32)
            ^ RollbackFaultInputForPeer(peer, frame);
        packet.prediction_age_frames =
            static_cast<uint16_t>(
                prediction_age > 0xFFFFu ? 0xFFFFu : prediction_age);
        packet.rollback_depth_frames =
            static_cast<uint16_t>(
                rollback_depth > 0xFFFFu ? 0xFFFFu : rollback_depth);
        return packet;
    }

    static inline RollbackFaultProfileRunReport
    RunRollbackFaultProfileSimulation(
        const RollbackNetworkProfile& profile) noexcept
    {
        RollbackFaultProfileRunReport report {};
        report.profile_name = profile.name;
        report.frame_count = profile.frame_count;
        report.failure = "ok";

        RollbackTransportPeerModel<1024> peer_a {};
        RollbackTransportPeerModel<1024> peer_b {};
        RollbackFaultInjectedChannel<4096> a_to_b {profile.seed ^ 0xA100u};
        RollbackFaultInjectedChannel<4096> b_to_a {profile.seed ^ 0xB100u};
        RollbackFaultAcceptedInputTracker peer_a_remote_inputs {};
        RollbackFaultAcceptedInputTracker peer_b_remote_inputs {};
        std::array<uint8_t, kRollbackFaultMaxTrackedFrames>
            a_to_b_first_send_faulted {};
        std::array<uint8_t, kRollbackFaultMaxTrackedFrames>
            b_to_a_first_send_faulted {};

        auto record_first_send_fault =
            [](const RollbackFaultSendReport& send,
               std::array<uint8_t, kRollbackFaultMaxTrackedFrames>& faulted,
               uint32_t& fault_count) noexcept {
                if (send.resend
                    || send.frame >= kRollbackFaultMaxTrackedFrames
                    || (!send.dropped && !send.corrupted))
                {
                    return;
                }
                if (!faulted[send.frame])
                {
                    faulted[send.frame] = 1;
                    ++fault_count;
                }
            };

        for (uint32_t tick = 0; tick < profile.max_ticks; ++tick)
        {
            const RollbackTransportMetrics& metrics_a = peer_a.metrics();
            const RollbackTransportMetrics& metrics_b = peer_b.metrics();
            const uint32_t a_ack = metrics_a.contiguous_remote_frame;
            const uint32_t b_ack = metrics_b.contiguous_remote_frame;

            if (tick < profile.frame_count)
            {
                const RollbackTransportPacket a_packet =
                    MakeRollbackFaultInputPacket(
                        0xA1,
                        tick,
                        a_ack,
                        profile.base_latency_frames,
                        profile.base_latency_frames);
                const RollbackTransportPacket b_packet =
                    MakeRollbackFaultInputPacket(
                        0xB1,
                        tick,
                        b_ack,
                        profile.base_latency_frames,
                        profile.base_latency_frames);
                RollbackFaultSendReport send_a {};
                RollbackFaultSendReport send_b {};
                const bool sent_a =
                    a_to_b.send(profile, a_packet, tick, false, &send_a);
                const bool sent_b =
                    b_to_a.send(profile, b_packet, tick, false, &send_b);
                record_first_send_fault(
                    send_a,
                    a_to_b_first_send_faulted,
                    report.first_send_faults_a_to_b);
                record_first_send_fault(
                    send_b,
                    b_to_a_first_send_faulted,
                    report.first_send_faults_b_to_a);
                if (!sent_a || !sent_b)
                {
                    report.failure = "fault-channel-overflow";
                    break;
                }
            }

            if (tick != 0
                && (tick % profile.resend_interval_frames) == 0)
            {
                const uint32_t latest =
                    tick < profile.frame_count
                    ? tick
                    : profile.frame_count - 1u;
                const uint32_t first_a =
                    RollbackFaultFirstUnacked(peer_a.metrics());
                const uint32_t first_b =
                    RollbackFaultFirstUnacked(peer_b.metrics());
                const uint32_t max_a =
                    first_a + profile.resend_window_frames - 1u;
                const uint32_t max_b =
                    first_b + profile.resend_window_frames - 1u;
                for (uint32_t frame = first_a;
                     frame <= latest && frame <= max_a;
                     ++frame)
                {
                    const RollbackTransportPacket packet =
                        MakeRollbackFaultInputPacket(
                            0xA1,
                            frame,
                            peer_a.metrics().contiguous_remote_frame,
                            tick > frame ? tick - frame : 0,
                            tick > frame ? tick - frame : 0);
                    if (!a_to_b.send(profile, packet, tick, true))
                    {
                        report.failure = "fault-channel-overflow";
                        break;
                    }
                }
                for (uint32_t frame = first_b;
                     frame <= latest && frame <= max_b;
                     ++frame)
                {
                    const RollbackTransportPacket packet =
                        MakeRollbackFaultInputPacket(
                            0xB1,
                            frame,
                            peer_b.metrics().contiguous_remote_frame,
                            tick > frame ? tick - frame : 0,
                            tick > frame ? tick - frame : 0);
                    if (!b_to_a.send(profile, packet, tick, true))
                    {
                        report.failure = "fault-channel-overflow";
                        break;
                    }
                }
            }

            b_to_a.drain_due(
                peer_a,
                tick,
                tick,
                profile.rollback_window_frames,
                &peer_a_remote_inputs,
                0xB1,
                &b_to_a_first_send_faulted);
            a_to_b.drain_due(
                peer_b,
                tick,
                tick,
                profile.rollback_window_frames,
                &peer_b_remote_inputs,
                0xA1,
                &a_to_b_first_send_faulted);

            report.ticks = tick + 1u;
            if (peer_a.metrics().contiguous_remote_frame
                    == profile.frame_count - 1u
                && peer_b.metrics().contiguous_remote_frame
                    == profile.frame_count - 1u)
            {
                break;
            }
        }

        report.a_to_b = a_to_b.stats();
        report.b_to_a = b_to_a.stats();
        report.peer_a_metrics = peer_a.metrics();
        report.peer_b_metrics = peer_b.metrics();
        report.peer_a_contiguous =
            report.peer_a_metrics.contiguous_remote_frame;
        report.peer_b_contiguous =
            report.peer_b_metrics.contiguous_remote_frame;
        report.peer_a_ack_of_local =
            RollbackFaultAckFloor(report.peer_a_metrics);
        report.peer_b_ack_of_local =
            RollbackFaultAckFloor(report.peer_b_metrics);
        report.both_peers_converged =
            report.peer_a_contiguous == profile.frame_count - 1u
            && report.peer_b_contiguous == profile.frame_count - 1u;
        report.peer_a_unique_accepted =
            peer_a_remote_inputs.unique_accepted;
        report.peer_b_unique_accepted =
            peer_b_remote_inputs.unique_accepted;
        report.peer_a_payload_mismatches =
            peer_a_remote_inputs.payload_mismatches
            + peer_a_remote_inputs.out_of_range;
        report.peer_b_payload_mismatches =
            peer_b_remote_inputs.payload_mismatches
            + peer_b_remote_inputs.out_of_range;
        report.recovered_by_resend_a_to_b =
            peer_b_remote_inputs.recovered_first_send_faults;
        report.recovered_by_resend_b_to_a =
            peer_a_remote_inputs.recovered_first_send_faults;
        const bool needs_resend_recovery =
            RollbackFaultProfileNeedsResendRecovery(profile);
        report.first_send_faults_observed =
            !needs_resend_recovery
            || report.first_send_faults_a_to_b
            + report.first_send_faults_b_to_a > 0;
        report.ack_resend_recovered =
            !needs_resend_recovery
            || (report.first_send_faults_observed
                && report.recovered_by_resend_a_to_b
                + report.recovered_by_resend_b_to_a > 0);
        report.fault_profile_exercised =
            !RollbackFaultProfileHasFaults(profile)
            || report.a_to_b.packets_dropped != 0
            || report.b_to_a.packets_dropped != 0
            || report.a_to_b.packets_duplicated != 0
            || report.b_to_a.packets_duplicated != 0
            || report.a_to_b.packets_reordered != 0
            || report.b_to_a.packets_reordered != 0
            || report.a_to_b.packets_corrupted != 0
            || report.b_to_a.packets_corrupted != 0
            || report.a_to_b.spike_applied
            || report.b_to_a.spike_applied
            || report.a_to_b.burst_applied
            || report.b_to_a.burst_applied;
        report.no_conflicts =
            report.peer_a_metrics.conflicts == 0
            && report.peer_b_metrics.conflicts == 0;
        report.no_over_window_late =
            report.peer_a_metrics.over_window_late == 0
            && report.peer_b_metrics.over_window_late == 0;
        report.expected_checksum =
            RollbackFaultTwoPeerChecksum(profile.frame_count);
        report.checksum_a =
            RollbackFaultAcceptedTwoPeerChecksum(
                0xA1,
                0xB1,
                peer_a_remote_inputs,
                profile.frame_count);
        report.checksum_b =
            RollbackFaultAcceptedTwoPeerChecksum(
                0xB1,
                0xA1,
                peer_b_remote_inputs,
                profile.frame_count);
        report.accepted_payloads_match =
            peer_a_remote_inputs.complete(profile.frame_count)
            && peer_b_remote_inputs.complete(profile.frame_count)
            && report.checksum_a == report.expected_checksum
            && report.checksum_b == report.expected_checksum
            && report.checksum_a == report.checksum_b;

        report.ok =
            report.failure
            && report.failure[0] == 'o'
            && report.both_peers_converged
            && report.fault_profile_exercised
            && report.ack_resend_recovered
            && report.first_send_faults_observed
            && report.accepted_payloads_match
            && report.no_conflicts
            && report.no_over_window_late
            && report.a_to_b.queue_overflow == 0
            && report.b_to_a.queue_overflow == 0;
        if (!report.ok && report.failure && report.failure[0] == 'o')
            report.failure = "fault-profile-did-not-converge";
        return report;
    }

    static inline RollbackFaultInjectSelfTestReport
    RunRollbackFaultInjectSelfTest() noexcept
    {
        RollbackFaultInjectSelfTestReport report {};
        report.failure = "ok";

        const RollbackNetworkProfileKind profiles[] = {
            RollbackNetworkProfileKind::Clean0ms,
            RollbackNetworkProfileKind::Wifi50msJitter,
            RollbackNetworkProfileKind::BadWifi120ms5PctLoss,
            RollbackNetworkProfileKind::Overseas180ms2PctLoss,
            RollbackNetworkProfileKind::WiredIntercontinental200msRtt,
            RollbackNetworkProfileKind::SpikeEvery10s,
            RollbackNetworkProfileKind::BurstLoss500ms,
            RollbackNetworkProfileKind::CorruptProbe,
            RollbackNetworkProfileKind::DuplicateOnly,
        };

        for (RollbackNetworkProfileKind kind : profiles)
        {
            ++report.profiles_run;
            const RollbackFaultProfileRunReport run =
                RunRollbackFaultProfileSimulation(
                    GetRollbackNetworkProfile(kind));
            if (run.ok)
                ++report.profiles_passed;
            else if (report.last_failure.profile_name
                     && report.last_failure.profile_name[0] == 'u')
            {
                report.last_failure = run;
            }

            switch (kind)
            {
            case RollbackNetworkProfileKind::Clean0ms:
                report.clean_profile_ok = run.ok;
                break;
            case RollbackNetworkProfileKind::Wifi50msJitter:
                report.wifi_jitter_profile_ok = run.ok;
                break;
            case RollbackNetworkProfileKind::BadWifi120ms5PctLoss:
                report.bad_wifi_profile_ok = run.ok;
                break;
            case RollbackNetworkProfileKind::Overseas180ms2PctLoss:
                report.overseas_profile_ok = run.ok;
                break;
            case RollbackNetworkProfileKind::WiredIntercontinental200msRtt:
                report.wired_intercontinental_profile_ok = run.ok;
                break;
            case RollbackNetworkProfileKind::SpikeEvery10s:
                report.spike_profile_ok =
                    run.ok
                    && (run.a_to_b.spike_applied
                        || run.b_to_a.spike_applied);
                break;
            case RollbackNetworkProfileKind::BurstLoss500ms:
                report.burst_profile_ok =
                    run.ok
                    && (run.a_to_b.burst_applied
                        || run.b_to_a.burst_applied);
                break;
            case RollbackNetworkProfileKind::CorruptProbe:
                report.corrupt_probe_ok =
                    run.ok
                    && (run.a_to_b.packets_corrupted
                        + run.b_to_a.packets_corrupted) > 0
                    && (run.a_to_b.packets_rejected
                        + run.b_to_a.packets_rejected) > 0;
                break;
            case RollbackNetworkProfileKind::DuplicateOnly:
                report.duplicate_only_ok =
                    run.ok
                    && (run.a_to_b.packets_duplicated
                        + run.b_to_a.packets_duplicated) > 0
                    && run.a_to_b.packets_dropped == 0
                    && run.b_to_a.packets_dropped == 0
                    && run.a_to_b.packets_reordered == 0
                    && run.b_to_a.packets_reordered == 0
                    && run.a_to_b.packets_corrupted == 0
                    && run.b_to_a.packets_corrupted == 0;
                break;
            }
        }

        report.same_machine_profiles_converged =
            report.profiles_run == report.profiles_passed;
        report.ok =
            report.clean_profile_ok
            && report.wifi_jitter_profile_ok
            && report.bad_wifi_profile_ok
            && report.overseas_profile_ok
            && report.wired_intercontinental_profile_ok
            && report.spike_profile_ok
            && report.burst_profile_ok
            && report.corrupt_probe_ok
            && report.duplicate_only_ok
            && report.same_machine_profiles_converged;
        if (!report.ok)
            report.failure = "fault-injection-selftest-failed";
        return report;
    }
}
