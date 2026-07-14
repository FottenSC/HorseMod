// ============================================================================
// Horse::RollbackGekkoUdpAdapter
//
// Horse-owned localhost UDP transport for GekkoNet's custom adapter callbacks.
// This proves real socket I/O for the HRG1/Gekko path without touching SC6's
// stock Steam/Lux online transport or local input delay.
// ============================================================================

#pragma once

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
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#if defined(_WINSOCKAPI_) && !defined(_WINSOCK2API_)
#include <winsock.h>
#else
#include <winsock2.h>
#endif

#include <gekkonet.h>
#endif

namespace Horse
{
    struct RollbackGekkoUdpAdapterSelfTestReport
    {
        bool ok {false};
        bool dependency_enabled {false};
        bool wsa_started {false};
        bool sockets_open {false};
        bool bound_loopback {false};
        bool nonblocking {false};
        bool manual_udp_roundtrip {false};
        bool wrong_endpoint_rejected {false};
        bool wrong_source_rejected {false};
        bool wrong_destination_rejected {false};
        bool wrong_session_rejected {false};
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
        bool gameplay_inputs_decoded {false};
        bool gameplay_slots_present {false};
        bool gameplay_inputs_drive_state {false};
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
        uint32_t endpoint_packets_rejected {0};
        uint32_t gameplay_decoded_events {0};
        uint32_t gameplay_decoded_inputs {0};
        uint16_t port_a {0};
        uint16_t port_b {0};
        uint32_t final_checksum_a {0};
        uint32_t final_checksum_b {0};
        int32_t wsa_startup_error {0};
        int32_t socket_error {0};
        int32_t bind_error {0};
        int32_t getsockname_error {0};
        int32_t ioctlsocket_error {0};
        int32_t sendto_error {0};
        int32_t recvfrom_error {0};
        const char* failure {"not-run"};
    };

#if HORSE_ENABLE_GEKKONET
    namespace RollbackGekkoUdpAdapterDetail
    {
        static constexpr uint32_t kMaxReceiveResults = 256;
        static constexpr uint32_t kMaxOutboundPackets = 256;
        static constexpr uint64_t kUdpSessionId =
            0x484F525345554450ull; // "HORSEUDP"

        struct UdpPeerAddress
        {
            uint8_t peer {0};
            uint8_t reserved {0};
            uint16_t port {0};
            uint32_t ipv4 {0};
        };

        struct WsaSession
        {
            bool ok {false};
            int error {0};

            WsaSession() noexcept
            {
                WSADATA data {};
                error = WSAStartup(MAKEWORD(2, 2), &data);
                ok = error == 0;
            }

            ~WsaSession() noexcept
            {
                if (ok)
                    WSACleanup();
            }

            WsaSession(const WsaSession&) = delete;
            WsaSession& operator=(const WsaSession&) = delete;
        };

        struct QueuedUdpPacket
        {
            sockaddr_in dst {};
            uint32_t size {0};
            uint32_t deliver_at {0};
            std::array<uint8_t, kRollbackGekkoBridgeMaxWireBytes> bytes {};
        };

        struct UdpAdapterContext
        {
            uint8_t peer_id {0};
            SOCKET socket_handle {INVALID_SOCKET};
            sockaddr_in local_addr {};
            sockaddr_in peer_sockaddr {};
            UdpPeerAddress public_addr {};
            UdpPeerAddress expected_peer {};
            std::array<QueuedUdpPacket, kMaxOutboundPackets> outbound {};
            bool open {false};
            bool nonblocking {false};
            uint32_t clock {0};
            uint32_t outbound_head {0};
            uint32_t outbound_count {0};
            uint32_t outbound_delay {0};
            uint32_t packets_sent {0};
            uint32_t packets_received {0};
            uint32_t receive_calls {0};
            uint32_t free_calls {0};
            uint32_t send_failures {0};
            uint32_t allocation_failures {0};
            uint32_t bridge_packets_encoded {0};
            uint32_t bridge_packets_decoded {0};
            uint32_t bridge_packets_rejected {0};
            uint32_t endpoint_packets_rejected {0};
            uint32_t next_sequence {0};
            uint32_t last_received_sequence {kRollbackTransportNoFrame};
            int32_t socket_error {0};
            int32_t bind_error {0};
            int32_t getsockname_error {0};
            int32_t ioctlsocket_error {0};
            int32_t sendto_error {0};
            int32_t recvfrom_error {0};
            RollbackTransportPeerModel<512> bridge_peer {};
            std::array<GekkoNetResult*, kMaxReceiveResults> receive_results {};
        };

