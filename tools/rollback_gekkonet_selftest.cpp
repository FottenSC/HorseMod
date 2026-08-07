#include "../HorseMod/horselib/RollbackGekkoAdapter.hpp"

#include <cstdio>

int main()
{
    const Horse::RollbackGekkoSessionSelfTestReport report =
        Horse::RunRollbackGekkoSessionSelfTest();
    const Horse::RollbackGekkoAdapterSelfTestReport adapter_report =
        Horse::RunRollbackGekkoAdapterSelfTest();
    if (!report.ok || !adapter_report.ok)
    {
        std::printf(
            "rollback gekkonet self-test failed session_failure=%s "
            "adapter_failure=%s "
            "enabled=%d create=%d start=%d actors=%d save=%d load=%d "
            "advance=%d rollback_advance=%d no_desync=%d destroy=%d "
            "checksum_expected=%d "
            "adapter_create=%d adapter_start=%d adapter_actors=%d "
            "adapter_connected=%d adapter_started=%d adapter_save=%d "
            "adapter_load=%d adapter_advance=%d adapter_rollback=%d "
             "adapter_no_desync=%d adapter_destroy=%d "
             "adapter_sent=%d adapter_recv=%d adapter_free=%d "
             "adapter_bidirectional=%d bridge=%d bridge_meta=%d "
             "bridge_reject=%d adapter_gameplay_decode=%d "
             "adapter_gameplay_slots=%d adapter_gameplay_state=%d "
             "adapter_baseline_a=%d adapter_baseline_b=%d "
             "adapter_preframe_a=%d adapter_preframe_b=%d "
             "adapter_checksums=%d "
             "frames=%u saves=%u loads=%u advances=%u rollback_advances=%u "
             "adapter_packets_sent=%u adapter_packets_recv=%u "
             "adapter_frees=%u bridge_encoded=%u bridge_decoded=%u "
             "bridge_bad=%u adapter_gameplay_events=%u "
             "adapter_gameplay_inputs=%u\n",
            report.failure ? report.failure : "?",
            adapter_report.failure ? adapter_report.failure : "?",
            report.dependency_enabled ? 1 : 0,
            report.create_ok ? 1 : 0,
            report.start_ok ? 1 : 0,
            report.actors_ok ? 1 : 0,
            report.saw_save ? 1 : 0,
            report.saw_load ? 1 : 0,
            report.saw_advance ? 1 : 0,
            report.saw_rollback_advance ? 1 : 0,
            report.no_desync ? 1 : 0,
            report.destroy_ok ? 1 : 0,
            report.final_checksum_expected ? 1 : 0,
            adapter_report.create_ok ? 1 : 0,
            adapter_report.start_ok ? 1 : 0,
            adapter_report.actors_ok ? 1 : 0,
            adapter_report.saw_player_connected ? 1 : 0,
            adapter_report.saw_session_started ? 1 : 0,
            adapter_report.saw_save ? 1 : 0,
            adapter_report.saw_load ? 1 : 0,
            adapter_report.saw_advance ? 1 : 0,
            adapter_report.saw_rollback_advance ? 1 : 0,
            adapter_report.no_desync ? 1 : 0,
            adapter_report.destroy_ok ? 1 : 0,
            adapter_report.callbacks_sent ? 1 : 0,
            adapter_report.callbacks_received ? 1 : 0,
            adapter_report.callbacks_freed ? 1 : 0,
            adapter_report.bidirectional_payloads ? 1 : 0,
            adapter_report.bridge_roundtrip ? 1 : 0,
            adapter_report.bridge_metadata_accepted ? 1 : 0,
            adapter_report.bridge_rejections_ok ? 1 : 0,
            adapter_report.gameplay_inputs_decoded ? 1 : 0,
            adapter_report.gameplay_slots_present ? 1 : 0,
            adapter_report.gameplay_inputs_drive_state ? 1 : 0,
            adapter_report.initial_baseline_event_order_a ? 1 : 0,
            adapter_report.initial_baseline_event_order_b ? 1 : 0,
            adapter_report.preframe_transition_rollback_a ? 1 : 0,
            adapter_report.preframe_transition_rollback_b ? 1 : 0,
            adapter_report.final_checksums_match ? 1 : 0,
            report.frames_submitted,
            report.save_events,
            report.load_events,
            report.advance_events,
            report.rollback_advance_events,
            adapter_report.packets_sent,
            adapter_report.packets_received,
            adapter_report.free_calls,
            adapter_report.bridge_packets_encoded,
            adapter_report.bridge_packets_decoded,
            adapter_report.bridge_packets_rejected,
            adapter_report.gameplay_decoded_events,
            adapter_report.gameplay_decoded_inputs);
        return 1;
    }

    std::printf(
        "rollback gekkonet self-test passed enabled=%d save=%d load=%d "
        "advance=%d rollback_advance=%d no_desync=%d frames=%u "
        "saves=%u loads=%u advances=%u rollback_advances=%u "
        "checksum=0x%08X checksum_expected=%d adapter_sent=%d "
        "adapter_recv=%d "
         "adapter_free=%d adapter_bidirectional=%d adapter_checksums=%d "
         "adapter_load=%d adapter_rollback=%d "
         "adapter_packets_sent=%u adapter_packets_recv=%u adapter_frees=%u "
         "bridge=%d bridge_meta=%d bridge_reject=%d "
         "bridge_encoded=%u bridge_decoded=%u bridge_bad=%u "
         "adapter_gameplay_decode=%d adapter_gameplay_slots=%d "
         "adapter_gameplay_state=%d baseline_a=%d baseline_b=%d "
         "preframe_a=%d preframe_b=%d "
         "adapter_gameplay_events=%u "
         "adapter_gameplay_inputs=%u "
         "adapter_checksum_a=0x%08X adapter_checksum_b=0x%08X\n",
        report.dependency_enabled ? 1 : 0,
        report.saw_save ? 1 : 0,
        report.saw_load ? 1 : 0,
        report.saw_advance ? 1 : 0,
        report.saw_rollback_advance ? 1 : 0,
        report.no_desync ? 1 : 0,
        report.frames_submitted,
        report.save_events,
        report.load_events,
        report.advance_events,
        report.rollback_advance_events,
        report.final_checksum,
        report.final_checksum_expected ? 1 : 0,
        adapter_report.callbacks_sent ? 1 : 0,
        adapter_report.callbacks_received ? 1 : 0,
        adapter_report.callbacks_freed ? 1 : 0,
        adapter_report.bidirectional_payloads ? 1 : 0,
        adapter_report.final_checksums_match ? 1 : 0,
        adapter_report.saw_load ? 1 : 0,
        adapter_report.saw_rollback_advance ? 1 : 0,
        adapter_report.packets_sent,
        adapter_report.packets_received,
        adapter_report.free_calls,
        adapter_report.bridge_roundtrip ? 1 : 0,
        adapter_report.bridge_metadata_accepted ? 1 : 0,
        adapter_report.bridge_rejections_ok ? 1 : 0,
        adapter_report.bridge_packets_encoded,
        adapter_report.bridge_packets_decoded,
        adapter_report.bridge_packets_rejected,
        adapter_report.gameplay_inputs_decoded ? 1 : 0,
        adapter_report.gameplay_slots_present ? 1 : 0,
        adapter_report.gameplay_inputs_drive_state ? 1 : 0,
        adapter_report.initial_baseline_event_order_a ? 1 : 0,
        adapter_report.initial_baseline_event_order_b ? 1 : 0,
        adapter_report.preframe_transition_rollback_a ? 1 : 0,
        adapter_report.preframe_transition_rollback_b ? 1 : 0,
        adapter_report.gameplay_decoded_events,
        adapter_report.gameplay_decoded_inputs,
        adapter_report.final_checksum_a,
        adapter_report.final_checksum_b);
    return 0;
}
