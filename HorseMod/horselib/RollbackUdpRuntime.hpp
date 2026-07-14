// ============================================================================
// Horse::RollbackUdpRuntime
//
// Shared RAII UDP endpoint and authenticated network worker.  The worker owns
// socket I/O only; Gekko and simulation stay on the game thread.
// ============================================================================

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <WinSock2.h>
#include <WS2tcpip.h>

#include "RollbackFrameStamp.hpp"
#include "RollbackFaultInject.hpp"
#include "RollbackLaunchContract.hpp"
#include "RollbackProtocolV2.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <thread>

namespace Horse
{
    enum class RollbackSessionDomain : uint8_t
    {
        Production = 0,
        ReplayForkLab = 1,
    };

    static constexpr const char* RollbackSessionDomainName(
        RollbackSessionDomain domain) noexcept
    {
        return domain == RollbackSessionDomain::ReplayForkLab
            ? "replay-fork-lab" : "production";
    }

    static constexpr bool RollbackSessionDomainValid(
        RollbackSessionDomain domain) noexcept
    {
        return domain == RollbackSessionDomain::Production
            || domain == RollbackSessionDomain::ReplayForkLab;
    }

    static constexpr uint8_t kRollbackUdpHandshakeProfileVersion = 4;
    static constexpr uint32_t kRollbackUdpHeartbeatMs = 250;
    static constexpr uint32_t kRollbackUdpReadinessExpiryMs = 2000;
    static constexpr uint32_t kRollbackUdpReopenMinMs = 250;
    static constexpr uint32_t kRollbackUdpReopenMaxMs = 4000;

#pragma pack(push, 1)
    struct RollbackUdpHandshakeProfile
    {
        uint8_t profile_version {kRollbackUdpHandshakeProfileVersion};
        uint8_t local_player_slot {0};
        RollbackLifecycleMode lifecycle_mode {
            RollbackLifecycleMode::StockOnlinePvp};
        uint8_t native_input_source_slot {0};
        uint16_t rollback_window {0};
        uint16_t input_delay {0};
        uint32_t launch_seed {0};
        RollbackNetworkProfileKind network_profile {
            RollbackNetworkProfileKind::Clean0ms};
        RollbackSessionDomain session_domain {
            RollbackSessionDomain::Production};
        uint8_t reserved8[2] {};
        uint32_t fault_seed {0};
        uint32_t reserved {0};
        uint64_t desired_launch_descriptor_hash {0};
        std::array<uint8_t, 32> replay_sha256 {};
        int32_t replay_anchor_sequence {-1};
        int32_t replay_anchor_round {-1};
        int32_t replay_anchor_master {-1};
        uint64_t replay_run_nonce_hash {0};
        uint64_t expected_build_id {0};
        uint64_t expected_schema_id {0};
    };
#pragma pack(pop)

    static_assert(sizeof(RollbackUdpHandshakeProfile) == 100);

    struct RollbackSequenceStamp
    {
        uint64_t value {0};
        bool valid {false};

        static constexpr RollbackSequenceStamp From(
            uint64_t sequence) noexcept
        {
            return RollbackSequenceStamp {sequence, true};
        }

        constexpr void clear() noexcept
        {
            value = 0;
            valid = false;
        }
    };

    struct RollbackProductionConfig
    {
        bool enabled {false};
        std::string bind_address {"0.0.0.0"};
        uint16_t bind_port {0};
        std::string peer_address;
        uint16_t peer_port {0};
        uint8_t local_player_slot {0};
        uint8_t native_input_source_slot {0};
        uint8_t local_peer {1};
        uint8_t remote_peer {2};
        RollbackLifecycleMode lifecycle_mode {
            RollbackLifecycleMode::StockOnlinePvp};
        RollbackSessionDomain session_domain {
            RollbackSessionDomain::Production};
        RollbackBattleLaunchDescriptor launch_descriptor {};
        std::array<uint8_t, 32> replay_sha256 {};
        int32_t replay_anchor_sequence {-1};
        int32_t replay_anchor_round {-1};
        int32_t replay_anchor_master {-1};
        uint64_t replay_run_nonce_hash {0};
        std::string secret;
        uint16_t rollback_window {60};
        uint16_t input_delay {1};
        RollbackNetworkProfileKind network_profile {
            RollbackNetworkProfileKind::Clean0ms};
        uint32_t fault_seed {0x5C6B0001u};
        uint64_t expected_build_id {0};
        uint64_t expected_schema_id {0};

        bool valid() const noexcept
        {
            return enabled
                && bind_port != 0
                && peer_port != 0
                && !peer_address.empty()
                && local_player_slot < 2
                && native_input_source_slot < 2
                && local_peer != 0
                && remote_peer != 0
                && local_peer != remote_peer
                && secret.size() >= 16
                && rollback_window != 0
                && rollback_window <= 60
                && input_delay != 0
                && input_delay <= rollback_window
                && static_cast<uint8_t>(network_profile)
                    <= static_cast<uint8_t>(
                        RollbackNetworkProfileKind::CorruptProbe)
                && fault_seed != 0
                && expected_build_id != 0
                && expected_schema_id != 0
                && RollbackLifecycleModeValid(lifecycle_mode)
                && RollbackSessionDomainValid(session_domain)
                && (session_domain == RollbackSessionDomain::ReplayForkLab
                    ? (native_input_source_slot == local_player_slot
                       && std::any_of(
                           replay_sha256.begin(), replay_sha256.end(),
                           [](uint8_t value) { return value != 0; })
                       && replay_anchor_sequence >= 0
                       && replay_anchor_round >= 0
                       && replay_anchor_master >= 0
                       && replay_run_nonce_hash != 0)
                    : (lifecycle_mode
                            == RollbackLifecycleMode::StockOnlinePvp
                        ? native_input_source_slot == local_player_slot
                        : (native_input_source_slot == 0
                           && launch_descriptor.valid())));
        }
    };

    static inline bool RollbackProductionConfigEquivalent(
        const RollbackProductionConfig& left,
        const RollbackProductionConfig& right) noexcept
    {
        const RollbackBattleLaunchDescriptor& left_launch =
            left.launch_descriptor;
        const RollbackBattleLaunchDescriptor& right_launch =
            right.launch_descriptor;
        return left.enabled == right.enabled
            && left.bind_address == right.bind_address
            && left.bind_port == right.bind_port
            && left.peer_address == right.peer_address
            && left.peer_port == right.peer_port
            && left.local_player_slot == right.local_player_slot
            && left.native_input_source_slot
                == right.native_input_source_slot
            && left.local_peer == right.local_peer
            && left.remote_peer == right.remote_peer
            && left.lifecycle_mode == right.lifecycle_mode
            && left.session_domain == right.session_domain
            && left_launch.left_character == right_launch.left_character
            && left_launch.right_character == right_launch.right_character
            && left_launch.left_color == right_launch.left_color
            && left_launch.right_color == right_launch.right_color
            && left_launch.stage == right_launch.stage
            && left_launch.battle_time_seconds
                == right_launch.battle_time_seconds
            && left_launch.battle_rule_type
                == right_launch.battle_rule_type
            && left_launch.versus_type == right_launch.versus_type
            && left_launch.seed == right_launch.seed
            && left_launch.auto_start == right_launch.auto_start
            && left_launch.local_battle_provider
                == right_launch.local_battle_provider
            && left.replay_sha256 == right.replay_sha256
            && left.replay_anchor_sequence == right.replay_anchor_sequence
            && left.replay_anchor_round == right.replay_anchor_round
            && left.replay_anchor_master == right.replay_anchor_master
            && left.replay_run_nonce_hash == right.replay_run_nonce_hash
            && left.secret == right.secret
            && left.rollback_window == right.rollback_window
            && left.input_delay == right.input_delay
            && left.network_profile == right.network_profile
            && left.fault_seed == right.fault_seed
            && left.expected_build_id == right.expected_build_id
            && left.expected_schema_id == right.expected_schema_id;
    }

