#include "../HorseMod/horselib/RollbackGekkoGameplayInputBridge.hpp"

#include <cstdio>

int main()
{
    const Horse::RollbackGekkoGameplayInputBridgeSelfTestReport report =
        Horse::RunRollbackGekkoGameplayInputBridgeSelfTest();
    if (!report.ok)
    {
        std::printf(
            "rollback gekko-gameplay-input self-test failed failure=%s "
            "enabled=%d raw=%d raw_p0=%d raw_p1=%d null=%d bad_frame=%d "
            "bad_size=%d bad_players=%d bad_slot=%d pipeline=%d "
            "payload_separate=%d "
            "create=%d start=%d actors=%d advance_decode=%d "
            "rollback_decode=%d no_desync=%d destroy=%d "
            "decoded_events=%u decoded_inputs=%u frames=%u advances=%u "
            "rollback_advances=%u checksum=0x%08X checksum_expected=%d\n",
            report.failure ? report.failure : "?",
            report.dependency_enabled ? 1 : 0,
            report.raw_decode_ok ? 1 : 0,
            report.raw_decode_player0 ? 1 : 0,
            report.raw_decode_player1 ? 1 : 0,
            report.null_inputs_rejected ? 1 : 0,
            report.bad_frame_rejected ? 1 : 0,
            report.bad_size_rejected ? 1 : 0,
            report.bad_player_count_rejected ? 1 : 0,
            report.bad_slot_rejected ? 1 : 0,
            report.pipeline_apply_ok ? 1 : 0,
            report.payload_hash_separate ? 1 : 0,
            report.create_ok ? 1 : 0,
            report.start_ok ? 1 : 0,
            report.actors_ok ? 1 : 0,
            report.actual_gekko_advance_decode ? 1 : 0,
            report.actual_gekko_rollback_decode ? 1 : 0,
            report.no_desync ? 1 : 0,
            report.destroy_ok ? 1 : 0,
            report.decoded_events,
            report.decoded_inputs,
            report.frames_submitted,
            report.advance_events,
            report.rollback_advance_events,
            report.final_checksum,
            report.final_checksum_expected ? 1 : 0);
        return 1;
    }

    std::printf(
        "rollback gekko-gameplay-input self-test passed "
        "enabled=%d raw=%d raw_p0=%d raw_p1=%d null=%d bad_frame=%d "
        "bad_size=%d bad_players=%d bad_slot=%d pipeline=%d "
        "payload_separate=%d "
        "create=%d start=%d actors=%d advance_decode=%d "
        "rollback_decode=%d no_desync=%d destroy=%d "
        "decoded_events=%u decoded_inputs=%u frames=%u advances=%u "
        "rollback_advances=%u checksum=0x%08X checksum_expected=%d\n",
        report.dependency_enabled ? 1 : 0,
        report.raw_decode_ok ? 1 : 0,
        report.raw_decode_player0 ? 1 : 0,
        report.raw_decode_player1 ? 1 : 0,
        report.null_inputs_rejected ? 1 : 0,
        report.bad_frame_rejected ? 1 : 0,
        report.bad_size_rejected ? 1 : 0,
        report.bad_player_count_rejected ? 1 : 0,
        report.bad_slot_rejected ? 1 : 0,
        report.pipeline_apply_ok ? 1 : 0,
        report.payload_hash_separate ? 1 : 0,
        report.create_ok ? 1 : 0,
        report.start_ok ? 1 : 0,
        report.actors_ok ? 1 : 0,
        report.actual_gekko_advance_decode ? 1 : 0,
        report.actual_gekko_rollback_decode ? 1 : 0,
        report.no_desync ? 1 : 0,
        report.destroy_ok ? 1 : 0,
        report.decoded_events,
        report.decoded_inputs,
        report.frames_submitted,
        report.advance_events,
        report.rollback_advance_events,
        report.final_checksum,
        report.final_checksum_expected ? 1 : 0);
    return 0;
}
