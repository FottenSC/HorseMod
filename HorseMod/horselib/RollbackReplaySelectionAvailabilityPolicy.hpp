// ============================================================================
// Horse::RollbackReplaySelectionAvailabilityPolicy
//
// Pure policy for the replay-corpus-only stock selection availability hook.
// This does not grant content ownership and is never used by beta/production
// configuration.  It only admits the exact replay-authored character/stage
// tuple after the two peers have accepted the same rollback session contract.
// ============================================================================

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace Horse
{
    enum class RollbackReplaySelectionAvailabilityDecision : uint8_t
    {
        Disabled = 0,
        InvalidScope,
        SelectionMismatch,
        AwaitingPeerContract,
        Active,
        SetupComplete,
    };

    struct RollbackReplaySelectionAvailabilityPolicyInput
    {
        bool requested {false};
        bool stock_online_local_lab {false};
        bool replay_input_enabled {false};
        bool bind_observed_stock_selection {false};
        bool peer_contract_ready {false};
        bool setup_complete {false};
        uint64_t expected_selection_hash {0};
        uint64_t requested_selection_hash {0};
        std::string left_character_code;
        std::string right_character_code;
        std::string stage_code;
    };

    inline bool RollbackReplaySelectionCharacterCodeValid(
        const std::string& code) noexcept
    {
        if (code.size() != 3) return false;
        for (const unsigned char c : code)
        {
            const bool decimal = c >= '0' && c <= '9';
            const bool uppercase = c >= 'A' && c <= 'Z';
            if (!decimal && !uppercase) return false;
        }
        return true;
    }

    inline bool RollbackReplaySelectionStageCodeValid(
        const std::string& code) noexcept
    {
        if (code.size() < 6 || code.size() > 15
            || code.compare(0, 3, "STG") != 0)
            return false;
        for (size_t i = 3; i < code.size(); ++i)
        {
            const unsigned char c =
                static_cast<unsigned char>(code[i]);
            const bool decimal = c >= '0' && c <= '9';
            const bool uppercase = c >= 'A' && c <= 'Z';
            if (!decimal && !uppercase && c != '_') return false;
        }
        return true;
    }

    inline RollbackReplaySelectionAvailabilityDecision
    EvaluateRollbackReplaySelectionAvailability(
        const RollbackReplaySelectionAvailabilityPolicyInput& input) noexcept
    {
        if (!input.requested)
            return RollbackReplaySelectionAvailabilityDecision::Disabled;
        if (!input.stock_online_local_lab
            || !input.replay_input_enabled
            || input.bind_observed_stock_selection
            || input.expected_selection_hash == 0
            || !RollbackReplaySelectionCharacterCodeValid(
                input.left_character_code)
            || !RollbackReplaySelectionCharacterCodeValid(
                input.right_character_code)
            || !RollbackReplaySelectionStageCodeValid(input.stage_code))
        {
            return RollbackReplaySelectionAvailabilityDecision::InvalidScope;
        }
        if (input.requested_selection_hash == 0
            || input.requested_selection_hash
                != input.expected_selection_hash)
        {
            return RollbackReplaySelectionAvailabilityDecision::
                SelectionMismatch;
        }
        if (input.setup_complete)
            return RollbackReplaySelectionAvailabilityDecision::SetupComplete;
        if (!input.peer_contract_ready)
        {
            return RollbackReplaySelectionAvailabilityDecision::
                AwaitingPeerContract;
        }
        return RollbackReplaySelectionAvailabilityDecision::Active;
    }

    inline const char* RollbackReplaySelectionAvailabilityDecisionName(
        RollbackReplaySelectionAvailabilityDecision decision) noexcept
    {
        switch (decision)
        {
        case RollbackReplaySelectionAvailabilityDecision::Disabled:
            return "disabled";
        case RollbackReplaySelectionAvailabilityDecision::InvalidScope:
            return "invalid-scope";
        case RollbackReplaySelectionAvailabilityDecision::SelectionMismatch:
            return "selection-mismatch";
        case RollbackReplaySelectionAvailabilityDecision::
                AwaitingPeerContract:
            return "awaiting-peer-contract";
        case RollbackReplaySelectionAvailabilityDecision::Active:
            return "active";
        case RollbackReplaySelectionAvailabilityDecision::SetupComplete:
            return "setup-complete";
        }
        return "unknown";
    }
}
