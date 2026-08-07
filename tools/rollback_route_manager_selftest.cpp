#include "../HorseMod/horselib/RollbackRouteManager.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <thread>

namespace
{
    class FakeClock final : public Horse::IRollbackRouteClock
    {
    public:
        uint64_t now_microseconds() const noexcept override { return now; }
        void advance_ms(uint64_t milliseconds) noexcept
        {
            now += milliseconds * 1000;
        }

        uint64_t now {1};
    };

    class FakeTransport final : public Horse::IRollbackTransport
    {
    public:
        using MessageArray =
            std::array<Horse::RollbackUdpMessage, 300>;

        FakeTransport()
            : inbound(std::make_unique<MessageArray>()),
              outbound(std::make_unique<MessageArray>())
        {
        }

        bool start(
            const Horse::RollbackProductionConfig&) noexcept override
        {
            ++start_calls;
            if (!start_allowed) return false;
            running = true;
            endpoint_open = true;
            ready = ready_on_start;
            return true;
        }

        void stop() noexcept override
        {
            ++stop_calls;
            running = false;
            endpoint_open = false;
            ready = false;
            inbound_count = 0;
            inbound_head = 0;
            outbound_count = 0;
            pending_callbacks = 0;
        }

        bool enqueue(
            Horse::RollbackProtocolV2PacketType type,
            const void* payload,
            uint16_t payload_bytes,
            Horse::RollbackSequenceStamp ack = {},
            uint64_t expected_generation = UINT64_MAX) noexcept
            override
        {
            if (!running || !ready || fail_next_enqueue
                || outbound_count == outbound->size()
                || (expected_generation != UINT64_MAX
                    && expected_generation != generation))
            {
                fail_next_enqueue = false;
                return false;
            }
            Horse::RollbackUdpMessage& message =
                (*outbound)[outbound_count++];
            message = {};
            message.packet_type = type;
            message.ack = ack;
            message.handshake_generation = generation;
            message.sequence = next_sequence++;
            message.sequence_assigned = true;
            message.payload_bytes = payload_bytes;
            if (payload_bytes != 0)
                std::memcpy(message.payload.data(), payload, payload_bytes);
            return true;
        }

        bool dequeue(Horse::RollbackUdpMessage& message) noexcept override
        {
            if (inbound_head == inbound_count) return false;
            message = (*inbound)[inbound_head++];
            if (inbound_head == inbound_count)
            {
                inbound_head = 0;
                inbound_count = 0;
            }
            return true;
        }

        bool peer_ready() const noexcept override { return ready; }

        Horse::RollbackUdpWorkerStatus status() const noexcept override
        {
            Horse::RollbackUdpWorkerStatus out {};
            out.running = running;
            out.endpoint_open = endpoint_open;
            out.peer_ready = ready;
            out.endpoint_pinned = ready;
            out.handshake_generation = generation;
            out.failure = failure;
            return out;
        }

        bool inject(
            Horse::RollbackProtocolV2PacketType type,
            uint64_t sequence,
            uint64_t message_generation = 1,
            const void* payload = nullptr,
            uint16_t payload_bytes = 0) noexcept
        {
            if (inbound_count == inbound->size()) return false;
            Horse::RollbackUdpMessage& message =
                (*inbound)[inbound_count++];
            message = {};
            message.packet_type = type;
            message.handshake_generation = message_generation;
            message.sequence = sequence;
            message.sequence_assigned = true;
            message.payload_bytes = payload_bytes;
            if (payload_bytes != 0)
                std::memcpy(message.payload.data(), payload, payload_bytes);
            return true;
        }

        void set_next_sequence(uint64_t value) noexcept
        {
            next_sequence = value;
        }

        bool start_allowed {true};
        bool ready_on_start {true};
        bool fail_next_enqueue {false};
        bool running {false};
        bool endpoint_open {false};
        bool ready {true};
        uint64_t generation {1};
        uint64_t next_sequence {1};
        uint32_t start_calls {0};
        uint32_t stop_calls {0};
        uint32_t pending_callbacks {0};
        Horse::RollbackUdpWorkerFailure failure {
            Horse::RollbackUdpWorkerFailure::None};
        std::unique_ptr<MessageArray> inbound;
        size_t inbound_count {0};
        size_t inbound_head {0};
        std::unique_ptr<MessageArray> outbound;
        size_t outbound_count {0};
    };

    Horse::RollbackProductionConfig config()
    {
        Horse::RollbackProductionConfig out {};
        out.enabled = true;
        out.bind_address = "127.0.0.1";
        out.bind_port = 42000;
        out.peer_address = "127.0.0.1";
        out.peer_port = 42001;
        out.local_player_slot = 0;
        out.native_input_source_slot = 0;
        out.local_peer = 1;
        out.remote_peer = 2;
        out.secret = "route-manager-selftest-secret";
        out.rollback_window = 12;
        out.input_delay = 1;
        out.expected_build_id = 1;
        out.expected_schema_id = 2;
        out.expected_native_stage_identity = 3;
        return out;
    }

