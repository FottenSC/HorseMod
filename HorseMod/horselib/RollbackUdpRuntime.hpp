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
#include "RollbackRuntimePolicy.hpp"
#include "RollbackProtocolV2.hpp"
#include "RollbackStateHash.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
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

    static constexpr uint8_t kRollbackUdpHandshakeProfileVersion = 9;
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
        RollbackSavePolicy save_policy {
            RollbackSavePolicy::ConfirmedSpeculative};
        uint8_t lead_pacing_enabled {1};
        uint16_t lead_pacing_enter_milliframes {1500};
        uint16_t lead_pacing_exit_milliframes {500};
        uint8_t lead_pacing_maximum_holds {2};
        RollbackNetworkProfileKind network_profile {
            RollbackNetworkProfileKind::Clean0ms};
        RollbackSessionDomain session_domain {
            RollbackSessionDomain::Production};
        uint8_t diagnostic_forced_rollback_depth {0};
        uint32_t fault_seed {0};
        uint32_t expected_native_stage_identity {0};
        uint32_t fixture_id {0};
        uint32_t fixture_correction_start {0};
        uint16_t fixture_hold_updates {0};
        uint16_t fixture_prediction_lead_updates {0};
        uint8_t fixture_delay_owner_slot {0};
        uint8_t fixture_enabled {0};
        uint8_t replay_input_enabled {0};
        uint8_t replay_input_swap_players {0};
        RollbackReplayInputAlignment replay_input_alignment {
            RollbackReplayInputAlignment::ExactConsumedFrame};
        uint8_t replay_input_reserved {0};
        uint32_t replay_input_round {0};
        uint32_t replay_input_start_frame {0};
        uint32_t replay_input_round_frames {0};
        std::array<uint64_t, 2> replay_input_round_hash {};
        std::array<uint8_t, 32> replay_input_sha256 {};
        std::array<uint8_t, 32> replay_sha256 {};
        int32_t replay_anchor_sequence {-1};
        int32_t replay_anchor_round {-1};
        int32_t replay_anchor_master {-1};
        uint64_t replay_run_nonce_hash {0};
        uint64_t expected_build_id {0};
        uint64_t expected_schema_id {0};
    };
