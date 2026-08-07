// ============================================================================
// Horse::RollbackRouteManager
//
// Provider-neutral route selection below Gekko.  Child transports authenticate
// and validate Horse Protocol V2 packets before dequeueing them.  The manager
// assigns one logical Protocol V2 sequence before fan-out, consumes authenticated
// route probes, keeps bounded per-route measurements, and exposes one logical
// transport to the rollback runtime.
// ============================================================================

#pragma once

#include "RollbackUdpRuntime.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <new>

namespace Horse
{
    enum class RollbackRouteKind : uint8_t
    {
        DirectUdp = 0,
        SteamP2P = 1,
        // Source-compatibility alias for pre-provider callers. SC6 ships the
        // legacy ISteamNetworking P2P API, not SteamNetworkingSockets.
        SteamNetworkingSockets = SteamP2P,
        CloudflareTurn = 2,
        Count = 3,
    };

    static constexpr const char* RollbackRouteKindName(
        RollbackRouteKind route) noexcept
    {
        switch (route)
        {
        case RollbackRouteKind::DirectUdp: return "direct-udp";
        case RollbackRouteKind::SteamP2P: return "steam-p2p";
        case RollbackRouteKind::CloudflareTurn:
            return "cloudflare-turn";
        case RollbackRouteKind::Count: break;
        }
        return "unknown";
    }

    static constexpr size_t RollbackRouteIndex(
        RollbackRouteKind route) noexcept
    {
        return static_cast<size_t>(route);
    }

    class RollbackRouteDeadlineTracker
    {
    public:
        void reset() noexcept
        {
            m_last_receive = RollbackRouteKind::DirectUdp;
            m_missed_route = RollbackRouteKind::DirectUdp;
            m_last_receive_valid = false;
            m_missed_route_valid = false;
            m_missed_since_forward = false;
        }

        void note_gameplay_receive(RollbackRouteKind route) noexcept
        {
            if (route == RollbackRouteKind::Count) return;
            m_last_receive = route;
            m_last_receive_valid = true;
        }

        bool observe_advance(
            bool rolling_back,
            RollbackRouteKind selected,
            RollbackRouteKind& attributed_route,
            bool& missed) noexcept
        {
            if (rolling_back)
            {
                if (!m_missed_since_forward)
                {
                    m_missed_since_forward = true;
                    if (m_last_receive_valid)
                    {
                        m_missed_route = m_last_receive;
                        m_missed_route_valid = true;
                    }
                }
                return false;
            }
            missed = m_missed_since_forward;
            attributed_route = missed && m_missed_route_valid
                ? m_missed_route
                : m_last_receive_valid ? m_last_receive : selected;
            reset();
            return true;
        }

    private:
        RollbackRouteKind m_last_receive {
            RollbackRouteKind::DirectUdp};
        RollbackRouteKind m_missed_route {
            RollbackRouteKind::DirectUdp};
        bool m_last_receive_valid {false};
        bool m_missed_route_valid {false};
        bool m_missed_since_forward {false};
    };

    class IRollbackRouteClock
    {
    public:
        virtual ~IRollbackRouteClock() = default;
        virtual uint64_t now_microseconds() const noexcept = 0;
    };

    class RollbackSteadyRouteClock final : public IRollbackRouteClock
    {
    public:
        uint64_t now_microseconds() const noexcept override
        {
            return static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count());
        }
    };

    struct RollbackRouteSelectionPolicy
    {
        uint32_t probe_interval_ms {250};
        uint32_t probe_timeout_ms {2000};
        uint32_t health_timeout_ms {2500};
        uint32_t minimum_samples {8};
        uint32_t minimum_dwell_ms {5000};
        uint32_t minimum_latency_improvement_us {3000};
        uint16_t minimum_relative_improvement_per_mille {100};
        uint16_t minimum_deadline_improvement_per_mille {10};
        uint16_t maximum_loss_regression_per_mille {20};
        uint16_t maximum_loss_burst_regression {1};
        bool race_critical_packets {false};

        bool valid() const noexcept
        {
            return probe_interval_ms != 0
                && probe_timeout_ms >= probe_interval_ms
                && health_timeout_ms >= probe_interval_ms
                && minimum_samples != 0
                && minimum_samples <= 128
                && minimum_relative_improvement_per_mille <= 1000
                && minimum_deadline_improvement_per_mille <= 1000
                && maximum_loss_regression_per_mille <= 1000;
        }
    };

    struct RollbackRouteStatistics
    {
        RollbackRouteKind route {RollbackRouteKind::DirectUdp};
        bool configured {false};
        bool available {false};
        bool healthy {false};
        bool proven {false};
        uint64_t probes_sent {0};
        uint64_t probes_received {0};
        uint64_t probes_acknowledged {0};
        uint64_t probes_lost {0};
        uint64_t gameplay_packets_sent {0};
        uint64_t gameplay_packets_received {0};
        uint64_t duplicates {0};
        uint64_t reordered {0};
        uint64_t input_deadline_samples {0};
        uint64_t input_deadline_misses {0};
        uint64_t health_changes {0};
        uint64_t last_successful_receive_us {0};
        uint64_t current_rtt_us {0};
        uint64_t median_rtt_us {0};
        uint64_t p95_rtt_us {0};
        uint64_t p99_rtt_us {0};
        uint64_t jitter_us {0};
        uint32_t rolling_samples {0};
        uint32_t deadline_window_samples {0};
        uint32_t consecutive_loss_burst {0};
        uint32_t maximum_loss_burst {0};
        uint32_t estimated_loss_per_mille {0};
        uint32_t deadline_miss_per_mille {0};
        RollbackUdpWorkerFailure transport_failure {
            RollbackUdpWorkerFailure::None};
    };

    enum class RollbackRouteSwitchReason : uint8_t
    {
        InitialDirect,
        PrimaryUnhealthy,
        LowerDeadlineMissRate,
        LowerTailLatency,
        LowerJitterOrLossBurst,
        LowerMedianLatency,
        RecoveredPreferredRoute,
        HysteresisHeld,
        MinimumDwellHeld,
        NoHealthyAlternative,
    };

    static constexpr const char* RollbackRouteSwitchReasonName(
        RollbackRouteSwitchReason reason) noexcept
    {
        switch (reason)
        {
        case RollbackRouteSwitchReason::InitialDirect:
            return "initial-direct";
        case RollbackRouteSwitchReason::PrimaryUnhealthy:
            return "primary-unhealthy";
        case RollbackRouteSwitchReason::LowerDeadlineMissRate:
            return "lower-deadline-miss-rate";
        case RollbackRouteSwitchReason::LowerTailLatency:
            return "lower-tail-latency";
        case RollbackRouteSwitchReason::LowerJitterOrLossBurst:
            return "lower-jitter-or-loss-burst";
        case RollbackRouteSwitchReason::LowerMedianLatency:
            return "lower-median-latency";
        case RollbackRouteSwitchReason::RecoveredPreferredRoute:
            return "recovered-preferred-route";
        case RollbackRouteSwitchReason::HysteresisHeld:
            return "hysteresis-held";
        case RollbackRouteSwitchReason::MinimumDwellHeld:
            return "minimum-dwell-held";
        case RollbackRouteSwitchReason::NoHealthyAlternative:
            return "no-healthy-alternative";
        }
        return "unknown";
    }

    struct RollbackRouteDecision
    {
        RollbackRouteKind selected {RollbackRouteKind::DirectUdp};
        RollbackRouteKind previous {RollbackRouteKind::DirectUdp};
        RollbackRouteSwitchReason reason {
            RollbackRouteSwitchReason::InitialDirect};
        bool switched {false};
        uint64_t decision_time_us {0};
        uint64_t decision_count {0};
        RollbackRouteStatistics selected_statistics {};
        RollbackRouteStatistics candidate_statistics {};
    };

