// ============================================================================
// Horse::RollbackStockTransportSurface
//
// Ghidra-backed guardrail for native SC6 online transport send surfaces. This
// documents the stock channels/slots we have identified and keeps HRG1 traffic
// on the Horse-owned adapter path until a live hook is explicitly validated.
// ============================================================================

#pragma once

#include "RollbackGekkoTransportBridge.hpp"

#include <cstddef>
#include <cstdint>

namespace Horse
{
    static constexpr uint32_t kLuxOnlineTransportSendBattleSyncSlot = 0x18u;
    static constexpr uint32_t kLuxOnlineTransportSendInputSlot = 0x20u;
    static constexpr uint32_t kLuxOnlineTransportSendBattleSyncTypedSlot =
        0x28u;

    static constexpr uint32_t kLuxOnlineChannelInputBinary = 5u;
    static constexpr uint32_t kLuxOnlineChannelBattleSync = 6u;
    static constexpr uint32_t kLuxOnlineChannelHighLevelKvMirror = 7u;

    static constexpr size_t kLuxSharedTransportSessionPtrSize = 16u;
    static constexpr size_t kLuxSharedTransportSessionPtrSessionOffset = 0u;
    static constexpr size_t kLuxSharedTransportSessionPtrControllerOffset = 8u;
    static constexpr uint64_t kRollbackHorseAdapterRouteCookie =
        0x4852473141445054ull; // "HRG1ADPT"

    enum class RollbackStockTransportPath : uint8_t
    {
        Unknown,
        StockInputBinary,
        BattleSyncKv,
        HighLevelKvMirror,
        HorseOwnedAdapter,
    };

    enum class RollbackStockTransportDecision : uint8_t
    {
        RejectUnknownStockPath,
        RejectStockInputHrg1,
        RejectBattleSyncHrg1,
        RejectMissingHorseAdapterProvenance,
        RejectMissingStrictIdentity,
        AllowStockNativePayload,
        AllowHorseOwnedHrg1,
    };

    struct RollbackStockTransportRoute
    {
        uint32_t vtable_slot {0};
        uint32_t channel {0};
        bool horse_owned_adapter {false};
        bool payload_is_hrg1 {false};
        bool source_peer_bound {false};
        bool destination_peer_bound {false};
        bool session_id_bound {false};
        uint8_t source_peer {0};
        uint8_t destination_peer {0};
        uint64_t session_id {0};
        uint64_t horse_adapter_cookie {0};
    };

    struct RollbackStockTransportSurfaceSelfTestReport
    {
        bool ok {false};
        bool shared_ptr_layout_ok {false};
        bool transport_slots_documented {false};
        bool stock_channels_documented {false};
        bool input_slot_rejects_hrg1 {false};
        bool battle_sync_rejects_hrg1 {false};
        bool high_level_kv_rejects_hrg1 {false};
        bool unknown_stock_path_rejected {false};
        bool adapter_provenance_required {false};
        bool strict_identity_required {false};
        bool strict_identity_values_required {false};
        bool horse_adapter_allows_hrg1 {false};
        bool stock_native_payloads_preserved {false};
        bool stock_paths_do_not_allow_hrg1 {false};
        bool adapter_flag_cannot_override_stock {false};
        bool bridge_v2_identity_required {false};
        const char* failure {"not-run"};
    };

    static inline bool RollbackStockTransportHasStrictIdentity(
        const RollbackStockTransportRoute& route) noexcept
    {
        return route.source_peer_bound
            && route.destination_peer_bound
            && route.session_id_bound
            && route.source_peer != 0
            && route.destination_peer != 0
            && route.source_peer != route.destination_peer
            && route.session_id != 0;
    }

    static inline bool RollbackStockTransportHasHorseAdapterProvenance(
        const RollbackStockTransportRoute& route) noexcept
    {
        return route.horse_owned_adapter
            && route.horse_adapter_cookie
                == kRollbackHorseAdapterRouteCookie;
    }

    static inline RollbackStockTransportPath ClassifyRollbackStockTransportPath(
        const RollbackStockTransportRoute& route) noexcept
    {
        if (route.vtable_slot == kLuxOnlineTransportSendInputSlot
            && route.channel == kLuxOnlineChannelInputBinary)
            return RollbackStockTransportPath::StockInputBinary;

        if ((route.vtable_slot == kLuxOnlineTransportSendBattleSyncSlot
             || route.vtable_slot == kLuxOnlineTransportSendBattleSyncTypedSlot)
            && route.channel == kLuxOnlineChannelBattleSync)
            return RollbackStockTransportPath::BattleSyncKv;

        if (route.channel == kLuxOnlineChannelHighLevelKvMirror)
            return RollbackStockTransportPath::HighLevelKvMirror;

        if (route.horse_owned_adapter)
            return RollbackStockTransportPath::HorseOwnedAdapter;

        return RollbackStockTransportPath::Unknown;
    }