        static inline UdpAdapterContext*& current_context() noexcept
        {
            static UdpAdapterContext* ctx = nullptr;
            return ctx;
        }

        class ScopedContext
        {
        public:
            explicit ScopedContext(UdpAdapterContext& ctx) noexcept
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
            UdpAdapterContext* m_prev {nullptr};
        };

        static inline sockaddr_in sockaddr_from_public(
            const UdpPeerAddress& a) noexcept
        {
            sockaddr_in out {};
            out.sin_family = AF_INET;
            out.sin_port = htons(a.port);
            out.sin_addr.s_addr = a.ipv4;
            return out;
        }

        static inline bool endpoint_matches(
            const sockaddr_in& actual,
            const UdpPeerAddress& expected) noexcept
        {
            return actual.sin_family == AF_INET
                && actual.sin_port == htons(expected.port)
                && actual.sin_addr.s_addr == expected.ipv4;
        }

        static inline void clear_context_for_open(
            UdpAdapterContext& ctx) noexcept
        {
            if (ctx.socket_handle != INVALID_SOCKET)
                closesocket(ctx.socket_handle);
            ctx.peer_id = 0;
            ctx.socket_handle = INVALID_SOCKET;
            ctx.local_addr = {};
            ctx.peer_sockaddr = {};
            ctx.public_addr = {};
            ctx.expected_peer = {};
            for (QueuedUdpPacket& packet : ctx.outbound)
            {
                packet.dst = {};
                packet.size = 0;
                packet.deliver_at = 0;
                packet.bytes.fill(0);
            }
            ctx.open = false;
            ctx.nonblocking = false;
            ctx.clock = 0;
            ctx.outbound_head = 0;
            ctx.outbound_count = 0;
            ctx.outbound_delay = 0;
            ctx.packets_sent = 0;
            ctx.packets_received = 0;
            ctx.receive_calls = 0;
            ctx.free_calls = 0;
            ctx.send_failures = 0;
            ctx.allocation_failures = 0;
            ctx.bridge_packets_encoded = 0;
            ctx.bridge_packets_decoded = 0;
            ctx.bridge_packets_rejected = 0;
            ctx.endpoint_packets_rejected = 0;
            ctx.next_sequence = 0;
            ctx.last_received_sequence = kRollbackTransportNoFrame;
            ctx.socket_error = 0;
            ctx.bind_error = 0;
            ctx.getsockname_error = 0;
            ctx.ioctlsocket_error = 0;
            ctx.sendto_error = 0;
            ctx.recvfrom_error = 0;
            ctx.bridge_peer.clear();
            ctx.receive_results.fill(nullptr);
        }

