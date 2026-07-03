// ============================================================================
// Horse::RollbackGekkoTransportBridge
//
// Horse-owned envelope for tunneling GekkoNet adapter payloads through the
// rollback transport contract. This is still an offline model: no SC6 or Steam
// transport is touched here.
// ============================================================================

#pragma once

#include "RollbackTransport.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Horse
{
    static constexpr uint32_t kRollbackGekkoBridgeMagic = 0x31475248u; // "HRG1"
    static constexpr uint16_t kRollbackGekkoBridgeVersion = 2;
    static constexpr size_t kRollbackGekkoBridgeHeaderBytes = 88;
    static constexpr size_t kRollbackGekkoBridgeMaxPayloadBytes = 4096;
    static constexpr uint64_t kRollbackGekkoBridgeStateHashSalt =
        0x47454B4B42524944ull;
    static constexpr size_t kRollbackGekkoBridgeMaxWireBytes =
        kRollbackGekkoBridgeHeaderBytes + kRollbackGekkoBridgeMaxPayloadBytes;

    struct RollbackGekkoBridgeWirePacket
    {
        std::array<uint8_t, kRollbackGekkoBridgeMaxWireBytes> bytes {};
        size_t size {0};
    };

    struct RollbackGekkoBridgePacket
    {
        uint8_t source_peer {0};
        uint8_t dest_peer {0};
        uint64_t session_id {0};
        uint32_t sequence {0};
        uint64_t payload_hash {0};
        uint32_t payload_size {0};
        RollbackTransportPacket metadata {};
        std::array<uint8_t, kRollbackGekkoBridgeMaxPayloadBytes> payload {};
    };

    struct RollbackGekkoBridgeDecodePolicy
    {
        uint8_t expected_source_peer {0};
        uint8_t expected_dest_peer {0};
        uint64_t expected_session_id {0};
        bool require_source_peer {false};
        bool require_session_id {false};
    };

    struct RollbackGekkoBridgeSelfTestReport
    {
        bool ok {false};
        bool roundtrip {false};
        bool metadata_roundtrip {false};
        bool corrupt_magic_rejected {false};
        bool corrupt_length_rejected {false};
        bool corrupt_hash_rejected {false};
        bool metadata_payload_hash_rejected {false};
        bool metadata_state_hash_rejected {false};
        bool metadata_flags_rejected {false};
        bool wrong_source_rejected {false};
        bool wrong_destination_rejected {false};
        bool wrong_session_rejected {false};
        bool session_roundtrip {false};
        bool null_payload_rejected {false};
        bool empty_payload_rejected {false};
        bool oversize_rejected {false};
        bool metadata_accepts_in_transport_model {false};
        const char* failure {"not-run"};
    };

    static inline uint64_t RollbackGekkoBridgeHash(
        const void* data,
        size_t size) noexcept
    {
        if (!data && size != 0)
            return 0;
        const auto* p = static_cast<const uint8_t*>(data);
        uint64_t h = 1469598103934665603ull;
        for (size_t i = 0; i < size; ++i)
        {
            h ^= p[i];
            h *= 1099511628211ull;
        }
        return h;
    }

    static inline uint64_t MakeRollbackGekkoBridgeStateHash(
        uint32_t sequence,
        uint64_t payload_hash) noexcept
    {
        return payload_hash
            ^ (static_cast<uint64_t>(sequence) << 32)
            ^ kRollbackGekkoBridgeStateHashSalt;
    }

    static inline RollbackTransportPacket MakeRollbackGekkoBridgeMetadata(
        uint32_t sequence,
        uint32_t last_confirmed_remote_sequence,
        const void* payload,
        size_t payload_size) noexcept
    {
        RollbackTransportPacket p {};
        p.flags = RollbackTransportFlag_InputPresent
            | RollbackTransportFlag_StateHashPresent;
        p.local_frame = sequence;
        p.last_confirmed_remote_frame = last_confirmed_remote_sequence;
        p.local_input =
            RollbackGekkoBridgeHash(payload, payload_size);
        p.state_hash =
            MakeRollbackGekkoBridgeStateHash(sequence, p.local_input);
        return p;
    }

    static inline bool EncodeRollbackGekkoBridgePacketWithSession(
        uint8_t source_peer,
        uint8_t dest_peer,
        uint64_t session_id,
        uint32_t sequence,
        const RollbackTransportPacket& metadata,
        const void* payload,
        size_t payload_size,
        RollbackGekkoBridgeWirePacket& out) noexcept
    {
        out = {};
        if (!payload || payload_size == 0
            || payload_size > kRollbackGekkoBridgeMaxPayloadBytes)
        {
            return false;
        }

        RollbackTransportWirePacket metadata_wire {};
        if (!EncodeRollbackTransportPacket(metadata, metadata_wire)
            || metadata_wire.size != kRollbackTransportWireBytes)
        {
            return false;
        }

        out.size = kRollbackGekkoBridgeHeaderBytes + payload_size;
        RollbackTransportWrite32(out.bytes.data() + 0x00,
                                 kRollbackGekkoBridgeMagic);
        RollbackTransportWrite16(out.bytes.data() + 0x04,
                                 kRollbackGekkoBridgeVersion);
        RollbackTransportWrite16(out.bytes.data() + 0x06,
                                 static_cast<uint16_t>(
                                     kRollbackGekkoBridgeHeaderBytes));
        RollbackTransportWrite32(out.bytes.data() + 0x08,
                                 static_cast<uint32_t>(out.size));
        out.bytes[0x0C] = source_peer;
        out.bytes[0x0D] = dest_peer;
        RollbackTransportWrite16(out.bytes.data() + 0x0E, 0);
        RollbackTransportWrite32(out.bytes.data() + 0x10, sequence);
        RollbackTransportWrite32(out.bytes.data() + 0x14,
                                 static_cast<uint32_t>(payload_size));
        RollbackTransportWrite64(out.bytes.data() + 0x18,
                                 RollbackGekkoBridgeHash(
                                     payload, payload_size));
        RollbackTransportWrite64(out.bytes.data() + 0x20, session_id);
        std::memcpy(
            out.bytes.data() + 0x28,
            metadata_wire.bytes.data(),
            kRollbackTransportWireBytes);
        std::memcpy(
            out.bytes.data() + kRollbackGekkoBridgeHeaderBytes,
            payload,
            payload_size);
        return true;
    }

    static inline bool DecodeRollbackGekkoBridgePacket(
        const uint8_t* bytes,
        size_t size,
        const RollbackGekkoBridgeDecodePolicy& policy,
        RollbackGekkoBridgePacket& out) noexcept
    {
        out = {};
        if (!bytes || size < kRollbackGekkoBridgeHeaderBytes)
            return false;
        if (RollbackTransportRead32(bytes + 0x00)
            != kRollbackGekkoBridgeMagic)
            return false;
        if (RollbackTransportRead16(bytes + 0x04)
            != kRollbackGekkoBridgeVersion)
            return false;
        const size_t header_size = RollbackTransportRead16(bytes + 0x06);
        if (header_size != kRollbackGekkoBridgeHeaderBytes)
            return false;
        const size_t total_size = RollbackTransportRead32(bytes + 0x08);
        if (total_size != size)
            return false;
        out.source_peer = bytes[0x0C];
        out.dest_peer = bytes[0x0D];
        if (policy.require_source_peer
            && out.source_peer != policy.expected_source_peer)
        {
            return false;
        }
        if (out.dest_peer != policy.expected_dest_peer)
            return false;
        out.sequence = RollbackTransportRead32(bytes + 0x10);
        out.payload_size = RollbackTransportRead32(bytes + 0x14);
        if (out.payload_size == 0
            || out.payload_size > kRollbackGekkoBridgeMaxPayloadBytes)
        {
            return false;
        }
        if (header_size + out.payload_size != size)
            return false;
        out.payload_hash = RollbackTransportRead64(bytes + 0x18);
        out.session_id = RollbackTransportRead64(bytes + 0x20);
        if (policy.require_session_id
            && out.session_id != policy.expected_session_id)
        {
            return false;
        }

        if (!DecodeRollbackTransportPacket(
                bytes + 0x28,
                kRollbackTransportWireBytes,
                out.metadata))
        {
            return false;
        }
        if (out.metadata.local_frame != out.sequence)
            return false;

        const uint8_t* payload = bytes + header_size;
        const uint64_t actual_payload_hash =
            RollbackGekkoBridgeHash(payload, out.payload_size);
        if (actual_payload_hash != out.payload_hash)
        {
            return false;
        }
        const uint32_t required_metadata_flags =
            RollbackTransportFlag_InputPresent
            | RollbackTransportFlag_StateHashPresent;
        if ((out.metadata.flags & required_metadata_flags)
            != required_metadata_flags)
        {
            return false;
        }
        if (out.metadata.local_input != out.payload_hash)
            return false;
        if (out.metadata.state_hash
            != MakeRollbackGekkoBridgeStateHash(
                out.sequence, out.payload_hash))
        {
            return false;
        }
        std::memcpy(out.payload.data(), payload, out.payload_size);
        return true;
    }

    static inline bool EncodeRollbackGekkoBridgePacket(
        uint8_t source_peer,
        uint8_t dest_peer,
        uint32_t sequence,
        const RollbackTransportPacket& metadata,
        const void* payload,
        size_t payload_size,
        RollbackGekkoBridgeWirePacket& out) noexcept
    {
        return EncodeRollbackGekkoBridgePacketWithSession(
            source_peer,
            dest_peer,
            0,
            sequence,
            metadata,
            payload,
            payload_size,
            out);
    }

    static inline bool DecodeRollbackGekkoBridgePacket(
        const uint8_t* bytes,
        size_t size,
        uint8_t expected_dest_peer,
        RollbackGekkoBridgePacket& out) noexcept
    {
        RollbackGekkoBridgeDecodePolicy policy {};
        policy.expected_dest_peer = expected_dest_peer;
        return DecodeRollbackGekkoBridgePacket(bytes, size, policy, out);
    }

    static inline RollbackGekkoBridgeSelfTestReport
    RunRollbackGekkoBridgeSelfTest() noexcept
    {
        RollbackGekkoBridgeSelfTestReport report {};
        report.failure = "ok";

        std::array<uint8_t, 9> payload {
            0x47, 0x45, 0x4B, 0x4B, 0x4F, 0x2D, 0x48, 0x52, 0x42};
        RollbackTransportPacket metadata =
            MakeRollbackGekkoBridgeMetadata(
                7,
                5,
                payload.data(),
                payload.size());
        metadata.prediction_age_frames = 2;
        metadata.rollback_depth_frames = 3;
        const uint64_t session_id = 0x1122334455667788ull;

        RollbackGekkoBridgeWirePacket wire {};
        RollbackGekkoBridgePacket decoded {};
        report.roundtrip =
            EncodeRollbackGekkoBridgePacketWithSession(
                0xA0,
                0xB0,
                session_id,
                7,
                metadata,
                payload.data(),
                payload.size(),
                wire)
            && DecodeRollbackGekkoBridgePacket(
                wire.bytes.data(), wire.size, 0xB0, decoded)
            && decoded.source_peer == 0xA0
            && decoded.dest_peer == 0xB0
            && decoded.session_id == session_id
            && decoded.sequence == 7
            && decoded.payload_size == payload.size()
            && std::memcmp(
                decoded.payload.data(),
                payload.data(),
                payload.size()) == 0;
        const RollbackGekkoBridgePacket valid_decoded = decoded;
        report.metadata_roundtrip =
            report.roundtrip
            && valid_decoded.metadata.local_frame == metadata.local_frame
            && valid_decoded.metadata.last_confirmed_remote_frame
                == metadata.last_confirmed_remote_frame
            && valid_decoded.metadata.local_input == metadata.local_input
            && valid_decoded.metadata.state_hash == metadata.state_hash
            && valid_decoded.metadata.prediction_age_frames == 2
            && valid_decoded.metadata.rollback_depth_frames == 3;
        report.session_roundtrip =
            report.roundtrip && valid_decoded.session_id == session_id;

        RollbackGekkoBridgeWirePacket bad = wire;
        bad.bytes[0] ^= 0xFFu;
        report.corrupt_magic_rejected =
            !DecodeRollbackGekkoBridgePacket(
                bad.bytes.data(), bad.size, 0xB0, decoded);
        bad = wire;
        bad.bytes[0x08] ^= 0x01u;
        report.corrupt_length_rejected =
            !DecodeRollbackGekkoBridgePacket(
                bad.bytes.data(), bad.size, 0xB0, decoded);
        bad = wire;
        bad.bytes[kRollbackGekkoBridgeHeaderBytes] ^= 0x80u;
        report.corrupt_hash_rejected =
            !DecodeRollbackGekkoBridgePacket(
                bad.bytes.data(), bad.size, 0xB0, decoded);
        RollbackTransportPacket bad_metadata = metadata;
        bad_metadata.local_input ^= 0x01ull;
        report.metadata_payload_hash_rejected =
            EncodeRollbackGekkoBridgePacketWithSession(
                0xA0,
                0xB0,
                session_id,
                7,
                bad_metadata,
                payload.data(),
                payload.size(),
                bad)
            && !DecodeRollbackGekkoBridgePacket(
                bad.bytes.data(), bad.size, 0xB0, decoded);
        bad_metadata = metadata;
        bad_metadata.state_hash ^= 0x01ull;
        report.metadata_state_hash_rejected =
            EncodeRollbackGekkoBridgePacketWithSession(
                0xA0,
                0xB0,
                session_id,
                7,
                bad_metadata,
                payload.data(),
                payload.size(),
                bad)
            && !DecodeRollbackGekkoBridgePacket(
                bad.bytes.data(), bad.size, 0xB0, decoded);
        bad_metadata = metadata;
        bad_metadata.flags = RollbackTransportFlag_InputPresent;
        report.metadata_flags_rejected =
            EncodeRollbackGekkoBridgePacketWithSession(
                0xA0,
                0xB0,
                session_id,
                7,
                bad_metadata,
                payload.data(),
                payload.size(),
                bad)
            && !DecodeRollbackGekkoBridgePacket(
                bad.bytes.data(), bad.size, 0xB0, decoded);
        report.wrong_destination_rejected =
            !DecodeRollbackGekkoBridgePacket(
                wire.bytes.data(), wire.size, 0xA0, decoded);
        RollbackGekkoBridgeDecodePolicy strict_policy {};
        strict_policy.expected_source_peer = 0xA1;
        strict_policy.expected_dest_peer = 0xB0;
        strict_policy.expected_session_id = session_id;
        strict_policy.require_source_peer = true;
        strict_policy.require_session_id = true;
        report.wrong_source_rejected =
            !DecodeRollbackGekkoBridgePacket(
                wire.bytes.data(), wire.size, strict_policy, decoded);
        strict_policy.expected_source_peer = 0xA0;
        strict_policy.expected_session_id = session_id ^ 0x10ull;
        report.wrong_session_rejected =
            !DecodeRollbackGekkoBridgePacket(
                wire.bytes.data(), wire.size, strict_policy, decoded);
        report.null_payload_rejected =
            !EncodeRollbackGekkoBridgePacket(
                0xA0,
                0xB0,
                8,
                metadata,
                nullptr,
                payload.size(),
                bad);
        report.empty_payload_rejected =
            !EncodeRollbackGekkoBridgePacket(
                0xA0,
                0xB0,
                8,
                metadata,
                payload.data(),
                0,
                bad);
        RollbackGekkoBridgeWirePacket oversize {};
        report.oversize_rejected =
            !EncodeRollbackGekkoBridgePacket(
                0xA0,
                0xB0,
                8,
                metadata,
                oversize.bytes.data(),
                kRollbackGekkoBridgeMaxPayloadBytes + 1,
                oversize);

        RollbackTransportPeerModel<512> peer {};
        const RollbackTransportAcceptReport accept =
            peer.accept_remote_input(valid_decoded.metadata, 7, 60);
        report.metadata_accepts_in_transport_model =
            accept.accepted
            && peer.metrics().contiguous_remote_frame
                == kRollbackTransportNoFrame
            && peer.metrics().highest_remote_frame == 7
            && peer.metrics().last_peer_confirmed_frame == 5
            && peer.metrics().peer_confirmation_known;

        report.ok =
            report.roundtrip
            && report.metadata_roundtrip
            && report.corrupt_magic_rejected
            && report.corrupt_length_rejected
            && report.corrupt_hash_rejected
            && report.metadata_payload_hash_rejected
            && report.metadata_state_hash_rejected
            && report.metadata_flags_rejected
            && report.wrong_source_rejected
            && report.wrong_destination_rejected
            && report.wrong_session_rejected
            && report.session_roundtrip
            && report.null_payload_rejected
            && report.empty_payload_rejected
            && report.oversize_rejected
            && report.metadata_accepts_in_transport_model;
        if (!report.ok)
            report.failure = "gekko-bridge-selftest-failed";
        return report;
    }
}
