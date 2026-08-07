// ============================================================================
// Horse::RollbackProtocolV2
//
// Authenticated Horse-owned rollback datagrams.  SC6/Steam transport is not
// involved.  HMAC-SHA256 is provided by Windows CNG and truncated to 128 bits.
// ============================================================================

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <bcrypt.h>

#include <array>
#include <bitset>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace Horse
{
    static constexpr uint32_t kRollbackProtocolV2Magic = 0x32425248u;
    static constexpr uint16_t kRollbackProtocolV2Version = 2;
    static constexpr size_t kRollbackProtocolV2TagBytes = 16;
    static constexpr size_t kRollbackProtocolV2NonceBytes = 16;
    // Steam's legacy k_EP2PSendUnreliable contract limits the complete
    // datagram, including Horse's authenticated header, to 1200 bytes.
    // Keeping the common Protocol V2 ceiling at that transport-safe size also
    // prevents direct UDP from relying on IP fragmentation.
    static constexpr size_t kRollbackProtocolV2MaxWireBytes = 1200;
    // Every application packet carried by the route manager receives this
    // fixed authenticated logical-routing prefix. Keep upstream application
    // limits below the wire payload ceiling instead of failing late in send.
    static constexpr size_t kRollbackRoutedEnvelopeHeaderBytes = 36;

    struct RollbackProtocolV2HmacCacheStats
    {
        uint64_t provider_initializations {0};
        uint64_t property_queries {0};
        uint64_t workspace_growths {0};
    };

    inline std::atomic<uint64_t>
        g_rollback_protocol_v2_hmac_provider_initializations {0};
    inline std::atomic<uint64_t>
        g_rollback_protocol_v2_hmac_property_queries {0};
    inline std::atomic<uint64_t>
        g_rollback_protocol_v2_hmac_workspace_growths {0};

    class RollbackProtocolV2HmacProvider final
    {
    public:
        RollbackProtocolV2HmacProvider() noexcept
        {
            g_rollback_protocol_v2_hmac_provider_initializations.fetch_add(
                1, std::memory_order_relaxed);
            if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(
                    &m_algorithm,
                    BCRYPT_SHA256_ALGORITHM,
                    nullptr,
                    BCRYPT_ALG_HANDLE_HMAC_FLAG)))
            {
                m_algorithm = nullptr;
                return;
            }
            DWORD result_bytes = 0;
            g_rollback_protocol_v2_hmac_property_queries.fetch_add(
                2, std::memory_order_relaxed);
            if (!BCRYPT_SUCCESS(BCryptGetProperty(
                    m_algorithm,
                    BCRYPT_OBJECT_LENGTH,
                    reinterpret_cast<PUCHAR>(&m_object_bytes),
                    sizeof(m_object_bytes),
                    &result_bytes,
                    0))
                || !BCRYPT_SUCCESS(BCryptGetProperty(
                    m_algorithm,
                    BCRYPT_HASH_LENGTH,
                    reinterpret_cast<PUCHAR>(&m_digest_bytes),
                    sizeof(m_digest_bytes),
                    &result_bytes,
                    0))
                || m_digest_bytes < kRollbackProtocolV2TagBytes)
            {
                BCryptCloseAlgorithmProvider(m_algorithm, 0);
                m_algorithm = nullptr;
                m_object_bytes = 0;
                m_digest_bytes = 0;
            }
        }

        // Deliberately process-lifetime. Closing the shared CNG provider from
        // DLL static destruction would call bcrypt while loader lock is held.
        // Windows reclaims the handle at process exit, and UE4SS hot unload is
        // refused once rollback hooks have been installed.
        ~RollbackProtocolV2HmacProvider() = default;

        RollbackProtocolV2HmacProvider(
            const RollbackProtocolV2HmacProvider&) = delete;
        RollbackProtocolV2HmacProvider& operator=(
            const RollbackProtocolV2HmacProvider&) = delete;

        bool ready() const noexcept
        {
            return m_algorithm != nullptr;
        }

        BCRYPT_ALG_HANDLE algorithm() const noexcept
        {
            return m_algorithm;
        }

        DWORD object_bytes() const noexcept
        {
            return m_object_bytes;
        }

        DWORD digest_bytes() const noexcept
        {
            return m_digest_bytes;
        }

    private:
        BCRYPT_ALG_HANDLE m_algorithm {nullptr};
        DWORD m_object_bytes {0};
        DWORD m_digest_bytes {0};
    };

    struct RollbackProtocolV2HmacWorkspace
    {
        std::vector<uint8_t> object;
        std::vector<uint8_t> digest;

        bool ensure_capacity(
            size_t object_bytes, size_t digest_bytes) noexcept
        {
            try
            {
                if (object.capacity() < object_bytes)
                {
                    object.reserve(object_bytes);
                    g_rollback_protocol_v2_hmac_workspace_growths.fetch_add(
                        1, std::memory_order_relaxed);
                }
                if (digest.capacity() < digest_bytes)
                {
                    digest.reserve(digest_bytes);
                    g_rollback_protocol_v2_hmac_workspace_growths.fetch_add(
                        1, std::memory_order_relaxed);
                }
                object.resize(object_bytes);
                digest.resize(digest_bytes);
                return true;
            }
            catch (...)
            {
                return false;
            }
        }
    };

    inline RollbackProtocolV2HmacProvider&
    RollbackProtocolV2GetHmacProvider() noexcept
    {
        static RollbackProtocolV2HmacProvider provider;
        return provider;
    }

    inline RollbackProtocolV2HmacWorkspace&
    RollbackProtocolV2GetHmacWorkspace() noexcept
    {
        thread_local RollbackProtocolV2HmacWorkspace workspace;
        return workspace;
    }

    inline RollbackProtocolV2HmacCacheStats
    GetRollbackProtocolV2HmacCacheStats() noexcept
    {
        return {
            g_rollback_protocol_v2_hmac_provider_initializations.load(
                std::memory_order_relaxed),
            g_rollback_protocol_v2_hmac_property_queries.load(
                std::memory_order_relaxed),
            g_rollback_protocol_v2_hmac_workspace_growths.load(
                std::memory_order_relaxed),
        };
    }

    enum class RollbackProtocolV2PacketType : uint8_t
    {
        Hello = 1,
        HelloAck = 2,
        Input = 3,
        Gekko = 4,
        Heartbeat = 5,
        Disconnect = 6,
        Desync = 7,
        LaunchBarrier = 8,
        FixtureBarrier = 9,
        SessionContract = 10,
        RoundTransition = 11,
        SecondaryEventAuthority = 12,
        MotionBankAuthority = 13,
        StageWindAuthority = 14,
        Routed = 15,
        RouteProbe = 16,
        RouteProbeAck = 17,
    };

    enum class RollbackPreGekkoPacketDisposition : uint8_t
    {
        AcceptLaunchBarrier,
        AcceptSessionContract,
        AcceptRoundBoundary,
        AcceptSecondaryEventAuthority,
        AcceptMotionBankAuthority,
        AcceptStageWindAuthority,
        QueueForSession,
        PeerDisconnected,
        PeerDesynced,
        Reject,
    };

    constexpr RollbackPreGekkoPacketDisposition
    ClassifyRollbackPreGekkoPacket(
        RollbackProtocolV2PacketType packet_type) noexcept
    {
        switch (packet_type)
        {
        case RollbackProtocolV2PacketType::LaunchBarrier:
            return RollbackPreGekkoPacketDisposition::AcceptLaunchBarrier;
        case RollbackProtocolV2PacketType::SessionContract:
            return RollbackPreGekkoPacketDisposition::AcceptSessionContract;
        case RollbackProtocolV2PacketType::RoundTransition:
            return RollbackPreGekkoPacketDisposition::AcceptRoundBoundary;
        case RollbackProtocolV2PacketType::SecondaryEventAuthority:
            return RollbackPreGekkoPacketDisposition::
                AcceptSecondaryEventAuthority;
        case RollbackProtocolV2PacketType::MotionBankAuthority:
            return RollbackPreGekkoPacketDisposition::
                AcceptMotionBankAuthority;
        case RollbackProtocolV2PacketType::StageWindAuthority:
            return RollbackPreGekkoPacketDisposition::
                AcceptStageWindAuthority;
        case RollbackProtocolV2PacketType::Gekko:
        case RollbackProtocolV2PacketType::Input:
        case RollbackProtocolV2PacketType::FixtureBarrier:
            // One peer can finish the bilateral barrier first. Preserve both
            // Gekko traffic and confirmed-frame summaries until the lagging
            // peer has created its session.
            return RollbackPreGekkoPacketDisposition::QueueForSession;
        case RollbackProtocolV2PacketType::Disconnect:
            return RollbackPreGekkoPacketDisposition::PeerDisconnected;
        case RollbackProtocolV2PacketType::Desync:
            return RollbackPreGekkoPacketDisposition::PeerDesynced;
        default:
            return RollbackPreGekkoPacketDisposition::Reject;
        }
    }

    enum RollbackProtocolV2Flags : uint16_t
    {
        RollbackProtocolV2FlagAckPresent = 1u << 0,
    };