        static inline bool open_udp_context(
            UdpAdapterContext& ctx,
            uint8_t peer_id) noexcept
        {
            clear_context_for_open(ctx);
            ctx.peer_id = peer_id;
            ctx.socket_handle =
                socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (ctx.socket_handle == INVALID_SOCKET)
            {
                ctx.socket_error = WSAGetLastError();
                return false;
            }

            sockaddr_in bind_addr {};
            bind_addr.sin_family = AF_INET;
            bind_addr.sin_port = htons(0);
            bind_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            if (bind(
                    ctx.socket_handle,
                    reinterpret_cast<const sockaddr*>(&bind_addr),
                    sizeof(bind_addr))
                == SOCKET_ERROR)
            {
                ctx.bind_error = WSAGetLastError();
                closesocket(ctx.socket_handle);
                ctx.socket_handle = INVALID_SOCKET;
                return false;
            }

            int len = sizeof(ctx.local_addr);
            if (getsockname(
                    ctx.socket_handle,
                    reinterpret_cast<sockaddr*>(&ctx.local_addr),
                    &len)
                == SOCKET_ERROR)
            {
                ctx.getsockname_error = WSAGetLastError();
                closesocket(ctx.socket_handle);
                ctx.socket_handle = INVALID_SOCKET;
                return false;
            }

            u_long nonblocking = 1;
            if (ioctlsocket(
                    ctx.socket_handle,
                    FIONBIO,
                    &nonblocking)
                == SOCKET_ERROR)
            {
                ctx.ioctlsocket_error = WSAGetLastError();
                closesocket(ctx.socket_handle);
                ctx.socket_handle = INVALID_SOCKET;
                return false;
            }

            ctx.public_addr.peer = peer_id;
            ctx.public_addr.port = ntohs(ctx.local_addr.sin_port);
            ctx.public_addr.ipv4 = ctx.local_addr.sin_addr.s_addr;
            ctx.open = true;
            ctx.nonblocking = true;
            return true;
        }

        static inline void close_udp_context(UdpAdapterContext& ctx) noexcept
        {
            if (ctx.socket_handle != INVALID_SOCKET)
            {
                closesocket(ctx.socket_handle);
                ctx.socket_handle = INVALID_SOCKET;
            }
            ctx.open = false;
        }

        static inline int32_t first_nonzero_error(
            int32_t a,
            int32_t b,
            int32_t c) noexcept
        {
            if (a != 0) return a;
            if (b != 0) return b;
            return c;
        }

        static inline void collect_udp_context_errors(
            RollbackGekkoUdpAdapterSelfTestReport& report,
            const UdpAdapterContext& a,
            const UdpAdapterContext& b,
            const UdpAdapterContext& c) noexcept
        {
            report.socket_error = first_nonzero_error(
                report.socket_error, a.socket_error,
                first_nonzero_error(b.socket_error, c.socket_error, 0));
            report.bind_error = first_nonzero_error(
                report.bind_error, a.bind_error,
                first_nonzero_error(b.bind_error, c.bind_error, 0));
            report.getsockname_error = first_nonzero_error(
                report.getsockname_error, a.getsockname_error,
                first_nonzero_error(
                    b.getsockname_error, c.getsockname_error, 0));
            report.ioctlsocket_error = first_nonzero_error(
                report.ioctlsocket_error, a.ioctlsocket_error,
                first_nonzero_error(
                    b.ioctlsocket_error, c.ioctlsocket_error, 0));
            report.sendto_error = first_nonzero_error(
                report.sendto_error, a.sendto_error,
                first_nonzero_error(b.sendto_error, c.sendto_error, 0));
            report.recvfrom_error = first_nonzero_error(
                report.recvfrom_error, a.recvfrom_error,
                first_nonzero_error(b.recvfrom_error, c.recvfrom_error, 0));
        }

        static inline void connect_contexts(
            UdpAdapterContext& a,
            UdpAdapterContext& b) noexcept
        {
            a.expected_peer = b.public_addr;
            b.expected_peer = a.public_addr;
            a.peer_sockaddr = sockaddr_from_public(b.public_addr);
            b.peer_sockaddr = sockaddr_from_public(a.public_addr);
        }

        static inline void reset_network_model(
            UdpAdapterContext& ctx) noexcept
        {
            for (QueuedUdpPacket& packet : ctx.outbound)
                packet = {};
            ctx.clock = 0;
            ctx.outbound_head = 0;
            ctx.outbound_count = 0;
            ctx.packets_sent = 0;
            ctx.packets_received = 0;
            ctx.receive_calls = 0;
            ctx.free_calls = 0;
            ctx.send_failures = 0;
            ctx.allocation_failures = 0;
            ctx.bridge_packets_encoded = 0;
            ctx.bridge_packets_decoded = 0;
            ctx.bridge_packets_rejected = 0;
            ctx.endpoint_packets_rejected = 0;
            ctx.next_sequence = 0;
            ctx.last_received_sequence = kRollbackTransportNoFrame;
            ctx.bridge_peer.clear();
            ctx.receive_results.fill(nullptr);
        }