#pragma pack(pop)

    static_assert(sizeof(RollbackUdpHandshakeProfile) == 172);

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

    enum class RollbackTransportMode : uint8_t
    {
        DirectUdp = 0,
        SteamP2P = 1,
    };

    static constexpr bool RollbackTransportModeValid(
        RollbackTransportMode mode) noexcept
    {
        return mode == RollbackTransportMode::DirectUdp
            || mode == RollbackTransportMode::SteamP2P;
    }

    static constexpr const char* RollbackTransportModeName(
        RollbackTransportMode mode) noexcept
    {
        return mode == RollbackTransportMode::SteamP2P
            ? "steam-p2p" : "direct-udp";
    }

    struct RollbackProductionConfig
    {
        bool enabled {false};
        RollbackTransportMode transport_mode {
            RollbackTransportMode::DirectUdp};
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
        std::array<uint8_t, 32> replay_sha256 {};
        int32_t replay_anchor_sequence {-1};
        int32_t replay_anchor_round {-1};
        int32_t replay_anchor_master {-1};
        uint64_t replay_run_nonce_hash {0};
        std::string secret;
        uint16_t rollback_window {12};
        uint16_t input_delay {1};
        RollbackSavePolicy save_policy {
            RollbackSavePolicy::ConfirmedSpeculative};
        RollbackLeadPacingConfig lead_pacing {};
        RollbackDeterministicInputConfig deterministic_input {};
        std::string replay_input_file;
        RollbackReplayInputConfig replay_input {};
        bool replay_trace_compare {false};
        bool replay_deep_trace_diagnostics {false};
        bool replay_trace_input_only {false};
        bool callback_inventory_only {false};
        bool native_correction_only {false};
        std::string request_id;
        std::string client_role;
        RollbackNetworkProfileKind network_profile {
            RollbackNetworkProfileKind::Clean0ms};
        uint32_t fault_seed {0x5C6B0001u};
        // Test-only bounded application-level transport worker stall. These
        // fields are deliberately excluded from the handshake and protocol
        // profile: they stress one local worker without changing wire or
        // snapshot semantics.
        uint32_t test_worker_stall_after_ms {0};
        uint32_t test_worker_stall_duration_ms {0};
        // Request-file-only correctness stress. Persistent beta parsing never
        // populates this field and release tooling rejects nonzero values.
        uint8_t test_forced_rollback_depth {0};
        uint64_t expected_build_id {0};
        uint64_t expected_schema_id {0};
        uint32_t expected_native_stage_identity {0};
        uint64_t expected_selection_hash {0};
        bool bind_observed_stock_selection {false};
        bool replay_test_selection_override {false};

        bool valid() const noexcept
        {
            return enabled
                && RollbackTransportModeValid(transport_mode)
                && (transport_mode == RollbackTransportMode::SteamP2P
                    || (bind_port != 0
                        && peer_port != 0
                        && !peer_address.empty()
                        && secret.size() >= 16))
                && local_player_slot < 2
                && native_input_source_slot < 2
                && local_peer != 0
                && remote_peer != 0
                && local_peer != remote_peer
                && rollback_window != 0
                && rollback_window <= 60
                && input_delay != 0
                && input_delay <= rollback_window
                && RollbackSavePolicyValid(save_policy)
                && lead_pacing.valid()
                && (test_forced_rollback_depth == 0
                    || (test_forced_rollback_depth
                            <= rollback_window - input_delay
                        && (test_forced_rollback_depth == 1
                            || test_forced_rollback_depth == 6
                            || test_forced_rollback_depth
                                == rollback_window - input_delay)))
                && test_worker_stall_duration_ms <= 1000
                && ((test_worker_stall_after_ms == 0
                        && test_worker_stall_duration_ms == 0)
                    || (test_worker_stall_after_ms != 0
                        && test_worker_stall_duration_ms != 0))
                && deterministic_input.valid()
                && deterministic_input.prediction_lead_updates
                    <= rollback_window
                && replay_input.requested_valid()
                && (!replay_deep_trace_diagnostics
                    || (replay_trace_compare && replay_input.enabled))
                && (!replay_input.enabled
                    || (deterministic_input.enabled
                        && !replay_input_file.empty()))
                && static_cast<uint8_t>(network_profile)
                    <= static_cast<uint8_t>(
                        RollbackNetworkProfileKind::CorruptProbe)
                && fault_seed != 0
                && expected_build_id != 0
                && expected_schema_id != 0
                && (session_domain != RollbackSessionDomain::Production
                    || expected_native_stage_identity != 0
                    || bind_observed_stock_selection)
                && (!replay_test_selection_override
                    || (replay_input.enabled
                        && !bind_observed_stock_selection
                        && expected_selection_hash != 0))
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
                    : native_input_source_slot == local_player_slot);
        }
    };

    static inline bool RollbackProductionConfigEquivalent(
        const RollbackProductionConfig& left,
        const RollbackProductionConfig& right) noexcept
    {
        return left.enabled == right.enabled
            && left.transport_mode == right.transport_mode
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
            && left.replay_sha256 == right.replay_sha256
            && left.replay_anchor_sequence == right.replay_anchor_sequence
            && left.replay_anchor_round == right.replay_anchor_round
            && left.replay_anchor_master == right.replay_anchor_master
            && left.replay_run_nonce_hash == right.replay_run_nonce_hash
            && left.secret == right.secret
            && left.rollback_window == right.rollback_window
            && left.input_delay == right.input_delay
            && left.save_policy == right.save_policy
            && left.lead_pacing.enabled == right.lead_pacing.enabled
            && left.lead_pacing.enter_frames
                == right.lead_pacing.enter_frames
            && left.lead_pacing.exit_frames
                == right.lead_pacing.exit_frames
            && left.lead_pacing.maximum_consecutive_holds
                == right.lead_pacing.maximum_consecutive_holds
            && left.deterministic_input.hash()
                == right.deterministic_input.hash()
            && left.replay_input_file == right.replay_input_file
            && left.replay_input.hash() == right.replay_input.hash()
            && left.replay_trace_compare == right.replay_trace_compare
            && left.replay_deep_trace_diagnostics
                == right.replay_deep_trace_diagnostics
            && left.replay_trace_input_only
                == right.replay_trace_input_only
            && left.callback_inventory_only == right.callback_inventory_only
            && left.native_correction_only == right.native_correction_only
            && left.request_id == right.request_id
            && left.client_role == right.client_role
            && left.network_profile == right.network_profile
            && left.fault_seed == right.fault_seed
            && left.test_worker_stall_after_ms
                == right.test_worker_stall_after_ms
            && left.test_worker_stall_duration_ms
                == right.test_worker_stall_duration_ms
            && left.test_forced_rollback_depth
                == right.test_forced_rollback_depth
            && left.expected_build_id == right.expected_build_id
            && left.expected_schema_id == right.expected_schema_id
            && left.expected_native_stage_identity
                == right.expected_native_stage_identity
            && left.expected_selection_hash
                == right.expected_selection_hash
            && left.bind_observed_stock_selection
                == right.bind_observed_stock_selection
            && left.replay_test_selection_override
                == right.replay_test_selection_override;
    }

    static inline uint64_t ComputeRollbackSessionContractHash(
        const RollbackProductionConfig& config,
        uint64_t selection_hash) noexcept
    {
        RollbackHash hash {};
        hash.add_scalar(kRollbackProtocolV2Version);
        hash.add_scalar(static_cast<uint8_t>(config.transport_mode));
        hash.add_scalar(config.expected_build_id);
        hash.add_scalar(config.expected_schema_id);
        hash.add_scalar(config.rollback_window);
        hash.add_scalar(config.input_delay);
        hash.add_scalar(static_cast<uint8_t>(config.save_policy));
        hash.add_scalar(config.lead_pacing.enabled);
        hash.add_scalar(RollbackFramesToMilliframes(
            config.lead_pacing.enter_frames));
        hash.add_scalar(RollbackFramesToMilliframes(
            config.lead_pacing.exit_frames));
        hash.add_scalar(config.lead_pacing.maximum_consecutive_holds);
        hash.add_scalar(static_cast<uint8_t>(config.lifecycle_mode));
        hash.add_scalar(static_cast<uint8_t>(config.session_domain));
        hash.add_scalar(static_cast<uint8_t>(config.network_profile));
        hash.add_scalar(config.fault_seed);
        hash.add_scalar(config.expected_native_stage_identity);
        hash.add_scalar(config.expected_selection_hash);
        hash.add_scalar(config.bind_observed_stock_selection);
        hash.add_scalar(config.replay_test_selection_override);
        hash.add_scalar(config.deterministic_input.hash());
        hash.add_scalar(config.replay_input.hash());
        hash.add_scalar(config.callback_inventory_only);
        hash.add_scalar(config.native_correction_only);
        hash.add_bytes(config.request_id.data(), config.request_id.size());
        hash.add_bytes(config.replay_input.file_sha256.data(),
            config.replay_input.file_sha256.size());
        hash.add_scalar(selection_hash);
        return hash.value ? hash.value : 1;
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
            if (port == 0 || address.empty()) return false;
            if (InetPtonA(
                    AF_INET, address.c_str(), &out.sin_addr) == 1)
                return true;

            // Resolve once during transport startup, then pin every packet to
            // the selected numeric endpoint. DNS is never consulted on the
            // worker hot path and a later DNS change cannot redirect an
            // authenticated live session.
            WSADATA data {};
            if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
                return false;
            addrinfo hints {};
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_DGRAM;
            hints.ai_protocol = IPPROTO_UDP;
            char service[6] {};
            if (sprintf_s(
                    service, sizeof(service), "%u",
                    static_cast<unsigned>(port)) <= 0)
            {
                WSACleanup();
                return false;
            }
            addrinfo* results = nullptr;
            const int resolve = getaddrinfo(
                address.c_str(), service, &hints, &results);
            bool found = false;
            if (resolve == 0)
            {
                for (const addrinfo* item = results;
                     item != nullptr; item = item->ai_next)
                {
                    if (item->ai_family != AF_INET
                        || item->ai_addrlen
                            < static_cast<int>(sizeof(sockaddr_in))
                        || !item->ai_addr)
                        continue;
                    std::memcpy(
                        &out, item->ai_addr, sizeof(sockaddr_in));
                    out.sin_port = htons(port);
                    found = true;
                    break;
                }
            }
            if (results) freeaddrinfo(results);
            WSACleanup();
            if (!found) out = {};
            return found;
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

    // Backend-neutral datagram seam used by the authenticated Protocol V2
    // worker. A backend owns and validates its peer address/identity before a
    // datagram reaches CNG, so an unexpected sender cannot force HMAC work.
    class IRollbackWireEndpoint
    {
    public:
        enum class ReceiveStatus : uint8_t
        {
            NoData,
            Packet,
            Rejected,
            Error,
        };

        virtual ~IRollbackWireEndpoint() = default;
        virtual bool open(
            const RollbackProductionConfig& config) noexcept = 0;
        virtual void close() noexcept = 0;
        virtual bool is_open() const noexcept = 0;
        virtual bool send(
            const RollbackProtocolV2WirePacket& packet) noexcept = 0;
        virtual ReceiveStatus receive(
            RollbackProtocolV2WirePacket& packet) noexcept = 0;
        virtual ReceiveStatus wait_readable(
            uint32_t timeout_ms) noexcept = 0;
    };

    class RollbackWinsockWireEndpoint final : public IRollbackWireEndpoint
    {
    public:
        bool open(
            const RollbackProductionConfig& config) noexcept override
        {
            sockaddr_in expected {};
            if (!RollbackUdpEndpoint::parse_address(
                    config.peer_address, config.peer_port, expected))
            {
                return false;
            }
            m_expected_peer = expected;
            return m_endpoint.open(config.bind_address, config.bind_port);
        }

        void close() noexcept override
        {
            m_endpoint.close();
            m_expected_peer = {};
        }

        bool is_open() const noexcept override
        {
            return m_endpoint.is_open();
        }

        bool send(
            const RollbackProtocolV2WirePacket& packet) noexcept override
        {
            return m_endpoint.send(packet, m_expected_peer);
        }

        ReceiveStatus receive(
            RollbackProtocolV2WirePacket& packet) noexcept override
        {
            sockaddr_in from {};
            const RollbackUdpEndpoint::ReceiveStatus status =
                m_endpoint.receive(packet, from);
            if (status == RollbackUdpEndpoint::ReceiveStatus::NoData)
                return ReceiveStatus::NoData;
            if (status == RollbackUdpEndpoint::ReceiveStatus::Error)
                return ReceiveStatus::Error;
            return endpoint_equal(from, m_expected_peer)
                ? ReceiveStatus::Packet : ReceiveStatus::Rejected;
        }

        ReceiveStatus wait_readable(
            uint32_t timeout_ms) noexcept override
        {
            const RollbackUdpEndpoint::ReceiveStatus status =
                m_endpoint.wait_readable(timeout_ms);
            if (status == RollbackUdpEndpoint::ReceiveStatus::NoData)
                return ReceiveStatus::NoData;
            if (status == RollbackUdpEndpoint::ReceiveStatus::Error)
                return ReceiveStatus::Error;
            return ReceiveStatus::Packet;
        }

    private:
        static bool endpoint_equal(
            const sockaddr_in& left,
            const sockaddr_in& right) noexcept
        {
            return left.sin_family == right.sin_family
                && left.sin_port == right.sin_port
                && left.sin_addr.s_addr == right.sin_addr.s_addr;
        }

        RollbackUdpEndpoint m_endpoint {};
        sockaddr_in m_expected_peer {};
    };

    struct RollbackUdpMessage
    {
        RollbackProtocolV2PacketType packet_type {
            RollbackProtocolV2PacketType::Heartbeat};
        RollbackSequenceStamp ack {};
        uint64_t handshake_generation {0};
        uint64_t sequence {0};
        bool sequence_assigned {false};
        // Internal delivery metadata, never serialized onto Protocol V2.
        // Route managers stamp the authenticated child route that won
        // cross-route deduplication so deadline evidence is attributable.
        uint8_t route_index {UINT8_MAX};
        bool route_index_assigned {false};
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

    enum class RollbackTransportLifecycle : uint8_t
    {
        Stopped = 0,
        Starting = 1,
        RetryDelay = 2,
        Ready = 3,
        Failed = 4,
    };

    struct RollbackUdpWorkerStatus
    {
        RollbackTransportLifecycle transport_lifecycle {
            RollbackTransportLifecycle::Stopped};
        bool running {false};
        bool endpoint_open {false};
        bool peer_ready {false};
        bool endpoint_pinned {false};
        uint32_t bootstrap_attempt {0};
        uint32_t bootstrap_attempt_limit {0};
        bool retry_exhausted {false};
        uint64_t bound_native_epoch_key {0};
        RollbackUdpWorkerFailure last_failure {
            RollbackUdpWorkerFailure::None};
        uint64_t packets_sent {0};
        uint64_t packets_received {0};
        uint64_t packets_authenticated {0};
        uint64_t packets_rejected {0};
        uint64_t packets_decode_rejected {0};
        uint64_t packets_route_rejected {0};
        uint64_t packets_replay_rejected {0};
        uint64_t queue_overflows {0};
        uint64_t redundant_enqueue_deferrals {0};
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
        uint64_t test_worker_stalls_started {0};
        uint64_t test_worker_stalls_completed {0};
        uint64_t test_worker_stall_actual_ms {0};
        RollbackUdpWorkerFailure failure {RollbackUdpWorkerFailure::None};
    };

    class IRollbackTransport
    {
    public:
        virtual ~IRollbackTransport() = default;

        virtual bool start(
            const RollbackProductionConfig& config) noexcept = 0;
        virtual void stop() noexcept = 0;
        virtual bool enqueue(
            RollbackProtocolV2PacketType type,
            const void* payload,
            uint16_t payload_bytes,
            RollbackSequenceStamp ack = {},
            uint64_t expected_generation = UINT64_MAX) noexcept = 0;
        // Idempotent reliability traffic must never terminate an otherwise
        // healthy session merely because the primary gameplay queue is under
        // transient pressure. Concrete transports may give this traffic a
        // separate bounded lane and report a deferral without latching a
        // QueueOverflow failure.
        virtual bool enqueue_redundant(
            RollbackProtocolV2PacketType type,
            const void* payload,
            uint16_t payload_bytes,
            RollbackSequenceStamp ack = {},
            uint64_t expected_generation = UINT64_MAX) noexcept
        {
            return enqueue(
                type, payload, payload_bytes, ack, expected_generation);
        }
        virtual bool dequeue(RollbackUdpMessage& message) noexcept = 0;
        virtual bool peer_ready() const noexcept = 0;
        virtual RollbackUdpWorkerStatus status() const noexcept = 0;
    };

    class RollbackAuthenticatedNetworkWorker final : public IRollbackTransport
    {
    public:
        explicit RollbackAuthenticatedNetworkWorker(
            IRollbackWireEndpoint& endpoint) noexcept
            : m_endpoint(endpoint)
        {
        }
        ~RollbackAuthenticatedNetworkWorker() noexcept { stop(); }
        RollbackAuthenticatedNetworkWorker(
            const RollbackAuthenticatedNetworkWorker&) = delete;
        RollbackAuthenticatedNetworkWorker& operator=(
            const RollbackAuthenticatedNetworkWorker&) = delete;

        bool start(const RollbackProductionConfig& config) noexcept override
        {
            m_preconfirmed_start = false;
            return start_worker(config);
        }

        bool start_preconfirmed(
            const RollbackProductionConfig& config,
            const std::array<uint8_t, kRollbackProtocolV2NonceBytes>&
                local_nonce,
            const std::array<uint8_t, kRollbackProtocolV2NonceBytes>&
                remote_nonce) noexcept
        {
            const std::array<uint8_t, kRollbackProtocolV2NonceBytes> zero {};
            if (local_nonce == zero || remote_nonce == zero
                || local_nonce == remote_nonce)
            {
                m_failure.store(
                    RollbackUdpWorkerFailure::InvalidConfig,
                    std::memory_order_release);
                return false;
            }
            m_preconfirmed_local_nonce = local_nonce;
            m_preconfirmed_remote_nonce = remote_nonce;
            m_preconfirmed_start = true;
            if (start_worker(config)) return true;
            m_preconfirmed_start = false;
            return false;
        }

    private:
        bool start_worker(const RollbackProductionConfig& config) noexcept
        {
            stop();
            if (!m_outbound.initialized()
                || !m_redundant_outbound.initialized()
                || !m_inbound.initialized()
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
            m_outbound.clear();
            m_redundant_outbound.clear();
            m_inbound.clear();
            reset_status();
            if (m_preconfirmed_start)
            {
                // Publish generation 1 before start() returns. Callers may
                // enqueue immediately; leaving generation at zero until the
                // worker thread is scheduled would silently stale-drop that
                // first gameplay message.
                m_handshake_generation.store(1, std::memory_order_relaxed);
                m_endpoint_pinned.store(true, std::memory_order_relaxed);
                m_ready.store(true, std::memory_order_relaxed);
            }
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

    public:
        void stop() noexcept override
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
            uint64_t expected_generation = UINT64_MAX) noexcept override
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

        bool enqueue_redundant(
            RollbackProtocolV2PacketType type,
            const void* payload,
            uint16_t payload_bytes,
            RollbackSequenceStamp ack = {},
            uint64_t expected_generation = UINT64_MAX) noexcept override
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
            if (!m_redundant_outbound.push(message))
            {
                m_redundant_enqueue_deferrals.fetch_add(
                    1, std::memory_order_relaxed);
                return false;
            }
            return true;
        }

        bool dequeue(RollbackUdpMessage& message) noexcept override
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

        bool peer_ready() const noexcept override
        {
            return m_ready.load(std::memory_order_acquire);
        }

        RollbackUdpWorkerStatus status() const noexcept override
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
            out.packets_decode_rejected =
                m_packets_decode_rejected.load(std::memory_order_relaxed);
            out.packets_route_rejected =
                m_packets_route_rejected.load(std::memory_order_relaxed);
            out.packets_replay_rejected =
                m_packets_replay_rejected.load(std::memory_order_relaxed);
            out.queue_overflows =
                m_queue_overflows.load(std::memory_order_relaxed);
            out.redundant_enqueue_deferrals =
                m_redundant_enqueue_deferrals.load(
                    std::memory_order_relaxed);
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
            out.test_worker_stalls_started =
                m_test_worker_stalls_started.load(
                    std::memory_order_relaxed);
            out.test_worker_stalls_completed =
                m_test_worker_stalls_completed.load(
                    std::memory_order_relaxed);
            out.test_worker_stall_actual_ms =
                m_test_worker_stall_actual_ms.load(
                    std::memory_order_relaxed);
            out.failure = m_failure.load(std::memory_order_acquire);
            out.last_failure = out.failure;
            out.transport_lifecycle = out.failure
                    != RollbackUdpWorkerFailure::None
                ? RollbackTransportLifecycle::Failed
                : (out.peer_ready
                    ? RollbackTransportLifecycle::Ready
                    : (out.running
                        ? RollbackTransportLifecycle::Starting
                        : RollbackTransportLifecycle::Stopped));
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
            m_packets_decode_rejected.store(0, std::memory_order_relaxed);
            m_packets_route_rejected.store(0, std::memory_order_relaxed);
            m_packets_replay_rejected.store(0, std::memory_order_relaxed);
            m_queue_overflows.store(0, std::memory_order_relaxed);
            m_redundant_enqueue_deferrals.store(
                0, std::memory_order_relaxed);
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
            m_test_worker_stalls_started.store(
                0, std::memory_order_relaxed);
            m_test_worker_stalls_completed.store(
                0, std::memory_order_relaxed);
            m_test_worker_stall_actual_ms.store(
                0, std::memory_order_relaxed);
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
            m_next_sequence.store(1, std::memory_order_release);
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
                if (!m_endpoint.send(wire)) return false;
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
                if (!m_endpoint.send(wire)) return false;
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
            header.sequence = m_next_sequence.fetch_add(
                1, std::memory_order_acq_rel);
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
            const RollbackProtocolV2Packet& packet) const noexcept
        {
            if (packet.header.source_peer != m_config.remote_peer
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
            profile.save_policy = m_config.save_policy;
            profile.lead_pacing_enabled =
                m_config.lead_pacing.enabled ? 1u : 0u;
            profile.lead_pacing_enter_milliframes =
                RollbackFramesToMilliframes(
                    m_config.lead_pacing.enter_frames);
            profile.lead_pacing_exit_milliframes =
                RollbackFramesToMilliframes(
                    m_config.lead_pacing.exit_frames);
            profile.lead_pacing_maximum_holds =
                m_config.lead_pacing.maximum_consecutive_holds;
            profile.diagnostic_forced_rollback_depth =
                m_config.test_forced_rollback_depth;
            profile.network_profile = m_config.network_profile;
            profile.fault_seed = m_config.fault_seed;
            profile.expected_native_stage_identity =
                m_config.expected_native_stage_identity;
            profile.fixture_id = m_config.deterministic_input.fixture_id;
            profile.fixture_correction_start =
                m_config.deterministic_input.correction_start;
            profile.fixture_hold_updates =
                m_config.deterministic_input.hold_updates;
            profile.fixture_prediction_lead_updates =
                m_config.deterministic_input.prediction_lead_updates;
            profile.fixture_delay_owner_slot =
                m_config.deterministic_input.delay_owner_slot;
            profile.fixture_enabled =
                m_config.deterministic_input.enabled ? 1u : 0u;
            profile.replay_input_enabled =
                m_config.replay_input.enabled ? 1u : 0u;
            profile.replay_input_swap_players =
                m_config.replay_input.swap_players ? 1u : 0u;
            profile.replay_input_alignment =
                m_config.replay_input.alignment;
            profile.replay_input_round =
                m_config.replay_input.round_index;
            profile.replay_input_start_frame =
                m_config.replay_input.start_frame;
            profile.replay_input_round_frames =
                m_config.replay_input.round_frame_count;
            profile.replay_input_round_hash =
                m_config.replay_input.round_input_hash;
            profile.replay_input_sha256 =
                m_config.replay_input.file_sha256;
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
            const bool source_policy_matches =
                peer.native_input_source_slot == peer.local_player_slot;
            return peer.profile_version
                    == kRollbackUdpHandshakeProfileVersion
                && peer.local_player_slot
                    == static_cast<uint8_t>(1u - m_config.local_player_slot)
                && peer.session_domain == m_config.session_domain
                && peer.lifecycle_mode == m_config.lifecycle_mode
                && source_policy_matches
                && peer.rollback_window == m_config.rollback_window
                && peer.input_delay == m_config.input_delay
                && peer.save_policy == m_config.save_policy
                && peer.lead_pacing_enabled
                    == (m_config.lead_pacing.enabled ? 1u : 0u)
                && peer.lead_pacing_enter_milliframes
                    == RollbackFramesToMilliframes(
                        m_config.lead_pacing.enter_frames)
                && peer.lead_pacing_exit_milliframes
                    == RollbackFramesToMilliframes(
                        m_config.lead_pacing.exit_frames)
                && peer.lead_pacing_maximum_holds
                    == m_config.lead_pacing.maximum_consecutive_holds
                && peer.diagnostic_forced_rollback_depth
                    == m_config.test_forced_rollback_depth
                && peer.network_profile == m_config.network_profile
                && peer.fault_seed == m_config.fault_seed
                && peer.fixture_id
                    == m_config.deterministic_input.fixture_id
                && peer.fixture_correction_start
                    == m_config.deterministic_input.correction_start
                && peer.fixture_hold_updates
                    == m_config.deterministic_input.hold_updates
                && peer.fixture_prediction_lead_updates
                    == m_config.deterministic_input.prediction_lead_updates
                && peer.fixture_delay_owner_slot
                    == m_config.deterministic_input.delay_owner_slot
                && peer.fixture_enabled
                    == (m_config.deterministic_input.enabled ? 1u : 0u)
                && peer.replay_input_enabled
                    == (m_config.replay_input.enabled ? 1u : 0u)
                && peer.replay_input_swap_players
                    == (m_config.replay_input.swap_players ? 1u : 0u)
                && peer.replay_input_alignment
                    == m_config.replay_input.alignment
                && peer.replay_input_reserved == 0
                && peer.replay_input_round
                    == m_config.replay_input.round_index
                && peer.replay_input_start_frame
                    == m_config.replay_input.start_frame
                && peer.replay_input_round_frames
                    == m_config.replay_input.round_frame_count
                && peer.replay_input_round_hash
                    == m_config.replay_input.round_input_hash
                && peer.replay_input_sha256
                    == m_config.replay_input.file_sha256
                && peer.expected_build_id == m_config.expected_build_id
                && peer.expected_schema_id == m_config.expected_schema_id
                && peer.expected_native_stage_identity
                    == m_config.expected_native_stage_identity
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
                ;
        }

        bool receive_one(const Clock::time_point now) noexcept
        {
            RollbackProtocolV2WirePacket wire {};
            const IRollbackWireEndpoint::ReceiveStatus status =
                m_endpoint.receive(wire);
            if (status == IRollbackWireEndpoint::ReceiveStatus::NoData)
                return false;
            if (status == IRollbackWireEndpoint::ReceiveStatus::Error)
            {
                mark_io_failure();
                return false;
            }
            m_packets_received.fetch_add(1, std::memory_order_relaxed);

            // Reject an unexpected source before invoking CNG HMAC.  Route
            // validation is repeated after authentication for defense in
            // depth, but unauthenticated hosts must not be able to force the
            // comparatively expensive crypto path.
            if (status == IRollbackWireEndpoint::ReceiveStatus::Rejected)
            {
                m_packets_rejected.fetch_add(1, std::memory_order_relaxed);
                m_packets_route_rejected.fetch_add(
                    1, std::memory_order_relaxed);
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
                m_packets_decode_rejected.fetch_add(
                    1, std::memory_order_relaxed);
                return true;
            }

            if (packet.header.packet_type
                == RollbackProtocolV2PacketType::Hello)
            {
                if (packet.header.source_peer != m_config.remote_peer
                    || packet.header.destination_peer != m_config.local_peer
                    || !peer_profile_valid(packet))
                {
                    m_packets_rejected.fetch_add(1, std::memory_order_relaxed);
                    m_packets_route_rejected.fetch_add(
                        1, std::memory_order_relaxed);
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
                    m_packets_replay_rejected.fetch_add(
                        1, std::memory_order_relaxed);
                    return true;
                }
                if (m_has_retired_remote_nonce
                    && packet.header.source_nonce == m_retired_remote_nonce)
                {
                    m_packets_rejected.fetch_add(
                        1, std::memory_order_relaxed);
                    m_packets_replay_rejected.fetch_add(
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
                if (packet.header.source_peer != m_config.remote_peer
                    || packet.header.destination_peer != m_config.local_peer
                    || packet.header.destination_nonce != m_local_nonce
                    || !peer_profile_valid(packet))
                {
                    m_packets_rejected.fetch_add(1, std::memory_order_relaxed);
                    m_packets_route_rejected.fetch_add(
                        1, std::memory_order_relaxed);
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
                    m_packets_replay_rejected.fetch_add(
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
                    m_packets_replay_rejected.fetch_add(
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
                || !route_and_nonce_valid(packet))
            {
                m_packets_rejected.fetch_add(1, std::memory_order_relaxed);
                m_packets_route_rejected.fetch_add(
                    1, std::memory_order_relaxed);
                return true;
            }
            if (!m_replay.accept(
                    packet.header.source_nonce, packet.header.sequence))
            {
                m_packets_rejected.fetch_add(1, std::memory_order_relaxed);
                m_packets_replay_rejected.fetch_add(
                    1, std::memory_order_relaxed);
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
                message.sequence_assigned = true;
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
            if (m_preconfirmed_start)
            {
                m_local_nonce = m_preconfirmed_local_nonce;
                m_remote_nonce = m_preconfirmed_remote_nonce;
                m_has_retired_remote_nonce = false;
                m_replay.bind(m_remote_nonce);
                m_handshake_replay.clear();
                m_highest_remote_sequence.clear();
                m_next_sequence.store(1, std::memory_order_release);
                m_handshake_generation.store(1, std::memory_order_release);
                m_endpoint_pinned.store(true, std::memory_order_release);
                m_ready.store(true, std::memory_order_release);
                m_last_authenticated = Clock::now();
            }
            else if (!reset_session())
            {
                m_running.store(false, std::memory_order_release);
                return;
            }
            uint32_t reopen_backoff = kRollbackUdpReopenMinMs;
            Clock::time_point next_open = Clock::now();
            const Clock::time_point worker_started = next_open;
            Clock::time_point next_handshake = next_open;
            Clock::time_point next_heartbeat = next_open;
            if (m_endpoint.is_open())
            {
                // A composed transport may authenticate and open its physical
                // endpoint before starting this common protocol worker.
                m_endpoint_open.store(true, std::memory_order_release);
            }

            while (!m_stop.load(std::memory_order_acquire))
            {
                const Clock::time_point now = Clock::now();
                if (m_config.test_worker_stall_duration_ms != 0
                    && m_test_worker_stalls_started.load(
                        std::memory_order_relaxed) == 0
                    && now - worker_started >= std::chrono::milliseconds(
                        m_config.test_worker_stall_after_ms))
                {
                    m_test_worker_stalls_started.fetch_add(
                        1, std::memory_order_relaxed);
                    const Clock::time_point stall_started = Clock::now();
                    const Clock::time_point stall_until = stall_started
                        + std::chrono::milliseconds(
                            m_config.test_worker_stall_duration_ms);
                    while (!m_stop.load(std::memory_order_acquire)
                        && Clock::now() < stall_until)
                    {
                        std::this_thread::sleep_for(
                            std::chrono::milliseconds(1));
                    }
                    const uint64_t actual_ms = static_cast<uint64_t>(
                        std::chrono::duration_cast<
                            std::chrono::milliseconds>(
                            Clock::now() - stall_started).count());
                    m_test_worker_stall_actual_ms.store(
                        actual_ms, std::memory_order_relaxed);
                    m_test_worker_stalls_completed.fetch_add(
                        1, std::memory_order_release);
                    continue;
                }
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
                    if (!m_endpoint.open(m_config))
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
                    // Redundant control traffic has an independent bounded
                    // lane so it cannot crowd out Gekko/input traffic. A
                    // producer-side full lane is a retryable deferral, not a
                    // session-fatal queue overflow.
                    for (uint32_t i = 0;
                         i < 4 && m_redundant_outbound.pop(outbound);
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
                        == IRollbackWireEndpoint::ReceiveStatus::Error)
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
        IRollbackWireEndpoint& m_endpoint;
        RollbackBoundedSpscQueue<RollbackUdpMessage, 256> m_outbound {};
        RollbackBoundedSpscQueue<RollbackUdpMessage, 8>
            m_redundant_outbound {};
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
        std::atomic<uint64_t> m_next_sequence {1};
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
        std::atomic<uint64_t> m_packets_decode_rejected {0};
        std::atomic<uint64_t> m_packets_route_rejected {0};
        std::atomic<uint64_t> m_packets_replay_rejected {0};
        std::atomic<uint64_t> m_queue_overflows {0};
        std::atomic<uint64_t> m_redundant_enqueue_deferrals {0};
        std::atomic<uint64_t> m_reopen_count {0};
        std::atomic<uint64_t> m_handshake_generation {0};
        bool m_preconfirmed_start {false};
        std::array<uint8_t, kRollbackProtocolV2NonceBytes>
            m_preconfirmed_local_nonce {};
        std::array<uint8_t, kRollbackProtocolV2NonceBytes>
            m_preconfirmed_remote_nonce {};
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
        std::atomic<uint64_t> m_test_worker_stalls_started {0};
        std::atomic<uint64_t> m_test_worker_stalls_completed {0};
        std::atomic<uint64_t> m_test_worker_stall_actual_ms {0};
    };

    // Compatibility wrapper preserving the existing Direct UDP production and
    // self-test surface while the authenticated worker is shared by additional
    // datagram backends.
    class RollbackUdpNetworkWorker final : public IRollbackTransport
    {
    public:
        RollbackUdpNetworkWorker() noexcept
            : m_worker(m_endpoint)
        {
        }
        ~RollbackUdpNetworkWorker() noexcept { stop(); }
        RollbackUdpNetworkWorker(const RollbackUdpNetworkWorker&) = delete;
        RollbackUdpNetworkWorker& operator=(
            const RollbackUdpNetworkWorker&) = delete;

        bool start(
            const RollbackProductionConfig& config) noexcept override
        {
            m_prestart_failure = RollbackUdpWorkerFailure::None;
            sockaddr_in expected {};
            if (!RollbackUdpEndpoint::parse_address(
                    config.peer_address, config.peer_port, expected))
            {
                m_prestart_failure =
                    RollbackUdpWorkerFailure::InvalidConfig;
                return false;
            }
            return m_worker.start(config);
        }

        void stop() noexcept override
        {
            m_worker.stop();
            m_prestart_failure = RollbackUdpWorkerFailure::None;
        }

        bool enqueue(
            RollbackProtocolV2PacketType type,
            const void* payload,
            uint16_t payload_bytes,
            RollbackSequenceStamp ack = {},
            uint64_t expected_generation = UINT64_MAX) noexcept override
        {
            return m_worker.enqueue(
                type, payload, payload_bytes, ack, expected_generation);
        }

        bool enqueue_redundant(
            RollbackProtocolV2PacketType type,
            const void* payload,
            uint16_t payload_bytes,
            RollbackSequenceStamp ack = {},
            uint64_t expected_generation = UINT64_MAX) noexcept override
        {
            return m_worker.enqueue_redundant(
                type, payload, payload_bytes, ack, expected_generation);
        }

        bool dequeue(RollbackUdpMessage& message) noexcept override
        {
            return m_worker.dequeue(message);
        }

        bool peer_ready() const noexcept override
        {
            return m_worker.peer_ready();
        }

        RollbackUdpWorkerStatus status() const noexcept override
        {
            RollbackUdpWorkerStatus out = m_worker.status();
            if (m_prestart_failure != RollbackUdpWorkerFailure::None)
            {
                out.failure = m_prestart_failure;
                out.last_failure = m_prestart_failure;
                out.transport_lifecycle =
                    RollbackTransportLifecycle::Failed;
            }
            return out;
        }

    private:
        RollbackWinsockWireEndpoint m_endpoint {};
        RollbackAuthenticatedNetworkWorker m_worker;
        RollbackUdpWorkerFailure m_prestart_failure {
            RollbackUdpWorkerFailure::None};
    };
}