    template<typename T, size_t N>
    class RollbackBoundedSpscQueue
    {
    public:
        static_assert(N >= 2, "queue requires at least two cells");

        bool push(const T& value) noexcept
        {
            if (!m_cells) return false;
            const size_t head = m_head.load(std::memory_order_relaxed);
            const size_t next = (head + 1) % N;
            if (next == m_tail.load(std::memory_order_acquire))
                return false;
            m_cells[head] = value;
            m_head.store(next, std::memory_order_release);
            return true;
        }

        bool pop(T& value) noexcept
        {
            if (!m_cells) return false;
            const size_t tail = m_tail.load(std::memory_order_relaxed);
            if (tail == m_head.load(std::memory_order_acquire))
                return false;
            value = m_cells[tail];
            m_tail.store((tail + 1) % N, std::memory_order_release);
            return true;
        }

        void clear() noexcept
        {
            m_tail.store(0, std::memory_order_relaxed);
            m_head.store(0, std::memory_order_relaxed);
        }

        bool initialized() const noexcept
        {
            return static_cast<bool>(m_cells);
        }

    private:
        std::unique_ptr<T[]> m_cells {new (std::nothrow) T[N]};
        std::atomic<size_t> m_head {0};
        std::atomic<size_t> m_tail {0};
    };

    struct RollbackUdpEndpointReport
    {
        bool wsa_started {false};
        bool socket_open {false};
        bool bound {false};
        bool nonblocking {false};
        uint16_t bound_port {0};
        int32_t wsa_error {0};
        int32_t socket_error {0};
        int32_t bind_error {0};
        int32_t address_error {0};
        int32_t nonblocking_error {0};
        int32_t send_error {0};
        int32_t receive_error {0};
    };

    class RollbackUdpEndpoint
    {
    public:
        RollbackUdpEndpoint() = default;
        ~RollbackUdpEndpoint() noexcept { close(); }
        RollbackUdpEndpoint(const RollbackUdpEndpoint&) = delete;
        RollbackUdpEndpoint& operator=(const RollbackUdpEndpoint&) = delete;

        bool open(
            const std::string& bind_address,
            uint16_t bind_port) noexcept
        {
            close();
            m_report = {};
            WSADATA data {};
            m_report.wsa_error = WSAStartup(MAKEWORD(2, 2), &data);
            if (m_report.wsa_error != 0)
                return false;
            m_wsa_started = true;
            m_report.wsa_started = true;

            m_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (m_socket == INVALID_SOCKET)
            {
                m_report.socket_error = WSAGetLastError();
                close_preserving_report();
                return false;
            }
            m_report.socket_open = true;

            static constexpr DWORD kSioUdpConnreset = 0x9800000C;
            BOOL disable_connreset = FALSE;
            DWORD returned = 0;
            (void)WSAIoctl(
                m_socket,
                kSioUdpConnreset,
                &disable_connreset,
                sizeof(disable_connreset),
                nullptr,
                0,
                &returned,
                nullptr,
                nullptr);

            sockaddr_in local {};
            local.sin_family = AF_INET;
            local.sin_port = htons(bind_port);
            if (bind_address.empty() || bind_address == "0.0.0.0")
                local.sin_addr.s_addr = htonl(INADDR_ANY);
            else if (InetPtonA(
                    AF_INET, bind_address.c_str(), &local.sin_addr) != 1)
            {
                m_report.address_error = WSAGetLastError();
                close_preserving_report();
                return false;
            }
            if (bind(
                    m_socket,
                    reinterpret_cast<const sockaddr*>(&local),
                    sizeof(local)) == SOCKET_ERROR)
            {
                m_report.bind_error = WSAGetLastError();
                close_preserving_report();
                return false;
            }
            m_report.bound = true;

            sockaddr_in actual {};
            int actual_bytes = sizeof(actual);
            if (getsockname(
                    m_socket,
                    reinterpret_cast<sockaddr*>(&actual),
                    &actual_bytes) == SOCKET_ERROR)
            {
                m_report.bind_error = WSAGetLastError();
                close_preserving_report();
                return false;
            }
            m_report.bound_port = ntohs(actual.sin_port);

            u_long nonblocking = 1;
            if (ioctlsocket(
                    m_socket, FIONBIO, &nonblocking) == SOCKET_ERROR)
            {
                m_report.nonblocking_error = WSAGetLastError();
                close_preserving_report();
                return false;
            }
            m_report.nonblocking = true;
            return true;
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
            m_report.socket_open = false;
            m_report.wsa_started = false;
        }

        bool is_open() const noexcept
        {
            return m_socket != INVALID_SOCKET && m_wsa_started;
        }

        const RollbackUdpEndpointReport& report() const noexcept
        {
            return m_report;
        }

        static bool parse_address(
            const std::string& address,
            uint16_t port,
            sockaddr_in& out) noexcept
        {
            out = {};
            out.sin_family = AF_INET;
            out.sin_port = htons(port);
            return port != 0
                && InetPtonA(AF_INET, address.c_str(), &out.sin_addr) == 1;
        }

        bool send(
            const RollbackProtocolV2WirePacket& packet,
            const sockaddr_in& destination) noexcept
        {
            if (!is_open() || packet.size == 0) return false;
            const int sent = sendto(
                m_socket,
                reinterpret_cast<const char*>(packet.bytes.data()),
                packet.size,
                0,
                reinterpret_cast<const sockaddr*>(&destination),
                sizeof(destination));
            if (sent == packet.size) return true;
            m_report.send_error = sent == SOCKET_ERROR
                ? WSAGetLastError() : WSAEMSGSIZE;
            return false;
        }

        enum class ReceiveStatus : uint8_t
        {
            NoData,
            Packet,
            Error,
        };

        ReceiveStatus wait_readable(uint32_t timeout_ms) noexcept
        {
            if (!is_open()) return ReceiveStatus::Error;
            fd_set read_set;
            FD_ZERO(&read_set);
            FD_SET(m_socket, &read_set);
            timeval timeout {};
            timeout.tv_sec = static_cast<long>(timeout_ms / 1000u);
            timeout.tv_usec = static_cast<long>(
                (timeout_ms % 1000u) * 1000u);
            const int ready = select(
                0, &read_set, nullptr, nullptr, &timeout);
            if (ready > 0 && FD_ISSET(m_socket, &read_set))
                return ReceiveStatus::Packet;
            if (ready == 0) return ReceiveStatus::NoData;
            m_report.receive_error = WSAGetLastError();
            return ReceiveStatus::Error;
        }

