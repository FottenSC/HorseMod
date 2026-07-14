// ============================================================================
// Horse::RollbackSidecar
//
// Tiny, activation-token-bound UDP sidecar used by the two-client acceptance
// harness. This never touches SC6/Steam stock sockets; it only proves that the
// host and sandboxed HorseMod processes can reserve the intended loopback ports
// and see a reciprocal peer before live rollback proof events are accepted.
// ============================================================================

#pragma once

#include "RollbackStockInviteFallback.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#if defined(_WINSOCKAPI_) && !defined(_WINSOCK2API_)
#include <winsock.h>
#else
#include <winsock2.h>
#endif

namespace Horse
{
    static constexpr uint16_t kRollbackSteamReservedUdpPort = 27036u;
    static constexpr uint16_t kRollbackSidecarDefaultHostPort = 47160u;
    static constexpr uint16_t kRollbackSidecarDefaultSandboxPort = 47161u;

    struct RollbackSidecarReport
    {
        bool ok {false};
        bool enabled {false};
        bool direct_input_enabled {false};
        bool wsa_started {false};
        bool socket_open {false};
        bool bound_loopback {false};
        bool nonblocking {false};
        bool udp_connreset_disabled {false};
        bool reserved_steam_port_rejected {false};
        bool sent_hello {false};
        bool received_hello {false};
        bool validated_peer {false};
        bool sent_direct_input {false};
        bool received_direct_input {false};
        bool validated_direct_input {false};
        bool direct_payload_hash_valid {false};
        bool wrong_endpoint_rejected {false};
        bool wrong_route_rejected {false};
        bool wrong_token_rejected {false};
        bool wrong_packet_type_rejected {false};
        bool wrong_direct_sequence_rejected {false};
        bool wrong_direct_payload_rejected {false};
        bool stock_offer_enabled {false};
        bool sent_stock_offer {false};
        bool received_stock_offer {false};
        bool validated_stock_offer {false};
        bool remote_stock_fallback_ready {false};
        bool wrong_stock_offer_rejected {false};
        uint8_t local_peer {0};
        uint8_t remote_peer {0};
        uint8_t local_replay_player {0};
        uint8_t remote_replay_player {0};
        uint16_t local_port {0};
        uint16_t remote_port {0};
        uint64_t session_id {0};
        uint64_t activation_token_hash {0};
        uint64_t local_input_hash {0};
        uint64_t expected_remote_input_hash {0};
        uint64_t remote_input_hash {0};
        uint64_t local_direct_payload_hash {0};
        uint64_t remote_direct_payload_hash {0};
        uint64_t local_steam_id {0};
        uint64_t remote_steam_id {0};
        RollbackStockLobbyOffer remote_stock_offer {};
        uint32_t local_direct_sequence {0};
        uint32_t remote_direct_sequence {0};
        uint32_t local_first_frame {0};
        uint32_t local_frame_count {0};
        uint32_t remote_first_frame {0};
        uint32_t remote_frame_count {0};
        uint32_t packets_sent {0};
        uint32_t packets_received {0};
        uint32_t packets_rejected {0};
        uint32_t direct_packets_sent {0};
        uint32_t direct_packets_received {0};
        uint32_t direct_packets_rejected {0};
        uint32_t stock_offer_packets_sent {0};
        uint32_t stock_offer_packets_received {0};
        uint32_t stock_offer_packets_rejected {0};
        int32_t wsa_startup_error {0};
        int32_t socket_error {0};
        int32_t bind_error {0};
        int32_t ioctlsocket_error {0};
        int32_t udp_connreset_error {0};
        int32_t sendto_error {0};
        int32_t recvfrom_error {0};
        const char* failure {"not-run"};
    };

#pragma pack(push, 1)
    struct RollbackSidecarHelloPacket
    {
        uint32_t magic {0x43535248u}; // "HRSC" little-endian.
        uint16_t version {1};
        uint8_t packet_type {1};
        uint8_t header_bytes {0};
        uint8_t source_peer {0};
        uint8_t destination_peer {0};
        uint64_t session_id {0};
        uint64_t activation_token_hash {0};
        uint16_t source_port {0};
        uint16_t destination_port {0};
        uint32_t flags {0};
        uint64_t local_steam_id {0};
    };

