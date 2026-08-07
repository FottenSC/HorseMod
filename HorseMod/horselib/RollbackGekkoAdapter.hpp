// ============================================================================
// Horse::RollbackGekkoAdapter
//
// Deterministic loopback harness for GekkoNet's custom adapter callbacks. This
// does not touch SC6 or Steam transport; it proves that Horse-owned send,
// receive, and free hooks can drive two Gekko game sessions in one process.
// ============================================================================

#pragma once

#include "RollbackGekkoSessionStart.hpp"

#ifndef HORSE_ENABLE_GEKKONET
#define HORSE_ENABLE_GEKKONET 0
#endif

#include "RollbackGekkoGameplayInputBridge.hpp"
#include "RollbackGekkoSession.hpp"
#include "RollbackGekkoTransportBridge.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#if HORSE_ENABLE_GEKKONET
#include <gekkonet.h>
#endif

namespace Horse
{
    struct RollbackGekkoAdapterSelfTestReport
    {
        bool ok {false};
        bool dependency_enabled {false};
        bool create_ok {false};
        bool adapter_set {false};
        bool start_ok {false};
        bool actors_ok {false};
        bool saw_player_connected {false};
        bool saw_session_started {false};
        bool saw_save {false};
        bool saw_load {false};
        bool saw_advance {false};
        bool saw_rollback_advance {false};
        bool no_desync {false};
        bool callbacks_sent {false};
        bool callbacks_received {false};
        bool callbacks_freed {false};
        bool bidirectional_payloads {false};
        bool bridge_roundtrip {false};
        bool bridge_metadata_accepted {false};
        bool bridge_rejections_ok {false};
        bool gameplay_inputs_decoded {false};
        bool gameplay_slots_present {false};
        bool gameplay_inputs_drive_state {false};
        bool initial_baseline_event_order_a {false};
        bool initial_baseline_event_order_b {false};
        bool preframe_transition_rollback_a {false};
        bool preframe_transition_rollback_b {false};
        bool final_checksums_match {false};
        bool destroy_ok {false};
        uint32_t frames_submitted {0};
        uint32_t save_events {0};
        uint32_t load_events {0};
        uint32_t advance_events {0};
        uint32_t rollback_advance_events {0};
        uint32_t session_events {0};
        uint32_t packets_sent {0};
        uint32_t packets_received {0};
        uint32_t free_calls {0};
        uint32_t bridge_packets_encoded {0};
        uint32_t bridge_packets_decoded {0};
        uint32_t bridge_packets_rejected {0};
        uint32_t gameplay_decoded_events {0};
        uint32_t gameplay_decoded_inputs {0};
        uint32_t final_checksum_a {0};
        uint32_t final_checksum_b {0};
        const char* failure {"not-run"};
    };

#if HORSE_ENABLE_GEKKONET
    namespace RollbackGekkoAdapterDetail
    {
        static constexpr uint32_t kMaxQueuedPackets = 256;
        static constexpr uint32_t kMaxPacketBytes =
            static_cast<uint32_t>(kRollbackGekkoBridgeMaxWireBytes);
        static constexpr uint64_t kAdapterSessionId =
            0x484F525345474B4Full;

        struct QueuedPacket
        {
            uint8_t from {0};
            uint32_t size {0};
            uint32_t deliver_at {0};
            std::array<uint8_t, kMaxPacketBytes> bytes {};
        };

        struct AdapterContext
        {
            uint8_t address {0};
            AdapterContext* peer {nullptr};
            std::array<QueuedPacket, kMaxQueuedPackets> inbox {};
            uint32_t head {0};
            uint32_t count {0};
            uint32_t clock {0};
            uint32_t delivery_delay {0};
            uint32_t packets_sent {0};
            uint32_t packets_received {0};
            uint32_t receive_calls {0};
            uint32_t free_calls {0};
            uint32_t dropped_packets {0};
            uint32_t allocation_failures {0};
            uint32_t next_sequence {0};
            uint32_t last_received_sequence {kRollbackTransportNoFrame};
            uint64_t session_id {0};
            uint32_t bridge_packets_encoded {0};
            uint32_t bridge_packets_decoded {0};
            uint32_t bridge_packets_rejected {0};
            RollbackTransportPeerModel<512> bridge_peer {};
            std::array<GekkoNetResult*, kMaxQueuedPackets> receive_results {};
        };