        ReceiveStatus receive(
            RollbackProtocolV2WirePacket& packet,
            sockaddr_in& from) noexcept
        {
            packet = {};
            from = {};
            if (!is_open()) return ReceiveStatus::Error;
            int from_bytes = sizeof(from);
            const int received = recvfrom(
                m_socket,
                reinterpret_cast<char*>(packet.bytes.data()),
                static_cast<int>(packet.bytes.size()),
                0,
                reinterpret_cast<sockaddr*>(&from),
                &from_bytes);
            if (received == SOCKET_ERROR)
            {
                const int error = WSAGetLastError();
                if (error == WSAEWOULDBLOCK)
                    return ReceiveStatus::NoData;
                m_report.receive_error = error;
                return ReceiveStatus::Error;
            }
            if (received <= 0
                || received > static_cast<int>(packet.bytes.size()))
            {
                m_report.receive_error = WSAEMSGSIZE;
                return ReceiveStatus::Error;
            }
            packet.size = static_cast<uint16_t>(received);
            return ReceiveStatus::Packet;
        }

    private:
        void close_preserving_report() noexcept
        {
            const RollbackUdpEndpointReport saved = m_report;
            close();
            m_report = saved;
            m_report.socket_open = false;
            m_report.wsa_started = false;
        }

        SOCKET m_socket {INVALID_SOCKET};
        bool m_wsa_started {false};
        RollbackUdpEndpointReport m_report {};
    };

    struct RollbackUdpMessage
    {
        RollbackProtocolV2PacketType packet_type {
            RollbackProtocolV2PacketType::Heartbeat};
        RollbackSequenceStamp ack {};
        uint64_t handshake_generation {0};
        uint64_t sequence {0};
        uint16_t payload_bytes {0};
        std::array<uint8_t, kRollbackProtocolV2MaxPayloadBytes> payload {};
    };

    enum class RollbackUdpWorkerFailure : uint8_t
    {
        None,
        InvalidConfig,
        EndpointOpenFailed,
        EndpointIoFailed,
        AuthenticationFailed,
        RouteRejected,
        ReplayRejected,
        QueueOverflow,
        PeerTimeout,
        NonceGenerationFailed,
        PeerSessionChanged,
        ResourceAllocationFailed,
    };

    static constexpr const char* RollbackUdpWorkerFailureName(
        RollbackUdpWorkerFailure failure) noexcept
    {
        switch (failure)
        {
        case RollbackUdpWorkerFailure::None: return "none";
        case RollbackUdpWorkerFailure::InvalidConfig:
            return "invalid-config";
        case RollbackUdpWorkerFailure::EndpointOpenFailed:
            return "endpoint-open-failed";
        case RollbackUdpWorkerFailure::EndpointIoFailed:
            return "endpoint-io-failed";
        case RollbackUdpWorkerFailure::AuthenticationFailed:
            return "authentication-failed";
        case RollbackUdpWorkerFailure::RouteRejected:
            return "route-rejected";
        case RollbackUdpWorkerFailure::ReplayRejected:
            return "replay-rejected";
        case RollbackUdpWorkerFailure::QueueOverflow:
            return "queue-overflow";
        case RollbackUdpWorkerFailure::PeerTimeout: return "peer-timeout";
        case RollbackUdpWorkerFailure::NonceGenerationFailed:
            return "nonce-generation-failed";
        case RollbackUdpWorkerFailure::PeerSessionChanged:
            return "peer-session-changed";
        case RollbackUdpWorkerFailure::ResourceAllocationFailed:
            return "resource-allocation-failed";
        }
        return "unknown";
    }

    struct RollbackUdpWorkerStatus
    {
        bool running {false};
        bool endpoint_open {false};
        bool peer_ready {false};
        bool endpoint_pinned {false};
        uint64_t packets_sent {0};
        uint64_t packets_received {0};
        uint64_t packets_authenticated {0};
        uint64_t packets_rejected {0};
        uint64_t queue_overflows {0};
        uint64_t reopen_count {0};
        uint64_t handshake_generation {0};
        RollbackNetworkProfileKind network_profile {
            RollbackNetworkProfileKind::Clean0ms};
        uint32_t fault_seed {0};
        uint64_t fault_packets_submitted {0};
        uint64_t fault_packets_queued {0};
        uint64_t fault_packets_delivered {0};
        uint64_t fault_packets_dropped {0};
        uint64_t fault_packets_duplicated {0};
        uint64_t fault_packets_reordered {0};
        uint64_t fault_packets_corrupted {0};
        uint64_t fault_packets_spiked {0};
        uint64_t fault_packets_burst_dropped {0};
        uint64_t fault_queue_overflows {0};
        RollbackUdpWorkerFailure failure {RollbackUdpWorkerFailure::None};
    };

    class RollbackUdpNetworkWorker
    {
    public:
        RollbackUdpNetworkWorker() = default;
        ~RollbackUdpNetworkWorker() noexcept { stop(); }
        RollbackUdpNetworkWorker(const RollbackUdpNetworkWorker&) = delete;
        RollbackUdpNetworkWorker& operator=(
            const RollbackUdpNetworkWorker&) = delete;

        bool start(const RollbackProductionConfig& config) noexcept
        {
            stop();
            if (!m_outbound.initialized() || !m_inbound.initialized()
                || !m_fault_datagrams)
            {
                m_failure.store(
                    RollbackUdpWorkerFailure::ResourceAllocationFailed,
                    std::memory_order_release);
                return false;
            }
            if (!config.valid())
            {
                m_failure.store(
                    RollbackUdpWorkerFailure::InvalidConfig,
                    std::memory_order_release);
                return false;
            }
            sockaddr_in expected_peer {};
            if (!RollbackUdpEndpoint::parse_address(
                    config.peer_address, config.peer_port, expected_peer))
            {
                m_failure.store(
                    RollbackUdpWorkerFailure::InvalidConfig,
                    std::memory_order_release);
                return false;
            }
            try
            {
                m_config = config;
            }
            catch (...)
            {
                m_config.enabled = false;
                m_failure.store(
                    RollbackUdpWorkerFailure::ResourceAllocationFailed,
                    std::memory_order_release);
                return false;
            }
            m_expected_peer = expected_peer;
            m_outbound.clear();
            m_inbound.clear();
            reset_status();
            m_stop.store(false, std::memory_order_release);
            try
            {
                m_thread = std::thread([this]() noexcept { run(); });
            }
            catch (...)
            {
                m_stop.store(true, std::memory_order_release);
                m_failure.store(
                    RollbackUdpWorkerFailure::EndpointOpenFailed,
                    std::memory_order_release);
                return false;
            }
            return true;
        }

        void stop() noexcept
        {
            m_stop.store(true, std::memory_order_release);
            if (m_thread.joinable())
            {
                if (m_thread.get_id() == std::this_thread::get_id())
                {
                    // The worker will observe m_stop and exit.  Its owner must
                    // join it later from the game thread; self-join would
                    // throw and terminate this noexcept teardown path.
                    m_failure.store(
                        RollbackUdpWorkerFailure::EndpointIoFailed,
                        std::memory_order_release);
                    return;
                }
                try
                {
                    m_thread.join();
                }
                catch (...)
                {
                    m_failure.store(
                        RollbackUdpWorkerFailure::EndpointIoFailed,
                        std::memory_order_release);
                    (void)WaitForSingleObject(
                        static_cast<HANDLE>(m_thread.native_handle()),
                        INFINITE);
                    try
                    {
                        m_thread.detach();
                    }
                    catch (...)
                    {
                        // A joinable, non-self std::thread should always
                        // support either join or detach.  Preserve the fault
                        // latch if the C++ runtime violates that contract.
                    }
                    return;
                }
            }
            m_running.store(false, std::memory_order_release);
            m_ready.store(false, std::memory_order_release);
            m_endpoint_open.store(false, std::memory_order_release);
        }

