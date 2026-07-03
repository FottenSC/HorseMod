#include "../HorseMod/horselib/RollbackGekkoUdpAdapter.hpp"

#include <cstdio>

int main()
{
    const Horse::RollbackGekkoUdpAdapterSelfTestReport report =
        Horse::RunRollbackGekkoUdpAdapterSelfTest();
    if (!report.ok)
    {
        std::printf(
            "rollback gekko-udp self-test failed failure=%s "
            "enabled=%d wsa=%d sockets=%d loopback=%d nonblocking=%d "
            "manual=%d wrong_endpoint=%d wrong_source=%d wrong_dest=%d "
            "wrong_session=%d create=%d adapter=%d start=%d actors=%d "
            "connected=%d session_started=%d save=%d load=%d advance=%d "
            "rollback=%d no_desync=%d sent=%d received=%d freed=%d "
            "bidirectional=%d bridge=%d bridge_meta=%d gameplay_decode=%d "
            "slots=%d state=%d checksums=%d destroy=%d "
            "packets_sent=%u packets_recv=%u bridge_bad=%u endpoint_bad=%u\n",
            report.failure ? report.failure : "?",
            report.dependency_enabled ? 1 : 0,
            report.wsa_started ? 1 : 0,
            report.sockets_open ? 1 : 0,
            report.bound_loopback ? 1 : 0,
            report.nonblocking ? 1 : 0,
            report.manual_udp_roundtrip ? 1 : 0,
            report.wrong_endpoint_rejected ? 1 : 0,
            report.wrong_source_rejected ? 1 : 0,
            report.wrong_destination_rejected ? 1 : 0,
            report.wrong_session_rejected ? 1 : 0,
            report.create_ok ? 1 : 0,
            report.adapter_set ? 1 : 0,
            report.start_ok ? 1 : 0,
            report.actors_ok ? 1 : 0,
            report.saw_player_connected ? 1 : 0,
            report.saw_session_started ? 1 : 0,
            report.saw_save ? 1 : 0,
            report.saw_load ? 1 : 0,
            report.saw_advance ? 1 : 0,
            report.saw_rollback_advance ? 1 : 0,
            report.no_desync ? 1 : 0,
            report.callbacks_sent ? 1 : 0,
            report.callbacks_received ? 1 : 0,
            report.callbacks_freed ? 1 : 0,
            report.bidirectional_payloads ? 1 : 0,
            report.bridge_roundtrip ? 1 : 0,
            report.bridge_metadata_accepted ? 1 : 0,
            report.gameplay_inputs_decoded ? 1 : 0,
            report.gameplay_slots_present ? 1 : 0,
            report.gameplay_inputs_drive_state ? 1 : 0,
            report.final_checksums_match ? 1 : 0,
            report.destroy_ok ? 1 : 0,
            report.packets_sent,
            report.packets_received,
            report.bridge_packets_rejected,
            report.endpoint_packets_rejected);
        return 1;
    }

    std::printf(
        "rollback gekko-udp self-test passed enabled=%d wsa=%d sockets=%d "
        "loopback=%d nonblocking=%d manual=%d wrong_endpoint=%d "
        "wrong_source=%d wrong_dest=%d wrong_session=%d sent=%d "
        "received=%d freed=%d bidirectional=%d bridge=%d bridge_meta=%d "
        "gameplay_decode=%d slots=%d state=%d checksums=%d save=%d load=%d "
        "advance=%d rollback=%d no_desync=%d "
        "frames=%u packets_sent=%u packets_recv=%u bridge_encoded=%u "
        "bridge_decoded=%u bridge_bad=%u endpoint_bad=%u gameplay_events=%u "
        "gameplay_inputs=%u port_a=%u port_b=%u checksum_a=0x%08X "
        "checksum_b=0x%08X\n",
        report.dependency_enabled ? 1 : 0,
        report.wsa_started ? 1 : 0,
        report.sockets_open ? 1 : 0,
        report.bound_loopback ? 1 : 0,
        report.nonblocking ? 1 : 0,
        report.manual_udp_roundtrip ? 1 : 0,
        report.wrong_endpoint_rejected ? 1 : 0,
        report.wrong_source_rejected ? 1 : 0,
        report.wrong_destination_rejected ? 1 : 0,
        report.wrong_session_rejected ? 1 : 0,
        report.callbacks_sent ? 1 : 0,
        report.callbacks_received ? 1 : 0,
        report.callbacks_freed ? 1 : 0,
        report.bidirectional_payloads ? 1 : 0,
        report.bridge_roundtrip ? 1 : 0,
        report.bridge_metadata_accepted ? 1 : 0,
        report.gameplay_inputs_decoded ? 1 : 0,
        report.gameplay_slots_present ? 1 : 0,
        report.gameplay_inputs_drive_state ? 1 : 0,
        report.final_checksums_match ? 1 : 0,
        report.saw_save ? 1 : 0,
        report.saw_load ? 1 : 0,
        report.saw_advance ? 1 : 0,
        report.saw_rollback_advance ? 1 : 0,
        report.no_desync ? 1 : 0,
        report.frames_submitted,
        report.packets_sent,
        report.packets_received,
        report.bridge_packets_encoded,
        report.bridge_packets_decoded,
        report.bridge_packets_rejected,
        report.endpoint_packets_rejected,
        report.gameplay_decoded_events,
        report.gameplay_decoded_inputs,
        report.port_a,
        report.port_b,
        report.final_checksum_a,
        report.final_checksum_b);
    return 0;
}