        struct AdapterState
        {
            RollbackGekkoDetail::State gameplay {};
            uint32_t current_new_round {1};
            uint32_t queued_active_battle {1};
            uint32_t native_finalize_calls {1};
            uint32_t new_round_ticks {0};
        };

        static inline AdapterContext*& current_context() noexcept
        {
            static AdapterContext* ctx = nullptr;
            return ctx;
        }

        class ScopedContext
        {
        public:
            explicit ScopedContext(AdapterContext& ctx) noexcept
                : m_prev(current_context())
            {
                current_context() = &ctx;
            }

            ~ScopedContext() noexcept
            {
                current_context() = m_prev;
            }

            ScopedContext(const ScopedContext&) = delete;
            ScopedContext& operator=(const ScopedContext&) = delete;

        private:
            AdapterContext* m_prev {nullptr};
        };

        static inline bool enqueue_packet(
            AdapterContext& dst,
            uint8_t from,
            const char* data,
            int length) noexcept
        {
            if (!data || length <= 0
                || static_cast<uint32_t>(length) > kMaxPacketBytes
                || dst.count >= kMaxQueuedPackets)
            {
                ++dst.dropped_packets;
                return false;
            }

            const uint32_t idx = (dst.head + dst.count) % kMaxQueuedPackets;
            QueuedPacket& packet = dst.inbox[idx];
            packet.from = from;
            packet.size = static_cast<uint32_t>(length);
            packet.deliver_at = dst.clock + dst.delivery_delay;
            std::memcpy(packet.bytes.data(), data, packet.size);
            ++dst.count;
            return true;
        }

        static inline void reset_adapter_context(AdapterContext& ctx) noexcept
        {
            ctx.address = 0;
            ctx.peer = nullptr;
            for (QueuedPacket& packet : ctx.inbox)
            {
                packet.from = 0;
                packet.size = 0;
                packet.deliver_at = 0;
                packet.bytes.fill(0);
            }
            ctx.head = 0;
            ctx.count = 0;
            ctx.clock = 0;
            ctx.delivery_delay = 0;
            ctx.packets_sent = 0;
            ctx.packets_received = 0;
            ctx.receive_calls = 0;
            ctx.free_calls = 0;
            ctx.dropped_packets = 0;
            ctx.allocation_failures = 0;
            ctx.next_sequence = 0;
            ctx.last_received_sequence = kRollbackTransportNoFrame;
            ctx.session_id = 0;
            ctx.bridge_packets_encoded = 0;
            ctx.bridge_packets_decoded = 0;
            ctx.bridge_packets_rejected = 0;
            ctx.bridge_peer.clear();
            ctx.receive_results.fill(nullptr);
        }

        static inline bool peek_packet_ready(
            const AdapterContext& src) noexcept
        {
            if (src.count == 0)
                return false;
            return src.inbox[src.head].deliver_at <= src.clock;
        }

        static inline bool pop_packet(
            AdapterContext& src,
            QueuedPacket& out) noexcept
        {
            if (!peek_packet_ready(src))
                return false;
            out = src.inbox[src.head];
            src.head = (src.head + 1) % kMaxQueuedPackets;
            --src.count;
            return true;
        }

        static inline void send_data(
            GekkoNetAddress* addr,
            const char* data,
            int length) noexcept
        {
            AdapterContext* ctx = current_context();
            if (!ctx || !ctx->peer || !addr || !addr->data || addr->size != 1)
                return;
            const uint8_t dst = *static_cast<uint8_t*>(addr->data);
            if (dst != ctx->peer->address)
            {
                ++ctx->dropped_packets;
                return;
            }
            if (!data || length <= 0
                || static_cast<uint32_t>(length)
                    > kRollbackGekkoBridgeMaxPayloadBytes)
            {
                ++ctx->dropped_packets;
                return;
            }
            RollbackGekkoBridgeWirePacket bridge_wire {};
            const uint32_t sequence = ctx->next_sequence++;
            const RollbackTransportPacket metadata =
                MakeRollbackGekkoBridgeMetadata(
                    sequence,
                    ctx->last_received_sequence,
                    data,
                    static_cast<size_t>(length));
            if (!EncodeRollbackGekkoBridgePacketWithSession(
                    ctx->address,
                    dst,
                    ctx->session_id,
                    sequence,
                    metadata,
                    data,
                    static_cast<size_t>(length),
                    bridge_wire))
            {
                ++ctx->dropped_packets;
                return;
            }
            ++ctx->bridge_packets_encoded;
            if (enqueue_packet(
                    *ctx->peer,
                    ctx->address,
                    reinterpret_cast<const char*>(bridge_wire.bytes.data()),
                    static_cast<int>(bridge_wire.size)))
            {
                ++ctx->packets_sent;
            }
        }

