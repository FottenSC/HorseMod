#include "../HorseMod/horselib/RollbackStockTransportSurface.hpp"

#include <cstdio>

int main()
{
    const Horse::RollbackStockTransportSurfaceSelfTestReport report =
        Horse::RunRollbackStockTransportSurfaceSelfTest();
    if (!report.ok)
    {
        std::printf(
            "rollback stock-transport self-test failed failure=%s "
            "shared_ptr=%d slots=%d channels=%d input_reject=%d "
            "battle_reject=%d kv_reject=%d unknown_reject=%d "
            "provenance=%d identity=%d identity_values=%d horse_allow=%d "
            "stock_native=%d stock_no_hrg1=%d flag_override=%d "
            "bridge_v2=%d\n",
            report.failure ? report.failure : "?",
            report.shared_ptr_layout_ok ? 1 : 0,
            report.transport_slots_documented ? 1 : 0,
            report.stock_channels_documented ? 1 : 0,
            report.input_slot_rejects_hrg1 ? 1 : 0,
            report.battle_sync_rejects_hrg1 ? 1 : 0,
            report.high_level_kv_rejects_hrg1 ? 1 : 0,
            report.unknown_stock_path_rejected ? 1 : 0,
            report.adapter_provenance_required ? 1 : 0,
            report.strict_identity_required ? 1 : 0,
            report.strict_identity_values_required ? 1 : 0,
            report.horse_adapter_allows_hrg1 ? 1 : 0,
            report.stock_native_payloads_preserved ? 1 : 0,
            report.stock_paths_do_not_allow_hrg1 ? 1 : 0,
            report.adapter_flag_cannot_override_stock ? 1 : 0,
            report.bridge_v2_identity_required ? 1 : 0);
        return 1;
    }

    std::printf(
        "rollback stock-transport self-test passed failure=%s "
        "shared_ptr=%d slots=%d channels=%d input_reject=%d "
        "battle_reject=%d kv_reject=%d unknown_reject=%d "
        "provenance=%d identity=%d identity_values=%d horse_allow=%d "
        "stock_native=%d stock_no_hrg1=%d flag_override=%d bridge_v2=%d\n",
        report.failure ? report.failure : "?",
        report.shared_ptr_layout_ok ? 1 : 0,
        report.transport_slots_documented ? 1 : 0,
        report.stock_channels_documented ? 1 : 0,
        report.input_slot_rejects_hrg1 ? 1 : 0,
        report.battle_sync_rejects_hrg1 ? 1 : 0,
        report.high_level_kv_rejects_hrg1 ? 1 : 0,
        report.unknown_stock_path_rejected ? 1 : 0,
        report.adapter_provenance_required ? 1 : 0,
        report.strict_identity_required ? 1 : 0,
        report.strict_identity_values_required ? 1 : 0,
        report.horse_adapter_allows_hrg1 ? 1 : 0,
        report.stock_native_payloads_preserved ? 1 : 0,
        report.stock_paths_do_not_allow_hrg1 ? 1 : 0,
        report.adapter_flag_cannot_override_stock ? 1 : 0,
        report.bridge_v2_identity_required ? 1 : 0);
    return 0;
}