    static inline RollbackStockTransportDecision
    DecideRollbackStockTransportSurface(
        const RollbackStockTransportRoute& route) noexcept
    {
        const RollbackStockTransportPath path =
            ClassifyRollbackStockTransportPath(route);

        if (path == RollbackStockTransportPath::HorseOwnedAdapter)
        {
            if (!RollbackStockTransportHasHorseAdapterProvenance(route))
            {
                return RollbackStockTransportDecision::
                    RejectMissingHorseAdapterProvenance;
            }
            if (!route.payload_is_hrg1)
                return RollbackStockTransportDecision::AllowStockNativePayload;
            if (!RollbackStockTransportHasStrictIdentity(route))
            {
                return RollbackStockTransportDecision::
                    RejectMissingStrictIdentity;
            }
            return RollbackStockTransportDecision::AllowHorseOwnedHrg1;
        }

        if (path == RollbackStockTransportPath::Unknown)
            return RollbackStockTransportDecision::RejectUnknownStockPath;

        if (!route.payload_is_hrg1)
            return RollbackStockTransportDecision::AllowStockNativePayload;

        if (path == RollbackStockTransportPath::StockInputBinary)
            return RollbackStockTransportDecision::RejectStockInputHrg1;

        return RollbackStockTransportDecision::RejectBattleSyncHrg1;
    }