#pragma pack(push, 1)
    struct RollbackProtocolV2Header
    {
        uint32_t magic {kRollbackProtocolV2Magic};
        uint16_t version {kRollbackProtocolV2Version};
        RollbackProtocolV2PacketType packet_type {
            RollbackProtocolV2PacketType::Heartbeat};
        uint8_t header_bytes {0};
        uint16_t flags {0};
        uint8_t source_peer {0};
        uint8_t destination_peer {0};
        uint16_t payload_bytes {0};
        uint64_t sequence {0};
        uint64_t ack_sequence {0};
        std::array<uint8_t, kRollbackProtocolV2NonceBytes> source_nonce {};
        std::array<uint8_t, kRollbackProtocolV2NonceBytes>
            destination_nonce {};
        uint64_t build_id {0};
        uint64_t schema_id {0};
        std::array<uint8_t, kRollbackProtocolV2TagBytes> tag {};
    };
#pragma pack(pop)

    static_assert(
        sizeof(RollbackProtocolV2Header) < 256,
        "protocol header byte count must fit uint8");
    static_assert(
        sizeof(RollbackProtocolV2Header) == 94,
        "Protocol V2 wire header is a frozen compatibility contract");
    static_assert(
        kRollbackProtocolV2MaxWireBytes
            > sizeof(RollbackProtocolV2Header)
                + kRollbackRoutedEnvelopeHeaderBytes,
        "transport-safe wire ceiling must carry routed payloads");

    static constexpr size_t kRollbackProtocolV2MaxPayloadBytes =
        kRollbackProtocolV2MaxWireBytes
        - sizeof(RollbackProtocolV2Header);
    static constexpr size_t kRollbackProtocolV2ApplicationMaxPayloadBytes =
        kRollbackProtocolV2MaxPayloadBytes
        - kRollbackRoutedEnvelopeHeaderBytes;
    static_assert(kRollbackProtocolV2MaxPayloadBytes == 1106);
    static_assert(kRollbackProtocolV2ApplicationMaxPayloadBytes == 1070);

    struct RollbackProtocolV2WirePacket
    {
        std::array<
            uint8_t,
            sizeof(RollbackProtocolV2Header)
                + kRollbackProtocolV2MaxPayloadBytes> bytes {};
        uint16_t size {0};
    };
    static_assert(
        std::tuple_size<
            decltype(RollbackProtocolV2WirePacket::bytes)>::value
            == kRollbackProtocolV2MaxWireBytes,
        "Protocol V2 wire storage must match the unreliable datagram ceiling");

    struct RollbackProtocolV2Packet
    {
        RollbackProtocolV2Header header {};
        std::array<uint8_t, kRollbackProtocolV2MaxPayloadBytes> payload {};
        uint16_t payload_bytes {0};
    };

    struct RollbackProtocolV2DecodeReport
    {
        bool ok {false};
        bool authenticated {false};
        bool build_match {false};
        bool schema_match {false};
        const char* failure {"not-run"};
    };

    // The same authenticated packet sequence covers Gekko traffic, frame
    // summaries, acknowledgement windows, and control messages.  Under the
    // qualified impairment profiles this can exceed 500 packets/second, and
    // a packet may be deliberately reordered by more than 400 ms.  A 64-bit
    // history therefore rejected authenticated, unique packets which were
    // still inside the supported network envelope.  This is receiver-local
    // state; increasing it does not change the wire protocol.
    static constexpr size_t kRollbackProtocolV2ReplayWindowBits = 1024;

    struct RollbackProtocolV2ReplayWindow
    {
        uint64_t highest_sequence {0};
        std::bitset<kRollbackProtocolV2ReplayWindowBits> bitmap {};
        bool valid {false};

        void clear() noexcept
        {
            highest_sequence = 0;
            bitmap.reset();
            valid = false;
        }

        bool accept(uint64_t sequence) noexcept
        {
            if (sequence == 0) return false;
            if (!valid)
            {
                highest_sequence = sequence;
                bitmap.reset();
                bitmap.set(0);
                valid = true;
                return true;
            }
            if (sequence > highest_sequence)
            {
                const uint64_t distance = sequence - highest_sequence;
                if (distance >= kRollbackProtocolV2ReplayWindowBits)
                    bitmap.reset();
                else
                    bitmap <<= static_cast<size_t>(distance);
                bitmap.set(0);
                highest_sequence = sequence;
                return true;
            }
            const uint64_t distance = highest_sequence - sequence;
            if (distance >= kRollbackProtocolV2ReplayWindowBits)
                return false;
            const size_t bit = static_cast<size_t>(distance);
            if (bitmap.test(bit)) return false;
            bitmap.set(bit);
            return true;
        }
    };

    struct RollbackProtocolV2NonceReplayWindow
    {
        std::array<uint8_t, kRollbackProtocolV2NonceBytes> source_nonce {};
        RollbackProtocolV2ReplayWindow sequences {};
        bool bound {false};

        void clear() noexcept
        {
            source_nonce.fill(0);
            sequences.clear();
            bound = false;
        }

        bool bound_to(const std::array<
            uint8_t, kRollbackProtocolV2NonceBytes>& nonce) const noexcept
        {
            return bound && source_nonce == nonce;
        }

        void bind(const std::array<
            uint8_t, kRollbackProtocolV2NonceBytes>& nonce) noexcept
        {
            source_nonce = nonce;
            sequences.clear();
            bound = true;
        }

        bool accept(const std::array<
                uint8_t, kRollbackProtocolV2NonceBytes>& nonce,
            uint64_t sequence) noexcept
        {
            return bound_to(nonce) && sequences.accept(sequence);
        }

        bool rebind_and_accept(const std::array<
                uint8_t, kRollbackProtocolV2NonceBytes>& nonce,
            uint64_t sequence) noexcept
        {
            if (!bound_to(nonce)) bind(nonce);
            return sequences.accept(sequence);
        }
    };

    static inline bool RollbackProtocolV2PacketTypeValid(
        RollbackProtocolV2PacketType type) noexcept
    {
        return type >= RollbackProtocolV2PacketType::Hello
            && type
                <= RollbackProtocolV2PacketType::RouteProbeAck;
    }

    static constexpr uint8_t kRollbackGekkoRoundDatagramVersion = 1;