        bool enqueue(
            RollbackProtocolV2PacketType type,
            const void* payload,
            uint16_t payload_bytes,
            RollbackSequenceStamp ack = {},
            uint64_t expected_generation = UINT64_MAX) noexcept
        {
            if (payload_bytes > kRollbackProtocolV2MaxPayloadBytes
                || (payload_bytes != 0 && !payload))
            {
                return false;
            }
            RollbackUdpMessage message {};
            message.packet_type = type;
            message.ack = ack;
            const uint64_t current_generation =
                m_handshake_generation.load(std::memory_order_acquire);
            if (expected_generation != UINT64_MAX
                && current_generation != expected_generation)
            {
                return false;
            }
            message.handshake_generation = expected_generation != UINT64_MAX
                ? expected_generation : current_generation;
            message.payload_bytes = payload_bytes;
            if (payload_bytes)
                std::memcpy(message.payload.data(), payload, payload_bytes);
            if (!m_outbound.push(message))
            {
                m_queue_overflows.fetch_add(1, std::memory_order_relaxed);
                m_failure.store(
                    RollbackUdpWorkerFailure::QueueOverflow,
                    std::memory_order_release);
                m_stop.store(true, std::memory_order_release);
                return false;
            }
            return true;
        }

        bool dequeue(RollbackUdpMessage& message) noexcept
        {
            const uint64_t generation =
                m_handshake_generation.load(std::memory_order_acquire);
            while (m_inbound.pop(message))
            {
                if (message.handshake_generation == generation)
                    return true;
            }
            return false;
        }

        bool peer_ready() const noexcept
        {
            return m_ready.load(std::memory_order_acquire);
        }

        RollbackUdpWorkerStatus status() const noexcept
        {
            RollbackUdpWorkerStatus out {};
            out.running = m_running.load(std::memory_order_acquire);
            out.endpoint_open =
                m_endpoint_open.load(std::memory_order_acquire);
            out.peer_ready = m_ready.load(std::memory_order_acquire);
            out.endpoint_pinned =
                m_endpoint_pinned.load(std::memory_order_acquire);
            out.packets_sent = m_packets_sent.load(std::memory_order_relaxed);
            out.packets_received =
                m_packets_received.load(std::memory_order_relaxed);
            out.packets_authenticated =
                m_packets_authenticated.load(std::memory_order_relaxed);
            out.packets_rejected =
                m_packets_rejected.load(std::memory_order_relaxed);
            out.queue_overflows =
                m_queue_overflows.load(std::memory_order_relaxed);
            out.reopen_count = m_reopen_count.load(std::memory_order_relaxed);
            out.handshake_generation =
                m_handshake_generation.load(std::memory_order_acquire);
            out.network_profile = m_config.network_profile;
            out.fault_seed = m_config.fault_seed;
            out.fault_packets_submitted =
                m_fault_packets_submitted.load(std::memory_order_relaxed);
            out.fault_packets_queued =
                m_fault_packets_queued.load(std::memory_order_relaxed);
            out.fault_packets_delivered =
                m_fault_packets_delivered.load(std::memory_order_relaxed);
            out.fault_packets_dropped =
                m_fault_packets_dropped.load(std::memory_order_relaxed);
            out.fault_packets_duplicated =
                m_fault_packets_duplicated.load(std::memory_order_relaxed);
            out.fault_packets_reordered =
                m_fault_packets_reordered.load(std::memory_order_relaxed);
            out.fault_packets_corrupted =
                m_fault_packets_corrupted.load(std::memory_order_relaxed);
            out.fault_packets_spiked =
                m_fault_packets_spiked.load(std::memory_order_relaxed);
            out.fault_packets_burst_dropped =
                m_fault_packets_burst_dropped.load(
                    std::memory_order_relaxed);
            out.fault_queue_overflows =
                m_fault_queue_overflows.load(std::memory_order_relaxed);
            out.failure = m_failure.load(std::memory_order_acquire);
            return out;
        }

    private:
        using Clock = std::chrono::steady_clock;

        struct QueuedFaultDatagram
        {
            bool active {false};
            Clock::time_point due {};
            uint64_t order {0};
            RollbackProtocolV2WirePacket wire {};
        };

        static constexpr size_t kFaultDatagramCapacity = 512;
        static constexpr uint32_t kFaultFrameMillisecondsNumerator = 1000;
        static constexpr uint32_t kFaultFramesPerSecond = 60;

        static bool endpoint_equal(
            const sockaddr_in& a,
            const sockaddr_in& b) noexcept
        {
            return a.sin_family == b.sin_family
                && a.sin_port == b.sin_port
                && a.sin_addr.s_addr == b.sin_addr.s_addr;
        }

        void reset_status() noexcept
        {
            m_running.store(false, std::memory_order_relaxed);
            m_endpoint_open.store(false, std::memory_order_relaxed);
            m_ready.store(false, std::memory_order_relaxed);
            m_endpoint_pinned.store(false, std::memory_order_relaxed);
            m_failure.store(
                RollbackUdpWorkerFailure::None,
                std::memory_order_relaxed);
            m_packets_sent.store(0, std::memory_order_relaxed);
            m_packets_received.store(0, std::memory_order_relaxed);
            m_packets_authenticated.store(0, std::memory_order_relaxed);
            m_packets_rejected.store(0, std::memory_order_relaxed);
            m_queue_overflows.store(0, std::memory_order_relaxed);
            m_reopen_count.store(0, std::memory_order_relaxed);
            m_handshake_generation.store(0, std::memory_order_relaxed);
            m_fault_packets_submitted.store(0, std::memory_order_relaxed);
            m_fault_packets_queued.store(0, std::memory_order_relaxed);
            m_fault_packets_delivered.store(0, std::memory_order_relaxed);
            m_fault_packets_dropped.store(0, std::memory_order_relaxed);
            m_fault_packets_duplicated.store(0, std::memory_order_relaxed);
            m_fault_packets_reordered.store(0, std::memory_order_relaxed);
            m_fault_packets_corrupted.store(0, std::memory_order_relaxed);
            m_fault_packets_spiked.store(0, std::memory_order_relaxed);
            m_fault_packets_burst_dropped.store(
                0, std::memory_order_relaxed);
            m_fault_queue_overflows.store(0, std::memory_order_relaxed);
            m_fault_last_spike_period = 0;
            for (size_t i = 0; i < kFaultDatagramCapacity; ++i)
                m_fault_datagrams[i] = {};
            m_fault_next_order = 0;
            m_fault_started = Clock::now();
            m_fault_rng = RollbackDeterministicRng(
                m_config.fault_seed
                ^ (static_cast<uint32_t>(m_config.local_peer) << 24));
        }

        bool reset_session() noexcept
        {
            m_ready.store(false, std::memory_order_release);
            m_endpoint_pinned.store(false, std::memory_order_release);
            if (m_remote_nonce
                != std::array<uint8_t, kRollbackProtocolV2NonceBytes> {})
            {
                m_retired_remote_nonce = m_remote_nonce;
                m_has_retired_remote_nonce = true;
            }
            m_remote_nonce.fill(0);
            m_replay.clear();
            m_handshake_replay.clear();
            m_highest_remote_sequence.clear();
            m_next_sequence = 1;
            for (size_t i = 0; i < kFaultDatagramCapacity; ++i)
                m_fault_datagrams[i] = {};
            if (!RollbackProtocolV2RandomNonce(m_local_nonce))
            {
                m_failure.store(
                    RollbackUdpWorkerFailure::NonceGenerationFailed,
                    std::memory_order_release);
                return false;
            }
            return true;
        }

