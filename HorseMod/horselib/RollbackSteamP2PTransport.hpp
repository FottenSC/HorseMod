// ============================================================================
// Horse::RollbackSteamP2PTransport
//
// Legacy Steamworks v139 P2P datagram backend. Horse reuses SC6's initialized
// Steam client/user/pipe, owns one dedicated virtual channel, and never owns
// Steam initialization or callback dispatch.
// ============================================================================

#pragma once

#include "RollbackUdpRuntime.hpp"

#include <Windows.h>
#include <bcrypt.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>

namespace Horse
{
    static constexpr int kRollbackSteamP2PChannel = 0x484F;
    static constexpr uint32_t kRollbackSteamBootstrapMagic = 0x42535248u;
    static constexpr uint16_t kRollbackSteamBootstrapVersion = 1;
    static constexpr uint32_t kRollbackSteamBootstrapTimeoutMs = 10000;
    static constexpr uint32_t kRollbackSteamBootstrapResendMs = 250;
    static constexpr uint32_t kRollbackSteamBootstrapMaxAttempts = 3;
    static constexpr uint32_t kRollbackSteamBootstrapRetryDelayMs[2] {
        250, 1000};

    static constexpr bool RollbackSteamBootstrapFailureRetryable(
        RollbackUdpWorkerFailure failure) noexcept
    {
        return failure == RollbackUdpWorkerFailure::EndpointOpenFailed
            || failure == RollbackUdpWorkerFailure::EndpointIoFailed
            || failure == RollbackUdpWorkerFailure::PeerTimeout;
    }

    // Flat Steam API values for EP2PSend. Protocol V2 deliberately uses the
    // UDP-like mode; reliable gameplay delivery would introduce head-of-line
    // blocking into rollback input.
    static constexpr int kRollbackSteamP2PSendUnreliable = 0;
    // Bootstrap is a bounded pre-game exchange. Reliability here prevents a
    // dropped final confirmation from leaving one peer authenticated while
    // the other times out; gameplay packets remain explicitly unreliable.
    static constexpr int kRollbackSteamP2PSendReliable = 2;

    struct RollbackSteamP2PSessionState
    {
        uint8_t connection_active {0};
        uint8_t connecting {0};
        uint8_t session_error {0};
        uint8_t using_relay {0};
        int32_t bytes_queued_for_send {0};
        int32_t packets_queued_for_send {0};
        uint32_t remote_ip {0};
        uint16_t remote_port {0};
        uint16_t reserved {0};
    };
    static_assert(sizeof(RollbackSteamP2PSessionState) == 20);

    class IRollbackSteamLegacyApi
    {
    public:
        virtual ~IRollbackSteamLegacyApi() = default;
        virtual bool initialize() noexcept = 0;
        virtual bool available() const noexcept = 0;
        virtual bool send(
            uint64_t remote_steam_id,
            const void* data,
            uint32_t bytes,
            int send_type,
            int channel) noexcept = 0;
        virtual bool packet_available(
            uint32_t& bytes,
            int channel) noexcept = 0;
        virtual bool read(
            void* destination,
            uint32_t capacity,
            uint32_t& bytes,
            uint64_t& remote_steam_id,
            int channel) noexcept = 0;
        virtual bool session_state(
            uint64_t remote_steam_id,
            RollbackSteamP2PSessionState& state) noexcept = 0;
        virtual bool close_channel(
            uint64_t remote_steam_id,
            int channel) noexcept = 0;
        virtual const char* interface_version() const noexcept = 0;
    };

    class RollbackSteamLegacyApi final : public IRollbackSteamLegacyApi
    {
    public:
        bool initialize() noexcept override
        {
            // Never load or initialize Steam from Horse. SC6 owns this module,
            // its client context, and callback dispatch.
            const HMODULE module = GetModuleHandleW(L"steam_api64.dll");
            if (!module) return false;

            if (!m_symbols_resolved)
            {
                m_steam_client = proc<SteamClientFn>(module, "SteamClient");
                m_get_user = proc<GetHSteamUserFn>(
                    module, "SteamAPI_GetHSteamUser");
                m_get_pipe = proc<GetHSteamPipeFn>(
                    module, "SteamAPI_GetHSteamPipe");
                m_get_networking = proc<GetNetworkingFn>(
                    module, "SteamAPI_ISteamClient_GetISteamNetworking");
                m_send = proc<SendFn>(
                    module, "SteamAPI_ISteamNetworking_SendP2PPacket");
                m_available = proc<AvailableFn>(
                    module,
                    "SteamAPI_ISteamNetworking_IsP2PPacketAvailable");
                m_read = proc<ReadFn>(
                    module, "SteamAPI_ISteamNetworking_ReadP2PPacket");
                m_state = proc<StateFn>(
                    module, "SteamAPI_ISteamNetworking_GetP2PSessionState");
                m_close_channel = proc<CloseChannelFn>(
                    module,
                    "SteamAPI_ISteamNetworking_CloseP2PChannelWithUser");
            }
            if (!m_steam_client || !m_get_user || !m_get_pipe
                || !m_get_networking || !m_send || !m_available || !m_read
                || !m_state || !m_close_channel)
            {
                return false;
            }
            m_symbols_resolved = true;

            void* client = m_steam_client();
            const int user = m_get_user();
            const int pipe = m_get_pipe();
            if (!client || user == 0 || pipe == 0) return false;
            if (m_networking && client == m_client
                && user == m_user && pipe == m_pipe)
            {
                return true;
            }
            // Steam can replace the client user/pipe across login recovery.
            // Never retain an interface acquired from stale handles.
            m_networking = nullptr;
            m_version = "unavailable";
            m_client = client;
            m_user = user;
            m_pipe = pipe;

            static constexpr const char* kVersion = "SteamNetworking005";
            m_networking = m_get_networking(
                client, user, pipe, kVersion);
            if (!m_networking) return false;
            m_version = kVersion;
            return true;
        }