    struct RollbackSidecarDirectInputPacket
    {
        uint32_t magic {0x43535248u}; // "HRSC" little-endian.
        uint16_t version {1};
        uint8_t packet_type {2};
        uint8_t header_bytes {0};
        uint8_t source_peer {0};
        uint8_t destination_peer {0};
        uint8_t local_replay_player {0};
        uint8_t remote_replay_player {0};
        uint64_t session_id {0};
        uint64_t activation_token_hash {0};
        uint16_t source_port {0};
        uint16_t destination_port {0};
        uint32_t sequence {0};
        uint32_t first_frame {0};
        uint32_t frame_count {0};
        uint64_t input_hash {0};
        uint64_t expected_remote_input_hash {0};
        uint64_t payload_hash {0};
        uint32_t flags {0};
    };

    struct RollbackSidecarStockLobbyOfferPacket
    {
        uint32_t magic {0x43535248u}; // "HRSC" little-endian.
        uint16_t version {1};
        uint8_t packet_type {3};
        uint8_t header_bytes {0};
        uint8_t source_peer {0};
        uint8_t destination_peer {0};
        uint64_t session_id {0};
        uint64_t activation_token_hash {0};
        uint16_t source_port {0};
        uint16_t destination_port {0};
        RollbackStockLobbyOffer offer {};
        uint32_t flags {0};
    };
#pragma pack(pop)

    static inline uint64_t RollbackSidecarTokenHash(
        const std::string& token) noexcept
    {
        uint64_t h = 1469598103934665603ull;
        for (unsigned char c : token)
        {
            h ^= static_cast<uint64_t>(c);
            h *= 1099511628211ull;
        }
        return h ? h : 1ull;
    }

    class RollbackSidecarRuntime
    {
    public:
        RollbackSidecarRuntime() = default;
        ~RollbackSidecarRuntime() noexcept
        {
            close();
        }

        RollbackSidecarRuntime(const RollbackSidecarRuntime&) = delete;
        RollbackSidecarRuntime& operator=(
            const RollbackSidecarRuntime&) = delete;

        void configure(
            bool enabled,
            uint8_t local_peer,
            uint8_t remote_peer,
            uint64_t session_id,
            uint16_t local_port,
            uint16_t remote_port,
            const std::string& remote_addr,
            const std::string& activation_token) noexcept
        {
            close();
            m_report = {};
            m_report.enabled = enabled;
            m_report.local_peer = local_peer;
            m_report.remote_peer = remote_peer;
            m_report.session_id = session_id;
            m_report.local_port = local_port;
            m_report.remote_port = remote_port;
            m_report.activation_token_hash =
                RollbackSidecarTokenHash(activation_token);
            m_activation_token_hash = m_report.activation_token_hash;
            m_direct_input_enabled = false;
            m_stock_offer_enabled = false;
            m_direct_sequence = 1;
            if (!enabled)
            {
                m_remote_addr.clear();
                m_enabled = false;
                m_report.failure = "disabled";
                return;
            }
            try
            {
                std::string staged_remote_addr = remote_addr.empty()
                    ? std::string("127.0.0.1") : remote_addr;
                m_remote_addr = std::move(staged_remote_addr);
            }
            catch (...)
            {
                m_remote_addr.clear();
                m_enabled = false;
                m_report.enabled = false;
                m_report.failure = "allocation-failed";
                return;
            }
            m_enabled = true;
        }