        static bool fatal_failure(
            RollbackUdpWorkerFailure failure) noexcept
        {
            return failure == RollbackUdpWorkerFailure::InvalidConfig
                || failure == RollbackUdpWorkerFailure::QueueOverflow
                || failure
                    == RollbackUdpWorkerFailure::ResourceAllocationFailed
                || failure
                    == RollbackUdpWorkerFailure::NonceGenerationFailed
                || failure
                    == RollbackUdpWorkerFailure::PeerSessionChanged;
        }

        void mark_io_failure() noexcept
        {
            set_recoverable_failure(
                RollbackUdpWorkerFailure::EndpointIoFailed);
        }

        void set_recoverable_failure(
            RollbackUdpWorkerFailure failure) noexcept
        {
            RollbackUdpWorkerFailure current =
                m_failure.load(std::memory_order_acquire);
            while (!fatal_failure(current)
                && !m_failure.compare_exchange_weak(
                    current,
                    failure,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire))
            {
            }
        }

        void clear_recoverable_failure() noexcept
        {
            RollbackUdpWorkerFailure current =
                m_failure.load(std::memory_order_acquire);
            while (!fatal_failure(current)
                && current != RollbackUdpWorkerFailure::None
                && !m_failure.compare_exchange_weak(
                    current,
                    RollbackUdpWorkerFailure::None,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire))
            {
            }
        }