    static inline RollbackStockTransportSurfaceSelfTestReport
    RunRollbackStockTransportSurfaceSelfTest() noexcept
    {
        RollbackStockTransportSurfaceSelfTestReport report {};

        report.shared_ptr_layout_ok =
            kLuxSharedTransportSessionPtrSize == 16u
            && kLuxSharedTransportSessionPtrSessionOffset == 0u
            && kLuxSharedTransportSessionPtrControllerOffset == 8u;
        report.transport_slots_documented =
            kLuxOnlineTransportSendBattleSyncSlot == 0x18u
            && kLuxOnlineTransportSendInputSlot == 0x20u
            && kLuxOnlineTransportSendBattleSyncTypedSlot == 0x28u;
        report.stock_channels_documented =
            kLuxOnlineChannelInputBinary == 5u
            && kLuxOnlineChannelBattleSync == 6u
            && kLuxOnlineChannelHighLevelKvMirror == 7u;

        const RollbackStockTransportRoute stock_input {
            kLuxOnlineTransportSendInputSlot,
            kLuxOnlineChannelInputBinary,
            false,
            true,
            true,
            true,
            true};
        report.input_slot_rejects_hrg1 =
            DecideRollbackStockTransportSurface(stock_input)
            == RollbackStockTransportDecision::RejectStockInputHrg1;

        const RollbackStockTransportRoute battle_sync {
            kLuxOnlineTransportSendBattleSyncSlot,
            kLuxOnlineChannelBattleSync,
            false,
            true,
            true,
            true,
            true};
        const RollbackStockTransportRoute battle_sync_typed {
            kLuxOnlineTransportSendBattleSyncTypedSlot,
            kLuxOnlineChannelBattleSync,
            false,
            true,
            true,
            true,
            true};
        report.battle_sync_rejects_hrg1 =
            DecideRollbackStockTransportSurface(battle_sync)
                == RollbackStockTransportDecision::RejectBattleSyncHrg1
            && DecideRollbackStockTransportSurface(battle_sync_typed)
                == RollbackStockTransportDecision::RejectBattleSyncHrg1;

        const RollbackStockTransportRoute high_level_kv {
            kLuxOnlineTransportSendBattleSyncTypedSlot,
            kLuxOnlineChannelHighLevelKvMirror,
            false,
            true,
            true,
            true,
            true};
        report.high_level_kv_rejects_hrg1 =
            DecideRollbackStockTransportSurface(high_level_kv)
            == RollbackStockTransportDecision::RejectBattleSyncHrg1;

        const RollbackStockTransportRoute unknown_stock {
            0x30u,
            9u,
            false,
            true,
            true,
            true,
            true};
        report.unknown_stock_path_rejected =
            DecideRollbackStockTransportSurface(unknown_stock)
            == RollbackStockTransportDecision::RejectUnknownStockPath;

        const RollbackStockTransportRoute horse_missing_provenance {
            0u,
            0u,
            true,
            true,
            true,
            true,
            true,
            0xA0u,
            0xB0u,
            0x4C495645414354ull};
        report.adapter_provenance_required =
            DecideRollbackStockTransportSurface(horse_missing_provenance)
            == RollbackStockTransportDecision::
                RejectMissingHorseAdapterProvenance;

        const RollbackStockTransportRoute horse_missing_identity {
            0u,
            0u,
            true,
            true,
            true,
            true,
            false,
            0xA0u,
            0xB0u,
            0u,
            kRollbackHorseAdapterRouteCookie};
        report.strict_identity_required =
            DecideRollbackStockTransportSurface(horse_missing_identity)
            == RollbackStockTransportDecision::RejectMissingStrictIdentity;

        const RollbackStockTransportRoute horse_missing_identity_values {
            0u,
            0u,
            true,
            true,
            true,
            true,
            true,
            0u,
            0u,
            0u,
            kRollbackHorseAdapterRouteCookie};
        report.strict_identity_values_required =
            DecideRollbackStockTransportSurface(horse_missing_identity_values)
            == RollbackStockTransportDecision::RejectMissingStrictIdentity;

        const RollbackStockTransportRoute horse_hrg1 {
            0u,
            0u,
            true,
            true,
            true,
            true,
            true,
            0xA0u,
            0xB0u,
            0x4C495645414354ull,
            kRollbackHorseAdapterRouteCookie};
        report.horse_adapter_allows_hrg1 =
            DecideRollbackStockTransportSurface(horse_hrg1)
            == RollbackStockTransportDecision::AllowHorseOwnedHrg1;

        RollbackStockTransportRoute native_input = stock_input;
        native_input.payload_is_hrg1 = false;
        RollbackStockTransportRoute native_battle_sync = battle_sync;
        native_battle_sync.payload_is_hrg1 = false;
        report.stock_native_payloads_preserved =
            DecideRollbackStockTransportSurface(native_input)
                == RollbackStockTransportDecision::AllowStockNativePayload
            && DecideRollbackStockTransportSurface(native_battle_sync)
                == RollbackStockTransportDecision::AllowStockNativePayload;

        report.stock_paths_do_not_allow_hrg1 =
            report.input_slot_rejects_hrg1
            && report.battle_sync_rejects_hrg1
            && report.high_level_kv_rejects_hrg1
            && report.unknown_stock_path_rejected;

        RollbackStockTransportRoute flagged_stock_input = stock_input;
        flagged_stock_input.horse_owned_adapter = true;
        flagged_stock_input.horse_adapter_cookie =
            kRollbackHorseAdapterRouteCookie;
        RollbackStockTransportRoute flagged_battle_sync = battle_sync;
        flagged_battle_sync.horse_owned_adapter = true;
        flagged_battle_sync.horse_adapter_cookie =
            kRollbackHorseAdapterRouteCookie;
        RollbackStockTransportRoute flagged_battle_sync_typed =
            battle_sync_typed;
        flagged_battle_sync_typed.horse_owned_adapter = true;
        flagged_battle_sync_typed.horse_adapter_cookie =
            kRollbackHorseAdapterRouteCookie;
        RollbackStockTransportRoute flagged_high_level_kv = high_level_kv;
        flagged_high_level_kv.horse_owned_adapter = true;
        flagged_high_level_kv.horse_adapter_cookie =
            kRollbackHorseAdapterRouteCookie;
        report.adapter_flag_cannot_override_stock =
            DecideRollbackStockTransportSurface(flagged_stock_input)
                == RollbackStockTransportDecision::RejectStockInputHrg1
            && DecideRollbackStockTransportSurface(flagged_battle_sync)
                == RollbackStockTransportDecision::RejectBattleSyncHrg1
            && DecideRollbackStockTransportSurface(flagged_battle_sync_typed)
                == RollbackStockTransportDecision::RejectBattleSyncHrg1
            && DecideRollbackStockTransportSurface(flagged_high_level_kv)
                == RollbackStockTransportDecision::RejectBattleSyncHrg1;

        report.bridge_v2_identity_required =
            kRollbackGekkoBridgeVersion == 2
            && kRollbackGekkoBridgeHeaderBytes >= 88u;

        report.ok =
            report.shared_ptr_layout_ok
            && report.transport_slots_documented
            && report.stock_channels_documented
            && report.input_slot_rejects_hrg1
            && report.battle_sync_rejects_hrg1
            && report.high_level_kv_rejects_hrg1
            && report.unknown_stock_path_rejected
            && report.adapter_provenance_required
            && report.strict_identity_required
            && report.strict_identity_values_required
            && report.horse_adapter_allows_hrg1
            && report.stock_native_payloads_preserved
            && report.stock_paths_do_not_allow_hrg1
            && report.adapter_flag_cannot_override_stock
            && report.bridge_v2_identity_required;
        report.failure = report.ok
            ? "ok"
            : "stock-transport-surface-selftest-failed";
        return report;
    }
}