        void configure_direct_input(
            bool enabled,
            uint8_t local_replay_player,
            uint8_t remote_replay_player,
            uint32_t first_frame,
            uint32_t frame_count,
            uint64_t local_input_hash,
            uint64_t expected_remote_input_hash) noexcept
        {
            m_direct_input_enabled =
                enabled
                && frame_count > 0
                && local_input_hash != 0
                && expected_remote_input_hash != 0
                && local_replay_player < 2
                && remote_replay_player < 2
                && local_replay_player != remote_replay_player;
            m_report.direct_input_enabled = m_direct_input_enabled;
            m_report.local_replay_player = local_replay_player;
            m_report.remote_replay_player = remote_replay_player;
            m_report.local_first_frame = first_frame;
            m_report.local_frame_count = frame_count;
            m_report.local_input_hash = local_input_hash;
            m_report.expected_remote_input_hash =
                expected_remote_input_hash;
            if (!m_direct_input_enabled)
                return;
            m_direct_sequence =
                m_direct_sequence == 0 ? 1u : m_direct_sequence;
            m_report.local_direct_sequence = m_direct_sequence;
            const RollbackSidecarDirectInputPacket packet =
                make_direct_input();
            m_report.local_direct_payload_hash = packet.payload_hash;
        }

        void configure_stock_lobby_offer(
            bool enabled,
            uint64_t local_steam_id,
            uint64_t request_generation,
            const RollbackStockLobbyOffer& local_offer) noexcept
        {
            m_report.local_steam_id = local_steam_id;
            m_stock_request_generation = request_generation;
            m_local_stock_offer = local_offer;
            m_stock_offer_enabled = enabled && local_steam_id != 0;
            m_report.stock_offer_enabled = m_stock_offer_enabled;
        }

        void close() noexcept
        {
            if (m_socket != INVALID_SOCKET)
            {
                closesocket(m_socket);
                m_socket = INVALID_SOCKET;
            }
            if (m_wsa_started)
            {
                WSACleanup();
                m_wsa_started = false;
            }
            m_opened = false;
        }

        const RollbackSidecarReport& report() const noexcept
        {
            return m_report;
        }

        RollbackSidecarReport tick() noexcept
        {
            if (!m_enabled)
            {
                m_report.ok = false;
                m_report.failure = "disabled";
                return m_report;
            }
            if (!m_opened && !open())
                return m_report;
            send_hello();
            if (m_direct_input_enabled)
                send_direct_input();
            if (m_stock_offer_enabled && m_local_stock_offer.lobby_id != 0)
                send_stock_lobby_offer();
            receive_hellos();
            m_report.ok =
                m_report.validated_peer
                && (!m_direct_input_enabled
                    || m_report.validated_direct_input);
            if (m_report.ok)
                m_report.failure = "ok";
            else if (m_report.failure == nullptr
                     || std::strcmp(m_report.failure, "not-run") == 0
                     || std::strcmp(m_report.failure, "ok") == 0)
                m_report.failure = "waiting-for-peer";
            return m_report;
        }