        static inline bool queue_outbound_packet(
            UdpAdapterContext& ctx,
            const sockaddr_in& dst,
            const RollbackGekkoBridgeWirePacket& wire) noexcept
        {
            if (wire.size == 0
                || wire.size > kRollbackGekkoBridgeMaxWireBytes
                || ctx.outbound_count >= kMaxOutboundPackets)
            {
                ++ctx.send_failures;
                return false;
            }
            const uint32_t idx =
                (ctx.outbound_head + ctx.outbound_count)
                % kMaxOutboundPackets;
            QueuedUdpPacket& packet = ctx.outbound[idx];
            packet.dst = dst;
            packet.size = static_cast<uint32_t>(wire.size);
            packet.deliver_at = ctx.clock + ctx.outbound_delay;
            std::memcpy(packet.bytes.data(), wire.bytes.data(), wire.size);
            ++ctx.outbound_count;
            return true;
        }

        static inline bool send_bridge_wire(
            UdpAdapterContext& ctx,
            const sockaddr_in& dst,
            const RollbackGekkoBridgeWirePacket& wire) noexcept
        {
            const int sent = sendto(
                ctx.socket_handle,
                reinterpret_cast<const char*>(wire.bytes.data()),
                static_cast<int>(wire.size),
                0,
                reinterpret_cast<const sockaddr*>(&dst),
                sizeof(dst));
            if (sent != static_cast<int>(wire.size))
            {
                if (sent == SOCKET_ERROR)
                    ctx.sendto_error = WSAGetLastError();
                ++ctx.send_failures;
                return false;
            }
            return true;
        }

        static inline void flush_outbound(UdpAdapterContext& ctx) noexcept
        {
            while (ctx.outbound_count > 0)
            {
                QueuedUdpPacket& packet = ctx.outbound[ctx.outbound_head];
                if (packet.deliver_at > ctx.clock)
                    break;
                RollbackGekkoBridgeWirePacket wire {};
                wire.size = packet.size;
                std::memcpy(
                    wire.bytes.data(),
                    packet.bytes.data(),
                    packet.size);
                if (send_bridge_wire(ctx, packet.dst, wire))
                    ++ctx.packets_sent;
                packet = {};
                ctx.outbound_head =
                    (ctx.outbound_head + 1) % kMaxOutboundPackets;
                --ctx.outbound_count;
            }
        }

        static inline void send_data(
            GekkoNetAddress* addr,
            const char* data,
            int length) noexcept
        {
            UdpAdapterContext* ctx = current_context();
            if (!ctx || !ctx->open || !addr || !addr->data
                || addr->size != sizeof(UdpPeerAddress))
            {
                return;
            }
            const UdpPeerAddress* dst =
                static_cast<const UdpPeerAddress*>(addr->data);
            if (dst->peer != ctx->expected_peer.peer
                || dst->port != ctx->expected_peer.port
                || dst->ipv4 != ctx->expected_peer.ipv4
                || !data
                || length <= 0
                || static_cast<uint32_t>(length)
                    > kRollbackGekkoBridgeMaxPayloadBytes)
            {
                ++ctx->send_failures;
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
                    ctx->peer_id,
                    dst->peer,
                    kUdpSessionId,
                    sequence,
                    metadata,
                    data,
                    static_cast<size_t>(length),
                    bridge_wire))
            {
                ++ctx->send_failures;
                return;
            }

            ++ctx->bridge_packets_encoded;
            const sockaddr_in dst_sockaddr = sockaddr_from_public(*dst);
            if (ctx->outbound_delay == 0)
            {
                if (send_bridge_wire(*ctx, dst_sockaddr, bridge_wire))
                    ++ctx->packets_sent;
                return;
            }
            if (queue_outbound_packet(*ctx, dst_sockaddr, bridge_wire))
            {
                flush_outbound(*ctx);
            }
        }