    Horse::RollbackRouteSelectionPolicy policy()
    {
        Horse::RollbackRouteSelectionPolicy out {};
        out.probe_interval_ms = 1000000000u;
        out.probe_timeout_ms = 1000000000u;
        out.health_timeout_ms = 1000000000u;
        out.minimum_samples = 8;
        out.minimum_dwell_ms = 1000;
        out.minimum_latency_improvement_us = 3000;
        out.minimum_relative_improvement_per_mille = 100;
        out.minimum_deadline_improvement_per_mille = 10;
        return out;
    }

    void samples(
        Horse::RollbackRouteManagerTransport& manager,
        Horse::RollbackRouteKind route,
        const std::array<uint64_t, 8>& values)
    {
        for (uint64_t value : values)
            manager.observe_probe_result(route, value, true);
    }

    bool inject_logical(
        FakeTransport& transport,
        uint64_t logical_session_id,
        Horse::RollbackProtocolV2PacketType type,
        uint64_t logical_sequence,
        uint64_t wire_sequence,
        uint64_t route_generation = UINT64_MAX) noexcept
    {
        Horse::RollbackRoutedPayloadHeader header {};
        header.packet_type = type;
        header.logical_session_id = logical_session_id;
        header.logical_sequence = logical_sequence;
        std::array<uint8_t, sizeof(header)> payload {};
        std::memcpy(payload.data(), &header, sizeof(header));
        return transport.inject(
            Horse::RollbackProtocolV2PacketType::Routed,
            wire_sequence,
            route_generation == UINT64_MAX
                ? transport.generation : route_generation,
            payload.data(),
            static_cast<uint16_t>(payload.size()));
    }

    void relay_new_messages(
        FakeTransport& source,
        FakeTransport& destination,
        size_t& cursor,
        uint64_t& destination_wire_sequence) noexcept
    {
        while (cursor < source.outbound_count)
        {
            const Horse::RollbackUdpMessage& message =
                (*source.outbound)[cursor++];
            destination.inject(
                message.packet_type,
                destination_wire_sequence++,
                destination.generation,
                message.payload.data(),
                message.payload_bytes);
        }
    }

    struct Report
    {
        bool direct_only {false};
        bool payload_boundary {false};
        bool authenticated_wire {false};
        bool steam_late {false};
        bool steam_unavailable {false};
        bool lower_latency {false};
        bool consistency {false};
        bool dwell {false};
        bool hysteresis {false};
        bool failure_recovery {false};
        bool best_failover {false};
        bool duplicate {false};
        bool delayed_duplicate {false};
        bool reorder {false};
        bool loss_burst {false};
        bool stale_session {false};
        bool wraparound {false};
        bool independent_reconnect {false};
        bool deadline_window_reset {false};
        bool deadline_attribution {false};
        bool deadline_priority_selection {false};
        bool probe_recovery {false};
        bool metric_safety {false};
        bool rolling_recovery {false};
        bool racing_fallback {false};
        bool racing_two_route_limit {false};
        bool queue_pressure {false};
        bool shutdown {false};

        bool ok() const noexcept
        {
            return direct_only && payload_boundary && authenticated_wire
                && steam_late && steam_unavailable
                && lower_latency && consistency && dwell && hysteresis
                && failure_recovery && best_failover
                && duplicate && delayed_duplicate
                && reorder && loss_burst
                && stale_session && wraparound
                && independent_reconnect && deadline_window_reset
                && deadline_attribution && deadline_priority_selection
                && probe_recovery
                && metric_safety && rolling_recovery
                && racing_fallback && racing_two_route_limit
                && queue_pressure && shutdown;
        }
    };