    private:
        bool open() noexcept
        {
            m_report.failure = "ok";
            if (m_report.local_port == kRollbackSteamReservedUdpPort
                || m_report.remote_port == kRollbackSteamReservedUdpPort)
            {
                m_report.reserved_steam_port_rejected = true;
                m_report.failure = "steam-udp-port-reserved";
                return false;
            }
            if (m_report.local_port == 0 || m_report.remote_port == 0
                || m_report.local_peer == 0 || m_report.remote_peer == 0
                || m_report.local_peer == m_report.remote_peer
                || m_report.session_id == 0)
            {
                m_report.failure = "invalid-sidecar-route";
                return false;
            }

            WSADATA data {};
            m_report.wsa_startup_error = WSAStartup(MAKEWORD(2, 2), &data);
            m_report.wsa_started = m_report.wsa_startup_error == 0;
            m_wsa_started = m_report.wsa_started;
            if (!m_report.wsa_started)
            {
                m_report.failure = "wsa-startup-failed";
                return false;
            }

            m_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (m_socket == INVALID_SOCKET)
            {
                m_report.socket_error = WSAGetLastError();
                m_report.failure = "socket-failed";
                return false;
            }
            m_report.socket_open = true;

            static constexpr long kSioUdpConnreset = 0x9800000C;
            u_long udp_connreset = 0;
            if (ioctlsocket(
                    m_socket,
                    kSioUdpConnreset,
                    &udp_connreset) == SOCKET_ERROR)
            {
                m_report.udp_connreset_error = WSAGetLastError();
                m_report.failure = "udp-connreset-disable-failed";
                return false;
            }
            m_report.udp_connreset_disabled = true;

            sockaddr_in local {};
            local.sin_family = AF_INET;
            local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            local.sin_port = htons(m_report.local_port);
            if (bind(m_socket, reinterpret_cast<const sockaddr*>(&local),
                     sizeof(local)) == SOCKET_ERROR)
            {
                m_report.bind_error = WSAGetLastError();
                m_report.failure = "bind-failed";
                return false;
            }
            m_report.bound_loopback = true;

            u_long nonblocking = 1;
            if (ioctlsocket(m_socket, FIONBIO, &nonblocking) == SOCKET_ERROR)
            {
                m_report.ioctlsocket_error = WSAGetLastError();
                m_report.failure = "nonblocking-failed";
                return false;
            }
            m_report.nonblocking = true;

            m_remote = {};
            m_remote.sin_family = AF_INET;
            m_remote.sin_port = htons(m_report.remote_port);
            if (m_remote_addr == "127.0.0.1" || m_remote_addr.empty())
                m_remote.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            else
                m_remote.sin_addr.s_addr = inet_addr(m_remote_addr.c_str());
            if (m_remote.sin_addr.s_addr == INADDR_NONE)
            {
                m_report.failure = "invalid-remote-address";
                return false;
            }

            m_opened = true;
            return true;
        }

        RollbackSidecarHelloPacket make_hello() const noexcept
        {
            RollbackSidecarHelloPacket packet {};
            packet.header_bytes =
                static_cast<uint8_t>(sizeof(RollbackSidecarHelloPacket));
            packet.source_peer = m_report.local_peer;
            packet.destination_peer = m_report.remote_peer;
            packet.session_id = m_report.session_id;
            packet.activation_token_hash = m_activation_token_hash;
            packet.source_port = m_report.local_port;
            packet.destination_port = m_report.remote_port;
            packet.flags = 1u | (m_stock_offer_enabled ? 2u : 0u);
            packet.local_steam_id = m_report.local_steam_id;
            return packet;
        }

        RollbackSidecarStockLobbyOfferPacket
        make_stock_lobby_offer() const noexcept
        {
            RollbackSidecarStockLobbyOfferPacket packet {};
            packet.header_bytes = static_cast<uint8_t>(sizeof(packet));
            packet.source_peer = m_report.local_peer;
            packet.destination_peer = m_report.remote_peer;
            packet.session_id = m_report.session_id;
            packet.activation_token_hash = m_activation_token_hash;
            packet.source_port = m_report.local_port;
            packet.destination_port = m_report.remote_port;
            packet.offer = m_local_stock_offer;
            if (packet.offer.host_steam_id == 0)
                packet.offer.host_steam_id = m_report.local_steam_id;
            if (packet.offer.invitee_steam_id == 0)
                packet.offer.invitee_steam_id = m_report.remote_steam_id;
            packet.offer.authentication_tag = RollbackStockLobbyOfferTag(
                packet.offer, m_activation_token_hash);
            packet.flags = 1u;
            return packet;
        }

        static uint64_t hash_direct_input_packet(
            RollbackSidecarDirectInputPacket packet) noexcept
        {
            packet.payload_hash = 0;
            const uint8_t* bytes =
                reinterpret_cast<const uint8_t*>(&packet);
            uint64_t h = 1469598103934665603ull;
            for (size_t i = 0; i < sizeof(packet); ++i)
            {
                h ^= static_cast<uint64_t>(bytes[i]);
                h *= 1099511628211ull;
            }
            return h ? h : 1ull;
        }