        uint32_t fault_frame(const Clock::time_point now) const noexcept
        {
            const uint64_t elapsed_ms = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - m_fault_started).count());
            return static_cast<uint32_t>(
                (elapsed_ms * kFaultFramesPerSecond)
                / kFaultFrameMillisecondsNumerator);
        }

        bool fault_chance(uint32_t per_mille) noexcept
        {
            return per_mille != 0
                && (m_fault_rng.next() % 1000u) < per_mille;
        }

        bool queue_fault_datagram(
            RollbackProtocolV2WirePacket wire,
            const Clock::time_point due,
            bool corrupt) noexcept
        {
            size_t slot = kFaultDatagramCapacity;
            for (size_t i = 0; i < kFaultDatagramCapacity; ++i)
            {
                if (!m_fault_datagrams[i].active)
                {
                    slot = i;
                    break;
                }
            }
            if (slot == kFaultDatagramCapacity)
            {
                m_fault_queue_overflows.fetch_add(
                    1, std::memory_order_relaxed);
                m_queue_overflows.fetch_add(1, std::memory_order_relaxed);
                m_failure.store(
                    RollbackUdpWorkerFailure::QueueOverflow,
                    std::memory_order_release);
                m_stop.store(true, std::memory_order_release);
                return false;
            }
            if (corrupt && wire.size != 0)
            {
                wire.bytes[wire.size - 1] ^= 0x80;
                m_fault_packets_corrupted.fetch_add(
                    1, std::memory_order_relaxed);
            }
            QueuedFaultDatagram& queued = m_fault_datagrams[slot];
            queued.active = true;
            queued.due = due;
            queued.order = m_fault_next_order++;
            queued.wire = wire;
            m_fault_packets_queued.fetch_add(1, std::memory_order_relaxed);
            return true;
        }

        bool submit_fault_datagram(
            const RollbackProtocolV2WirePacket& wire,
            const Clock::time_point now) noexcept
        {
            m_fault_packets_submitted.fetch_add(
                1, std::memory_order_relaxed);
            const RollbackNetworkProfile profile =
                GetRollbackNetworkProfile(m_config.network_profile);
            if (!RollbackFaultProfileHasFaults(profile))
            {
                if (!m_endpoint.send(wire, m_expected_peer)) return false;
                m_packets_sent.fetch_add(1, std::memory_order_relaxed);
                m_fault_packets_delivered.fetch_add(
                    1, std::memory_order_relaxed);
                return true;
            }

            const uint32_t frame = fault_frame(now);
            const bool in_burst = profile.burst_period_frames != 0
                && profile.burst_duration_frames != 0
                && frame >= profile.burst_period_frames
                && (frame % profile.burst_period_frames)
                    < profile.burst_duration_frames;
            if (in_burst || fault_chance(profile.loss_per_mille))
            {
                m_fault_packets_dropped.fetch_add(
                    1, std::memory_order_relaxed);
                if (in_burst)
                    m_fault_packets_burst_dropped.fetch_add(
                        1, std::memory_order_relaxed);
                return true;
            }

            uint32_t latency_frames = profile.base_latency_frames;
            if (profile.jitter_frames != 0)
                latency_frames +=
                    m_fault_rng.next() % (profile.jitter_frames + 1u);
            const uint32_t spike_period =
                profile.spike_period_frames == 0
                    ? 0 : frame / profile.spike_period_frames;
            if (spike_period != 0
                && spike_period != m_fault_last_spike_period)
            {
                latency_frames += profile.spike_extra_latency_frames;
                m_fault_last_spike_period = spike_period;
                m_fault_packets_spiked.fetch_add(
                    1, std::memory_order_relaxed);
            }
            if (fault_chance(profile.reorder_per_mille))
            {
                latency_frames += profile.jitter_frames + 3u;
                m_fault_packets_reordered.fetch_add(
                    1, std::memory_order_relaxed);
            }
            const auto latency = std::chrono::milliseconds(
                (static_cast<uint64_t>(latency_frames)
                    * kFaultFrameMillisecondsNumerator
                    + kFaultFramesPerSecond - 1)
                / kFaultFramesPerSecond);
            const bool corrupt = fault_chance(profile.corrupt_per_mille);
            if (!queue_fault_datagram(wire, now + latency, corrupt))
                return false;
            if (fault_chance(profile.duplicate_per_mille))
            {
                m_fault_packets_duplicated.fetch_add(
                    1, std::memory_order_relaxed);
                if (!queue_fault_datagram(
                        wire, now + latency + std::chrono::milliseconds(1),
                        false))
                {
                    return false;
                }
            }
            return true;
        }

        bool drain_fault_datagrams(const Clock::time_point now) noexcept
        {
            for (;;)
            {
                size_t best = kFaultDatagramCapacity;
                for (size_t i = 0; i < kFaultDatagramCapacity; ++i)
                {
                    const QueuedFaultDatagram& queued =
                        m_fault_datagrams[i];
                    if (!queued.active || queued.due > now) continue;
                    if (best == kFaultDatagramCapacity
                        || queued.due < m_fault_datagrams[best].due
                        || (queued.due == m_fault_datagrams[best].due
                            && queued.order
                                < m_fault_datagrams[best].order))
                    {
                        best = i;
                    }
                }
                if (best == kFaultDatagramCapacity) return true;
                const RollbackProtocolV2WirePacket wire =
                    m_fault_datagrams[best].wire;
                m_fault_datagrams[best] = {};
                if (!m_endpoint.send(wire, m_expected_peer)) return false;
                m_packets_sent.fetch_add(1, std::memory_order_relaxed);
                m_fault_packets_delivered.fetch_add(
                    1, std::memory_order_relaxed);
            }
        }

        bool send_packet(
            RollbackProtocolV2PacketType type,
            const void* payload,
            uint16_t payload_bytes,
            RollbackSequenceStamp ack,
            bool hello) noexcept
        {
            RollbackProtocolV2Header header {};
            header.packet_type = type;
            header.source_peer = m_config.local_peer;
            header.destination_peer = m_config.remote_peer;
            header.sequence = m_next_sequence++;
            header.build_id = m_config.expected_build_id;
            header.schema_id = m_config.expected_schema_id;
            header.source_nonce = m_local_nonce;
            if (!hello) header.destination_nonce = m_remote_nonce;
            if (ack.valid)
            {
                header.flags |= RollbackProtocolV2FlagAckPresent;
                header.ack_sequence = ack.value;
            }
            RollbackProtocolV2WirePacket wire {};
            if (!EncodeRollbackProtocolV2Packet(
                    header,
                    payload,
                    payload_bytes,
                    m_config.secret,
                    wire)
                || !submit_fault_datagram(wire, Clock::now()))
            {
                return false;
            }
            return true;
        }

        bool route_and_nonce_valid(
            const RollbackProtocolV2Packet& packet,
            const sockaddr_in& from) const noexcept
        {
            if (!endpoint_equal(from, m_expected_peer)
                || packet.header.source_peer != m_config.remote_peer
                || packet.header.destination_peer != m_config.local_peer)
            {
                return false;
            }
            if (packet.header.packet_type
                == RollbackProtocolV2PacketType::Hello)
            {
                return packet.header.destination_nonce
                    == std::array<uint8_t,
                        kRollbackProtocolV2NonceBytes> {};
            }
            return packet.header.source_nonce == m_remote_nonce
                && packet.header.destination_nonce == m_local_nonce;
        }

        RollbackUdpHandshakeProfile local_profile() const noexcept
        {
            RollbackUdpHandshakeProfile profile {};
            profile.profile_version = kRollbackUdpHandshakeProfileVersion;
            profile.local_player_slot = m_config.local_player_slot;
            profile.lifecycle_mode = m_config.lifecycle_mode;
            profile.session_domain = m_config.session_domain;
            profile.native_input_source_slot =
                m_config.native_input_source_slot;
            profile.rollback_window = m_config.rollback_window;
            profile.input_delay = m_config.input_delay;
            profile.launch_seed = m_config.lifecycle_mode
                    == RollbackLifecycleMode::MirroredVersus
                ? m_config.launch_descriptor.seed : 0;
            profile.network_profile = m_config.network_profile;
            profile.fault_seed = m_config.fault_seed;
            profile.desired_launch_descriptor_hash =
                m_config.session_domain == RollbackSessionDomain::Production
                    && m_config.lifecycle_mode
                        == RollbackLifecycleMode::MirroredVersus
                    ? m_config.launch_descriptor.hash() : 0;
            profile.expected_build_id = m_config.expected_build_id;
            profile.expected_schema_id = m_config.expected_schema_id;
            if (m_config.session_domain
                == RollbackSessionDomain::ReplayForkLab)
            {
                profile.replay_sha256 = m_config.replay_sha256;
                profile.replay_anchor_sequence =
                    m_config.replay_anchor_sequence;
                profile.replay_anchor_round = m_config.replay_anchor_round;
                profile.replay_anchor_master = m_config.replay_anchor_master;
                profile.replay_run_nonce_hash =
                    m_config.replay_run_nonce_hash;
            }
            return profile;
        }

        bool peer_profile_valid(
            const RollbackProtocolV2Packet& packet) const noexcept
        {
            if (packet.payload_bytes != sizeof(RollbackUdpHandshakeProfile))
                return false;
            RollbackUdpHandshakeProfile peer {};
            std::memcpy(&peer, packet.payload.data(), sizeof(peer));
            const bool replay_fork = m_config.session_domain
                == RollbackSessionDomain::ReplayForkLab;
            const bool source_policy_matches = replay_fork
                ? peer.native_input_source_slot == peer.local_player_slot
                : (m_config.lifecycle_mode
                        == RollbackLifecycleMode::MirroredVersus
                    ? peer.native_input_source_slot
                        == m_config.native_input_source_slot
                    : peer.native_input_source_slot
                        == peer.local_player_slot);
            const uint64_t expected_launch_hash =
                !replay_fork && m_config.lifecycle_mode
                        == RollbackLifecycleMode::MirroredVersus
                    ? m_config.launch_descriptor.hash()
                    : 0;
            const uint32_t expected_launch_seed =
                !replay_fork && m_config.lifecycle_mode
                        == RollbackLifecycleMode::MirroredVersus
                    ? m_config.launch_descriptor.seed
                    : 0;
            return peer.profile_version
                    == kRollbackUdpHandshakeProfileVersion
                && peer.local_player_slot
                    == static_cast<uint8_t>(1u - m_config.local_player_slot)
                && peer.session_domain == m_config.session_domain
                && peer.lifecycle_mode == m_config.lifecycle_mode
                && source_policy_matches
                && peer.rollback_window == m_config.rollback_window
                && peer.input_delay == m_config.input_delay
                && peer.launch_seed == expected_launch_seed
                && peer.network_profile == m_config.network_profile
                && peer.fault_seed == m_config.fault_seed
                && peer.expected_build_id == m_config.expected_build_id
                && peer.expected_schema_id == m_config.expected_schema_id
                && peer.desired_launch_descriptor_hash
                    == expected_launch_hash
                && (!replay_fork
                    || (peer.replay_sha256 == m_config.replay_sha256
                        && peer.replay_anchor_sequence
                            == m_config.replay_anchor_sequence
                        && peer.replay_anchor_round
                            == m_config.replay_anchor_round
                        && peer.replay_anchor_master
                            == m_config.replay_anchor_master
                        && peer.replay_run_nonce_hash
                            == m_config.replay_run_nonce_hash))
                && (replay_fork
                    || (peer.replay_sha256
                            == std::array<uint8_t, 32> {}
                        && peer.replay_anchor_sequence == -1
                        && peer.replay_anchor_round == -1
                        && peer.replay_anchor_master == -1
                        && peer.replay_run_nonce_hash == 0))
                && peer.reserved8[0] == 0
                && peer.reserved8[1] == 0
                && peer.reserved == 0;
        }

        bool receive_one(const Clock::time_point now) noexcept
        {
            RollbackProtocolV2WirePacket wire {};
            sockaddr_in from {};
            const RollbackUdpEndpoint::ReceiveStatus status =
                m_endpoint.receive(wire, from);
            if (status == RollbackUdpEndpoint::ReceiveStatus::NoData)
                return false;
            if (status == RollbackUdpEndpoint::ReceiveStatus::Error)
            {
                mark_io_failure();
                return false;
            }
            m_packets_received.fetch_add(1, std::memory_order_relaxed);

            // Reject an unexpected source before invoking CNG HMAC.  Route
            // validation is repeated after authentication for defense in
            // depth, but unauthenticated hosts must not be able to force the
            // comparatively expensive crypto path.
            if (!endpoint_equal(from, m_expected_peer))
            {
                m_packets_rejected.fetch_add(1, std::memory_order_relaxed);
                return true;
            }

            RollbackProtocolV2Packet packet {};
            const RollbackProtocolV2DecodeReport decoded =
                DecodeRollbackProtocolV2Packet(
                    wire.bytes.data(),
                    wire.size,
                    m_config.secret,
                    m_config.expected_build_id,
                    m_config.expected_schema_id,
                    packet);
            if (!decoded.ok)
            {
                m_packets_rejected.fetch_add(1, std::memory_order_relaxed);
                return true;
            }

            if (packet.header.packet_type
                == RollbackProtocolV2PacketType::Hello)
            {
                if (!endpoint_equal(from, m_expected_peer)
                    || packet.header.source_peer != m_config.remote_peer
                    || packet.header.destination_peer != m_config.local_peer
                    || !peer_profile_valid(packet))
                {
                    m_packets_rejected.fetch_add(1, std::memory_order_relaxed);
                    return true;
                }
                const bool was_ready =
                    m_ready.load(std::memory_order_acquire);
                if (was_ready
                    && packet.header.source_nonce != m_remote_nonce)
                {
                    // A lone Hello is not sufficient proof that a live peer
                    // restarted; it may be a captured packet. Reject it before
                    // it can mutate the active session's replay window.
                    m_packets_rejected.fetch_add(
                        1, std::memory_order_relaxed);
                    return true;
                }
                if (m_has_retired_remote_nonce
                    && packet.header.source_nonce == m_retired_remote_nonce)
                {
                    m_packets_rejected.fetch_add(
                        1, std::memory_order_relaxed);
                    return true;
                }
                const bool replay_accepted = was_ready
                    ? m_replay.accept(
                        packet.header.source_nonce, packet.header.sequence)
                    : m_handshake_replay.rebind_and_accept(
                        packet.header.source_nonce, packet.header.sequence);
                if (!replay_accepted)
                {
                    m_packets_rejected.fetch_add(
                        1, std::memory_order_relaxed);
                    return true;
                }
                m_remote_nonce = packet.header.source_nonce;
                m_endpoint_pinned.store(true, std::memory_order_release);
                const RollbackUdpHandshakeProfile profile = local_profile();
                if (!send_packet(
                    RollbackProtocolV2PacketType::HelloAck,
                    &profile,
                    sizeof(profile),
                    {},
                    false))
                {
                    mark_io_failure();
                    return false;
                }
                m_packets_authenticated.fetch_add(
                    1, std::memory_order_relaxed);
                return true;
            }

            if (packet.header.packet_type
                == RollbackProtocolV2PacketType::HelloAck)
            {
                if (!endpoint_equal(from, m_expected_peer)
                    || packet.header.source_peer != m_config.remote_peer
                    || packet.header.destination_peer != m_config.local_peer
                    || packet.header.destination_nonce != m_local_nonce
                    || !peer_profile_valid(packet))
                {
                    m_packets_rejected.fetch_add(1, std::memory_order_relaxed);
                    return true;
                }
                const bool was_ready =
                    m_ready.load(std::memory_order_acquire);
                if (was_ready
                    && packet.header.source_nonce != m_remote_nonce)
                {
                    // A HelloAck targeted to the current local nonce proves
                    // that the peer session changed. Fail closed, but do not
                    // let the new nonce poison the old session's replay state.
                    m_ready.store(false, std::memory_order_release);
                    m_failure.store(
                        RollbackUdpWorkerFailure::PeerSessionChanged,
                        std::memory_order_release);
                    m_stop.store(true, std::memory_order_release);
                    return true;
                }
                if (m_has_retired_remote_nonce
                    && packet.header.source_nonce == m_retired_remote_nonce)
                {
                    m_packets_rejected.fetch_add(
                        1, std::memory_order_relaxed);
                    return true;
                }
                const bool replay_accepted = was_ready
                    ? m_replay.accept(
                        packet.header.source_nonce, packet.header.sequence)
                    : m_handshake_replay.rebind_and_accept(
                        packet.header.source_nonce, packet.header.sequence);
                if (!replay_accepted)
                {
                    m_packets_rejected.fetch_add(
                        1, std::memory_order_relaxed);
                    return true;
                }
                m_remote_nonce = packet.header.source_nonce;
                if (!was_ready)
                {
                    // Promote only the replay state bound to the nonce that
                    // completed the local-nonce challenge.
                    m_replay = m_handshake_replay;
                    m_handshake_replay.clear();
                }
                m_endpoint_pinned.store(true, std::memory_order_release);
                m_last_authenticated = now;
                if (!m_stop.load(std::memory_order_acquire))
                    clear_recoverable_failure();
                if (!was_ready)
                {
                    m_handshake_generation.fetch_add(
                        1, std::memory_order_release);
                }
                m_ready.store(true, std::memory_order_release);
                m_packets_authenticated.fetch_add(
                    1, std::memory_order_relaxed);
                return true;
            }

            if (!m_ready.load(std::memory_order_acquire)
                || !route_and_nonce_valid(packet, from))
            {
                m_packets_rejected.fetch_add(1, std::memory_order_relaxed);
                return true;
            }
            if (!m_replay.accept(
                    packet.header.source_nonce, packet.header.sequence))
            {
                m_packets_rejected.fetch_add(1, std::memory_order_relaxed);
                return true;
            }
            m_highest_remote_sequence =
                RollbackSequenceStamp::From(packet.header.sequence);
            m_last_authenticated = now;
            m_packets_authenticated.fetch_add(1, std::memory_order_relaxed);

            if (packet.header.packet_type
                != RollbackProtocolV2PacketType::Heartbeat)
            {
                RollbackUdpMessage message {};
                message.packet_type = packet.header.packet_type;
                message.handshake_generation =
                    m_handshake_generation.load(std::memory_order_acquire);
                message.sequence = packet.header.sequence;
                if ((packet.header.flags
                    & RollbackProtocolV2FlagAckPresent) != 0)
                {
                    message.ack = RollbackSequenceStamp::From(
                        packet.header.ack_sequence);
                }
                message.payload_bytes = packet.payload_bytes;
                if (packet.payload_bytes)
                {
                    std::memcpy(
                        message.payload.data(),
                        packet.payload.data(),
                        packet.payload_bytes);
                }
                if (!m_inbound.push(message))
                {
                    m_queue_overflows.fetch_add(
                        1, std::memory_order_relaxed);
                    m_failure.store(
                        RollbackUdpWorkerFailure::QueueOverflow,
                        std::memory_order_release);
                    m_stop.store(true, std::memory_order_release);
                }
            }
            return true;
        }

        void run() noexcept
        {
            m_running.store(true, std::memory_order_release);
            if (!reset_session())
            {
                m_running.store(false, std::memory_order_release);
                return;
            }
            uint32_t reopen_backoff = kRollbackUdpReopenMinMs;
            Clock::time_point next_open = Clock::now();
            Clock::time_point next_handshake = next_open;
            Clock::time_point next_heartbeat = next_open;

            while (!m_stop.load(std::memory_order_acquire))
            {
                const Clock::time_point now = Clock::now();
                if (!m_endpoint.is_open())
                {
                    if (now < next_open)
                    {
                        auto wait = std::chrono::duration_cast<
                            std::chrono::milliseconds>(next_open - now);
                        wait = (std::max)(
                            wait, std::chrono::milliseconds(1));
                        std::this_thread::sleep_for((std::min)(
                            wait, std::chrono::milliseconds(10)));
                        continue;
                    }
                    if (!m_endpoint.open(
                            m_config.bind_address,
                            m_config.bind_port))
                    {
                        set_recoverable_failure(
                            RollbackUdpWorkerFailure::EndpointOpenFailed);
                        next_open = now
                            + std::chrono::milliseconds(reopen_backoff);
                        reopen_backoff = (std::min)(
                            reopen_backoff * 2,
                            kRollbackUdpReopenMaxMs);
                        continue;
                    }
                    m_endpoint_open.store(true, std::memory_order_release);
                    m_reopen_count.fetch_add(1, std::memory_order_relaxed);
                    reopen_backoff = kRollbackUdpReopenMinMs;
                    next_handshake = now;
                    next_heartbeat = now;
                }

                if (!drain_fault_datagrams(now))
                {
                    mark_io_failure();
                    continue;
                }

                for (uint32_t i = 0; i < 32; ++i)
                {
                    if (!receive_one(now)) break;
                    if (m_stop.load(std::memory_order_acquire)) break;
                }
                if (m_stop.load(std::memory_order_acquire)) break;

                if (m_failure.load(std::memory_order_acquire)
                    == RollbackUdpWorkerFailure::EndpointIoFailed)
                {
                    m_ready.store(false, std::memory_order_release);
                    m_endpoint.close();
                    m_endpoint_open.store(false, std::memory_order_release);
                    if (!reset_session())
                    {
                        m_stop.store(true, std::memory_order_release);
                        continue;
                    }
                    // Keep EndpointIoFailed visible until a fresh authenticated
                    // HelloAck clears it. Active gameplay will fail closed;
                    // preactivation may follow the bounded reopen backoff.
                    mark_io_failure();
                    next_open = now + std::chrono::milliseconds(
                        reopen_backoff);
                    reopen_backoff = (std::min)(
                        reopen_backoff * 2,
                        kRollbackUdpReopenMaxMs);
                    continue;
                }

                if (!m_ready.load(std::memory_order_acquire))
                {
                    if (now >= next_handshake)
                    {
                        const RollbackUdpHandshakeProfile profile =
                            local_profile();
                        if (!send_packet(
                            RollbackProtocolV2PacketType::Hello,
                            &profile,
                            sizeof(profile),
                            {},
                            true))
                        {
                            mark_io_failure();
                            continue;
                        }
                        next_handshake = now + std::chrono::milliseconds(
                            kRollbackUdpHeartbeatMs);
                    }
                }
                else
                {
                    if (now - m_last_authenticated
                        > std::chrono::milliseconds(
                            kRollbackUdpReadinessExpiryMs))
                    {
                        set_recoverable_failure(
                            RollbackUdpWorkerFailure::PeerTimeout);
                        m_ready.store(false, std::memory_order_release);
                        m_endpoint_pinned.store(
                            false, std::memory_order_release);
                        m_endpoint.close();
                        m_endpoint_open.store(
                            false, std::memory_order_release);
                        if (!reset_session())
                        {
                            m_stop.store(true, std::memory_order_release);
                            continue;
                        }
                        set_recoverable_failure(
                            RollbackUdpWorkerFailure::PeerTimeout);
                        next_open = now + std::chrono::milliseconds(
                            reopen_backoff);
                        reopen_backoff = (std::min)(
                            reopen_backoff * 2,
                            kRollbackUdpReopenMaxMs);
                        continue;
                    }
                    if (now >= next_heartbeat)
                    {
                        if (!send_packet(
                            RollbackProtocolV2PacketType::Heartbeat,
                            nullptr,
                            0,
                            m_highest_remote_sequence,
                            false))
                        {
                            mark_io_failure();
                            continue;
                        }
                        next_heartbeat = now + std::chrono::milliseconds(
                            kRollbackUdpHeartbeatMs);
                    }
                    RollbackUdpMessage outbound {};
                    for (uint32_t i = 0;
                         i < 32 && m_outbound.pop(outbound);
                         ++i)
                    {
                        if (outbound.handshake_generation
                            != m_handshake_generation.load(
                                std::memory_order_acquire))
                        {
                            continue;
                        }
                        if (!send_packet(
                                outbound.packet_type,
                                outbound.payload.data(),
                                outbound.payload_bytes,
                                outbound.ack,
                                false))
                        {
                            mark_io_failure();
                            break;
                        }
                    }
                }
                if (m_endpoint.is_open()
                    && m_endpoint.wait_readable(2)
                        == RollbackUdpEndpoint::ReceiveStatus::Error)
                {
                    mark_io_failure();
                }
            }
            m_endpoint.close();
            m_endpoint_open.store(false, std::memory_order_release);
            m_ready.store(false, std::memory_order_release);
            m_endpoint_pinned.store(false, std::memory_order_release);
            m_running.store(false, std::memory_order_release);
        }

        RollbackProductionConfig m_config {};
        sockaddr_in m_expected_peer {};
        RollbackUdpEndpoint m_endpoint {};
        RollbackBoundedSpscQueue<RollbackUdpMessage, 256> m_outbound {};
        RollbackBoundedSpscQueue<RollbackUdpMessage, 256> m_inbound {};
        std::unique_ptr<QueuedFaultDatagram[]> m_fault_datagrams {
            new (std::nothrow)
                QueuedFaultDatagram[kFaultDatagramCapacity]};
        RollbackDeterministicRng m_fault_rng {0x5C6B0001u};
        Clock::time_point m_fault_started {Clock::now()};
        uint32_t m_fault_last_spike_period {0};
        uint64_t m_fault_next_order {0};
        std::thread m_thread {};
        std::array<uint8_t, kRollbackProtocolV2NonceBytes> m_local_nonce {};
        std::array<uint8_t, kRollbackProtocolV2NonceBytes> m_remote_nonce {};
        std::array<uint8_t, kRollbackProtocolV2NonceBytes>
            m_retired_remote_nonce {};
        bool m_has_retired_remote_nonce {false};
        RollbackProtocolV2NonceReplayWindow m_replay {};
        RollbackProtocolV2NonceReplayWindow m_handshake_replay {};
        RollbackSequenceStamp m_highest_remote_sequence {};
        Clock::time_point m_last_authenticated {Clock::now()};
        uint64_t m_next_sequence {1};
        std::atomic<bool> m_stop {true};
        std::atomic<bool> m_running {false};
        std::atomic<bool> m_endpoint_open {false};
        std::atomic<bool> m_ready {false};
        std::atomic<bool> m_endpoint_pinned {false};
        std::atomic<RollbackUdpWorkerFailure> m_failure {
            RollbackUdpWorkerFailure::None};
        std::atomic<uint64_t> m_packets_sent {0};
        std::atomic<uint64_t> m_packets_received {0};
        std::atomic<uint64_t> m_packets_authenticated {0};
        std::atomic<uint64_t> m_packets_rejected {0};
        std::atomic<uint64_t> m_queue_overflows {0};
        std::atomic<uint64_t> m_reopen_count {0};
        std::atomic<uint64_t> m_handshake_generation {0};
        std::atomic<uint64_t> m_fault_packets_submitted {0};
        std::atomic<uint64_t> m_fault_packets_queued {0};
        std::atomic<uint64_t> m_fault_packets_delivered {0};
        std::atomic<uint64_t> m_fault_packets_dropped {0};
        std::atomic<uint64_t> m_fault_packets_duplicated {0};
        std::atomic<uint64_t> m_fault_packets_reordered {0};
        std::atomic<uint64_t> m_fault_packets_corrupted {0};
        std::atomic<uint64_t> m_fault_packets_spiked {0};
        std::atomic<uint64_t> m_fault_packets_burst_dropped {0};
        std::atomic<uint64_t> m_fault_queue_overflows {0};
    };
}
