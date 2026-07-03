// ============================================================================
// Horse::RollbackCacheInjection
//
// Runtime report for the lab-only stock InputLog cache write/read probe. The
// first runtime probe is intentionally idempotent. The prediction probe writes
// a different cache value, verifies the stock consumer output, then restores
// the cache/current-input output state before returning to the game.
// ============================================================================

#pragma once

#include <cstdint>

namespace Horse
{
    struct FLuxBattleInputPair_Model
    {
        uint32_t dwInputWord {0};
        uint32_t dwFlags {0};
    };
    static_assert(sizeof(FLuxBattleInputPair_Model) == 8);

    struct RollbackCacheInjectionReport
    {
        bool ok {false};
        bool hooks_installed {false};
        bool probe_active {false};
        bool attempted {false};
        bool context_ready {false};
        bool source_cell_valid {false};
        bool wrote_cache {false};
        bool consumer_observed_cache {false};
        bool restored_cache {false};
        bool restored_current_input {false};
        bool restored_output_pair {false};
        bool idempotent_write {false};
        bool non_idempotent_write {false};
        bool injected_differs_from_original {false};
        bool output_pair_observed_prediction {false};
        bool network_event_mask_inferred {false};
        uint32_t invalid_context_count {0};
        uint32_t dwPlayerIndex {0};
        uint32_t dwMasterClock {0};
        uint32_t dwForbiddenInputMask {0};
        int32_t nFramesBack {0};
        int32_t nFrameIndex {0};
        int32_t nFrameID {0};
        uint32_t dwOriginalInput {0};
        uint32_t dwInjectedInput {0};
        uint32_t dwObservedCurrentInput {0};
        uint32_t dwRestoredCurrentInput {0};
        FLuxBattleInputPair_Model observed_output_pair {};
        FLuxBattleInputPair_Model expected_injected_output_pair {};
        FLuxBattleInputPair_Model expected_restored_output_pair {};
        FLuxBattleInputPair_Model restored_output_pair_value {};
        uintptr_t pBattleManager {0};
        uintptr_t pInputLog {0};
        uintptr_t pCacheEntry {0};
        uintptr_t pCurrentInputSlot {0};
        uintptr_t pInputPairSlot {0};
        const char* failure {"not-run"};
    };
}