        RollbackSidecarDirectInputPacket make_direct_input() const noexcept
        {
            RollbackSidecarDirectInputPacket packet {};
            packet.header_bytes = static_cast<uint8_t>(
                sizeof(RollbackSidecarDirectInputPacket));
            packet.source_peer = m_report.local_peer;
            packet.destination_peer = m_report.remote_peer;
            packet.local_replay_player = m_report.local_replay_player;
            packet.remote_replay_player = m_report.remote_replay_player;
            packet.session_id = m_report.session_id;
            packet.activation_token_hash = m_activation_token_hash;
            packet.source_port = m_report.local_port;
            packet.destination_port = m_report.remote_port;
            packet.sequence = m_direct_sequence;
            packet.first_frame = m_report.local_first_frame;
            packet.frame_count = m_report.local_frame_count;
            packet.input_hash = m_report.local_input_hash;
            packet.expected_remote_input_hash =
                m_report.expected_remote_input_hash;
            packet.flags = 1u;
            packet.payload_hash = hash_direct_input_packet(packet);
            return packet;
        }

        void send_hello() noexcept
        {
            const RollbackSidecarHelloPacket packet = make_hello();
            const int sent = sendto(
                m_socket,
                reinterpret_cast<const char*>(&packet),
                static_cast<int>(sizeof(packet)),
                0,
                reinterpret_cast<const sockaddr*>(&m_remote),
                sizeof(m_remote));
            if (sent == static_cast<int>(sizeof(packet)))
            {
                m_report.sent_hello = true;
                ++m_report.packets_sent;
                return;
            }
            if (sent == SOCKET_ERROR)
                m_report.sendto_error = WSAGetLastError();
            m_report.failure = "sendto-failed";
        }

        void send_direct_input() noexcept
        {
            const RollbackSidecarDirectInputPacket packet =
                make_direct_input();
            const int sent = sendto(
                m_socket,
                reinterpret_cast<const char*>(&packet),
                static_cast<int>(sizeof(packet)),
                0,
                reinterpret_cast<const sockaddr*>(&m_remote),
                sizeof(m_remote));
            if (sent == static_cast<int>(sizeof(packet)))
            {
                m_report.sent_direct_input = true;
                m_report.local_direct_payload_hash = packet.payload_hash;
                ++m_report.direct_packets_sent;
                ++m_report.packets_sent;
                return;
            }
            if (sent == SOCKET_ERROR)
                m_report.sendto_error = WSAGetLastError();
            m_report.failure = "sendto-failed";
        }

        void send_stock_lobby_offer() noexcept
        {
            const RollbackSidecarStockLobbyOfferPacket packet =
                make_stock_lobby_offer();
            if (packet.offer.invitee_steam_id == 0) return;
            const int sent = sendto(
                m_socket,
                reinterpret_cast<const char*>(&packet),
                static_cast<int>(sizeof(packet)),
                0,
                reinterpret_cast<const sockaddr*>(&m_remote),
                sizeof(m_remote));
            if (sent == static_cast<int>(sizeof(packet)))
            {
                m_report.sent_stock_offer = true;
                ++m_report.stock_offer_packets_sent;
                ++m_report.packets_sent;
                return;
            }
            if (sent == SOCKET_ERROR)
                m_report.sendto_error = WSAGetLastError();
            m_report.failure = "stock-offer-sendto-failed";
        }

        static bool endpoint_matches(
            const sockaddr_in& actual,
            uint16_t expected_port) noexcept
        {
            return actual.sin_family == AF_INET
                && actual.sin_addr.s_addr == htonl(INADDR_LOOPBACK)
                && actual.sin_port == htons(expected_port);
        }