    Report run()
    {
        Report report {};
        const Horse::RollbackProductionConfig cfg = config();

        {
            Horse::RollbackRouteDeadlineTracker tracker;
            Horse::RollbackRouteKind attributed =
                Horse::RollbackRouteKind::DirectUdp;
            bool missed = false;
            tracker.note_gameplay_receive(
                Horse::RollbackRouteKind::SteamNetworkingSockets);
            const bool rollback_has_no_sample = !tracker.observe_advance(
                true,
                Horse::RollbackRouteKind::SteamNetworkingSockets,
                attributed, missed);
            const bool miss_stays_with_winning_route =
                tracker.observe_advance(
                    false,
                    Horse::RollbackRouteKind::CloudflareTurn,
                    attributed, missed)
                && missed
                && attributed
                    == Horse::RollbackRouteKind::
                        SteamNetworkingSockets;
            tracker.note_gameplay_receive(
                Horse::RollbackRouteKind::CloudflareTurn);
            const bool success_credits_winning_route =
                tracker.observe_advance(
                    false,
                    Horse::RollbackRouteKind::
                        SteamNetworkingSockets,
                    attributed, missed)
                && !missed
                && attributed
                    == Horse::RollbackRouteKind::CloudflareTurn;
            report.deadline_attribution =
                rollback_has_no_sample
                && miss_stays_with_winning_route
                && success_credits_winning_route;
        }

        {
            FakeClock clock;
            FakeTransport direct;
            Horse::RollbackRouteManagerTransport manager(direct, &clock);
            manager.set_policy(policy());
            report.direct_only = manager.start(cfg)
                && manager.selected_route()
                    == Horse::RollbackRouteKind::DirectUdp
                && manager.peer_ready();
            std::array<uint8_t,
                Horse::kRollbackRoutedMaxPayloadBytes> maximum_payload {};
            std::array<uint8_t,
                Horse::kRollbackRoutedMaxPayloadBytes + 1>
                oversized_payload {};
            const bool maximum_accepted = manager.enqueue(
                Horse::RollbackProtocolV2PacketType::Gekko,
                maximum_payload.data(),
                static_cast<uint16_t>(maximum_payload.size()));
            const bool exact_wire_size = direct.outbound_count == 1
                && (*direct.outbound)[0].payload_bytes
                    == Horse::kRollbackProtocolV2MaxPayloadBytes;
            const bool oversized_rejected = !manager.enqueue(
                Horse::RollbackProtocolV2PacketType::Gekko,
                oversized_payload.data(),
                static_cast<uint16_t>(oversized_payload.size()));
            report.payload_boundary = maximum_accepted
                && exact_wire_size && oversized_rejected;
            manager.stop();
        }

        {
            Horse::RollbackProductionConfig wire_a = cfg;
            wire_a.bind_port = 65380;
            wire_a.peer_port = 65381;
            Horse::RollbackProductionConfig wire_b = wire_a;
            wire_b.bind_port = wire_a.peer_port;
            wire_b.peer_port = wire_a.bind_port;
            wire_b.local_peer = wire_a.remote_peer;
            wire_b.remote_peer = wire_a.local_peer;
            wire_b.local_player_slot = 1;
            wire_b.native_input_source_slot = 1;
            Horse::RollbackUdpNetworkWorker direct_a;
            Horse::RollbackUdpNetworkWorker direct_b;
            Horse::RollbackRouteManagerTransport manager_a(direct_a);
            Horse::RollbackRouteManagerTransport manager_b(direct_b);
            const bool started =
                manager_a.start(wire_a) && manager_b.start(wire_b);
            const auto ready_deadline =
                std::chrono::steady_clock::now()
                + std::chrono::seconds(3);
            while (started
                && std::chrono::steady_clock::now() < ready_deadline
                && (!manager_a.peer_ready()
                    || !manager_b.peer_ready()))
            {
                manager_a.service();
                manager_b.service();
                std::this_thread::yield();
            }
            const uint32_t payload = 0xA55A1234u;
            const bool queued = started
                && manager_a.peer_ready()
                && manager_b.peer_ready()
                && manager_a.enqueue(
                    Horse::RollbackProtocolV2PacketType::Gekko,
                    &payload,
                    sizeof(payload),
                    Horse::RollbackSequenceStamp::From(77),
                    manager_a.status().handshake_generation);
            Horse::RollbackUdpMessage received {};
            bool delivered = false;
            const auto delivery_deadline =
                std::chrono::steady_clock::now()
                + std::chrono::seconds(2);
            while (queued
                && std::chrono::steady_clock::now() < delivery_deadline)
            {
                manager_a.service();
                manager_b.service();
                if (manager_b.dequeue(received))
                {
                    delivered = received.packet_type
                            == Horse::RollbackProtocolV2PacketType::Gekko
                        && received.payload_bytes == sizeof(payload)
                        && std::memcmp(
                            received.payload.data(),
                            &payload,
                            sizeof(payload)) == 0
                        && received.ack.valid
                        && received.ack.value == 77
                        && received.sequence == 1;
                    break;
                }
                std::this_thread::yield();
            }
            manager_a.stop();
            manager_b.stop();
            report.authenticated_wire = started && queued && delivered
                && !direct_a.status().running
                && !direct_b.status().running;
            if (!report.authenticated_wire)
            {
                std::printf(
                    "authenticated wire detail started=%d queued=%d "
                    "delivered=%d type=%u bytes=%u ack=%d ack_value=%llu "
                    "sequence=%llu\n",
                    started, queued, delivered,
                    static_cast<unsigned>(received.packet_type),
                    received.payload_bytes, received.ack.valid,
                    static_cast<unsigned long long>(received.ack.value),
                    static_cast<unsigned long long>(received.sequence));
            }
        }

        {
            FakeClock clock;
            FakeTransport direct;
            FakeTransport steam;
            direct.generation = 3;
            steam.generation = 9;
            Horse::RollbackRouteManagerTransport manager(direct, &clock);
            manager.set_policy(policy());
            manager.set_optional_route(
                Horse::RollbackRouteKind::SteamNetworkingSockets, &steam);
            manager.start(cfg);
            const uint64_t logical_session =
                manager.logical_session_id_for_test();
            inject_logical(steam, logical_session,
                Horse::RollbackProtocolV2PacketType::Gekko, 1, 100, 9);
            manager.service();
            Horse::RollbackUdpMessage message {};
            const bool initial = manager.dequeue(message);

            steam.generation = 10;
            manager.service();
            for (uint32_t sample = 0; sample < 8; ++sample)
            {
                manager.observe_input_deadline(
                    Horse::RollbackRouteKind::SteamNetworkingSockets,
                    sample == 0);
            }
            steam.generation = 11;
            manager.service();
            const auto reset_statistics = manager.route_statistics(
                Horse::RollbackRouteKind::SteamNetworkingSockets);
            report.deadline_window_reset =
                reset_statistics.input_deadline_samples == 8
                && reset_statistics.input_deadline_misses == 1
                && reset_statistics.deadline_window_samples == 0
                && reset_statistics.deadline_miss_per_mille == 0;
            steam.generation = 10;
            manager.service();
            inject_logical(steam, logical_session,
                Horse::RollbackProtocolV2PacketType::Gekko, 2, 101, 9);
            inject_logical(steam, logical_session,
                Horse::RollbackProtocolV2PacketType::Gekko, 3, 102, 10);
            manager.service();
            const bool after_steam_restart = manager.dequeue(message)
                && message.sequence == 3
                && !manager.dequeue(message);

            direct.generation = 4;
            manager.service();
            inject_logical(steam, logical_session,
                Horse::RollbackProtocolV2PacketType::Gekko, 4, 103, 10);
            manager.service();
            const bool after_direct_restart = manager.dequeue(message)
                && message.sequence == 4
                && manager.status().handshake_generation == 4;
            report.independent_reconnect =
                initial && after_steam_restart && after_direct_restart;
            manager.stop();
        }

        {
            FakeClock clock;
            FakeTransport direct;
            FakeTransport steam;
            Horse::RollbackRouteSelectionPolicy deadline_policy = policy();
            deadline_policy.minimum_dwell_ms = 0;
            Horse::RollbackRouteManagerTransport manager(direct, &clock);
            manager.set_policy(deadline_policy);
            manager.set_optional_route(
                Horse::RollbackRouteKind::SteamP2P, &steam);
            manager.start(cfg);
            // Direct wins every latency metric but misses half of its
            // observed rollback input deadlines. Steam is slower but meets
            // all of them; deadline outcome must be the deciding metric.
            samples(manager, Horse::RollbackRouteKind::DirectUdp,
                {5000, 5000, 5000, 5000,
                 5000, 5000, 5000, 5000});
            samples(manager, Horse::RollbackRouteKind::SteamP2P,
                {20000, 20000, 20000, 20000,
                 20000, 20000, 20000, 20000});
            for (uint32_t sample = 0; sample < 8; ++sample)
            {
                manager.observe_input_deadline(
                    Horse::RollbackRouteKind::DirectUdp,
                    (sample & 1u) == 0);
                manager.observe_input_deadline(
                    Horse::RollbackRouteKind::SteamP2P, false);
            }
            manager.service();
            report.deadline_priority_selection =
                manager.selected_route()
                    == Horse::RollbackRouteKind::SteamP2P
                && manager.last_switch_decision().reason
                    == Horse::RollbackRouteSwitchReason::
                        LowerDeadlineMissRate;
            manager.stop();
        }

        {
            FakeClock clock;
            FakeTransport direct;
            FakeTransport steam;
            FakeTransport turn;
            Horse::RollbackRouteSelectionPolicy failover_policy = policy();
            failover_policy.minimum_dwell_ms = 0;
            Horse::RollbackRouteManagerTransport manager(direct, &clock);
            manager.set_policy(failover_policy);
            manager.set_optional_route(
                Horse::RollbackRouteKind::SteamNetworkingSockets, &steam);
            manager.set_optional_route(
                Horse::RollbackRouteKind::CloudflareTurn, &turn);
            manager.start(cfg);
            samples(manager,
                Horse::RollbackRouteKind::SteamNetworkingSockets,
                {10000, 10000, 10000, 10000,
                 10000, 10000, 10000, 10000});
            manager.observe_probe_result(
                Horse::RollbackRouteKind::SteamNetworkingSockets,
                0, false);
            samples(manager, Horse::RollbackRouteKind::CloudflareTurn,
                {10000, 10000, 10000, 10000,
                 10000, 10000, 10000, 10000});
            direct.ready = false;
            direct.failure =
                Horse::RollbackUdpWorkerFailure::PeerTimeout;
            manager.service();
            report.best_failover = manager.selected_route()
                == Horse::RollbackRouteKind::CloudflareTurn
                && manager.last_switch_decision().reason
                    == Horse::RollbackRouteSwitchReason::
                        PrimaryUnhealthy;
            manager.stop();
        }

        {
            FakeClock clock_a;
            FakeClock clock_b;
            FakeTransport direct_a;
            FakeTransport direct_b;
            direct_a.generation = 7;
            direct_b.generation = 13;
            Horse::RollbackRouteSelectionPolicy probe_policy = policy();
            probe_policy.probe_interval_ms = 100;
            probe_policy.probe_timeout_ms = 200;
            probe_policy.health_timeout_ms = 250;
            probe_policy.minimum_samples = 1;
            probe_policy.minimum_dwell_ms = 0;
            Horse::RollbackProductionConfig cfg_b = cfg;
            cfg_b.local_peer = cfg.remote_peer;
            cfg_b.remote_peer = cfg.local_peer;
            Horse::RollbackRouteManagerTransport manager_a(
                direct_a, &clock_a);
            Horse::RollbackRouteManagerTransport manager_b(
                direct_b, &clock_b);
            manager_a.set_policy(probe_policy);
            manager_b.set_policy(probe_policy);
            manager_a.start(cfg);
            manager_b.start(cfg_b);
            size_t a_cursor = 0;
            size_t b_cursor = 0;
            uint64_t a_wire = 1000;
            uint64_t b_wire = 2000;

            clock_a.advance_ms(101);
            clock_b.advance_ms(101);
            manager_a.service();
            manager_b.service();
            relay_new_messages(
                direct_a, direct_b, a_cursor, b_wire);
            manager_b.service();
            relay_new_messages(
                direct_b, direct_a, b_cursor, a_wire);
            manager_a.service();
            const uint64_t first_acks =
                manager_a.route_statistics(
                    Horse::RollbackRouteKind::DirectUdp)
                    .probes_acknowledged;

            clock_a.advance_ms(300);
            clock_b.advance_ms(300);
            manager_a.service();
            const bool became_unhealthy =
                !manager_a.route_statistics(
                    Horse::RollbackRouteKind::DirectUdp).healthy;
            manager_b.service();
            relay_new_messages(
                direct_a, direct_b, a_cursor, b_wire);
            manager_b.service();
            relay_new_messages(
                direct_b, direct_a, b_cursor, a_wire);
            manager_a.service();
            manager_a.service();
            const auto recovered =
                manager_a.route_statistics(
                    Horse::RollbackRouteKind::DirectUdp);
            report.probe_recovery = first_acks != 0
                && became_unhealthy
                && recovered.healthy
                && recovered.probes_acknowledged > first_acks;
            manager_a.stop();
            manager_b.stop();
        }

        {
            FakeClock clock;
            FakeTransport direct;
            FakeTransport steam;
            Horse::RollbackRouteManagerTransport manager(direct, &clock);
            manager.set_policy(policy());
            manager.set_optional_route(
                Horse::RollbackRouteKind::SteamNetworkingSockets, &steam);
            manager.start(cfg);
            samples(manager, Horse::RollbackRouteKind::DirectUdp,
                {20000, 20000, 20000, 20000,
                 20000, 20000, 20000, 20000});
            samples(manager,
                Horse::RollbackRouteKind::SteamNetworkingSockets,
                {10000, 10000, 10000, 10000,
                 10000, 10000, 10000, 10000});
            for (uint32_t index = 0; index < 4; ++index)
            {
                manager.observe_probe_result(
                    Horse::RollbackRouteKind::SteamNetworkingSockets,
                    0, false);
            }
            clock.advance_ms(1100);
            manager.service();
            report.metric_safety = manager.selected_route()
                == Horse::RollbackRouteKind::DirectUdp;
            for (uint32_t index = 0; index < 128; ++index)
            {
                manager.observe_probe_result(
                    Horse::RollbackRouteKind::SteamNetworkingSockets,
                    10000, true);
            }
            clock.advance_ms(1100);
            manager.service();
            const auto recovered =
                manager.route_statistics(
                    Horse::RollbackRouteKind::SteamNetworkingSockets);
            report.rolling_recovery =
                recovered.estimated_loss_per_mille == 0
                && recovered.maximum_loss_burst == 0
                && manager.selected_route()
                    == Horse::RollbackRouteKind::SteamNetworkingSockets;
            manager.stop();
        }

        {
            FakeClock clock;
            FakeTransport direct;
            FakeTransport steam;
            Horse::RollbackRouteSelectionPolicy racing_policy = policy();
            racing_policy.minimum_dwell_ms = 0;
            racing_policy.race_critical_packets = true;
            Horse::RollbackRouteManagerTransport manager(direct, &clock);
            manager.set_policy(racing_policy);
            manager.set_optional_route(
                Horse::RollbackRouteKind::SteamNetworkingSockets, &steam);
            manager.start(cfg);
            samples(manager, Horse::RollbackRouteKind::DirectUdp,
                {40000, 40000, 40000, 40000,
                 40000, 40000, 40000, 40000});
            samples(manager,
                Horse::RollbackRouteKind::SteamNetworkingSockets,
                {10000, 10000, 10000, 10000,
                 10000, 10000, 10000, 10000});
            manager.service();
            const size_t direct_before = direct.outbound_count;
            const size_t steam_before = steam.outbound_count;
            steam.fail_next_enqueue = true;
            const uint32_t input = 0x12345678u;
            const bool sent = manager.enqueue(
                Horse::RollbackProtocolV2PacketType::Gekko,
                &input, sizeof(input));
            report.racing_fallback = sent
                && direct.outbound_count == direct_before + 1
                && steam.outbound_count <= steam_before + 1;
            manager.stop();
        }

        {
            FakeClock clock;
            FakeTransport direct;
            FakeTransport steam;
            FakeTransport turn;
            Horse::RollbackRouteSelectionPolicy racing_policy = policy();
            racing_policy.minimum_dwell_ms = 0;
            racing_policy.race_critical_packets = true;
            Horse::RollbackRouteManagerTransport manager(direct, &clock);
            manager.set_policy(racing_policy);
            manager.set_optional_route(
                Horse::RollbackRouteKind::SteamP2P, &steam);
            manager.set_optional_route(
                Horse::RollbackRouteKind::CloudflareTurn, &turn);
            manager.start(cfg);
            samples(manager, Horse::RollbackRouteKind::DirectUdp,
                {40000, 40000, 40000, 40000,
                 40000, 40000, 40000, 40000});
            samples(manager, Horse::RollbackRouteKind::SteamP2P,
                {10000, 10000, 10000, 10000,
                 10000, 10000, 10000, 10000});
            samples(manager, Horse::RollbackRouteKind::CloudflareTurn,
                {20000, 20000, 20000, 20000,
                 20000, 20000, 20000, 20000});
            manager.service();
            const size_t direct_before = direct.outbound_count;
            const size_t steam_before = steam.outbound_count;
            const size_t turn_before = turn.outbound_count;
            const uint32_t input = 0x89ABCDEFu;
            const bool sent = manager.enqueue(
                Horse::RollbackProtocolV2PacketType::Gekko,
                &input, sizeof(input));
            const size_t direct_delta =
                direct.outbound_count - direct_before;
            const size_t steam_delta =
                steam.outbound_count - steam_before;
            const size_t turn_delta =
                turn.outbound_count - turn_before;
            report.racing_two_route_limit = sent
                && manager.selected_route()
                    == Horse::RollbackRouteKind::SteamP2P
                && steam_delta == 1
                && turn_delta == 1
                && direct_delta == 0;
            manager.stop();
        }

        {
            FakeClock clock;
            FakeTransport direct;
            Horse::RollbackRouteManagerTransport manager(direct, &clock);
            manager.set_policy(policy());
            manager.start(cfg);
            const uint64_t logical_session =
                manager.logical_session_id_for_test();
            for (uint64_t sequence = 1; sequence <= 270; ++sequence)
            {
                inject_logical(direct, logical_session,
                    Horse::RollbackProtocolV2PacketType::Gekko,
                    sequence, sequence + 1000);
            }
            for (uint32_t pump = 0; pump < 5; ++pump)
                manager.service();
            report.queue_pressure = manager.status().failure
                    == Horse::RollbackUdpWorkerFailure::QueueOverflow
                && !manager.peer_ready();
            manager.stop();
        }

        {
            FakeClock clock;
            FakeTransport direct;
            FakeTransport steam;
            steam.ready_on_start = false;
            Horse::RollbackRouteManagerTransport manager(direct, &clock);
            manager.set_policy(policy());
            manager.set_optional_route(
                Horse::RollbackRouteKind::SteamNetworkingSockets, &steam);
            const bool started = manager.start(cfg);
            const bool stayed_direct =
                manager.selected_route()
                    == Horse::RollbackRouteKind::DirectUdp;
            steam.ready = true;
            samples(manager, Horse::RollbackRouteKind::DirectUdp,
                {30000, 30000, 30000, 30000, 30000, 30000, 30000, 30000});
            samples(manager,
                Horse::RollbackRouteKind::SteamNetworkingSockets,
                {10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000});
            clock.advance_ms(1100);
            manager.service();
            report.steam_late = started && stayed_direct
                && manager.selected_route()
                    == Horse::RollbackRouteKind::SteamNetworkingSockets;
            manager.stop();
        }

        {
            FakeClock clock;
            FakeTransport direct;
            Horse::RollbackUnavailableSteamTransport steam;
            Horse::RollbackRouteManagerTransport manager(direct, &clock);
            manager.set_policy(policy());
            manager.set_optional_route(
                Horse::RollbackRouteKind::SteamNetworkingSockets, &steam);
            report.steam_unavailable = manager.start(cfg)
                && manager.selected_route()
                    == Horse::RollbackRouteKind::DirectUdp
                && direct.running
                && steam.start_attempted()
                && steam.status().failure
                    == Horse::RollbackUdpWorkerFailure::RouteRejected;
            manager.stop();
        }

        {
            FakeClock clock;
            FakeTransport direct;
            FakeTransport steam;
            Horse::RollbackRouteManagerTransport manager(direct, &clock);
            manager.set_policy(policy());
            manager.set_optional_route(
                Horse::RollbackRouteKind::SteamNetworkingSockets, &steam);
            manager.start(cfg);
            samples(manager, Horse::RollbackRouteKind::DirectUdp,
                {40000, 40000, 40000, 40000, 40000, 40000, 40000, 40000});
            samples(manager,
                Horse::RollbackRouteKind::SteamNetworkingSockets,
                {18000, 18000, 18000, 18000, 18000, 18000, 18000, 18000});
            manager.service();
            report.dwell = manager.selected_route()
                == Horse::RollbackRouteKind::DirectUdp;
            clock.advance_ms(1100);
            manager.service();
            report.lower_latency = manager.selected_route()
                == Horse::RollbackRouteKind::SteamNetworkingSockets;
            manager.stop();
        }

        {
            FakeClock clock;
            FakeTransport direct;
            FakeTransport steam;
            Horse::RollbackRouteManagerTransport manager(direct, &clock);
            manager.set_policy(policy());
            manager.set_optional_route(
                Horse::RollbackRouteKind::SteamNetworkingSockets, &steam);
            manager.start(cfg);
            samples(manager, Horse::RollbackRouteKind::DirectUdp,
                {10000, 10000, 10000, 10000, 10000, 10000, 50000, 80000});
            samples(manager,
                Horse::RollbackRouteKind::SteamNetworkingSockets,
                {12000, 12000, 12000, 12000, 12000, 12000, 12000, 12000});
            clock.advance_ms(1100);
            manager.service();
            report.consistency = manager.selected_route()
                == Horse::RollbackRouteKind::SteamNetworkingSockets;

            samples(manager, Horse::RollbackRouteKind::DirectUdp,
                {11500, 11500, 11500, 11500, 11500, 11500, 11500, 11500});
            clock.advance_ms(1100);
            manager.service();
            report.hysteresis = manager.selected_route()
                == Horse::RollbackRouteKind::SteamNetworkingSockets
                && !manager.last_decision().switched;

            steam.failure = Horse::RollbackUdpWorkerFailure::PeerTimeout;
            steam.ready = false;
            manager.service();
            const bool failed_over = manager.selected_route()
                == Horse::RollbackRouteKind::DirectUdp;
            steam.failure = Horse::RollbackUdpWorkerFailure::None;
            steam.ready = true;
            samples(manager,
                Horse::RollbackRouteKind::SteamNetworkingSockets,
                {8000, 8000, 8000, 8000, 8000, 8000, 8000, 8000});
            clock.advance_ms(1100);
            manager.service();
            report.failure_recovery = failed_over
                && manager.selected_route()
                    == Horse::RollbackRouteKind::SteamNetworkingSockets
                && manager.route_statistics(
                    Horse::RollbackRouteKind::SteamNetworkingSockets)
                    .health_changes >= 2;
            manager.stop();
        }

        {
            FakeClock clock;
            FakeTransport direct;
            FakeTransport steam;
            Horse::RollbackRouteManagerTransport manager(direct, &clock);
            manager.set_policy(policy());
            manager.set_optional_route(
                Horse::RollbackRouteKind::SteamNetworkingSockets, &steam);
            manager.start(cfg);
            const uint64_t logical_session =
                manager.logical_session_id_for_test();

            inject_logical(direct, logical_session,
                Horse::RollbackProtocolV2PacketType::Gekko, 42, 1);
            inject_logical(steam, logical_session,
                Horse::RollbackProtocolV2PacketType::Gekko, 42, 500);
            manager.service();
            Horse::RollbackUdpMessage message {};
            const bool first = manager.dequeue(message);
            const bool second = manager.dequeue(message);
            report.duplicate = first && !second
                && manager.route_statistics(
                    Horse::RollbackRouteKind::SteamNetworkingSockets)
                    .duplicates == 1;

            inject_logical(direct, logical_session,
                Horse::RollbackProtocolV2PacketType::Gekko, 100, 2);
            inject_logical(direct, logical_session,
                Horse::RollbackProtocolV2PacketType::Gekko, 99, 3);
            manager.service();
            const bool reorder_first = manager.dequeue(message);
            const bool reorder_second = manager.dequeue(message);
            report.reorder = reorder_first && reorder_second
                && manager.route_statistics(
                    Horse::RollbackRouteKind::DirectUdp).reordered == 1;

            inject_logical(direct, logical_session + 1,
                Horse::RollbackProtocolV2PacketType::Gekko, 101, 4);
            manager.service();
            report.stale_session = !manager.dequeue(message);

            manager.observe_probe_result(
                Horse::RollbackRouteKind::DirectUdp, 0, false);
            manager.observe_probe_result(
                Horse::RollbackRouteKind::DirectUdp, 0, false);
            manager.observe_probe_result(
                Horse::RollbackRouteKind::DirectUdp, 0, false);
            const Horse::RollbackRouteStatistics loss =
                manager.route_statistics(
                    Horse::RollbackRouteKind::DirectUdp);
            report.loss_burst = loss.consecutive_loss_burst == 3
                && loss.maximum_loss_burst >= 3
                && loss.estimated_loss_per_mille != 0;
            manager.stop();
        }

        {
            FakeClock clock;
            FakeTransport direct;
            Horse::RollbackRouteManagerTransport manager(direct, &clock);
            manager.set_policy(policy());
            manager.start(cfg);
            const uint64_t logical_session =
                manager.logical_session_id_for_test();
            inject_logical(direct, logical_session,
                Horse::RollbackProtocolV2PacketType::Gekko, 1, 1);
            manager.service();
            Horse::RollbackUdpMessage message {};
            const bool first = manager.dequeue(message);
            uint64_t wire_sequence = 2;
            for (uint64_t base = 2; base <= 4200; base += 250)
            {
                const uint64_t end = (std::min<uint64_t>)(
                    4200, base + 249);
                for (uint64_t sequence = base;
                     sequence <= end; ++sequence)
                {
                    inject_logical(direct, logical_session,
                        Horse::RollbackProtocolV2PacketType::Gekko,
                        sequence, wire_sequence++);
                }
                for (uint32_t pump = 0; pump < 4; ++pump)
                {
                    manager.service();
                    while (manager.dequeue(message))
                    {
                    }
                }
            }
            inject_logical(direct, logical_session,
                Horse::RollbackProtocolV2PacketType::Gekko,
                1, wire_sequence);
            manager.service();
            report.delayed_duplicate =
                first && !manager.dequeue(message);
            manager.stop();
        }

        {
            FakeClock clock;
            FakeTransport direct;
            direct.set_next_sequence(
                std::numeric_limits<uint64_t>::max() - 1);
            Horse::RollbackRouteManagerTransport manager(direct, &clock);
            manager.set_policy(policy());
            manager.start(cfg);
            const uint64_t logical_session =
                manager.logical_session_id_for_test();
            inject_logical(direct, logical_session,
                Horse::RollbackProtocolV2PacketType::Gekko,
                std::numeric_limits<uint64_t>::max() - 1, 10);
            inject_logical(direct, logical_session,
                Horse::RollbackProtocolV2PacketType::Gekko,
                std::numeric_limits<uint64_t>::max(), 11);
            inject_logical(direct, logical_session,
                Horse::RollbackProtocolV2PacketType::Gekko, 0, 12);
            inject_logical(direct, logical_session,
                Horse::RollbackProtocolV2PacketType::Gekko, 1, 13);
            manager.service();
            Horse::RollbackUdpMessage message {};
            const bool a = manager.dequeue(message);
            const bool b = manager.dequeue(message);
            const bool c = manager.dequeue(message);
            const bool d = manager.dequeue(message);
            report.wraparound = a && b && c && !d
                && manager.route_statistics(
                    Horse::RollbackRouteKind::DirectUdp).reordered == 0;

            direct.pending_callbacks = 2;
            inject_logical(direct, logical_session,
                Horse::RollbackProtocolV2PacketType::Gekko, 500, 14);
            manager.stop();
            report.shutdown = direct.stop_calls != 0
                && direct.pending_callbacks == 0
                && direct.inbound_count == 0
                && !manager.peer_ready();
        }

        return report;
    }
}