        static inline GekkoNetResult** receive_data(int* length) noexcept
        {
            AdapterContext* ctx = current_context();
            if (!ctx || !length)
                return nullptr;

            ++ctx->receive_calls;
            ++ctx->clock;
            *length = 0;
            QueuedPacket packet {};
            while (*length < static_cast<int>(kMaxQueuedPackets)
                   && pop_packet(*ctx, packet))
            {
                RollbackGekkoBridgePacket bridge {};
                RollbackGekkoBridgeDecodePolicy policy {};
                policy.expected_source_peer =
                    ctx->peer ? ctx->peer->address : 0;
                policy.expected_dest_peer = ctx->address;
                policy.expected_session_id = ctx->session_id;
                policy.require_source_peer = ctx->peer != nullptr;
                policy.require_session_id = true;
                if (!DecodeRollbackGekkoBridgePacket(
                        packet.bytes.data(),
                        packet.size,
                        policy,
                        bridge))
                {
                    ++ctx->bridge_packets_rejected;
                    continue;
                }
                const RollbackTransportAcceptReport accept =
                    ctx->bridge_peer.accept_remote_input(
                        bridge.metadata,
                        bridge.sequence,
                        60);
                if (!accept.accepted
                    && accept.status
                        != RollbackTransportAcceptStatus::Duplicate)
                {
                    ++ctx->bridge_packets_rejected;
                    continue;
                }
                ++ctx->bridge_packets_decoded;
                ctx->last_received_sequence = bridge.sequence;

                auto* result = static_cast<GekkoNetResult*>(
                    std::malloc(sizeof(GekkoNetResult)));
                auto* addr = static_cast<uint8_t*>(std::malloc(1));
                auto* bytes = static_cast<uint8_t*>(
                    std::malloc(bridge.payload_size));
                if (!result || !addr || !bytes)
                {
                    if (result) std::free(result);
                    if (addr) std::free(addr);
                    if (bytes) std::free(bytes);
                    ++ctx->allocation_failures;
                    break;
                }

                *addr = bridge.source_peer;
                std::memcpy(bytes, bridge.payload.data(), bridge.payload_size);
                result->addr.data = addr;
                result->addr.size = 1;
                result->data_len = bridge.payload_size;
                result->data = bytes;
                ctx->receive_results[static_cast<size_t>(*length)] = result;
                ++(*length);
                ++ctx->packets_received;
            }
            return ctx->receive_results.data();
        }

        static inline void free_data(void* p) noexcept
        {
            AdapterContext* ctx = current_context();
            if (ctx)
                ++ctx->free_calls;
            std::free(p);
        }

        static inline GekkoNetAdapter* adapter() noexcept
        {
            static GekkoNetAdapter a {
                &send_data,
                &receive_data,
                &free_data,
            };
            return &a;
        }

        static inline void collect_session_events(
            GekkoSession* session,
            AdapterContext& ctx,
            RollbackGekkoAdapterSelfTestReport& report) noexcept
        {
            ScopedContext scope(ctx);
            int session_event_count = 0;
            GekkoSessionEvent** session_events =
                gekko_session_events(session, &session_event_count);
            for (int i = 0; i < session_event_count; ++i)
            {
                GekkoSessionEvent* ev = session_events[i];
                if (!ev) continue;
                ++report.session_events;
                if (ev->type == GekkoPlayerConnected)
                    report.saw_player_connected = true;
                else if (ev->type == GekkoSessionStarted)
                    report.saw_session_started = true;
                else if (ev->type == GekkoDesyncDetected)
                    report.no_desync = false;
            }
        }