        static inline GekkoNetResult** receive_data(int* length) noexcept
        {
            UdpAdapterContext* ctx = current_context();
            if (!ctx || !length)
                return nullptr;

            ++ctx->receive_calls;
            ++ctx->clock;
            flush_outbound(*ctx);
            *length = 0;
            for (;;)
            {
                std::array<uint8_t, kRollbackGekkoBridgeMaxWireBytes> bytes {};
                sockaddr_in from {};
                int from_len = sizeof(from);
                const int received = recvfrom(
                    ctx->socket_handle,
                    reinterpret_cast<char*>(bytes.data()),
                    static_cast<int>(bytes.size()),
                    0,
                    reinterpret_cast<sockaddr*>(&from),
                    &from_len);
                if (received == SOCKET_ERROR)
                {
                    const int err = WSAGetLastError();
                    if (err != WSAEWOULDBLOCK)
                    {
                        ctx->recvfrom_error = err;
                        ++ctx->endpoint_packets_rejected;
                    }
                    break;
                }
                if (received <= 0)
                    break;
                if (*length >= static_cast<int>(kMaxReceiveResults))
                    break;
                if (!endpoint_matches(from, ctx->expected_peer))
                {
                    ++ctx->endpoint_packets_rejected;
                    continue;
                }

                RollbackGekkoBridgePacket bridge {};
                RollbackGekkoBridgeDecodePolicy policy {};
                policy.expected_source_peer = ctx->expected_peer.peer;
                policy.expected_dest_peer = ctx->peer_id;
                policy.expected_session_id = kUdpSessionId;
                policy.require_source_peer = true;
                policy.require_session_id = true;
                if (!DecodeRollbackGekkoBridgePacket(
                        bytes.data(),
                        static_cast<size_t>(received),
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
                auto* result_addr = static_cast<UdpPeerAddress*>(
                    std::malloc(sizeof(UdpPeerAddress)));
                auto* payload = static_cast<uint8_t*>(
                    std::malloc(bridge.payload_size));
                if (!result || !result_addr || !payload)
                {
                    if (result) std::free(result);
                    if (result_addr) std::free(result_addr);
                    if (payload) std::free(payload);
                    ++ctx->allocation_failures;
                    break;
                }

                *result_addr = ctx->expected_peer;
                std::memcpy(payload, bridge.payload.data(), bridge.payload_size);
                result->addr.data = result_addr;
                result->addr.size = sizeof(UdpPeerAddress);
                result->data_len = bridge.payload_size;
                result->data = payload;
                ctx->receive_results[static_cast<size_t>(*length)] = result;
                ++(*length);
                ++ctx->packets_received;
            }
            return ctx->receive_results.data();
        }

        static inline void free_data(void* p) noexcept
        {
            UdpAdapterContext* ctx = current_context();
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

        static inline void free_results(
            UdpAdapterContext& ctx,
            GekkoNetResult** results,
            int count) noexcept
        {
            ScopedContext scope(ctx);
            for (int i = 0; results && i < count; ++i)
            {
                GekkoNetResult* result = results[i];
                if (!result) continue;
                free_data(result->addr.data);
                free_data(result->data);
                free_data(result);
            }
        }

        static inline bool send_manual_bridge_packet(
            UdpAdapterContext& src,
            UdpAdapterContext& dst,
            uint8_t source_peer,
            uint8_t dest_peer,
            uint64_t session_id,
            uint32_t sequence,
            const char* payload) noexcept
        {
            if (!payload)
                return false;
            RollbackGekkoBridgeWirePacket wire {};
            const size_t payload_size = std::strlen(payload);
            const RollbackTransportPacket metadata =
                MakeRollbackGekkoBridgeMetadata(
                    sequence,
                    kRollbackTransportNoFrame,
                    payload,
                    payload_size);
            if (!EncodeRollbackGekkoBridgePacketWithSession(
                    source_peer,
                    dest_peer,
                    session_id,
                    sequence,
                    metadata,
                    payload,
                    payload_size,
                    wire))
            {
                return false;
            }
            return send_bridge_wire(src, dst.local_addr, wire);
        }

        static inline bool receive_one_manual_payload(
            UdpAdapterContext& dst,
            const char* expected_payload) noexcept
        {
            ScopedContext scope(dst);
            int count = 0;
            GekkoNetResult** results = receive_data(&count);
            bool ok = false;
            if (count == 1 && results && results[0] && expected_payload)
            {
                GekkoNetResult* r = results[0];
                ok = r->data
                    && r->data_len == std::strlen(expected_payload)
                    && std::memcmp(
                           r->data,
                           expected_payload,
                           r->data_len)
                        == 0;
            }
            free_results(dst, results, count);
            return ok;
        }

        static inline bool receive_rejects_next_packet(
            UdpAdapterContext& dst,
            uint32_t expected_rejections) noexcept
        {
            ScopedContext scope(dst);
            int count = 0;
            GekkoNetResult** results = receive_data(&count);
            free_results(dst, results, count);
            return count == 0
                && dst.bridge_packets_rejected == expected_rejections;
        }

        static inline bool receive_rejects_wrong_endpoint(
            UdpAdapterContext& dst,
            uint32_t expected_rejections) noexcept
        {
            ScopedContext scope(dst);
            int count = 0;
            GekkoNetResult** results = receive_data(&count);
            free_results(dst, results, count);
            return count == 0
                && dst.endpoint_packets_rejected == expected_rejections;
        }

        static inline void collect_session_events(
            GekkoSession* session,
            UdpAdapterContext& ctx,
            RollbackGekkoUdpAdapterSelfTestReport& report) noexcept
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
            UdpAdapterContext& ctx,
            RollbackGekkoDetail::State& state,
            RollbackGekkoUdpAdapterSelfTestReport& report) noexcept
        {
            ScopedContext scope(ctx);
            int event_count = 0;
            GekkoGameEvent** events =
                gekko_update_session(session, &event_count);
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
                        state,
                        ev->data.adv.frame,
                        inputs,
                        ev->data.adv.rolling_back);
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

    static inline RollbackGekkoUdpAdapterSelfTestReport
    RunRollbackGekkoUdpAdapterSelfTest() noexcept
    {
        RollbackGekkoUdpAdapterSelfTestReport report {};
        report.dependency_enabled = HORSE_ENABLE_GEKKONET != 0;

#if !HORSE_ENABLE_GEKKONET
        report.failure = "gekkonet-disabled";
        return report;
#else
        using namespace RollbackGekkoUdpAdapterDetail;

        WsaSession wsa {};
        report.wsa_started = wsa.ok;
        report.wsa_startup_error = wsa.error;
        if (!wsa.ok)
        {
            report.failure = "wsa-startup-failed";
            return report;
        }

        static UdpAdapterContext ctx_a_storage {};
        static UdpAdapterContext ctx_b_storage {};
        static UdpAdapterContext ctx_c_storage {};
        UdpAdapterContext& ctx_a = ctx_a_storage;
        UdpAdapterContext& ctx_b = ctx_b_storage;
        UdpAdapterContext& ctx_c = ctx_c_storage;
        const bool ctx_a_open = open_udp_context(ctx_a, 0xA1);
        const bool ctx_b_open = open_udp_context(ctx_b, 0xB1);
        const bool ctx_c_open = open_udp_context(ctx_c, 0xC1);
        report.sockets_open =
            ctx_a_open && ctx_b_open && ctx_c_open;
        collect_udp_context_errors(report, ctx_a, ctx_b, ctx_c);
        if (report.sockets_open)
            connect_contexts(ctx_a, ctx_b);
        report.bound_loopback =
            report.sockets_open
            && ctx_a.public_addr.ipv4 == htonl(INADDR_LOOPBACK)
            && ctx_b.public_addr.ipv4 == htonl(INADDR_LOOPBACK)
            && ctx_c.public_addr.ipv4 == htonl(INADDR_LOOPBACK)
            && ctx_a.public_addr.port != 0
            && ctx_b.public_addr.port != 0
            && ctx_c.public_addr.port != 0
            && ctx_a.public_addr.port != ctx_b.public_addr.port
            && ctx_a.public_addr.port != ctx_c.public_addr.port
            && ctx_b.public_addr.port != ctx_c.public_addr.port;
        report.nonblocking =
            report.sockets_open && ctx_a.nonblocking && ctx_b.nonblocking
            && ctx_c.nonblocking;
        report.port_a = ctx_a.public_addr.port;
        report.port_b = ctx_b.public_addr.port;

        GekkoSession* session_a = nullptr;
        GekkoSession* session_b = nullptr;
        auto finish = [&](const char* failure) noexcept {
            bool destroyed_a = true;
            bool destroyed_b = true;
            if (session_a) destroyed_a = gekko_destroy(&session_a);
            if (session_b) destroyed_b = gekko_destroy(&session_b);
            report.destroy_ok = destroyed_a && destroyed_b;
            collect_udp_context_errors(report, ctx_a, ctx_b, ctx_c);
            close_udp_context(ctx_a);
            close_udp_context(ctx_b);
            close_udp_context(ctx_c);
            report.failure = failure;
            report.ok =
                report.dependency_enabled
                && report.wsa_started
                && report.sockets_open
                && report.bound_loopback
                && report.nonblocking
                && report.manual_udp_roundtrip
                && report.wrong_endpoint_rejected
                && report.wrong_source_rejected
                && report.wrong_destination_rejected
                && report.wrong_session_rejected
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
                && report.gameplay_inputs_decoded
                && report.gameplay_slots_present
                && report.gameplay_inputs_drive_state
                && report.final_checksums_match
                && report.destroy_ok;
            if (report.ok)
                report.failure = "ok";
            return report;
        };

        if (!report.sockets_open || !report.bound_loopback
            || !report.nonblocking)
        {
            return finish("socket-setup-failed");
        }

        report.manual_udp_roundtrip =
            send_manual_bridge_packet(
                ctx_a,
                ctx_b,
                ctx_a.peer_id,
                ctx_b.peer_id,
                kUdpSessionId,
                0,
                "manual-good")
            && receive_one_manual_payload(ctx_b, "manual-good");

        const uint32_t endpoint_reject_base =
            ctx_b.endpoint_packets_rejected;
        report.wrong_endpoint_rejected =
            send_manual_bridge_packet(
                ctx_c,
                ctx_b,
                ctx_a.peer_id,
                ctx_b.peer_id,
                kUdpSessionId,
                1,
                "bad-endpoint")
            && receive_rejects_wrong_endpoint(
                ctx_b, endpoint_reject_base + 1);

        const uint32_t reject_base = ctx_b.bridge_packets_rejected;
        report.wrong_source_rejected =
            send_manual_bridge_packet(
                ctx_a,
                ctx_b,
                static_cast<uint8_t>(ctx_a.peer_id + 1),
                ctx_b.peer_id,
                kUdpSessionId,
                2,
                "bad-source")
            && receive_rejects_next_packet(ctx_b, reject_base + 1);
        report.wrong_destination_rejected =
            send_manual_bridge_packet(
                ctx_a,
                ctx_b,
                ctx_a.peer_id,
                static_cast<uint8_t>(ctx_b.peer_id + 1),
                kUdpSessionId,
                3,
                "bad-dest")
            && receive_rejects_next_packet(ctx_b, reject_base + 2);
        report.wrong_session_rejected =
            send_manual_bridge_packet(
                ctx_a,
                ctx_b,
                ctx_a.peer_id,
                ctx_b.peer_id,
                kUdpSessionId + 1,
                4,
                "bad-session")
            && receive_rejects_next_packet(ctx_b, reject_base + 3);

        reset_network_model(ctx_a);
        reset_network_model(ctx_b);
        reset_network_model(ctx_c);
        ctx_a.outbound_delay = 2;
        ctx_b.outbound_delay = 2;

        report.create_ok =
            gekko_create(&session_a, GekkoGameSession)
            && gekko_create(&session_b, GekkoGameSession)
            && session_a
            && session_b;
        if (!report.create_ok)
            return finish("create-failed");

        GekkoConfig config {};
        config.num_players = 2;
        config.max_spectators = 0;
        config.input_prediction_window = 4;
        config.input_size = sizeof(uint32_t);
        config.state_size =
            static_cast<unsigned int>(sizeof(RollbackGekkoDetail::State));
        config.limited_saving = false;
        config.desync_detection = true;
        config.check_distance = 4;

        {
            ScopedContext scope_a(ctx_a);
            gekko_start(session_a, &config);
        }
        {
            ScopedContext scope_b(ctx_b);
            gekko_start(session_b, &config);
        }
        report.start_ok = true;

        {
            ScopedContext scope_a(ctx_a);
            gekko_net_adapter_set(session_a, adapter());
        }
        {
            ScopedContext scope_b(ctx_b);
            gekko_net_adapter_set(session_b, adapter());
        }
        report.adapter_set = true;

        GekkoNetAddress gekko_addr_a {
            &ctx_a.public_addr,
            static_cast<int>(sizeof(ctx_a.public_addr)),
        };
        GekkoNetAddress gekko_addr_b {
            &ctx_b.public_addr,
            static_cast<int>(sizeof(ctx_b.public_addr)),
        };

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

        RollbackGekkoDetail::State state_a {};
        RollbackGekkoDetail::State state_b {};
        report.no_desync = true;

        for (uint32_t step = 0; step < 48; ++step)
        {
            uint32_t p0_input = 0x310u + step;
            uint32_t p1_input = 0x620u ^ (step * 7u);
            {
                ScopedContext scope_a(ctx_a);
                gekko_add_local_input(session_a, a_local, &p0_input);
            }
            {
                ScopedContext scope_b(ctx_b);
                gekko_add_local_input(session_b, b_local, &p1_input);
            }
            ++report.frames_submitted;

            for (uint32_t pump = 0; pump < 3; ++pump)
            {
                if (!pump_update(session_a, ctx_a, state_a, report))
                    return finish(report.failure);
                collect_session_events(session_a, ctx_a, report);
                {
                    ScopedContext scope_a(ctx_a);
                    gekko_network_poll(session_a);
                }
                collect_session_events(session_a, ctx_a, report);

                if (!pump_update(session_b, ctx_b, state_b, report))
                    return finish(report.failure);
                collect_session_events(session_b, ctx_b, report);
                {
                    ScopedContext scope_b(ctx_b);
                    gekko_network_poll(session_b);
                }
                collect_session_events(session_b, ctx_b, report);
            }
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
        report.endpoint_packets_rejected =
            ctx_a.endpoint_packets_rejected + ctx_b.endpoint_packets_rejected;
        report.callbacks_sent = report.packets_sent > 0;
        report.callbacks_received = report.packets_received > 0;
        report.callbacks_freed =
            report.free_calls >= report.packets_received * 3;
        report.bidirectional_payloads =
            ctx_a.packets_sent > 0 && ctx_b.packets_sent > 0
            && ctx_a.packets_received > 0 && ctx_b.packets_received > 0
            && ctx_a.send_failures == 0 && ctx_b.send_failures == 0
            && ctx_a.allocation_failures == 0
            && ctx_b.allocation_failures == 0
            && report.endpoint_packets_rejected == 0;
        report.bridge_roundtrip =
            report.bridge_packets_encoded == report.packets_sent
            && report.bridge_packets_decoded == report.packets_received
            && report.bridge_packets_rejected == 0;
        report.bridge_metadata_accepted =
            ctx_a.bridge_peer.metrics().packets_accepted > 0
            && ctx_b.bridge_peer.metrics().packets_accepted > 0
            && ctx_a.bridge_peer.metrics().highest_remote_frame
                != kRollbackTransportNoFrame
            && ctx_b.bridge_peer.metrics().highest_remote_frame
                != kRollbackTransportNoFrame;
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

        return finish("ok");
#endif
    }
}