int main()
{
    const Report report = run();
    if (!report.ok())
    {
        std::printf(
            "rollback route manager self-test failed "
            "direct=%d payload=%d authenticated=%d late=%d unavailable=%d "
            "latency=%d consistent=%d "
            "dwell=%d hysteresis=%d failover=%d best_failover=%d "
            "duplicate=%d delayed=%d "
            "reorder=%d "
            "loss=%d stale=%d wrap=%d reconnect=%d deadline_reset=%d "
            "deadline_attribution=%d deadline_priority=%d "
            "probe_recovery=%d "
            "metric_safety=%d rolling_recovery=%d racing=%d "
            "racing_limit=%d pressure=%d "
            "shutdown=%d\n",
            report.direct_only, report.payload_boundary,
            report.authenticated_wire,
            report.steam_late,
            report.steam_unavailable, report.lower_latency,
            report.consistency, report.dwell, report.hysteresis,
            report.failure_recovery, report.best_failover,
            report.duplicate,
            report.delayed_duplicate, report.reorder,
            report.loss_burst, report.stale_session, report.wraparound,
            report.independent_reconnect, report.deadline_window_reset,
            report.deadline_attribution,
            report.deadline_priority_selection,
            report.probe_recovery,
            report.metric_safety, report.rolling_recovery,
            report.racing_fallback, report.racing_two_route_limit,
            report.queue_pressure,
            report.shutdown);
        return 1;
    }
    std::printf(
        "rollback route manager self-test passed "
        "direct=1 payload=1 authenticated=1 steam_late=1 unavailable=1 "
        "selection=1 consistency=1 hysteresis=1 failover=1 best_failover=1 "
        "dedupe=1 delayed=1 reorder=1 "
        "loss=1 stale=1 wrap=1 reconnect=1 deadline_reset=1 "
        "deadline_attribution=1 deadline_priority=1 "
        "probe_recovery=1 "
        "metric_safety=1 rolling_recovery=1 racing=1 racing_limit=1 "
        "pressure=1 "
        "shutdown=1\n");
    return 0;
}
