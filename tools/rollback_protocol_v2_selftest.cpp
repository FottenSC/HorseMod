#include "../HorseMod/horselib/RollbackProtocolV2.hpp"

#include <array>
#include <cstdio>

int main()
{
    using namespace Horse;
    const bool pre_gekko_handoff =
        ClassifyRollbackPreGekkoPacket(
            RollbackProtocolV2PacketType::SessionContract)
            == RollbackPreGekkoPacketDisposition::AcceptSessionContract
        && ClassifyRollbackPreGekkoPacket(
            RollbackProtocolV2PacketType::LaunchBarrier)
            == RollbackPreGekkoPacketDisposition::AcceptLaunchBarrier
        && ClassifyRollbackPreGekkoPacket(
            RollbackProtocolV2PacketType::Gekko)
            == RollbackPreGekkoPacketDisposition::QueueForSession
        && ClassifyRollbackPreGekkoPacket(
            RollbackProtocolV2PacketType::Input)
            == RollbackPreGekkoPacketDisposition::QueueForSession
        && ClassifyRollbackPreGekkoPacket(
            RollbackProtocolV2PacketType::FixtureBarrier)
            == RollbackPreGekkoPacketDisposition::QueueForSession
        && ClassifyRollbackPreGekkoPacket(
            RollbackProtocolV2PacketType::RoundTransition)
            == RollbackPreGekkoPacketDisposition::AcceptRoundBoundary
        && ClassifyRollbackPreGekkoPacket(
            RollbackProtocolV2PacketType::SecondaryEventAuthority)
            == RollbackPreGekkoPacketDisposition::
                AcceptSecondaryEventAuthority
        && ClassifyRollbackPreGekkoPacket(
            RollbackProtocolV2PacketType::MotionBankAuthority)
            == RollbackPreGekkoPacketDisposition::AcceptMotionBankAuthority
        && ClassifyRollbackPreGekkoPacket(
            RollbackProtocolV2PacketType::StageWindAuthority)
            == RollbackPreGekkoPacketDisposition::AcceptStageWindAuthority
        && ClassifyRollbackPreGekkoPacket(
            RollbackProtocolV2PacketType::Disconnect)
            == RollbackPreGekkoPacketDisposition::PeerDisconnected
        && ClassifyRollbackPreGekkoPacket(
            RollbackProtocolV2PacketType::Desync)
            == RollbackPreGekkoPacketDisposition::PeerDesynced
        && ClassifyRollbackPreGekkoPacket(
            RollbackProtocolV2PacketType::Heartbeat)
            == RollbackPreGekkoPacketDisposition::Reject
        && ClassifyRollbackPreGekkoPacket(
            static_cast<RollbackProtocolV2PacketType>(15))
            == RollbackPreGekkoPacketDisposition::Reject;
    RollbackGekkoRoundDatagramHeader round_header {};
    round_header.source_player_slot = 1;
    round_header.round_ordinal = 4;
    round_header.session_epoch = 0x1111;
    round_header.round_epoch = 0x2222;
    round_header.payload_bytes = 8;
    const size_t round_bytes = sizeof(round_header)
        + round_header.payload_bytes;
    const bool round_scoping =
        ClassifyRollbackGekkoRoundDatagram(
            round_header, round_bytes, 1, 0x1111, 4, 0x2222)
            == RollbackGekkoRoundDatagramDisposition::Current
        && ClassifyRollbackGekkoRoundDatagram(
            round_header, round_bytes, 1, 0x1111, 5, 0x3333)
            == RollbackGekkoRoundDatagramDisposition::Stale
        && ClassifyRollbackGekkoRoundDatagram(
            round_header, round_bytes, 1, 0x1111, 3, 0x3333)
            == RollbackGekkoRoundDatagramDisposition::Future
        && ClassifyRollbackGekkoRoundDatagram(
            round_header, round_bytes, 1, 0x9999, 4, 0x2222)
            == RollbackGekkoRoundDatagramDisposition::WrongSession
        && ClassifyRollbackGekkoRoundDatagram(
            round_header, round_bytes - 1, 1, 0x1111, 4, 0x2222)
            == RollbackGekkoRoundDatagramDisposition::Invalid;
    const Horse::RollbackProtocolV2SelfTestReport report =
        Horse::RunRollbackProtocolV2SelfTest();
    std::array<uint8_t, kRollbackProtocolV2MaxPayloadBytes>
        maximum_payload {};
    RollbackProtocolV2Header boundary_header {};
    boundary_header.packet_type = RollbackProtocolV2PacketType::Gekko;
    boundary_header.source_peer = 1;
    boundary_header.destination_peer = 2;
    boundary_header.sequence = 1;
    boundary_header.build_id = 0x1111;
    boundary_header.schema_id = 0x2222;
    RollbackProtocolV2WirePacket maximum_wire {};
    const bool maximum_wire_accepted = EncodeRollbackProtocolV2Packet(
        boundary_header,
        maximum_payload.data(),
        static_cast<uint16_t>(maximum_payload.size()),
        "rollback-protocol-selftest-secret",
        maximum_wire);
    std::array<uint8_t, kRollbackProtocolV2MaxPayloadBytes + 1>
        oversized_payload {};
    RollbackProtocolV2WirePacket oversized_wire {};
    const bool transport_wire_boundary =
        maximum_wire_accepted
        && maximum_wire.size == kRollbackProtocolV2MaxWireBytes
        && !EncodeRollbackProtocolV2Packet(
            boundary_header,
            oversized_payload.data(),
            static_cast<uint16_t>(oversized_payload.size()),
            "rollback-protocol-selftest-secret",
            oversized_wire);
    std::printf(
        "rollback protocol-v2 self-test %s nonce=%d roundtrip=%d ack=%d "
        "payload_reject=%d tag_reject=%d secret_reject=%d build_reject=%d "
        "schema_reject=%d replay=%d nonce_replay=%d failure=%s\n",
        report.ok ? "passed" : "failed",
        report.nonce_generated ? 1 : 0,
        report.roundtrip ? 1 : 0,
        report.ack_present_roundtrip ? 1 : 0,
        report.corrupted_payload_rejected ? 1 : 0,
        report.corrupted_tag_rejected ? 1 : 0,
        report.wrong_secret_rejected ? 1 : 0,
        report.build_mismatch_rejected ? 1 : 0,
        report.schema_mismatch_rejected ? 1 : 0,
        report.replay_window_ok ? 1 : 0,
        report.nonce_scoped_replay_ok ? 1 : 0,
        report.failure ? report.failure : "?");
    return report.ok && pre_gekko_handoff && round_scoping
        && transport_wire_boundary ? 0 : 1;
}