#pragma pack(push, 1)
    struct RollbackGekkoRoundDatagramHeader
    {
        uint8_t version {kRollbackGekkoRoundDatagramVersion};
        uint8_t source_player_slot {0};
        uint16_t header_bytes {28};
        uint32_t round_ordinal {0};
        uint64_t session_epoch {0};
        uint64_t round_epoch {0};
        uint16_t payload_bytes {0};
        uint16_t reserved {0};
    };
#pragma pack(pop)

    static_assert(sizeof(RollbackGekkoRoundDatagramHeader) == 28);
    static constexpr size_t kRollbackGekkoRoundMaxPayloadBytes =
        kRollbackProtocolV2ApplicationMaxPayloadBytes
        - sizeof(RollbackGekkoRoundDatagramHeader);
    static_assert(
        kRollbackGekkoRoundMaxPayloadBytes
                + sizeof(RollbackGekkoRoundDatagramHeader)
                + kRollbackRoutedEnvelopeHeaderBytes
            == kRollbackProtocolV2MaxPayloadBytes);

    enum class RollbackGekkoRoundDatagramDisposition : uint8_t
    {
        Invalid,
        WrongSession,
        Stale,
        Current,
        Future,
    };

    static inline bool RollbackGekkoRoundDatagramHeaderValid(
        const RollbackGekkoRoundDatagramHeader& header,
        size_t available_bytes) noexcept
    {
        return header.version == kRollbackGekkoRoundDatagramVersion
            && header.source_player_slot < 2
            && header.header_bytes == sizeof(header)
            && (header.round_ordinal & 0xFFFF0000u) == 0
            && header.session_epoch != 0 && header.round_epoch != 0
            && header.payload_bytes != 0
            && header.payload_bytes <= kRollbackGekkoRoundMaxPayloadBytes
            && header.reserved == 0
            && available_bytes == sizeof(header) + header.payload_bytes;
    }

    static inline RollbackGekkoRoundDatagramDisposition
    ClassifyRollbackGekkoRoundDatagram(
        const RollbackGekkoRoundDatagramHeader& header,
        size_t available_bytes,
        uint8_t expected_source_slot,
        uint64_t expected_session_epoch,
        uint32_t expected_round_ordinal,
        uint64_t expected_round_epoch) noexcept
    {
        if (!RollbackGekkoRoundDatagramHeaderValid(
                header, available_bytes)
            || expected_source_slot >= 2
            || (expected_round_ordinal & 0xFFFF0000u) != 0)
        {
            return RollbackGekkoRoundDatagramDisposition::Invalid;
        }
        if (header.source_player_slot != expected_source_slot
            || header.session_epoch != expected_session_epoch)
        {
            return RollbackGekkoRoundDatagramDisposition::WrongSession;
        }
        const uint16_t delta_bits = static_cast<uint16_t>(
            header.round_ordinal - expected_round_ordinal);
        if (delta_bits == 0x8000u)
            return RollbackGekkoRoundDatagramDisposition::Invalid;
        const int16_t delta = static_cast<int16_t>(delta_bits);
        if (delta < 0) return RollbackGekkoRoundDatagramDisposition::Stale;
        if (delta > 0) return RollbackGekkoRoundDatagramDisposition::Future;
        return header.round_epoch == expected_round_epoch
            ? RollbackGekkoRoundDatagramDisposition::Current
            : RollbackGekkoRoundDatagramDisposition::Invalid;
    }

    static inline bool RollbackProtocolV2ConstantTimeEqual(
        const uint8_t* a,
        const uint8_t* b,
        size_t bytes) noexcept
    {
        if (!a || !b) return false;
        uint8_t diff = 0;
        for (size_t i = 0; i < bytes; ++i)
            diff |= static_cast<uint8_t>(a[i] ^ b[i]);
        return diff == 0;
    }

    static inline bool RollbackProtocolV2RandomNonce(
        std::array<uint8_t, kRollbackProtocolV2NonceBytes>& nonce) noexcept
    {
        return BCRYPT_SUCCESS(BCryptGenRandom(
            nullptr,
            nonce.data(),
            static_cast<ULONG>(nonce.size()),
            BCRYPT_USE_SYSTEM_PREFERRED_RNG));
    }

    static inline bool RollbackProtocolV2Hmac(
        std::string_view secret,
        const uint8_t* header,
        size_t header_bytes,
        const uint8_t* payload,
        size_t payload_bytes,
        std::array<uint8_t, kRollbackProtocolV2TagBytes>& tag) noexcept
    {
        tag.fill(0);
        if (secret.empty() || !header || header_bytes == 0)
            return false;

        auto& provider = RollbackProtocolV2GetHmacProvider();
        if (!provider.ready())
            return false;
        auto& workspace = RollbackProtocolV2GetHmacWorkspace();
        if (!workspace.ensure_capacity(
                provider.object_bytes(), provider.digest_bytes()))
        {
            return false;
        }

        BCRYPT_HASH_HANDLE hash = nullptr;
        bool ok = false;

        if (BCRYPT_SUCCESS(BCryptCreateHash(
                provider.algorithm(),
                &hash,
                workspace.object.data(),
                static_cast<ULONG>(workspace.object.size()),
                reinterpret_cast<PUCHAR>(
                    const_cast<char*>(secret.data())),
                static_cast<ULONG>(secret.size()),
                0))
            && BCRYPT_SUCCESS(BCryptHashData(
                hash,
                const_cast<PUCHAR>(header),
                static_cast<ULONG>(header_bytes),
                0))
            && (payload_bytes == 0
                || (payload
                    && BCRYPT_SUCCESS(BCryptHashData(
                        hash,
                        const_cast<PUCHAR>(payload),
                        static_cast<ULONG>(payload_bytes),
                        0))))
            && BCRYPT_SUCCESS(BCryptFinishHash(
                hash,
                workspace.digest.data(),
                static_cast<ULONG>(workspace.digest.size()),
                0)))
        {
            std::memcpy(
                tag.data(), workspace.digest.data(), tag.size());
            ok = true;
        }

        if (hash) BCryptDestroyHash(hash);
        return ok;
    }

    static inline bool EncodeRollbackProtocolV2Packet(
        RollbackProtocolV2Header header,
        const void* payload,
        uint16_t payload_bytes,
        std::string_view secret,
        RollbackProtocolV2WirePacket& out) noexcept
    {
        out = {};
        if (!RollbackProtocolV2PacketTypeValid(header.packet_type)
            || header.source_peer == 0
            || header.destination_peer == 0
            || header.source_peer == header.destination_peer
            || header.sequence == 0
            || header.build_id == 0
            || header.schema_id == 0
            || payload_bytes > kRollbackProtocolV2MaxPayloadBytes
            || (payload_bytes != 0 && !payload))
        {
            return false;
        }
        header.magic = kRollbackProtocolV2Magic;
        header.version = kRollbackProtocolV2Version;
        header.header_bytes = sizeof(RollbackProtocolV2Header);
        header.payload_bytes = payload_bytes;
        header.tag.fill(0);

        std::memcpy(out.bytes.data(), &header, sizeof(header));
        if (payload_bytes)
        {
            std::memcpy(
                out.bytes.data() + sizeof(header), payload, payload_bytes);
        }
        std::array<uint8_t, kRollbackProtocolV2TagBytes> tag {};
        if (!RollbackProtocolV2Hmac(
                secret,
                out.bytes.data(),
                sizeof(header),
                payload_bytes
                    ? out.bytes.data() + sizeof(header) : nullptr,
                payload_bytes,
                tag))
        {
            out = {};
            return false;
        }
        std::memcpy(
            out.bytes.data() + offsetof(RollbackProtocolV2Header, tag),
            tag.data(),
            tag.size());
        out.size = static_cast<uint16_t>(sizeof(header) + payload_bytes);
        return true;
    }

    static inline RollbackProtocolV2DecodeReport
    DecodeRollbackProtocolV2Packet(
        const void* data,
        size_t bytes,
        std::string_view secret,
        uint64_t expected_build_id,
        uint64_t expected_schema_id,
        RollbackProtocolV2Packet& out) noexcept
    {
        RollbackProtocolV2DecodeReport report {};
        report.failure = "ok";
        out = {};
        if (!data || bytes < sizeof(RollbackProtocolV2Header)
            || bytes > sizeof(RollbackProtocolV2Header)
                    + kRollbackProtocolV2MaxPayloadBytes)
        {
            report.failure = "invalid-packet-size";
            return report;
        }
        RollbackProtocolV2Header header {};
        std::memcpy(&header, data, sizeof(header));
        if (header.magic != kRollbackProtocolV2Magic
            || header.version != kRollbackProtocolV2Version
            || header.header_bytes != sizeof(RollbackProtocolV2Header)
            || !RollbackProtocolV2PacketTypeValid(header.packet_type)
            || header.source_peer == 0
            || header.destination_peer == 0
            || header.source_peer == header.destination_peer
            || header.sequence == 0
            || header.payload_bytes > kRollbackProtocolV2MaxPayloadBytes
            || bytes != sizeof(header) + header.payload_bytes
            || (header.flags & ~RollbackProtocolV2FlagAckPresent) != 0)
        {
            report.failure = "invalid-packet-header";
            return report;
        }
        report.build_match = header.build_id == expected_build_id;
        report.schema_match = header.schema_id == expected_schema_id;
        if (!report.build_match)
        {
            report.failure = "build-id-mismatch";
            return report;
        }
        if (!report.schema_match)
        {
            report.failure = "schema-id-mismatch";
            return report;
        }

        RollbackProtocolV2Header authenticated_header = header;
        authenticated_header.tag.fill(0);
        std::array<uint8_t, kRollbackProtocolV2TagBytes> expected_tag {};
        const auto* payload = static_cast<const uint8_t*>(data)
            + sizeof(RollbackProtocolV2Header);
        if (!RollbackProtocolV2Hmac(
                secret,
                reinterpret_cast<const uint8_t*>(&authenticated_header),
                sizeof(authenticated_header),
                header.payload_bytes ? payload : nullptr,
                header.payload_bytes,
                expected_tag)
            || !RollbackProtocolV2ConstantTimeEqual(
                header.tag.data(), expected_tag.data(), expected_tag.size()))
        {
            report.failure = "hmac-mismatch";
            return report;
        }
        report.authenticated = true;
        out.header = header;
        out.payload_bytes = header.payload_bytes;
        if (header.payload_bytes)
            std::memcpy(out.payload.data(), payload, header.payload_bytes);
        report.ok = true;
        return report;
    }

    struct RollbackProtocolV2SelfTestReport
    {
        bool ok {false};
        bool nonce_generated {false};
        bool roundtrip {false};
        bool ack_present_roundtrip {false};
        bool corrupted_payload_rejected {false};
        bool corrupted_tag_rejected {false};
        bool wrong_secret_rejected {false};
        bool build_mismatch_rejected {false};
        bool schema_mismatch_rejected {false};
        bool replay_window_ok {false};
        bool nonce_scoped_replay_ok {false};
        const char* failure {"not-run"};
    };

    static inline RollbackProtocolV2SelfTestReport
    RunRollbackProtocolV2SelfTest() noexcept
    {
        RollbackProtocolV2SelfTestReport report {};
        report.failure = "ok";
        constexpr uint64_t kBuild = 0x5343365F4255494Cull;
        constexpr uint64_t kSchema = 0xABCDEF1234567890ull;
        constexpr std::string_view secret =
            "rollback-protocol-v2-self-test-secret";

        RollbackProtocolV2Header header {};
        header.packet_type = RollbackProtocolV2PacketType::Input;
        header.flags = RollbackProtocolV2FlagAckPresent;
        header.source_peer = 1;
        header.destination_peer = 2;
        header.sequence = 65;
        header.ack_sequence = 61;
        header.build_id = kBuild;
        header.schema_id = kSchema;
        report.nonce_generated =
            RollbackProtocolV2RandomNonce(header.source_nonce)
            && RollbackProtocolV2RandomNonce(header.destination_nonce)
            && header.source_nonce != header.destination_nonce;

        const uint32_t payload[] = {0x10, 0x20, 0x30, 0x40};
        RollbackProtocolV2WirePacket wire {};
        RollbackProtocolV2Packet decoded {};
        const RollbackProtocolV2DecodeReport decode =
            EncodeRollbackProtocolV2Packet(
                header, payload, sizeof(payload), secret, wire)
            ? DecodeRollbackProtocolV2Packet(
                wire.bytes.data(), wire.size, secret, kBuild, kSchema,
                decoded)
            : RollbackProtocolV2DecodeReport {};
        report.roundtrip = decode.ok
            && decode.authenticated
            && decoded.payload_bytes == sizeof(payload)
            && std::memcmp(
                decoded.payload.data(), payload, sizeof(payload)) == 0;
        report.ack_present_roundtrip = report.roundtrip
            && (decoded.header.flags
                & RollbackProtocolV2FlagAckPresent) != 0
            && decoded.header.ack_sequence == 61;

        RollbackProtocolV2WirePacket corrupt = wire;
        corrupt.bytes[sizeof(RollbackProtocolV2Header)] ^= 0x80;
        report.corrupted_payload_rejected =
            !DecodeRollbackProtocolV2Packet(
                corrupt.bytes.data(), corrupt.size, secret, kBuild, kSchema,
                decoded).ok;
        corrupt = wire;
        corrupt.bytes[offsetof(RollbackProtocolV2Header, tag)] ^= 0x01;
        report.corrupted_tag_rejected =
            !DecodeRollbackProtocolV2Packet(
                corrupt.bytes.data(), corrupt.size, secret, kBuild, kSchema,
                decoded).ok;
        report.wrong_secret_rejected =
            !DecodeRollbackProtocolV2Packet(
                wire.bytes.data(), wire.size, "wrong", kBuild, kSchema,
                decoded).ok;
        report.build_mismatch_rejected =
            !DecodeRollbackProtocolV2Packet(
                wire.bytes.data(), wire.size, secret, kBuild ^ 1, kSchema,
                decoded).ok;
        report.schema_mismatch_rejected =
            !DecodeRollbackProtocolV2Packet(
                wire.bytes.data(), wire.size, secret, kBuild, kSchema ^ 1,
                decoded).ok;

        RollbackProtocolV2ReplayWindow replay {};
        const bool a = replay.accept(1);
        const bool duplicate = replay.accept(1);
        const bool c = replay.accept(3);
        const bool reordered = replay.accept(2);
        const bool stale = replay.accept(1);
        const bool far_ahead = replay.accept(2048);
        const bool deep_reordered = replay.accept(1536);
        const bool deep_duplicate = replay.accept(1536);
        const bool too_old = replay.accept(1023);
        report.replay_window_ok =
            a && !duplicate && c && reordered && !stale
            && far_ahead && deep_reordered && !deep_duplicate && !too_old;

        std::array<uint8_t, kRollbackProtocolV2NonceBytes> nonce_a {};
        std::array<uint8_t, kRollbackProtocolV2NonceBytes> nonce_b {};
        nonce_a.fill(0xA1);
        nonce_b.fill(0xB2);
        RollbackProtocolV2NonceReplayWindow active_replay {};
        active_replay.bind(nonce_a);
        const bool active_first = active_replay.accept(nonce_a, 10);
        const bool foreign_high_rejected =
            !active_replay.accept(nonce_b, UINT64_C(1) << 60);
        const bool active_survived = active_replay.accept(nonce_a, 11);
        RollbackProtocolV2NonceReplayWindow handshake_replay {};
        const bool stale_candidate = handshake_replay.rebind_and_accept(
            nonce_b, UINT64_C(1) << 60);
        const bool fresh_candidate = handshake_replay.rebind_and_accept(
            nonce_a, 1);
        const bool fresh_continues = handshake_replay.accept(nonce_a, 2);
        report.nonce_scoped_replay_ok = active_first
            && foreign_high_rejected && active_survived
            && stale_candidate && fresh_candidate && fresh_continues;

        report.ok = report.nonce_generated
            && report.roundtrip
            && report.ack_present_roundtrip
            && report.corrupted_payload_rejected
            && report.corrupted_tag_rejected
            && report.wrong_secret_rejected
            && report.build_mismatch_rejected
            && report.schema_mismatch_rejected
            && report.replay_window_ok
            && report.nonce_scoped_replay_ok;
        if (!report.ok)
            report.failure = "rollback-protocol-v2-selftest-failed";
        return report;
    }
}