        bool available() const noexcept override
        {
            return m_networking != nullptr;
        }

        bool send(
            uint64_t remote_steam_id,
            const void* data,
            uint32_t bytes,
            int send_type,
            int channel) noexcept override
        {
            return available() && remote_steam_id != 0 && data && bytes != 0
                && m_send(
                    m_networking,
                    remote_steam_id,
                    data,
                    bytes,
                    send_type,
                    channel);
        }

        bool packet_available(
            uint32_t& bytes,
            int channel) noexcept override
        {
            bytes = 0;
            return available()
                && m_available(m_networking, &bytes, channel);
        }

        bool read(
            void* destination,
            uint32_t capacity,
            uint32_t& bytes,
            uint64_t& remote_steam_id,
            int channel) noexcept override
        {
            bytes = 0;
            remote_steam_id = 0;
            return available() && destination && capacity != 0
                && m_read(
                    m_networking,
                    destination,
                    capacity,
                    &bytes,
                    &remote_steam_id,
                    channel);
        }

        bool session_state(
            uint64_t remote_steam_id,
            RollbackSteamP2PSessionState& state) noexcept override
        {
            state = {};
            return available() && remote_steam_id != 0
                && m_state(m_networking, remote_steam_id, &state);
        }

        bool close_channel(
            uint64_t remote_steam_id,
            int channel) noexcept override
        {
            return available() && remote_steam_id != 0
                && m_close_channel(
                    m_networking, remote_steam_id, channel);
        }

        const char* interface_version() const noexcept override
        {
            return m_version;
        }

    private:
        template <typename Fn>
        static Fn proc(HMODULE module, const char* name) noexcept
        {
            return reinterpret_cast<Fn>(GetProcAddress(module, name));
        }

        using SteamClientFn = void*(__cdecl*)();
        using GetHSteamUserFn = int(__cdecl*)();
        using GetHSteamPipeFn = int(__cdecl*)();
        using GetNetworkingFn =
            void*(__cdecl*)(void*, int, int, const char*);
        using SendFn =
            bool(__cdecl*)(void*, uint64_t, const void*, uint32_t, int, int);
        using AvailableFn = bool(__cdecl*)(void*, uint32_t*, int);
        using ReadFn = bool(__cdecl*)(
            void*, void*, uint32_t, uint32_t*, uint64_t*, int);
        using StateFn = bool(__cdecl*)(
            void*, uint64_t, RollbackSteamP2PSessionState*);
        using CloseChannelFn = bool(__cdecl*)(void*, uint64_t, int);

        bool m_symbols_resolved {false};
        void* m_client {nullptr};
        int m_user {0};
        int m_pipe {0};
        void* m_networking {nullptr};
        const char* m_version {"unavailable"};
        SteamClientFn m_steam_client {nullptr};
        GetHSteamUserFn m_get_user {nullptr};
        GetHSteamPipeFn m_get_pipe {nullptr};
        GetNetworkingFn m_get_networking {nullptr};
        SendFn m_send {nullptr};
        AvailableFn m_available {nullptr};
        ReadFn m_read {nullptr};
        StateFn m_state {nullptr};
        CloseChannelFn m_close_channel {nullptr};
    };

    enum class RollbackSteamBootstrapPacketType : uint8_t
    {
        Hello = 1,
        Confirm = 2,
    };

#pragma pack(push, 1)
    struct RollbackSteamBootstrapPacket
    {
        uint32_t magic {kRollbackSteamBootstrapMagic};
        uint16_t version {kRollbackSteamBootstrapVersion};
        RollbackSteamBootstrapPacketType type {
            RollbackSteamBootstrapPacketType::Hello};
        uint8_t reserved8 {0};
        uint16_t packet_bytes {0};
        uint16_t reserved16 {0};
        uint64_t lobby_id {0};
        uint64_t owner_steam_id {0};
        uint64_t source_steam_id {0};
        uint64_t destination_steam_id {0};
        uint64_t generation {0};
        uint64_t peer_generation {0};
        uint64_t build_id {0};
        uint64_t schema_id {0};
        std::array<uint8_t, kRollbackProtocolV2NonceBytes> nonce {};
        std::array<uint8_t, 72> public_key {};
        std::array<uint8_t, kRollbackProtocolV2TagBytes> confirmation {};
    };

    struct RollbackSteamBootstrapTranscript
    {
        uint32_t magic {kRollbackSteamBootstrapMagic};
        uint16_t version {kRollbackSteamBootstrapVersion};
        uint16_t transcript_bytes {0};
        uint64_t lobby_id {0};
        uint64_t owner_steam_id {0};
        uint64_t lower_steam_id {0};
        uint64_t upper_steam_id {0};
        uint64_t lower_generation {0};
        uint64_t upper_generation {0};
        uint64_t build_id {0};
        uint64_t schema_id {0};
        std::array<uint8_t, kRollbackProtocolV2NonceBytes> lower_nonce {};
        std::array<uint8_t, kRollbackProtocolV2NonceBytes> upper_nonce {};
        std::array<uint8_t, 72> lower_public_key {};
        std::array<uint8_t, 72> upper_public_key {};
    };
#pragma pack(pop)

    static_assert(
        sizeof(RollbackSteamBootstrapPacket)
            <= kRollbackProtocolV2MaxWireBytes);
    static_assert(
        sizeof(RollbackSteamBootstrapTranscript)
            <= kRollbackProtocolV2MaxWireBytes);