        static inline bool pump_update(
            GekkoSession* session,
            AdapterContext& ctx,
            AdapterState& state,
            RollbackGekkoAdapterSelfTestReport& report,
            bool& initial_baseline_event_order) noexcept
        {
            ScopedContext scope(ctx);
            int event_count = 0;
            GekkoGameEvent** events =
                gekko_update_session(session, &event_count);
            // A GameSession may need adapter traffic before its first gameplay
            // batch. Validate the baseline contract on the first non-empty
            // update for each peer, not on an arbitrary outer-loop step.
            if (!initial_baseline_event_order && event_count > 0)
            {
                initial_baseline_event_order = event_count == 3
                    && events
                    && events[0]
                    && events[0]->type == GekkoSaveEvent
                    && events[0]->data.save.frame
                        == kRollbackGekkoBaselineFrame
                    && events[1]
                    && events[1]->type == GekkoAdvanceEvent
                    && events[1]->data.adv.frame == 0
                    && events[2]
                    && events[2]->type == GekkoSaveEvent
                    && events[2]->data.save.frame == 0;
                if (!initial_baseline_event_order)
                {
                    report.failure = "unexpected-initial-baseline-events";
                    return false;
                }
            }
            for (int i = 0; i < event_count; ++i)
            {
                GekkoGameEvent* ev = events[i];
                if (!ev) continue;
                switch (ev->type)
                {
                case GekkoSaveEvent:
                    report.saw_save = true;
                    ++report.save_events;
                    *ev->data.save.state_len =
                        static_cast<unsigned int>(sizeof(state));
                    *ev->data.save.checksum =
                        RollbackGekkoDetail::checksum_bytes(
                            &state,
                            static_cast<uint32_t>(sizeof(state)));
                    std::memcpy(ev->data.save.state, &state, sizeof(state));
                    break;
                case GekkoLoadEvent:
                    report.saw_load = true;
                    ++report.load_events;
                    if (ev->data.load.state_len != sizeof(state))
                    {
                        report.failure = "load-size-mismatch";
                        return false;
                    }
                    std::memcpy(&state, ev->data.load.state, sizeof(state));
                    break;
                case GekkoAdvanceEvent:
                {
                    report.saw_advance = true;
                    ++report.advance_events;
                    if (ev->data.adv.rolling_back)
                    {
                        report.saw_rollback_advance = true;
                        ++report.rollback_advance_events;
                    }
                    const RollbackGekkoGameplayInputDecodeReport decoded =
                        DecodeRollbackGekkoGameplayInputs(
                            ev->data.adv.frame,
                            ev->data.adv.inputs,
                            static_cast<uint32_t>(ev->data.adv.input_len),
                            2);
                    if (!decoded.ok)
                    {
                        report.failure = decoded.failure;
                        return false;
                    }
                    ++report.gameplay_decoded_events;
                    report.gameplay_decoded_inputs += decoded.decoded_count;
                    RollbackDecodedGameplayInput player0 {};
                    RollbackDecodedGameplayInput player1 {};
                    if (!GetRollbackGekkoDecodedGameplayInput(
                            decoded, 0, player0)
                        || !GetRollbackGekkoDecodedGameplayInput(
                            decoded, 1, player1))
                    {
                        report.failure = "decoded-player-missing";
                        return false;
                    }
                    report.gameplay_slots_present = true;
                    const uint32_t inputs[2] {
                        player0.input_value,
                        player1.input_value,
                    };
                    RollbackGekkoDetail::advance_state(
                        state.gameplay,
                        ev->data.adv.frame,
                        inputs,
                        ev->data.adv.rolling_back);
                    if (state.queued_active_battle != 0)
                    {
                        state.queued_active_battle = 0;
                        state.current_new_round = 0;
                    }
                    else if (state.current_new_round != 0)
                    {
                        ++state.new_round_ticks;
                    }
                    break;
                }
                default:
                    break;
                }
            }
            return true;
        }
    }
#endif