#pragma pack(push, 1)
    struct RollbackRouteProbePayload
    {
        uint32_t magic {0x42505248u}; // "HRPB"
        uint16_t version {1};
        uint16_t reserved {0};
        uint64_t probe_id {0};
        uint64_t logical_session_id {0};
        uint64_t sender_time_us {0};
    };

    struct RollbackRoutedPayloadHeader
    {
        uint32_t magic {0x54525248u}; // "HRRT"
        uint16_t version {1};
        uint8_t header_bytes {36};
        RollbackProtocolV2PacketType packet_type {
            RollbackProtocolV2PacketType::Heartbeat};
        uint16_t payload_bytes {0};
        uint16_t flags {0};
        uint64_t logical_session_id {0};
        uint64_t logical_sequence {0};
        uint64_t logical_ack_sequence {0};
    };
#pragma pack(pop)

    static_assert(sizeof(RollbackRouteProbePayload) == 32);
    static_assert(
        sizeof(RollbackRoutedPayloadHeader)
            == kRollbackRoutedEnvelopeHeaderBytes);
    static constexpr size_t kRollbackRoutedMaxPayloadBytes =
        kRollbackProtocolV2ApplicationMaxPayloadBytes;

    template<size_t Capacity>
    class RollbackRouteRollingWindow
    {
    public:
        static_assert(Capacity != 0);

        void clear() noexcept
        {
            m_values = {};
            m_count = 0;
            m_next = 0;
        }

        void push(uint64_t value) noexcept
        {
            m_values[m_next] = value;
            m_next = (m_next + 1) % Capacity;
            if (m_count < Capacity) ++m_count;
        }

        size_t size() const noexcept { return m_count; }

        uint64_t latest() const noexcept
        {
            if (m_count == 0) return 0;
            const size_t index = (m_next + Capacity - 1) % Capacity;
            return m_values[index];
        }

        uint64_t percentile(uint32_t percentile_value) const noexcept
        {
            if (m_count == 0) return 0;
            std::array<uint64_t, Capacity> ordered {};
            for (size_t i = 0; i < m_count; ++i)
                ordered[i] = m_values[i];
            std::sort(ordered.begin(), ordered.begin() + m_count);
            const size_t numerator =
                static_cast<size_t>(percentile_value) * (m_count - 1);
            const size_t index = (numerator + 99) / 100;
            return ordered[(std::min)(index, m_count - 1)];
        }

        uint64_t mean_absolute_variation() const noexcept
        {
            if (m_count < 2) return 0;
            uint64_t total = 0;
            const size_t first = m_count == Capacity ? m_next : 0;
            uint64_t previous = m_values[first];
            for (size_t i = 1; i < m_count; ++i)
            {
                const uint64_t current =
                    m_values[(first + i) % Capacity];
                total += current > previous
                    ? current - previous : previous - current;
                previous = current;
            }
            return total / (m_count - 1);
        }

    private:
        std::array<uint64_t, Capacity> m_values {};
        size_t m_count {0};
        size_t m_next {0};
    };

    template<size_t Capacity>
    class RollbackRouteRollingBoolWindow
    {
    public:
        static_assert(Capacity != 0);

        void clear() noexcept
        {
            m_values = {};
            m_count = 0;
            m_next = 0;
            m_true_count = 0;
        }

        void push(bool value) noexcept
        {
            if (m_count == Capacity)
            {
                if (m_values[m_next] != 0) --m_true_count;
            }
            else
            {
                ++m_count;
            }
            m_values[m_next] = value ? 1u : 0u;
            if (value) ++m_true_count;
            m_next = (m_next + 1) % Capacity;
        }

        size_t size() const noexcept { return m_count; }
        size_t true_count() const noexcept { return m_true_count; }

        uint32_t tail_run() const noexcept
        {
            uint32_t run = 0;
            for (size_t i = 0; i < m_count; ++i)
            {
                const size_t index =
                    (m_next + Capacity - 1 - i) % Capacity;
                if (m_values[index] == 0) break;
                ++run;
            }
            return run;
        }

        uint32_t longest_run() const noexcept
        {
            uint32_t longest = 0;
            uint32_t current = 0;
            const size_t first = m_count == Capacity ? m_next : 0;
            for (size_t i = 0; i < m_count; ++i)
            {
                if (m_values[(first + i) % Capacity] != 0)
                {
                    ++current;
                    longest = (std::max)(longest, current);
                }
                else
                {
                    current = 0;
                }
            }
            return longest;
        }

    private:
        std::array<uint8_t, Capacity> m_values {};
        size_t m_count {0};
        size_t m_next {0};
        size_t m_true_count {0};
    };

    class RollbackUnavailableSteamTransport final : public IRollbackTransport
    {
    public:
        bool start(const RollbackProductionConfig&) noexcept override
        {
            m_start_attempted = true;
            return false;
        }

        void stop() noexcept override { m_start_attempted = false; }

        bool enqueue(
            RollbackProtocolV2PacketType,
            const void*,
            uint16_t,
            RollbackSequenceStamp = {},
            uint64_t = UINT64_MAX) noexcept override
        {
            return false;
        }

        bool dequeue(RollbackUdpMessage&) noexcept override { return false; }
        bool peer_ready() const noexcept override { return false; }

        RollbackUdpWorkerStatus status() const noexcept override
        {
            RollbackUdpWorkerStatus out {};
            out.failure = RollbackUdpWorkerFailure::RouteRejected;
            out.last_failure = out.failure;
            out.transport_lifecycle = RollbackTransportLifecycle::Failed;
            return out;
        }

        bool start_attempted() const noexcept { return m_start_attempted; }

    private:
        bool m_start_attempted {false};
    };

    class RollbackRouteManagerTransport final : public IRollbackTransport
    {
    public:
        explicit RollbackRouteManagerTransport(
            IRollbackTransport& direct,
            IRollbackRouteClock* clock = nullptr) noexcept
            : m_clock(clock ? clock : &m_default_clock)
        {
            m_routes[RollbackRouteIndex(RollbackRouteKind::DirectUdp)] =
                &direct;
            m_route_state[
                RollbackRouteIndex(RollbackRouteKind::DirectUdp)]
                .configured = true;
        }

        ~RollbackRouteManagerTransport() noexcept override { stop(); }
        RollbackRouteManagerTransport(
            const RollbackRouteManagerTransport&) = delete;
        RollbackRouteManagerTransport& operator=(
            const RollbackRouteManagerTransport&) = delete;

        bool set_optional_route(
            RollbackRouteKind route,
            IRollbackTransport* transport) noexcept
        {
            if (m_started || route == RollbackRouteKind::DirectUdp
                || route == RollbackRouteKind::Count)
            {
                return false;
            }
            const size_t index = RollbackRouteIndex(route);
            m_routes[index] = transport;
            m_route_state[index].configured = transport != nullptr;
            return true;
        }

        bool set_policy(const RollbackRouteSelectionPolicy& policy) noexcept
        {
            if (m_started || !policy.valid()) return false;
            m_policy = policy;
            return true;
        }

        bool start(const RollbackProductionConfig& config) noexcept override
        {
            stop();
            if (!m_policy.valid() || !m_routes[0]
                || !m_inbound.initialize())
            {
                return false;
            }
            if (!m_routes[0]->start(config))
            {
                m_inbound.release();
                return false;
            }
            m_started = true;
            m_selected = RollbackRouteKind::DirectUdp;
            m_next_logical_sequence = 1;
            m_logical_session_id = compute_logical_session_id(config);
            m_session_generation = 0;
            m_last_switch_us = m_clock->now_microseconds();
            m_last_decision = {};
            m_last_decision.selected = m_selected;
            m_last_decision.previous = m_selected;
            m_last_decision.reason =
                RollbackRouteSwitchReason::InitialDirect;
            m_last_decision.decision_time_us = m_last_switch_us;
            m_last_switch_decision = m_last_decision;
            clear_deduplication();

            for (size_t index = 1; index < m_routes.size(); ++index)
            {
                if (!m_routes[index]) continue;
                // Optional providers are allowed to be unavailable, rejected,
                // or unauthorized without taking down mandatory direct UDP.
                (void)m_routes[index]->start(config);
            }
            refresh(m_last_switch_us);
            return true;
        }

        void stop() noexcept override
        {
            for (size_t index = m_routes.size(); index-- > 0;)
            {
                if (m_routes[index]) m_routes[index]->stop();
            }
            m_started = false;
            m_session_generation = 0;
            m_logical_session_id = 0;
            m_next_logical_sequence = 1;
            m_selected = RollbackRouteKind::DirectUdp;
            m_inbound.release();
            m_queue_overflow = false;
            clear_deduplication();
            for (RouteState& state : m_route_state)
            {
                const bool configured = state.configured;
                state = {};
                state.configured = configured;
            }
        }

        bool enqueue(
            RollbackProtocolV2PacketType type,
            const void* payload,
            uint16_t payload_bytes,
            RollbackSequenceStamp ack = {},
            uint64_t expected_generation = UINT64_MAX) noexcept override
        {
            return enqueue_impl(
                type, payload, payload_bytes, ack,
                expected_generation, false);
        }

        bool enqueue_redundant(
            RollbackProtocolV2PacketType type,
            const void* payload,
            uint16_t payload_bytes,
            RollbackSequenceStamp ack = {},
            uint64_t expected_generation = UINT64_MAX) noexcept override
        {
            return enqueue_impl(
                type, payload, payload_bytes, ack,
                expected_generation, true);
        }

        bool enqueue_impl(
            RollbackProtocolV2PacketType type,
            const void* payload,
            uint16_t payload_bytes,
            RollbackSequenceStamp ack,
            uint64_t expected_generation,
            bool redundant) noexcept
        {
            if (!m_started
                || payload_bytes > kRollbackRoutedMaxPayloadBytes
                || (payload_bytes != 0 && !payload))
            {
                return false;
            }
            const uint64_t now = m_clock->now_microseconds();
            refresh(now);
            if (expected_generation != UINT64_MAX
                && expected_generation != m_session_generation)
            {
                return false;
            }
            RollbackRoutedPayloadHeader routed {};
            routed.packet_type = type;
            routed.payload_bytes = payload_bytes;
            routed.logical_session_id = m_logical_session_id;
            routed.logical_sequence = next_logical_sequence();
            if (ack.valid)
            {
                routed.flags = RollbackProtocolV2FlagAckPresent;
                routed.logical_ack_sequence = ack.value;
            }
            std::array<uint8_t, kRollbackProtocolV2MaxPayloadBytes>
                routed_payload {};
            std::memcpy(
                routed_payload.data(), &routed, sizeof(routed));
            if (payload_bytes != 0)
            {
                std::memcpy(
                    routed_payload.data() + sizeof(routed),
                    payload,
                    payload_bytes);
            }
            const uint16_t routed_bytes = static_cast<uint16_t>(
                sizeof(routed) + payload_bytes);
            const bool critical =
                type == RollbackProtocolV2PacketType::Gekko;
            const size_t selected_index = RollbackRouteIndex(m_selected);
            IRollbackTransport* selected_transport =
                m_routes[selected_index];
            const auto send_route = [&](size_t index) noexcept {
                if (!m_routes[index]) return false;
                return redundant
                    ? m_routes[index]->enqueue_redundant(
                        RollbackProtocolV2PacketType::Routed,
                        routed_payload.data(), routed_bytes, {},
                        route_expected_generation(index))
                    : m_routes[index]->enqueue(
                        RollbackProtocolV2PacketType::Routed,
                        routed_payload.data(), routed_bytes, {},
                        route_expected_generation(index));
            };
            std::array<bool, kRouteCount> sent_routes {};
            bool sent = selected_transport
                && send_route(selected_index);
            if (sent)
            {
                sent_routes[selected_index] = true;
                ++m_route_state[selected_index].gameplay_packets_sent;
            }

            if (!sent && selected_index != 0 && m_routes[0])
            {
                sent = send_route(0);
                if (sent)
                {
                    sent_routes[0] = true;
                    ++m_route_state[0].gameplay_packets_sent;
                }
            }

            if (sent && critical && m_policy.race_critical_packets)
            {
                size_t best_alternative = kRouteCount;
                for (size_t index = 0; index < m_routes.size(); ++index)
                {
                    if (sent_routes[index] || !m_routes[index]
                        || !m_route_state[index].healthy)
                    {
                        continue;
                    }
                    if (best_alternative == kRouteCount
                        || race_route_preferred(
                            index, best_alternative))
                    {
                        best_alternative = index;
                    }
                }
                // Packet racing is intentionally bounded to the selected
                // route plus one best healthy alternative. Adding providers
                // must not multiply gameplay traffic across every route.
                if (best_alternative != kRouteCount)
                {
                    if (send_route(best_alternative))
                    {
                        ++m_route_state[best_alternative]
                            .gameplay_packets_sent;
                    }
                }
            }
            return sent;
        }

        bool dequeue(RollbackUdpMessage& message) noexcept override
        {
            if (!m_started) return false;
            const uint64_t now = m_clock->now_microseconds();
            refresh(now);
            pump_inbound(now);
            return m_inbound.pop(message);
        }

        bool peer_ready() const noexcept override
        {
            const size_t index = RollbackRouteIndex(m_selected);
            if (!m_started || m_queue_overflow || !m_routes[index])
            {
                return false;
            }
            const RollbackUdpWorkerStatus route =
                m_routes[index]->status();
            return route.running && route.peer_ready
                && route.failure == RollbackUdpWorkerFailure::None;
        }

        RollbackUdpWorkerStatus status() const noexcept override
        {
            if (!m_started || !m_routes[0])
                return {};
            const size_t selected_index = RollbackRouteIndex(m_selected);
            RollbackUdpWorkerStatus out =
                m_routes[selected_index]
                    ? m_routes[selected_index]->status()
                    : m_routes[0]->status();
            const RollbackUdpWorkerStatus direct = m_routes[0]->status();
            out.handshake_generation = direct.handshake_generation;
            if (m_queue_overflow)
            {
                out.peer_ready = false;
                out.failure = RollbackUdpWorkerFailure::QueueOverflow;
                out.last_failure = out.failure;
                out.transport_lifecycle =
                    RollbackTransportLifecycle::Failed;
            }
            return out;
        }

        void service() noexcept
        {
            if (!m_started) return;
            const uint64_t now = m_clock->now_microseconds();
            refresh(now);
            pump_inbound(now);
        }

        void observe_input_deadline(
            RollbackRouteKind route,
            bool missed) noexcept
        {
            if (route == RollbackRouteKind::Count) return;
            RouteState& state = m_route_state[RollbackRouteIndex(route)];
            ++state.input_deadline_samples;
            if (missed) ++state.input_deadline_misses;
            state.deadline_miss.push(missed);
        }

        // Deterministic tests and provider bridges can report end-to-end probe
        // observations directly. Production providers normally use the
        // authenticated RouteProbe/RouteProbeAck exchange below.
        void observe_probe_result(
            RollbackRouteKind route,
            uint64_t rtt_us,
            bool acknowledged) noexcept
        {
            if (route == RollbackRouteKind::Count) return;
            RouteState& state = m_route_state[RollbackRouteIndex(route)];
            ++state.probes_sent;
            if (acknowledged)
            {
                ++state.probes_acknowledged;
                state.rtt.push(rtt_us);
                state.loss.push(false);
                state.consecutive_loss_burst = 0;
                state.last_successful_receive_us =
                    m_clock->now_microseconds();
            }
            else
            {
                note_probe_loss(state);
            }
        }

        RollbackRouteKind selected_route() const noexcept
        {
            return m_selected;
        }

        RollbackRouteStatistics route_statistics(
            RollbackRouteKind route) const noexcept
        {
            if (route == RollbackRouteKind::Count) return {};
            const size_t index = RollbackRouteIndex(route);
            return snapshot(
                route, m_route_state[index],
                m_routes[index] ? m_routes[index]->status()
                                : RollbackUdpWorkerStatus {});
        }

        const RollbackRouteDecision& last_decision() const noexcept
        {
            return m_last_decision;
        }

        const RollbackRouteDecision& last_switch_decision() const noexcept
        {
            return m_last_switch_decision;
        }

        void set_next_logical_sequence_for_test(uint64_t sequence) noexcept
        {
            m_next_logical_sequence = sequence;
        }

        uint64_t logical_session_id_for_test() const noexcept
        {
            return m_logical_session_id;
        }

    private:
        static constexpr size_t kRouteCount =
            RollbackRouteIndex(RollbackRouteKind::Count);
        static constexpr size_t kRttWindow = 128;
        static constexpr size_t kDecisionWindow = 128;
        static constexpr size_t kPendingProbeCount = 64;
        static constexpr size_t kLogicalReplayCount = 4096;
        static constexpr size_t kInboundCount = 256;

        struct PendingProbe
        {
            bool active {false};
            uint64_t id {0};
            uint64_t sent_us {0};
        };

        struct RouteState
        {
            bool configured {false};
            bool available {false};
            bool healthy {false};
            uint64_t probes_sent {0};
            uint64_t probes_received {0};
            uint64_t probes_acknowledged {0};
            uint64_t probes_lost {0};
            uint64_t gameplay_packets_sent {0};
            uint64_t gameplay_packets_received {0};
            uint64_t duplicates {0};
            uint64_t reordered {0};
            uint64_t input_deadline_samples {0};
            uint64_t input_deadline_misses {0};
            uint64_t health_changes {0};
            uint64_t available_since_us {0};
            uint64_t last_successful_receive_us {0};
            uint64_t last_probe_us {0};
            uint64_t next_probe_id {1};
            uint64_t route_generation {0};
            uint64_t highest_sequence {0};
            bool highest_sequence_valid {false};
            bool health_observed {false};
            uint32_t consecutive_loss_burst {0};
            uint32_t maximum_loss_burst {0};
            RollbackRouteRollingWindow<kRttWindow> rtt {};
            RollbackRouteRollingBoolWindow<kDecisionWindow> loss {};
            RollbackRouteRollingBoolWindow<kDecisionWindow> deadline_miss {};
            std::array<PendingProbe, kPendingProbeCount> pending {};
        };

        class LogicalReplayWindow
        {
        public:
            void clear() noexcept
            {
                m_valid = false;
                m_highest = 0;
                m_values = {};
                m_present = {};
            }

            // Returns true only for the first acceptable copy. Packets older
            // than the bounded window are treated as stale duplicates.
            bool accept(uint64_t sequence) noexcept
            {
                if (sequence == 0) return false;
                const size_t slot =
                    static_cast<size_t>(sequence % kLogicalReplayCount);
                if (m_present[slot] && m_values[slot] == sequence)
                    return false;
                if (!m_valid)
                {
                    m_valid = true;
                    m_highest = sequence;
                }
                else if (
                    static_cast<int64_t>(sequence - m_highest) > 0)
                {
                    m_highest = sequence;
                }
                else
                {
                    const uint64_t distance = m_highest - sequence;
                    if (distance >= kLogicalReplayCount) return false;
                }
                m_values[slot] = sequence;
                m_present[slot] = true;
                return true;
            }

        private:
            bool m_valid {false};
            uint64_t m_highest {0};
            std::array<uint64_t, kLogicalReplayCount> m_values {};
            std::array<bool, kLogicalReplayCount> m_present {};
        };

        template<typename T, size_t Capacity>
        class FixedQueue
        {
        public:
            bool push(const T& value) noexcept
            {
                if (!m_values || m_count == Capacity) return false;
                (*m_values)[m_tail] = value;
                m_tail = (m_tail + 1) % Capacity;
                ++m_count;
                return true;
            }

            bool pop(T& value) noexcept
            {
                if (!m_values || m_count == 0) return false;
                value = (*m_values)[m_head];
                m_head = (m_head + 1) % Capacity;
                --m_count;
                return true;
            }

            bool initialize() noexcept
            {
                if (!m_values)
                {
                    m_values.reset(new (std::nothrow)
                        std::array<T, Capacity> {});
                }
                m_head = 0;
                m_tail = 0;
                m_count = 0;
                return m_values != nullptr;
            }

            void release() noexcept
            {
                m_values.reset();
                m_head = 0;
                m_tail = 0;
                m_count = 0;
            }

        private:
            std::unique_ptr<std::array<T, Capacity>> m_values {};
            size_t m_head {0};
            size_t m_tail {0};
            size_t m_count {0};
        };

        static bool sequence_after(uint64_t left, uint64_t right) noexcept
        {
            return static_cast<int64_t>(left - right) > 0;
        }

        static void note_probe_loss(RouteState& state) noexcept
        {
            ++state.probes_lost;
            state.loss.push(true);
            ++state.consecutive_loss_burst;
            state.maximum_loss_burst = (std::max)(
                state.maximum_loss_burst,
                state.consecutive_loss_burst);
        }

        static uint32_t ratio_per_mille(
            uint64_t numerator,
            uint64_t denominator) noexcept
        {
            if (denominator == 0) return 0;
            return static_cast<uint32_t>(std::min<uint64_t>(
                1000, (numerator * 1000) / denominator));
        }

        RollbackRouteStatistics snapshot(
            RollbackRouteKind route,
            const RouteState& state,
            const RollbackUdpWorkerStatus& transport) const noexcept
        {
            RollbackRouteStatistics out {};
            out.route = route;
            out.configured = state.configured;
            out.available = state.available;
            out.healthy = state.healthy;
            out.proven =
                state.rtt.size() >= m_policy.minimum_samples;
            out.probes_sent = state.probes_sent;
            out.probes_received = state.probes_received;
            out.probes_acknowledged = state.probes_acknowledged;
            out.probes_lost = state.probes_lost;
            out.gameplay_packets_sent = state.gameplay_packets_sent;
            out.gameplay_packets_received = state.gameplay_packets_received;
            out.duplicates = state.duplicates;
            out.reordered = state.reordered;
            out.input_deadline_samples = state.input_deadline_samples;
            out.input_deadline_misses = state.input_deadline_misses;
            out.health_changes = state.health_changes;
            out.last_successful_receive_us =
                state.last_successful_receive_us;
            out.current_rtt_us = state.rtt.latest();
            out.median_rtt_us = state.rtt.percentile(50);
            out.p95_rtt_us = state.rtt.percentile(95);
            out.p99_rtt_us = state.rtt.percentile(99);
            out.jitter_us = state.rtt.mean_absolute_variation();
            out.rolling_samples =
                static_cast<uint32_t>(state.rtt.size());
            out.deadline_window_samples =
                static_cast<uint32_t>(state.deadline_miss.size());
            out.consecutive_loss_burst = state.loss.tail_run();
            out.maximum_loss_burst = state.loss.longest_run();
            out.estimated_loss_per_mille = ratio_per_mille(
                state.loss.true_count(), state.loss.size());
            out.deadline_miss_per_mille = ratio_per_mille(
                state.deadline_miss.true_count(),
                state.deadline_miss.size());
            out.transport_failure = transport.failure;
            return out;
        }

        bool meaningful_latency_improvement(
            uint64_t current,
            uint64_t candidate) const noexcept
        {
            if (candidate >= current || current == 0) return false;
            const uint64_t absolute = current - candidate;
            return absolute >= m_policy.minimum_latency_improvement_us
                && absolute * 1000
                    >= current
                        * m_policy.minimum_relative_improvement_per_mille;
        }

        RollbackRouteSwitchReason better_reason(
            const RollbackRouteStatistics& current,
            const RollbackRouteStatistics& candidate) const noexcept
        {
            const bool deadline_comparable =
                current.deadline_window_samples
                    >= m_policy.minimum_samples
                && candidate.deadline_window_samples
                    >= m_policy.minimum_samples;
            if (deadline_comparable
                && candidate.deadline_miss_per_mille
                    > current.deadline_miss_per_mille
                        + m_policy
                            .minimum_deadline_improvement_per_mille)
            {
                return RollbackRouteSwitchReason::HysteresisHeld;
            }
            if (candidate.estimated_loss_per_mille
                    > current.estimated_loss_per_mille
                        + m_policy.maximum_loss_regression_per_mille
                || candidate.maximum_loss_burst
                    > current.maximum_loss_burst
                        + m_policy.maximum_loss_burst_regression)
            {
                return RollbackRouteSwitchReason::HysteresisHeld;
            }
            if (candidate.deadline_miss_per_mille
                    + m_policy.minimum_deadline_improvement_per_mille
                <= current.deadline_miss_per_mille
                && deadline_comparable)
            {
                // A missed input deadline is the rollback-visible outcome.
                // Once both windows are statistically usable it outranks
                // probe latency, while the loss/burst safety bounds above
                // still prevent switching to an unstable route.
                return RollbackRouteSwitchReason::LowerDeadlineMissRate;
            }
            if ((candidate.p99_rtt_us
                        > current.p99_rtt_us
                            + m_policy.minimum_latency_improvement_us)
                || (candidate.p95_rtt_us
                        > current.p95_rtt_us
                            + m_policy.minimum_latency_improvement_us))
            {
                return RollbackRouteSwitchReason::HysteresisHeld;
            }
            if (meaningful_latency_improvement(
                    current.p99_rtt_us, candidate.p99_rtt_us)
                || meaningful_latency_improvement(
                    current.p95_rtt_us, candidate.p95_rtt_us))
            {
                return RollbackRouteSwitchReason::LowerTailLatency;
            }
            if ((candidate.jitter_us
                        + m_policy.minimum_latency_improvement_us
                    < current.jitter_us)
                || candidate.maximum_loss_burst
                    + 1 < current.maximum_loss_burst
                || candidate.estimated_loss_per_mille
                    < current.estimated_loss_per_mille)
            {
                return RollbackRouteSwitchReason::
                    LowerJitterOrLossBurst;
            }
            if (meaningful_latency_improvement(
                    current.median_rtt_us, candidate.median_rtt_us))
            {
                return RollbackRouteSwitchReason::LowerMedianLatency;
            }
            return RollbackRouteSwitchReason::HysteresisHeld;
        }

        bool race_route_preferred(
            size_t candidate_index,
            size_t current_index) const noexcept
        {
            const RollbackRouteStatistics candidate = snapshot(
                static_cast<RollbackRouteKind>(candidate_index),
                m_route_state[candidate_index],
                m_routes[candidate_index]->status());
            const RollbackRouteStatistics current = snapshot(
                static_cast<RollbackRouteKind>(current_index),
                m_route_state[current_index],
                m_routes[current_index]->status());
            if (candidate.proven != current.proven)
                return candidate.proven;
            const bool deadlines_comparable =
                candidate.deadline_window_samples
                    >= m_policy.minimum_samples
                && current.deadline_window_samples
                    >= m_policy.minimum_samples;
            if (deadlines_comparable
                && candidate.deadline_miss_per_mille
                    != current.deadline_miss_per_mille)
            {
                return candidate.deadline_miss_per_mille
                    < current.deadline_miss_per_mille;
            }
            if (candidate.p99_rtt_us != current.p99_rtt_us)
                return candidate.p99_rtt_us < current.p99_rtt_us;
            if (candidate.estimated_loss_per_mille
                != current.estimated_loss_per_mille)
            {
                return candidate.estimated_loss_per_mille
                    < current.estimated_loss_per_mille;
            }
            if (candidate.jitter_us != current.jitter_us)
                return candidate.jitter_us < current.jitter_us;
            return candidate_index < current_index;
        }

        void select_route(uint64_t now) noexcept
        {
            const size_t current_index = RollbackRouteIndex(m_selected);
            const RollbackRouteStatistics current = snapshot(
                m_selected,
                m_route_state[current_index],
                m_routes[current_index]
                    ? m_routes[current_index]->status()
                    : RollbackUdpWorkerStatus {});

            size_t best_index = current_index;
            RollbackRouteStatistics best_statistics = current;
            RollbackRouteStatistics candidate_statistics = current;
            RollbackRouteSwitchReason reason =
                RollbackRouteSwitchReason::NoHealthyAlternative;
            if (!current.healthy)
            {
                bool found = false;
                for (size_t index = 0; index < m_routes.size(); ++index)
                {
                    if (index != current_index
                        && m_route_state[index].healthy)
                    {
                        const RollbackRouteStatistics candidate = snapshot(
                            static_cast<RollbackRouteKind>(index),
                            m_route_state[index],
                            m_routes[index]->status());
                        candidate_statistics = candidate;
                        const bool prefer = !found
                            || (candidate.proven
                                && !best_statistics.proven)
                            || (candidate.proven
                                && best_statistics.proven
                                && better_reason(
                                    best_statistics, candidate)
                                    != RollbackRouteSwitchReason::
                                        HysteresisHeld);
                        if (prefer)
                        {
                            found = true;
                            best_index = index;
                            best_statistics = candidate;
                        }
                    }
                }
                reason = found
                    ? RollbackRouteSwitchReason::PrimaryUnhealthy
                    : RollbackRouteSwitchReason::NoHealthyAlternative;
            }
            else
            {
                if (!current.proven)
                {
                    reason = RollbackRouteSwitchReason::HysteresisHeld;
                }
                else if (now - m_last_switch_us
                    < static_cast<uint64_t>(
                        m_policy.minimum_dwell_ms) * 1000)
                {
                    reason = RollbackRouteSwitchReason::MinimumDwellHeld;
                }
                else
                {
                    for (size_t index = 0; index < m_routes.size(); ++index)
                    {
                        if (index == current_index
                            || !m_route_state[index].healthy)
                        {
                            continue;
                        }
                        const RollbackRouteKind candidate_route =
                            static_cast<RollbackRouteKind>(index);
                        const RollbackRouteStatistics candidate = snapshot(
                            candidate_route,
                            m_route_state[index],
                            m_routes[index]->status());
                        if (!candidate.proven) continue;
                        candidate_statistics = candidate;
                        const RollbackRouteSwitchReason candidate_reason =
                            better_reason(best_statistics, candidate);
                        if (candidate_reason
                            != RollbackRouteSwitchReason::HysteresisHeld)
                        {
                            best_index = index;
                            best_statistics = candidate;
                            reason = candidate_reason;
                        }
                    }
                    if (best_index == current_index)
                        reason = RollbackRouteSwitchReason::HysteresisHeld;
                }
            }

            m_last_decision.previous = m_selected;
            m_last_decision.selected =
                static_cast<RollbackRouteKind>(best_index);
            m_last_decision.reason = reason;
            m_last_decision.switched = best_index != current_index;
            m_last_decision.decision_time_us = now;
            ++m_last_decision.decision_count;
            m_last_decision.selected_statistics = best_statistics;
            m_last_decision.candidate_statistics = candidate_statistics;
            if (best_index != current_index)
            {
                m_selected = static_cast<RollbackRouteKind>(best_index);
                m_last_switch_us = now;
                m_last_switch_decision = m_last_decision;
            }
        }

        void refresh(uint64_t now) noexcept
        {
            const RollbackUdpWorkerStatus direct =
                m_routes[0] ? m_routes[0]->status()
                            : RollbackUdpWorkerStatus {};
            const uint64_t generation = direct.handshake_generation;
            if (generation != m_session_generation)
            {
                m_session_generation = generation;
            }

            for (size_t index = 0; index < m_routes.size(); ++index)
            {
                IRollbackTransport* route = m_routes[index];
                RouteState& state = m_route_state[index];
                const RollbackUdpWorkerStatus transport =
                    route ? route->status() : RollbackUdpWorkerStatus {};
                if (transport.handshake_generation
                    != state.route_generation)
                {
                    state.route_generation =
                        transport.handshake_generation;
                    state.pending = {};
                    state.highest_sequence_valid = false;
                    state.available = false;
                    state.available_since_us = 0;
                    state.last_successful_receive_us = 0;
                    state.last_probe_us = 0;
                    state.rtt.clear();
                    state.loss.clear();
                    state.deadline_miss.clear();
                }
                const bool available = route && transport.running
                    && transport.endpoint_open
                    && transport.failure
                        != RollbackUdpWorkerFailure::RouteRejected;
                if (available && !state.available)
                    state.available_since_us = now;
                else if (!available)
                    state.available_since_us = 0;
                state.available = available;
                const uint64_t health_reference =
                    state.last_successful_receive_us != 0
                    ? state.last_successful_receive_us
                    : state.available_since_us;
                const bool recently_received =
                    health_reference != 0
                    && now - health_reference
                        <= static_cast<uint64_t>(
                            m_policy.health_timeout_ms) * 1000;
                const bool healthy = state.available
                    && transport.peer_ready
                    && transport.failure
                        == RollbackUdpWorkerFailure::None
                    && recently_received;
                if (state.health_observed && healthy != state.healthy)
                    ++state.health_changes;
                state.health_observed = true;
                state.healthy = healthy;
                expire_probes(state, now);
                const bool probe_capable = state.available
                    && transport.peer_ready
                    && transport.failure
                        == RollbackUdpWorkerFailure::None;
                if (probe_capable
                    && now - state.last_probe_us
                        >= static_cast<uint64_t>(
                            m_policy.probe_interval_ms) * 1000)
                {
                    send_probe(index, now);
                }
            }
            select_route(now);
        }

        void send_probe(size_t route_index, uint64_t now) noexcept
        {
            RouteState& state = m_route_state[route_index];
            PendingProbe* pending = nullptr;
            for (PendingProbe& candidate : state.pending)
            {
                if (!candidate.active)
                {
                    pending = &candidate;
                    break;
                }
            }
            if (!pending)
            {
                state.last_probe_us = now;
                return;
            }
            RollbackRouteProbePayload probe {};
            probe.probe_id = state.next_probe_id++;
            probe.logical_session_id = m_logical_session_id;
            probe.sender_time_us = now;
            if (m_routes[route_index]->enqueue(
                    RollbackProtocolV2PacketType::RouteProbe,
                    &probe,
                    sizeof(probe),
                    {},
                    state.route_generation))
            {
                pending->active = true;
                pending->id = probe.probe_id;
                pending->sent_us = now;
                ++state.probes_sent;
            }
            state.last_probe_us = now;
        }

        void expire_probes(RouteState& state, uint64_t now) noexcept
        {
            const uint64_t timeout =
                static_cast<uint64_t>(m_policy.probe_timeout_ms) * 1000;
            for (PendingProbe& pending : state.pending)
            {
                if (pending.active && now - pending.sent_us > timeout)
                {
                    pending = {};
                    note_probe_loss(state);
                }
            }
        }

        bool handle_probe(
            size_t route_index,
            const RollbackUdpMessage& message,
            uint64_t now) noexcept
        {
            if (message.packet_type != RollbackProtocolV2PacketType::RouteProbe
                && message.packet_type
                    != RollbackProtocolV2PacketType::RouteProbeAck)
            {
                return false;
            }
            if (message.payload_bytes != sizeof(RollbackRouteProbePayload))
                return true;
            RollbackRouteProbePayload probe {};
            std::memcpy(&probe, message.payload.data(), sizeof(probe));
            if (probe.magic != 0x42505248u || probe.version != 1
                || probe.reserved != 0
                || probe.logical_session_id != m_logical_session_id)
            {
                return true;
            }
            RouteState& state = m_route_state[route_index];
            state.last_successful_receive_us = now;
            if (message.packet_type
                == RollbackProtocolV2PacketType::RouteProbe)
            {
                ++state.probes_received;
                (void)m_routes[route_index]->enqueue(
                    RollbackProtocolV2PacketType::RouteProbeAck,
                    &probe,
                    sizeof(probe),
                    {},
                    state.route_generation);
                return true;
            }

            for (PendingProbe& pending : state.pending)
            {
                if (!pending.active || pending.id != probe.probe_id)
                    continue;
                const uint64_t rtt = now >= pending.sent_us
                    ? now - pending.sent_us : 0;
                pending = {};
                ++state.probes_acknowledged;
                state.rtt.push(rtt);
                state.loss.push(false);
                state.consecutive_loss_burst = 0;
                return true;
            }
            ++state.duplicates;
            return true;
        }

        bool unwrap_routed(
            const RollbackUdpMessage& wire,
            RollbackUdpMessage& logical) const noexcept
        {
            if (wire.packet_type
                    != RollbackProtocolV2PacketType::Routed
                || wire.payload_bytes
                    < sizeof(RollbackRoutedPayloadHeader))
            {
                return false;
            }
            RollbackRoutedPayloadHeader header {};
            std::memcpy(
                &header, wire.payload.data(), sizeof(header));
            const size_t expected_bytes =
                sizeof(header) + header.payload_bytes;
            if (header.magic != 0x54525248u
                || header.version != 1
                || header.header_bytes != sizeof(header)
                || (header.flags & ~RollbackProtocolV2FlagAckPresent) != 0
                || header.logical_session_id != m_logical_session_id
                || header.logical_sequence == 0
                || expected_bytes != wire.payload_bytes
                || !RollbackProtocolV2PacketTypeValid(header.packet_type)
                || header.packet_type
                    == RollbackProtocolV2PacketType::Hello
                || header.packet_type
                    == RollbackProtocolV2PacketType::HelloAck
                || header.packet_type
                    == RollbackProtocolV2PacketType::Heartbeat
                || header.packet_type
                    == RollbackProtocolV2PacketType::Routed
                || header.packet_type
                    == RollbackProtocolV2PacketType::RouteProbe
                || header.packet_type
                    == RollbackProtocolV2PacketType::RouteProbeAck)
            {
                return false;
            }
            logical = {};
            logical.packet_type = header.packet_type;
            logical.handshake_generation = m_session_generation;
            logical.sequence = header.logical_sequence;
            logical.sequence_assigned = true;
            if ((header.flags & RollbackProtocolV2FlagAckPresent) != 0)
            {
                logical.ack = RollbackSequenceStamp::From(
                    header.logical_ack_sequence);
            }
            else if (header.logical_ack_sequence != 0)
            {
                return false;
            }
            logical.payload_bytes = header.payload_bytes;
            if (header.payload_bytes != 0)
            {
                std::memcpy(
                    logical.payload.data(),
                    wire.payload.data() + sizeof(header),
                    header.payload_bytes);
            }
            return true;
        }

        void pump_inbound(uint64_t now) noexcept
        {
            for (size_t route_index = 0;
                 route_index < m_routes.size();
                 ++route_index)
            {
                IRollbackTransport* route = m_routes[route_index];
                if (!route) continue;
                RollbackUdpMessage message {};
                for (uint32_t count = 0;
                     count < 64 && route->dequeue(message);
                     ++count)
                {
                    RouteState& state = m_route_state[route_index];
                    if (message.handshake_generation
                            != state.route_generation
                        || !message.sequence_assigned)
                    {
                        continue;
                    }
                    if (handle_probe(route_index, message, now))
                        continue;

                    RollbackUdpMessage logical {};
                    if (!unwrap_routed(message, logical))
                        continue;
                    logical.route_index =
                        static_cast<uint8_t>(route_index);
                    logical.route_index_assigned = true;
                    state.last_successful_receive_us = now;
                    ++state.gameplay_packets_received;
                    if (state.highest_sequence_valid
                        && sequence_after(
                            state.highest_sequence, logical.sequence))
                    {
                        ++state.reordered;
                    }
                    if (!state.highest_sequence_valid
                        || sequence_after(
                            logical.sequence, state.highest_sequence))
                    {
                        state.highest_sequence = logical.sequence;
                        state.highest_sequence_valid = true;
                    }
                    if (!m_logical_replay.accept(logical.sequence))
                    {
                        ++state.duplicates;
                        continue;
                    }
                    if (!m_inbound.push(logical))
                    {
                        // Preserve bounded behavior. Surface an overflow via
                        // the mandatory direct worker's existing fail-closed
                        // queue status by dropping no more data into Gekko.
                        m_queue_overflow = true;
                        return;
                    }
                }
            }
        }

        void clear_deduplication() noexcept
        {
            m_logical_replay.clear();
        }

        static uint64_t compute_logical_session_id(
            const RollbackProductionConfig& config) noexcept
        {
            uint64_t hash = 1469598103934665603ull;
            const auto mix = [&hash](const void* bytes, size_t count)
            {
                const auto* data = static_cast<const uint8_t*>(bytes);
                for (size_t index = 0; index < count; ++index)
                {
                    hash ^= data[index];
                    hash *= 1099511628211ull;
                }
            };
            mix(config.secret.data(), config.secret.size());
            mix(&config.expected_build_id, sizeof(config.expected_build_id));
            mix(&config.expected_schema_id, sizeof(config.expected_schema_id));
            mix(
                &config.expected_native_stage_identity,
                sizeof(config.expected_native_stage_identity));
            const uint8_t first_peer =
                (std::min)(config.local_peer, config.remote_peer);
            const uint8_t second_peer =
                (std::max)(config.local_peer, config.remote_peer);
            mix(&first_peer, sizeof(first_peer));
            mix(&second_peer, sizeof(second_peer));
            return hash != 0 ? hash : 1;
        }

        uint64_t route_expected_generation(size_t index) const noexcept
        {
            return m_route_state[index].route_generation != 0
                ? m_route_state[index].route_generation
                : UINT64_MAX;
        }

        uint64_t next_logical_sequence() noexcept
        {
            uint64_t sequence = m_next_logical_sequence++;
            if (sequence == 0)
            {
                sequence = m_next_logical_sequence++;
            }
            return sequence;
        }

        RollbackSteadyRouteClock m_default_clock {};
        IRollbackRouteClock* m_clock {nullptr};
        std::array<IRollbackTransport*, kRouteCount> m_routes {};
        std::array<RouteState, kRouteCount> m_route_state {};
        RollbackRouteSelectionPolicy m_policy {};
        bool m_started {false};
        bool m_queue_overflow {false};
        uint64_t m_session_generation {0};
        uint64_t m_logical_session_id {0};
        uint64_t m_next_logical_sequence {1};
        uint64_t m_last_switch_us {0};
        RollbackRouteKind m_selected {RollbackRouteKind::DirectUdp};
        RollbackRouteDecision m_last_decision {};
        RollbackRouteDecision m_last_switch_decision {};
        LogicalReplayWindow m_logical_replay {};
        FixedQueue<RollbackUdpMessage, kInboundCount> m_inbound {};
    };
}