    static inline bool RollbackSteamBootstrapPacketValid(
        const RollbackSteamBootstrapPacket& packet,
        const RollbackSteamSessionIdentity& identity,
        uint64_t build_id,
        uint64_t schema_id) noexcept
    {
        return packet.magic == kRollbackSteamBootstrapMagic
            && packet.version == kRollbackSteamBootstrapVersion
            && packet.packet_bytes == sizeof(packet)
            && packet.reserved8 == 0
            && packet.reserved16 == 0
            && (packet.type == RollbackSteamBootstrapPacketType::Hello
                || packet.type
                    == RollbackSteamBootstrapPacketType::Confirm)
            && packet.lobby_id == identity.lobby_id
            && packet.owner_steam_id == identity.owner_steam_id
            && packet.source_steam_id == identity.remote_steam_id
            && packet.destination_steam_id == identity.local_steam_id
            && packet.generation != 0
            && packet.build_id == build_id
            && packet.schema_id == schema_id;
    }

    class RollbackSteamEcdhKey
    {
    public:
        RollbackSteamEcdhKey() = default;
        ~RollbackSteamEcdhKey() noexcept { clear(); }
        RollbackSteamEcdhKey(const RollbackSteamEcdhKey&) = delete;
        RollbackSteamEcdhKey& operator=(
            const RollbackSteamEcdhKey&) = delete;