    static inline RollbackGekkoAdapterSelfTestReport
    RunRollbackGekkoAdapterSelfTest() noexcept
    {
        RollbackGekkoAdapterSelfTestReport report {};
        report.dependency_enabled = HORSE_ENABLE_GEKKONET != 0;

#if !HORSE_ENABLE_GEKKONET
        report.failure = "gekkonet-disabled";
        return report;
#else
        using namespace RollbackGekkoAdapterDetail;

        GekkoSession* session_a = nullptr;
        GekkoSession* session_b = nullptr;
        report.create_ok =
            gekko_create(&session_a, GekkoGameSession)
            && gekko_create(&session_b, GekkoGameSession)
            && session_a
            && session_b;
        if (!report.create_ok)
        {
            if (session_a) (void)gekko_destroy(&session_a);
            if (session_b) (void)gekko_destroy(&session_b);
            report.failure = "create-failed";
            return report;
        }

        auto finish = [&](const char* failure) noexcept {
            bool destroyed_a = true;
            bool destroyed_b = true;
            if (session_a) destroyed_a = gekko_destroy(&session_a);
            if (session_b) destroyed_b = gekko_destroy(&session_b);
            report.destroy_ok = destroyed_a && destroyed_b;
            report.failure = failure;
            report.ok =
                report.dependency_enabled
                && report.create_ok
                && report.adapter_set
                && report.start_ok
                && report.actors_ok
                && report.saw_player_connected
                && report.saw_session_started
                && report.saw_save
                && report.saw_load
                && report.saw_advance
                && report.saw_rollback_advance
                && report.no_desync
                && report.callbacks_sent
                && report.callbacks_received
                && report.callbacks_freed
                && report.bidirectional_payloads
                && report.bridge_roundtrip
                && report.bridge_metadata_accepted
                && report.bridge_rejections_ok
                && report.gameplay_inputs_decoded
                && report.gameplay_slots_present
                && report.gameplay_inputs_drive_state
                && report.initial_baseline_event_order_a
                && report.initial_baseline_event_order_b
                && report.preframe_transition_rollback_a
                && report.preframe_transition_rollback_b
                && report.final_checksums_match
                && report.destroy_ok;
            if (report.ok)
                report.failure = "ok";
            return report;
        };

        static AdapterContext ctx_a_storage {};
        static AdapterContext ctx_b_storage {};
        reset_adapter_context(ctx_a_storage);
        reset_adapter_context(ctx_b_storage);
        AdapterContext& ctx_a = ctx_a_storage;
        AdapterContext& ctx_b = ctx_b_storage;
        ctx_a.address = 0xA0;
        ctx_b.address = 0xB0;
        ctx_a.session_id = kAdapterSessionId;
        ctx_b.session_id = kAdapterSessionId;
        ctx_a.delivery_delay = 2;
        ctx_b.delivery_delay = 2;
        ctx_a.peer = &ctx_b;
        ctx_b.peer = &ctx_a;

        GekkoConfig config {};
        config.num_players = 2;
        config.max_spectators = 0;
        config.input_prediction_window = 4;
        config.input_size = sizeof(uint32_t);
        config.state_size =
            static_cast<unsigned int>(sizeof(AdapterState));
        config.limited_saving = false;
        config.desync_detection = true;
        config.check_distance = 4;

        {
            ScopedContext scope_a(ctx_a);
            if (!StartRollbackGekkoSessionWithAdapter(
                    session_a, config, adapter()))
            {
                return finish("session-a-start-or-adapter-failed");
            }
        }
        {
            ScopedContext scope_b(ctx_b);
            if (!StartRollbackGekkoSessionWithAdapter(
                    session_b, config, adapter()))
            {
                return finish("session-b-start-or-adapter-failed");
            }
        }
        report.start_ok = true;
        report.adapter_set = true;

        uint8_t addr_a = ctx_a.address;
        uint8_t addr_b = ctx_b.address;
        GekkoNetAddress gekko_addr_a {&addr_a, 1};
        GekkoNetAddress gekko_addr_b {&addr_b, 1};

        const int a_local = gekko_add_actor(
            session_a, GekkoLocalPlayer, nullptr);
        const int a_remote = gekko_add_actor(
            session_a, GekkoRemotePlayer, &gekko_addr_b);
        const int b_remote = gekko_add_actor(
            session_b, GekkoRemotePlayer, &gekko_addr_a);
        const int b_local = gekko_add_actor(
            session_b, GekkoLocalPlayer, nullptr);
        report.actors_ok =
            a_local == 0 && a_remote == 1 && b_remote == 0 && b_local == 1;
        if (!report.actors_ok)
            return finish("actor-add-failed");

        gekko_set_local_delay(session_a, a_local, 1);
        gekko_set_local_delay(session_b, b_local, 1);

        AdapterState state_a {};
        AdapterState state_b {};
        report.no_desync = true;

        for (uint32_t step = 0; step < 48; ++step)
        {
            uint32_t p0_input = 0x100u + step;
            uint32_t p1_input = 0x200u ^ (step * 5u);
            {
                ScopedContext scope_a(ctx_a);
                gekko_add_local_input(session_a, a_local, &p0_input);
            }
            {
                ScopedContext scope_b(ctx_b);
                gekko_add_local_input(session_b, b_local, &p1_input);
            }
            ++report.frames_submitted;

            if (!pump_update(
                    session_a, ctx_a, state_a, report,
                    report.initial_baseline_event_order_a))
                return finish(report.failure);
            collect_session_events(session_a, ctx_a, report);
            if (!pump_update(
                    session_b, ctx_b, state_b, report,
                    report.initial_baseline_event_order_b))
                return finish(report.failure);
            collect_session_events(session_b, ctx_b, report);

            {
                ScopedContext scope_a(ctx_a);
                gekko_network_poll(session_a);
            }
            collect_session_events(session_a, ctx_a, report);
            {
                ScopedContext scope_b(ctx_b);
                gekko_network_poll(session_b);
            }
            collect_session_events(session_b, ctx_b, report);
        }

        report.packets_sent = ctx_a.packets_sent + ctx_b.packets_sent;
        report.packets_received =
            ctx_a.packets_received + ctx_b.packets_received;
        report.free_calls = ctx_a.free_calls + ctx_b.free_calls;
        report.bridge_packets_encoded =
            ctx_a.bridge_packets_encoded + ctx_b.bridge_packets_encoded;
        report.bridge_packets_decoded =
            ctx_a.bridge_packets_decoded + ctx_b.bridge_packets_decoded;
        report.bridge_packets_rejected =
            ctx_a.bridge_packets_rejected + ctx_b.bridge_packets_rejected;
        report.callbacks_sent = report.packets_sent > 0;
        report.callbacks_received = report.packets_received > 0;
        report.callbacks_freed =
            report.free_calls >= report.packets_received * 3;
        report.bidirectional_payloads =
            ctx_a.packets_sent > 0 && ctx_b.packets_sent > 0
            && ctx_a.packets_received > 0 && ctx_b.packets_received > 0
            && ctx_a.dropped_packets == 0 && ctx_b.dropped_packets == 0
            && ctx_a.allocation_failures == 0
            && ctx_b.allocation_failures == 0;
        const RollbackGekkoBridgeSelfTestReport bridge_report =
            RunRollbackGekkoBridgeSelfTest();
        report.bridge_roundtrip =
            bridge_report.ok
            && report.bridge_packets_encoded == report.packets_sent
            && report.bridge_packets_decoded == report.packets_received;
        report.bridge_metadata_accepted =
            bridge_report.metadata_accepts_in_transport_model
            && ctx_a.bridge_peer.metrics().packets_accepted > 0
            && ctx_b.bridge_peer.metrics().packets_accepted > 0
            && ctx_a.bridge_peer.metrics().highest_remote_frame
                != kRollbackTransportNoFrame
            && ctx_b.bridge_peer.metrics().highest_remote_frame
                != kRollbackTransportNoFrame;
        report.bridge_rejections_ok =
            bridge_report.corrupt_magic_rejected
            && bridge_report.corrupt_length_rejected
            && bridge_report.corrupt_hash_rejected
            && bridge_report.wrong_source_rejected
            && bridge_report.wrong_destination_rejected
            && bridge_report.wrong_session_rejected
            && bridge_report.oversize_rejected
            && report.bridge_packets_rejected == 0;
        report.final_checksum_a =
            RollbackGekkoDetail::checksum_bytes(
                &state_a,
                static_cast<uint32_t>(sizeof(state_a)));
        report.final_checksum_b =
            RollbackGekkoDetail::checksum_bytes(
                &state_b,
                static_cast<uint32_t>(sizeof(state_b)));
        report.final_checksums_match =
            report.final_checksum_a == report.final_checksum_b
            && report.final_checksum_a != 0;
        report.gameplay_inputs_decoded =
            report.gameplay_decoded_events > 0
            && report.gameplay_decoded_inputs
                == report.advance_events * config.num_players;
        report.gameplay_inputs_drive_state =
            report.gameplay_inputs_decoded
            && report.gameplay_slots_present
            && report.final_checksums_match;
        report.preframe_transition_rollback_a =
            state_a.native_finalize_calls == 1
            && state_a.new_round_ticks == 0
            && state_a.current_new_round == 0
            && state_a.queued_active_battle == 0;
        report.preframe_transition_rollback_b =
            state_b.native_finalize_calls == 1
            && state_b.new_round_ticks == 0
            && state_b.current_new_round == 0
            && state_b.queued_active_battle == 0;

        return finish("ok");
#endif
    }
}