        void receive_hellos() noexcept
        {
            for (uint32_t i = 0; i < 16; ++i)
            {
                constexpr size_t kMaxPacketBytes =
                    sizeof(RollbackSidecarStockLobbyOfferPacket)
                        > sizeof(RollbackSidecarDirectInputPacket)
                    ? sizeof(RollbackSidecarStockLobbyOfferPacket)
                    : sizeof(RollbackSidecarDirectInputPacket);
                std::array<uint8_t, kMaxPacketBytes>
                    buffer {};
                sockaddr_in from {};
                int from_len = sizeof(from);
                const int received = recvfrom(
                    m_socket,
                    reinterpret_cast<char*>(buffer.data()),
                    static_cast<int>(buffer.size()),
                    0,
                    reinterpret_cast<sockaddr*>(&from),
                    &from_len);
                if (received == SOCKET_ERROR)
                {
                    const int err = WSAGetLastError();
                    if (err != WSAEWOULDBLOCK)
                    {
                        m_report.recvfrom_error = err;
                        m_report.failure = "recvfrom-failed";
                    }
                    return;
                }
                if (received == static_cast<int>(
                        sizeof(RollbackSidecarHelloPacket)))
                {
                    RollbackSidecarHelloPacket packet {};
                    std::memcpy(&packet, buffer.data(), sizeof(packet));
                    receive_hello_packet(packet, from);
                    continue;
                }
                if (received == static_cast<int>(
                        sizeof(RollbackSidecarDirectInputPacket)))
                {
                    RollbackSidecarDirectInputPacket packet {};
                    std::memcpy(&packet, buffer.data(), sizeof(packet));
                    receive_direct_input_packet(packet, from);
                    continue;
                }
                if (received == static_cast<int>(
                        sizeof(RollbackSidecarStockLobbyOfferPacket)))
                {
                    RollbackSidecarStockLobbyOfferPacket packet {};
                    std::memcpy(&packet, buffer.data(), sizeof(packet));
                    receive_stock_lobby_offer_packet(packet, from);
                    continue;
                }

                ++m_report.packets_rejected;
                ++m_report.direct_packets_rejected;
                m_report.wrong_packet_type_rejected = true;
            }
        }

        void receive_hello_packet(
            const RollbackSidecarHelloPacket& packet,
            const sockaddr_in& from) noexcept
        {
            ++m_report.packets_received;
            m_report.received_hello = true;

            if (!endpoint_matches(from, m_report.remote_port))
            {
                m_report.wrong_endpoint_rejected = true;
                ++m_report.packets_rejected;
                return;
            }
            if (packet.magic != 0x43535248u || packet.version != 1
                || packet.packet_type != 1
                || packet.header_bytes != sizeof(RollbackSidecarHelloPacket)
                || packet.source_peer != m_report.remote_peer
                || packet.destination_peer != m_report.local_peer
                || packet.session_id != m_report.session_id
                || packet.source_port != m_report.remote_port
                || packet.destination_port != m_report.local_port)
            {
                m_report.wrong_route_rejected = true;
                ++m_report.packets_rejected;
                return;
            }
            if (packet.activation_token_hash != m_activation_token_hash)
            {
                m_report.wrong_token_rejected = true;
                ++m_report.packets_rejected;
                return;
            }
            m_report.validated_peer = true;
            m_report.remote_steam_id = packet.local_steam_id;
            m_report.remote_stock_fallback_ready =
                (packet.flags & 2u) != 0;
        }

        void receive_stock_lobby_offer_packet(
            const RollbackSidecarStockLobbyOfferPacket& packet,
            const sockaddr_in& from) noexcept
        {
            ++m_report.packets_received;
            ++m_report.stock_offer_packets_received;
            m_report.received_stock_offer = true;
            const bool route_ok = endpoint_matches(from, m_report.remote_port)
                && packet.magic == 0x43535248u
                && packet.version == 1
                && packet.packet_type == 3
                && packet.header_bytes == sizeof(packet)
                && packet.source_peer == m_report.remote_peer
                && packet.destination_peer == m_report.local_peer
                && packet.session_id == m_report.session_id
                && packet.source_port == m_report.remote_port
                && packet.destination_port == m_report.local_port;
            const bool token_ok =
                packet.activation_token_hash == m_activation_token_hash;
            const bool offer_ok = packet.offer.lobby_id != 0
                && packet.offer.host_steam_id != 0
                && packet.offer.host_steam_id == m_report.remote_steam_id
                && packet.offer.invitee_steam_id == m_report.local_steam_id
                && packet.offer.owner_steam_id == packet.offer.host_steam_id
                && packet.offer.request_generation ==
                    m_stock_request_generation
                && packet.offer.build_id != 0
                && packet.offer.schema_id != 0
                && packet.offer.authentication_tag
                    == RollbackStockLobbyOfferTag(
                        packet.offer, m_activation_token_hash);
            if (!route_ok || !token_ok || !offer_ok)
            {
                m_report.wrong_stock_offer_rejected = true;
                ++m_report.packets_rejected;
                ++m_report.stock_offer_packets_rejected;
                return;
            }
            m_report.remote_stock_offer = packet.offer;
            m_report.validated_stock_offer = true;
        }