        bool generate() noexcept
        {
            clear();
            if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(
                    &m_algorithm,
                    BCRYPT_ECDH_P256_ALGORITHM,
                    nullptr,
                    0))
                || !BCRYPT_SUCCESS(BCryptGenerateKeyPair(
                    m_algorithm, &m_key, 256, 0))
                || !BCRYPT_SUCCESS(BCryptFinalizeKeyPair(m_key, 0)))
            {
                clear();
                return false;
            }
            ULONG bytes = 0;
            if (!BCRYPT_SUCCESS(BCryptExportKey(
                    m_key,
                    nullptr,
                    BCRYPT_ECCPUBLIC_BLOB,
                    m_public.data(),
                    static_cast<ULONG>(m_public.size()),
                    &bytes,
                    0))
                || bytes != m_public.size())
            {
                clear();
                return false;
            }
            return true;
        }

        const std::array<uint8_t, 72>& public_key() const noexcept
        {
            return m_public;
        }

        bool derive(
            const std::array<uint8_t, 72>& peer_public,
            const RollbackSteamBootstrapTranscript& transcript,
            std::array<uint8_t, 32>& key) noexcept
        {
            key.fill(0);
            if (!m_algorithm || !m_key) return false;
            BCRYPT_KEY_HANDLE peer = nullptr;
            BCRYPT_SECRET_HANDLE agreement = nullptr;
            bool ok = false;
            ULONG bytes = 0;
            BCryptBuffer buffers[3] {};
            BCryptBufferDesc descriptor {};
            const wchar_t hash_name[] = BCRYPT_SHA256_ALGORITHM;
            static constexpr char domain[] =
                "HorseMod-Steam-P2P-Bootstrap-v1";
            buffers[0].BufferType = KDF_HASH_ALGORITHM;
            buffers[0].cbBuffer = sizeof(hash_name);
            buffers[0].pvBuffer =
                const_cast<wchar_t*>(hash_name);
            buffers[1].BufferType = KDF_SECRET_PREPEND;
            buffers[1].cbBuffer = sizeof(transcript);
            buffers[1].pvBuffer =
                const_cast<RollbackSteamBootstrapTranscript*>(&transcript);
            buffers[2].BufferType = KDF_SECRET_APPEND;
            buffers[2].cbBuffer = sizeof(domain) - 1;
            buffers[2].pvBuffer = const_cast<char*>(domain);
            descriptor.ulVersion = BCRYPTBUFFER_VERSION;
            descriptor.cBuffers = 3;
            descriptor.pBuffers = buffers;

            if (BCRYPT_SUCCESS(BCryptImportKeyPair(
                    m_algorithm,
                    nullptr,
                    BCRYPT_ECCPUBLIC_BLOB,
                    &peer,
                    const_cast<PUCHAR>(peer_public.data()),
                    static_cast<ULONG>(peer_public.size()),
                    0))
                && BCRYPT_SUCCESS(BCryptSecretAgreement(
                    m_key, peer, &agreement, 0))
                && BCRYPT_SUCCESS(BCryptDeriveKey(
                    agreement,
                    BCRYPT_KDF_HASH,
                    &descriptor,
                    key.data(),
                    static_cast<ULONG>(key.size()),
                    &bytes,
                    0))
                && bytes == key.size())
            {
                ok = true;
            }
            if (agreement) BCryptDestroySecret(agreement);
            if (peer) BCryptDestroyKey(peer);
            if (!ok) key.fill(0);
            return ok;
        }

        void clear() noexcept
        {
            if (m_key)
            {
                BCryptDestroyKey(m_key);
                m_key = nullptr;
            }
            if (m_algorithm)
            {
                BCryptCloseAlgorithmProvider(m_algorithm, 0);
                m_algorithm = nullptr;
            }
            SecureZeroMemory(m_public.data(), m_public.size());
        }

    private:
        BCRYPT_ALG_HANDLE m_algorithm {nullptr};
        BCRYPT_KEY_HANDLE m_key {nullptr};
        std::array<uint8_t, 72> m_public {};
    };

    class RollbackSteamP2PWireEndpoint final : public IRollbackWireEndpoint
    {
    public:
        explicit RollbackSteamP2PWireEndpoint(
            IRollbackSteamLegacyApi& api) noexcept
            : m_api(api)
        {
        }

        bool set_identity(
            const RollbackSteamSessionIdentity& identity) noexcept
        {
            if (m_open.load(std::memory_order_acquire)
                || !identity.valid())
                return false;
            m_identity = identity;
            return true;
        }

        bool open(
            const RollbackProductionConfig&) noexcept override
        {
            if (m_open.load(std::memory_order_acquire)) return true;
            if (!m_identity.valid() || !m_api.initialize()
                || !m_api.available())
            {
                return false;
            }
            // Relay policy is interface-wide and has no prior-value getter.
            // Inherit SC6's existing policy instead of mutating shared state.
            // SC6's native P2P request callback owns session acceptance. Horse
            // opens only its dedicated channel after the exact native session
            // epoch has been proven ready; accepting again would make Horse a
            // second owner of SC6's connection lifecycle.
            m_open.store(true, std::memory_order_release);
            return true;
        }

        void close() noexcept override
        {
            if (m_open.exchange(false, std::memory_order_acq_rel)
                && m_identity.remote_steam_id != 0)
            {
                // This channel is Horse-owned. The shared SC6 P2P session is
                // intentionally never closed here.
                (void)m_api.close_channel(
                    m_identity.remote_steam_id,
                    kRollbackSteamP2PChannel);
            }
        }

        bool is_open() const noexcept override
        {
            return m_open.load(std::memory_order_acquire);
        }

        bool send(
            const RollbackProtocolV2WirePacket& packet) noexcept override
        {
            if (packet.size < sizeof(RollbackProtocolV2Header)
                || packet.size > kRollbackProtocolV2MaxWireBytes)
            {
                return false;
            }
            RollbackProtocolV2Header header {};
            std::memcpy(&header, packet.bytes.data(), sizeof(header));
            const bool handshake =
                header.packet_type == RollbackProtocolV2PacketType::Hello
                || header.packet_type
                    == RollbackProtocolV2PacketType::HelloAck;
            // The ECDH bootstrap and the authenticated protocol handshake are
            // bounded pre-game control traffic. Keep both reliable so Steam
            // cannot strand the session between key confirmation and gameplay
            // readiness. Heartbeats and all gameplay data stay unreliable.
            return send_raw(
                packet.bytes.data(),
                packet.size,
                handshake
                    ? kRollbackSteamP2PSendReliable
                    : kRollbackSteamP2PSendUnreliable);
        }

        ReceiveStatus receive(
            RollbackProtocolV2WirePacket& packet) noexcept override
        {
            packet = {};
            uint32_t bytes = 0;
            uint64_t sender = 0;
            std::array<uint8_t, kRollbackProtocolV2MaxWireBytes> raw {};
            const ReceiveStatus status =
                receive_raw(raw.data(), raw.size(), bytes, sender);
            if (status != ReceiveStatus::Packet) return status;
            if (bytes == 0 || bytes > packet.bytes.size())
                return ReceiveStatus::Rejected;
            std::memcpy(packet.bytes.data(), raw.data(), bytes);
            packet.size = static_cast<uint16_t>(bytes);
            return ReceiveStatus::Packet;
        }

        ReceiveStatus wait_readable(
            uint32_t timeout_ms) noexcept override
        {
            if (!m_open.load(std::memory_order_acquire))
                return ReceiveStatus::Error;
            uint32_t bytes = 0;
            if (m_api.packet_available(
                    bytes, kRollbackSteamP2PChannel))
            {
                return ReceiveStatus::Packet;
            }
            if (timeout_ms != 0)
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(timeout_ms));
            return ReceiveStatus::NoData;
        }

        bool send_raw(
            const void* data,
            uint32_t bytes,
            int send_type = kRollbackSteamP2PSendUnreliable) noexcept
        {
            return m_open.load(std::memory_order_acquire)
                && data && bytes != 0
                && bytes <= kRollbackProtocolV2MaxWireBytes
                && m_api.send(
                    m_identity.remote_steam_id,
                    data,
                    bytes,
                    send_type,
                    kRollbackSteamP2PChannel);
        }

        ReceiveStatus receive_raw(
            void* destination,
            uint32_t capacity,
            uint32_t& bytes,
            uint64_t& sender) noexcept
        {
            bytes = 0;
            sender = 0;
            if (!m_open.load(std::memory_order_acquire))
                return ReceiveStatus::Error;
            uint32_t available = 0;
            if (!m_api.packet_available(
                    available, kRollbackSteamP2PChannel))
            {
                return ReceiveStatus::NoData;
            }
            if (!m_api.read(
                    destination,
                    capacity,
                    bytes,
                    sender,
                    kRollbackSteamP2PChannel))
            {
                return ReceiveStatus::Error;
            }
            if (sender != m_identity.remote_steam_id
                || available > capacity
                || bytes != available)
            {
                return ReceiveStatus::Rejected;
            }
            return ReceiveStatus::Packet;
        }

        bool session_state(
            RollbackSteamP2PSessionState& state) noexcept
        {
            return m_api.session_state(
                m_identity.remote_steam_id, state);
        }

        const char* interface_version() const noexcept
        {
            return m_api.interface_version();
        }

    private:
        IRollbackSteamLegacyApi& m_api;
        RollbackSteamSessionIdentity m_identity {};
        std::atomic<bool> m_open {false};
    };

    class RollbackSteamP2PTransport final : public IRollbackTransport
    {
    public:
        RollbackSteamP2PTransport() noexcept
            : m_endpoint(m_api),
              m_worker(m_endpoint)
        {
        }

        explicit RollbackSteamP2PTransport(
            IRollbackSteamLegacyApi& api) noexcept
            : m_endpoint(api),
              m_worker(m_endpoint)
        {
        }

        ~RollbackSteamP2PTransport() noexcept { stop(); }
        RollbackSteamP2PTransport(
            const RollbackSteamP2PTransport&) = delete;
        RollbackSteamP2PTransport& operator=(
            const RollbackSteamP2PTransport&) = delete;

        bool set_peer_identity(
            const RollbackSteamSessionIdentity& identity) noexcept
        {
            if (m_running.load(std::memory_order_acquire)
                || !identity.valid())
            {
                return false;
            }
            m_identity = identity;
            return m_endpoint.set_identity(identity);
        }

        bool start(
            const RollbackProductionConfig& config) noexcept override
        {
            stop();
            if (config.transport_mode != RollbackTransportMode::SteamP2P
                || !config.valid() || !m_identity.valid())
            {
                m_failure.store(
                    RollbackUdpWorkerFailure::InvalidConfig,
                    std::memory_order_release);
                m_last_failure.store(
                    RollbackUdpWorkerFailure::InvalidConfig,
                    std::memory_order_release);
                m_lifecycle.store(
                    RollbackTransportLifecycle::Failed,
                    std::memory_order_release);
                return false;
            }
            try
            {
                m_config = config;
            }
            catch (...)
            {
                m_failure.store(
                    RollbackUdpWorkerFailure::ResourceAllocationFailed,
                    std::memory_order_release);
                m_last_failure.store(
                    RollbackUdpWorkerFailure::ResourceAllocationFailed,
                    std::memory_order_release);
                m_lifecycle.store(
                    RollbackTransportLifecycle::Failed,
                    std::memory_order_release);
                return false;
            }
            m_stop.store(false, std::memory_order_release);
            m_bootstrap_attempt.store(0, std::memory_order_release);
            m_retry_exhausted.store(false, std::memory_order_release);
            m_using_relay.store(false, std::memory_order_release);
            m_interface_revision.store(0, std::memory_order_release);
            m_native_epoch_key.store(
                m_identity.native_epoch_key(), std::memory_order_release);
            m_failure.store(
                RollbackUdpWorkerFailure::None,
                std::memory_order_release);
            m_last_failure.store(
                RollbackUdpWorkerFailure::None,
                std::memory_order_release);
            m_lifecycle.store(
                RollbackTransportLifecycle::Starting,
                std::memory_order_release);
            m_running.store(true, std::memory_order_release);
            try
            {
                m_bootstrap_thread =
                    std::thread([this]() noexcept { bootstrap(); });
            }
            catch (...)
            {
                m_running.store(false, std::memory_order_release);
                m_stop.store(true, std::memory_order_release);
                m_failure.store(
                    RollbackUdpWorkerFailure::EndpointOpenFailed,
                    std::memory_order_release);
                m_last_failure.store(
                    RollbackUdpWorkerFailure::EndpointOpenFailed,
                    std::memory_order_release);
                m_lifecycle.store(
                    RollbackTransportLifecycle::Failed,
                    std::memory_order_release);
                return false;
            }
            return true;
        }

        void stop() noexcept override
        {
            m_stop.store(true, std::memory_order_release);
            if (m_bootstrap_thread.joinable()
                && m_bootstrap_thread.get_id()
                    != std::this_thread::get_id())
            {
                try
                {
                    m_bootstrap_thread.join();
                }
                catch (...)
                {
                }
            }
            // Bootstrap owns worker startup. Join it before stopping the
            // worker so a bootstrap that passed its final stop observation
            // cannot start a fresh worker after shutdown.
            m_worker.stop();
            m_endpoint.close();
            m_worker_started.store(false, std::memory_order_release);
            m_key_confirmed.store(false, std::memory_order_release);
            m_running.store(false, std::memory_order_release);
            m_retry_exhausted.store(false, std::memory_order_release);
            m_bootstrap_attempt.store(0, std::memory_order_release);
            m_using_relay.store(false, std::memory_order_release);
            m_interface_revision.store(0, std::memory_order_release);
            m_native_epoch_key.store(0, std::memory_order_release);
            m_failure.store(
                RollbackUdpWorkerFailure::None,
                std::memory_order_release);
            m_last_failure.store(
                RollbackUdpWorkerFailure::None,
                std::memory_order_release);
            m_lifecycle.store(
                RollbackTransportLifecycle::Stopped,
                std::memory_order_release);
            SecureZeroMemory(m_session_key.data(), m_session_key.size());
        }

        bool enqueue(
            RollbackProtocolV2PacketType type,
            const void* payload,
            uint16_t payload_bytes,
            RollbackSequenceStamp ack = {},
            uint64_t expected_generation = UINT64_MAX) noexcept override
        {
            return m_worker_started.load(std::memory_order_acquire)
                && m_worker.enqueue(
                    type,
                    payload,
                    payload_bytes,
                    ack,
                    expected_generation);
        }

        bool enqueue_redundant(
            RollbackProtocolV2PacketType type,
            const void* payload,
            uint16_t payload_bytes,
            RollbackSequenceStamp ack = {},
            uint64_t expected_generation = UINT64_MAX) noexcept override
        {
            return m_worker_started.load(std::memory_order_acquire)
                && m_worker.enqueue_redundant(
                    type,
                    payload,
                    payload_bytes,
                    ack,
                    expected_generation);
        }

        bool dequeue(RollbackUdpMessage& message) noexcept override
        {
            return m_worker_started.load(std::memory_order_acquire)
                && m_worker.dequeue(message);
        }

        bool peer_ready() const noexcept override
        {
            return m_key_confirmed.load(std::memory_order_acquire)
                && m_worker_started.load(std::memory_order_acquire)
                && m_worker.peer_ready();
        }

        RollbackUdpWorkerStatus status() const noexcept override
        {
            RollbackUdpWorkerStatus out {};
            RollbackTransportLifecycle lifecycle_before {};
            RollbackTransportLifecycle lifecycle_after {};
            do
            {
                lifecycle_before =
                    m_lifecycle.load(std::memory_order_acquire);
                const bool worker_started =
                    m_worker_started.load(std::memory_order_acquire);
                out = worker_started
                    ? m_worker.status() : RollbackUdpWorkerStatus {};
                if (!worker_started)
                {
                    out.running =
                        m_running.load(std::memory_order_acquire);
                    out.endpoint_open = m_endpoint.is_open();
                    out.endpoint_pinned =
                        m_key_confirmed.load(std::memory_order_acquire);
                    out.peer_ready = false;
                    out.failure =
                        m_failure.load(std::memory_order_acquire);
                }
                out.bootstrap_attempt =
                    m_bootstrap_attempt.load(std::memory_order_acquire);
                out.retry_exhausted =
                    m_retry_exhausted.load(std::memory_order_acquire);
                out.bound_native_epoch_key =
                    m_native_epoch_key.load(std::memory_order_acquire);
                out.last_failure = out.failure
                        != RollbackUdpWorkerFailure::None
                    ? out.failure
                    : m_last_failure.load(std::memory_order_acquire);
                lifecycle_after =
                    m_lifecycle.load(std::memory_order_acquire);
            }
            while (lifecycle_before != lifecycle_after);
            out.transport_lifecycle = lifecycle_after;
            if (m_worker_started.load(std::memory_order_acquire)
                && (out.failure != RollbackUdpWorkerFailure::None
                    || !out.running))
            {
                out.transport_lifecycle =
                    RollbackTransportLifecycle::Failed;
            }
            out.bootstrap_attempt_limit =
                kRollbackSteamBootstrapMaxAttempts;
            return out;
        }

        bool key_confirmed() const noexcept
        {
            return m_key_confirmed.load(std::memory_order_acquire);
        }

        bool using_relay() noexcept
        {
            return m_using_relay.load(std::memory_order_acquire);
        }

        uint8_t interface_revision() const noexcept
        {
            return m_interface_revision.load(std::memory_order_acquire);
        }

        uint32_t bootstrap_attempt() const noexcept
        {
            return m_bootstrap_attempt.load(std::memory_order_acquire);
        }

        bool retry_exhausted() const noexcept
        {
            return m_retry_exhausted.load(std::memory_order_acquire);
        }

        uint64_t native_epoch_key() const noexcept
        {
            return m_native_epoch_key.load(std::memory_order_acquire);
        }

    private:
        static RollbackSteamBootstrapTranscript transcript(
            const RollbackSteamBootstrapPacket& local,
            const RollbackSteamBootstrapPacket& remote) noexcept
        {
            RollbackSteamBootstrapTranscript out {};
            out.transcript_bytes = sizeof(out);
            out.lobby_id = local.lobby_id;
            out.owner_steam_id = local.owner_steam_id;
            out.build_id = local.build_id;
            out.schema_id = local.schema_id;
            const bool local_lower =
                local.source_steam_id < remote.source_steam_id;
            const RollbackSteamBootstrapPacket& lower =
                local_lower ? local : remote;
            const RollbackSteamBootstrapPacket& upper =
                local_lower ? remote : local;
            out.lower_steam_id = lower.source_steam_id;
            out.upper_steam_id = upper.source_steam_id;
            out.lower_generation = lower.generation;
            out.upper_generation = upper.generation;
            out.lower_nonce = lower.nonce;
            out.upper_nonce = upper.nonce;
            out.lower_public_key = lower.public_key;
            out.upper_public_key = upper.public_key;
            return out;
        }

        static bool confirmation(
            const std::array<uint8_t, 32>& key,
            const RollbackSteamBootstrapTranscript& transcript_value,
            uint64_t sender,
            std::array<uint8_t, kRollbackProtocolV2TagBytes>& tag) noexcept
        {
            const std::string_view secret(
                reinterpret_cast<const char*>(key.data()), key.size());
            return RollbackProtocolV2Hmac(
                secret,
                reinterpret_cast<const uint8_t*>(&transcript_value),
                sizeof(transcript_value),
                reinterpret_cast<const uint8_t*>(&sender),
                sizeof(sender),
                tag);
        }

        static bool protocol_nonce(
            const std::array<uint8_t, 32>& key,
            uint64_t lobby_id,
            uint64_t steam_id,
            std::array<uint8_t, kRollbackProtocolV2NonceBytes>& nonce)
            noexcept
        {
            struct Context
            {
                uint64_t label;
                uint64_t lobby_id;
                uint64_t steam_id;
            };
            const Context context {
                0x3145434E4F4E5248ull, // "HRNONCE1"
                lobby_id,
                steam_id,
            };
            const std::string_view secret(
                reinterpret_cast<const char*>(key.data()), key.size());
            return RollbackProtocolV2Hmac(
                secret,
                reinterpret_cast<const uint8_t*>(&context),
                sizeof(context),
                nullptr,
                0,
                nonce);
        }

        void bootstrap() noexcept
        {
            m_retry_exhausted.store(false, std::memory_order_release);
            m_native_epoch_key.store(
                m_identity.native_epoch_key(), std::memory_order_release);
            for (uint32_t attempt = 1;
                 attempt <= kRollbackSteamBootstrapMaxAttempts
                    && !m_stop.load(std::memory_order_acquire);
                 ++attempt)
            {
                if (attempt > 1)
                {
                    m_lifecycle.store(
                        RollbackTransportLifecycle::RetryDelay,
                        std::memory_order_release);
                    const uint32_t delay =
                        kRollbackSteamBootstrapRetryDelayMs[attempt - 2];
                    const auto retry_at = std::chrono::steady_clock::now()
                        + std::chrono::milliseconds(delay);
                    while (!m_stop.load(std::memory_order_acquire)
                        && std::chrono::steady_clock::now() < retry_at)
                    {
                        std::this_thread::sleep_for(
                            std::chrono::milliseconds(1));
                    }
                    if (m_stop.load(std::memory_order_acquire)) break;
                }

                m_bootstrap_attempt.store(attempt, std::memory_order_release);
                m_lifecycle.store(
                    RollbackTransportLifecycle::Starting,
                    std::memory_order_release);
                m_failure.store(
                    RollbackUdpWorkerFailure::None,
                    std::memory_order_release);
                if (!m_endpoint.open(m_config))
                {
                    m_failure.store(
                        RollbackUdpWorkerFailure::EndpointOpenFailed,
                        std::memory_order_release);
                    m_last_failure.store(
                        RollbackUdpWorkerFailure::EndpointOpenFailed,
                        std::memory_order_release);
                    continue;
                }

                const char* version = m_endpoint.interface_version();
                m_interface_revision.store(
                    version && std::strstr(version, "005") ? 5u
                        : version && std::strstr(version, "004") ? 4u : 0u,
                    std::memory_order_release);
                RollbackSteamP2PSessionState native_state {};
                m_using_relay.store(
                    m_endpoint.session_state(native_state)
                        && native_state.using_relay != 0,
                    std::memory_order_release);

                if (bootstrap_attempt_once())
                {
                    m_lifecycle.store(
                        RollbackTransportLifecycle::Ready,
                        std::memory_order_release);
                    return;
                }
                const RollbackUdpWorkerFailure attempt_failure =
                    m_failure.load(std::memory_order_acquire);
                m_last_failure.store(
                    attempt_failure, std::memory_order_release);
                m_key_confirmed.store(false, std::memory_order_release);
                SecureZeroMemory(
                    m_session_key.data(), m_session_key.size());
                m_endpoint.close();
                if (!RollbackSteamBootstrapFailureRetryable(
                        attempt_failure))
                    break;
            }

            const bool stopped = m_stop.load(std::memory_order_acquire);
            if (!stopped)
            {
                const RollbackUdpWorkerFailure final_failure =
                    m_failure.load(std::memory_order_acquire);
                m_retry_exhausted.store(
                    RollbackSteamBootstrapFailureRetryable(final_failure)
                        && m_bootstrap_attempt.load(
                            std::memory_order_acquire)
                            >= kRollbackSteamBootstrapMaxAttempts,
                    std::memory_order_release);
            }
            m_endpoint.close();
            m_running.store(false, std::memory_order_release);
            m_lifecycle.store(
                stopped ? RollbackTransportLifecycle::Stopped
                        : RollbackTransportLifecycle::Failed,
                std::memory_order_release);
        }

        bool bootstrap_attempt_once() noexcept
        {
            RollbackSteamEcdhKey local_key {};
            RollbackSteamBootstrapPacket local {};
            local.packet_bytes = sizeof(local);
            local.lobby_id = m_identity.lobby_id;
            local.owner_steam_id = m_identity.owner_steam_id;
            local.source_steam_id = m_identity.local_steam_id;
            local.destination_steam_id = m_identity.remote_steam_id;
            local.build_id = m_config.expected_build_id;
            local.schema_id = m_config.expected_schema_id;
            if (!local_key.generate()
                || !RollbackProtocolV2RandomNonce(local.nonce)
                || !RollbackProtocolV2RandomNonce(
                    m_bootstrap_generation_bytes))
            {
                m_failure.store(
                    RollbackUdpWorkerFailure::NonceGenerationFailed,
                    std::memory_order_release);
                return false;
            }
            std::memcpy(
                &local.generation,
                m_bootstrap_generation_bytes.data(),
                sizeof(local.generation));
            if (local.generation == 0) local.generation = 1;
            local.public_key = local_key.public_key();

            RollbackSteamBootstrapPacket remote {};
            bool remote_valid = false;
            bool derived = false;
            bool confirmed = false;
            RollbackSteamBootstrapTranscript transcript_value {};
            auto next_send = std::chrono::steady_clock::now();
            const auto deadline = next_send
                + std::chrono::milliseconds(
                    kRollbackSteamBootstrapTimeoutMs);

            while (!m_stop.load(std::memory_order_acquire)
                && std::chrono::steady_clock::now() < deadline)
            {
                const auto now = std::chrono::steady_clock::now();
                if (now >= next_send)
                {
                    local.type = derived
                        ? RollbackSteamBootstrapPacketType::Confirm
                        : RollbackSteamBootstrapPacketType::Hello;
                    local.peer_generation =
                        remote_valid ? remote.generation : 0;
                    local.confirmation.fill(0);
                    if (derived
                        && !confirmation(
                            m_session_key,
                            transcript_value,
                            local.source_steam_id,
                            local.confirmation))
                    {
                        m_failure.store(
                            RollbackUdpWorkerFailure
                                ::AuthenticationFailed,
                            std::memory_order_release);
                        break;
                    }
                    if (!m_endpoint.send_raw(
                            &local,
                            sizeof(local),
                            kRollbackSteamP2PSendReliable))
                    {
                        m_failure.store(
                            RollbackUdpWorkerFailure::EndpointIoFailed,
                            std::memory_order_release);
                        break;
                    }
                    next_send = now + std::chrono::milliseconds(
                        kRollbackSteamBootstrapResendMs);
                }

                std::array<uint8_t, kRollbackProtocolV2MaxWireBytes> raw {};
                uint32_t bytes = 0;
                uint64_t sender = 0;
                const IRollbackWireEndpoint::ReceiveStatus receive =
                    m_endpoint.receive_raw(
                        raw.data(), raw.size(), bytes, sender);
                if (receive
                    == IRollbackWireEndpoint::ReceiveStatus::Packet)
                {
                    if (bytes != sizeof(RollbackSteamBootstrapPacket))
                        continue;
                    RollbackSteamBootstrapPacket candidate {};
                    std::memcpy(&candidate, raw.data(), sizeof(candidate));
                    if (sender != m_identity.remote_steam_id
                        || !RollbackSteamBootstrapPacketValid(
                            candidate,
                            m_identity,
                            m_config.expected_build_id,
                            m_config.expected_schema_id))
                    {
                        continue;
                    }
                    remote = candidate;
                    remote_valid = true;
                    transcript_value = transcript(local, remote);
                    if (!derived)
                    {
                        if (!local_key.derive(
                                remote.public_key,
                                transcript_value,
                                m_session_key))
                        {
                            m_failure.store(
                                RollbackUdpWorkerFailure
                                    ::AuthenticationFailed,
                                std::memory_order_release);
                            break;
                        }
                        derived = true;
                        next_send = now;
                    }
                    if (remote.type
                            == RollbackSteamBootstrapPacketType::Confirm
                        && remote.peer_generation == local.generation)
                    {
                        std::array<uint8_t,
                            kRollbackProtocolV2TagBytes> expected {};
                        if (confirmation(
                                m_session_key,
                                transcript_value,
                                remote.source_steam_id,
                                expected)
                            && RollbackProtocolV2ConstantTimeEqual(
                                expected.data(),
                                remote.confirmation.data(),
                                expected.size()))
                        {
                            confirmed = true;
                            break;
                        }
                        m_failure.store(
                            RollbackUdpWorkerFailure
                                ::AuthenticationFailed,
                            std::memory_order_release);
                        break;
                    }
                }
                else if (receive
                    == IRollbackWireEndpoint::ReceiveStatus::Error)
                {
                    m_failure.store(
                        RollbackUdpWorkerFailure::EndpointIoFailed,
                        std::memory_order_release);
                    break;
                }
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(1));
            }

            local_key.clear();
            if (!confirmed || m_stop.load(std::memory_order_acquire))
            {
                if (!m_stop.load(std::memory_order_acquire)
                    && m_failure.load(std::memory_order_acquire)
                        == RollbackUdpWorkerFailure::None)
                {
                    m_failure.store(
                        RollbackUdpWorkerFailure::PeerTimeout,
                        std::memory_order_release);
                }
                SecureZeroMemory(
                    m_session_key.data(), m_session_key.size());
                return false;
            }

            RollbackProductionConfig authenticated = m_config;
            try
            {
                authenticated.secret.assign(
                    reinterpret_cast<const char*>(m_session_key.data()),
                    m_session_key.size());
            }
            catch (...)
            {
                m_failure.store(
                    RollbackUdpWorkerFailure::ResourceAllocationFailed,
                    std::memory_order_release);
                return false;
            }
            m_key_confirmed.store(true, std::memory_order_release);
            static_assert(
                kRollbackProtocolV2NonceBytes
                == kRollbackProtocolV2TagBytes);
            std::array<uint8_t, kRollbackProtocolV2NonceBytes>
                local_protocol_nonce {};
            std::array<uint8_t, kRollbackProtocolV2NonceBytes>
                remote_protocol_nonce {};
            if (!protocol_nonce(
                    m_session_key,
                    m_identity.lobby_id,
                    m_identity.local_steam_id,
                    local_protocol_nonce)
                || !protocol_nonce(
                    m_session_key,
                    m_identity.lobby_id,
                    m_identity.remote_steam_id,
                    remote_protocol_nonce)
                || !m_worker.start_preconfirmed(
                    authenticated,
                    local_protocol_nonce,
                    remote_protocol_nonce))
            {
                m_key_confirmed.store(false, std::memory_order_release);
                m_failure.store(
                    RollbackUdpWorkerFailure::EndpointOpenFailed,
                    std::memory_order_release);
                return false;
            }
            m_worker_started.store(true, std::memory_order_release);
            return true;
        }

        RollbackSteamLegacyApi m_api {};
        RollbackSteamP2PWireEndpoint m_endpoint;
        RollbackAuthenticatedNetworkWorker m_worker;
        RollbackSteamSessionIdentity m_identity {};
        RollbackProductionConfig m_config {};
        std::thread m_bootstrap_thread {};
        std::array<uint8_t, 32> m_session_key {};
        std::array<uint8_t, kRollbackProtocolV2NonceBytes>
            m_bootstrap_generation_bytes {};
        std::atomic<bool> m_stop {true};
        std::atomic<bool> m_running {false};
        std::atomic<bool> m_worker_started {false};
        std::atomic<bool> m_key_confirmed {false};
        std::atomic<bool> m_using_relay {false};
        std::atomic<bool> m_retry_exhausted {false};
        std::atomic<uint8_t> m_interface_revision {0};
        std::atomic<uint32_t> m_bootstrap_attempt {0};
        std::atomic<uint64_t> m_native_epoch_key {0};
        std::atomic<RollbackTransportLifecycle> m_lifecycle {
            RollbackTransportLifecycle::Stopped};
        std::atomic<RollbackUdpWorkerFailure> m_failure {
            RollbackUdpWorkerFailure::None};
        std::atomic<RollbackUdpWorkerFailure> m_last_failure {
            RollbackUdpWorkerFailure::None};
    };
}
