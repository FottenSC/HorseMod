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
    static constexpr size_t kRollbackProtocolV2MaxPayloadBytes = 1200;

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
    };

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

    struct RollbackProtocolV2WirePacket
    {
        std::array<
            uint8_t,
            sizeof(RollbackProtocolV2Header)
                + kRollbackProtocolV2MaxPayloadBytes> bytes {};
        uint16_t size {0};
    };

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

    struct RollbackProtocolV2ReplayWindow
    {
        uint64_t highest_sequence {0};
        uint64_t bitmap {0};
        bool valid {false};

        void clear() noexcept
        {
            highest_sequence = 0;
            bitmap = 0;
            valid = false;
        }

        bool accept(uint64_t sequence) noexcept
        {
            if (sequence == 0) return false;
            if (!valid)
            {
                highest_sequence = sequence;
                bitmap = 1;
                valid = true;
                return true;
            }
            if (sequence > highest_sequence)
            {
                const uint64_t distance = sequence - highest_sequence;
                bitmap = distance >= 64 ? 1 : (bitmap << distance) | 1;
                highest_sequence = sequence;
                return true;
            }
            const uint64_t distance = highest_sequence - sequence;
            if (distance >= 64) return false;
            const uint64_t mask = uint64_t {1} << distance;
            if ((bitmap & mask) != 0) return false;
            bitmap |= mask;
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
            && type <= RollbackProtocolV2PacketType::LaunchBarrier;
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

        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_HASH_HANDLE hash = nullptr;
        DWORD object_bytes = 0;
        DWORD result_bytes = 0;
        DWORD digest_bytes = 0;
        std::vector<uint8_t> object;
        std::vector<uint8_t> digest;
        bool ok = false;

        if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(
                &algorithm,
                BCRYPT_SHA256_ALGORITHM,
                nullptr,
                BCRYPT_ALG_HANDLE_HMAC_FLAG)))
        {
            return false;
        }
        if (!BCRYPT_SUCCESS(BCryptGetProperty(
                algorithm,
                BCRYPT_OBJECT_LENGTH,
                reinterpret_cast<PUCHAR>(&object_bytes),
                sizeof(object_bytes),
                &result_bytes,
                0))
            || !BCRYPT_SUCCESS(BCryptGetProperty(
                algorithm,
                BCRYPT_HASH_LENGTH,
                reinterpret_cast<PUCHAR>(&digest_bytes),
                sizeof(digest_bytes),
                &result_bytes,
                0))
            || digest_bytes < kRollbackProtocolV2TagBytes)
        {
            BCryptCloseAlgorithmProvider(algorithm, 0);
            return false;
        }

        try
        {
            object.resize(object_bytes);
            digest.resize(digest_bytes);
        }
        catch (...)
        {
            BCryptCloseAlgorithmProvider(algorithm, 0);
            return false;
        }

        if (BCRYPT_SUCCESS(BCryptCreateHash(
                algorithm,
                &hash,
                object.data(),
                static_cast<ULONG>(object.size()),
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
                digest.data(),
                static_cast<ULONG>(digest.size()),
                0)))
        {
            std::memcpy(tag.data(), digest.data(), tag.size());
            ok = true;
        }

        if (hash) BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
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
        const bool far_ahead = replay.accept(70);
        const bool too_old = replay.accept(5);
        report.replay_window_ok =
            a && !duplicate && c && reordered && !stale
            && far_ahead && !too_old;

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