        void receive_direct_input_packet(
            const RollbackSidecarDirectInputPacket& packet,
            const sockaddr_in& from) noexcept
        {
            ++m_report.packets_received;
            ++m_report.direct_packets_received;
            m_report.received_direct_input = true;

            if (!endpoint_matches(from, m_report.remote_port))
            {
                m_report.wrong_endpoint_rejected = true;
                ++m_report.packets_rejected;
                ++m_report.direct_packets_rejected;
                return;
            }
            if (packet.magic != 0x43535248u || packet.version != 1
                || packet.packet_type != 2
                || packet.header_bytes
                       != sizeof(RollbackSidecarDirectInputPacket)
                || packet.source_peer != m_report.remote_peer
                || packet.destination_peer != m_report.local_peer
                || packet.session_id != m_report.session_id
                || packet.source_port != m_report.remote_port
                || packet.destination_port != m_report.local_port)
            {
                m_report.wrong_route_rejected = true;
                ++m_report.packets_rejected;
                ++m_report.direct_packets_rejected;
                return;
            }
            if (packet.activation_token_hash != m_activation_token_hash)
            {
                m_report.wrong_token_rejected = true;
                ++m_report.packets_rejected;
                ++m_report.direct_packets_rejected;
                return;
            }
            if (packet.sequence == 0
                || (m_report.remote_direct_sequence != 0
                    && packet.sequence < m_report.remote_direct_sequence))
            {
                m_report.wrong_direct_sequence_rejected = true;
                ++m_report.packets_rejected;
                ++m_report.direct_packets_rejected;
                return;
            }
            if (packet.payload_hash != hash_direct_input_packet(packet))
            {
                m_report.wrong_direct_payload_rejected = true;
                ++m_report.packets_rejected;
                ++m_report.direct_packets_rejected;
                return;
            }
            const bool route_matches_mapping =
                packet.local_replay_player == m_report.remote_replay_player
                && packet.remote_replay_player == m_report.local_replay_player;
            const bool payload_matches_expectation =
                packet.input_hash == m_report.expected_remote_input_hash
                && packet.expected_remote_input_hash
                       == m_report.local_input_hash
                && packet.frame_count == m_report.local_frame_count;
            if (!route_matches_mapping || !payload_matches_expectation)
            {
                m_report.wrong_direct_payload_rejected = true;
                ++m_report.packets_rejected;
                ++m_report.direct_packets_rejected;
                return;
            }
            m_report.remote_replay_player = packet.local_replay_player;
            m_report.remote_first_frame = packet.first_frame;
            m_report.remote_frame_count = packet.frame_count;
            m_report.remote_input_hash = packet.input_hash;
            m_report.remote_direct_sequence = packet.sequence;
            m_report.remote_direct_payload_hash = packet.payload_hash;
            m_report.direct_payload_hash_valid = true;
            m_report.validated_direct_input = true;
        }

        RollbackSidecarReport m_report {};
        SOCKET m_socket {INVALID_SOCKET};
        sockaddr_in m_remote {};
        std::string m_remote_addr {"127.0.0.1"};
        uint64_t m_activation_token_hash {0};
        uint64_t m_stock_request_generation {0};
        uint32_t m_direct_sequence {1};
        RollbackStockLobbyOffer m_local_stock_offer {};
        bool m_direct_input_enabled {false};
        bool m_stock_offer_enabled {false};
        bool m_enabled {false};
        bool m_opened {false};
        bool m_wsa_started {false};
    };
}
