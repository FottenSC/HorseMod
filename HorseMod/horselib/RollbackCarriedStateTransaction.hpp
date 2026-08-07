// ============================================================================
// Horse::RollbackCarriedStateTransaction
//
// Frozen-boundary carried-state transaction with preflight-before-mutation
// and verified recovery of the complete original state.
// ============================================================================

#pragma once

#include <cstdint>

namespace Horse
{
    enum RollbackCarriedStatePreflightFailure : uint32_t
    {
        RollbackCarriedStatePreflightSecondary = 1u << 0,
        RollbackCarriedStatePreflightMotion = 1u << 1,
        RollbackCarriedStatePreflightWindCapture = 1u << 2,
        RollbackCarriedStatePreflightWindTopology = 1u << 3,
        RollbackCarriedStatePreflightWindFighterOutputs = 1u << 4,
        RollbackCarriedStatePreflightWindCanonical = 1u << 5,
        RollbackCarriedStatePreflightWindIntegrity = 1u << 6,
    };

    static inline uint32_t RollbackCarriedStatePreflightFailureMask(
        bool secondary_ready, bool motion_ready, bool wind_capture_ready,
        bool wind_topology_ready, bool wind_fighter_outputs_ready,
        bool wind_canonical_ready, bool wind_integrity_ready) noexcept
    {
        uint32_t mask = 0;
        if (!secondary_ready)
            mask |= RollbackCarriedStatePreflightSecondary;
        if (!motion_ready)
            mask |= RollbackCarriedStatePreflightMotion;
        if (!wind_capture_ready)
            mask |= RollbackCarriedStatePreflightWindCapture;
        if (!wind_topology_ready)
            mask |= RollbackCarriedStatePreflightWindTopology;
        if (!wind_fighter_outputs_ready)
            mask |= RollbackCarriedStatePreflightWindFighterOutputs;
        if (!wind_canonical_ready)
            mask |= RollbackCarriedStatePreflightWindCanonical;
        if (!wind_integrity_ready)
            mask |= RollbackCarriedStatePreflightWindIntegrity;
        return mask;
    }

    static inline bool RollbackCarriedStateIncludesSecondaryHistory(
        uint64_t round_generation) noexcept
    {
        return round_generation > 1;
    }

    template <typename... ComponentPreflights>
    static inline bool RollbackPreflightCarriedStateComponents(
        ComponentPreflights&&... component_preflights) noexcept
    {
        bool ready = true;
        ((ready = static_cast<bool>(component_preflights()) && ready), ...);
        return ready;
    }

    enum class RollbackCarriedStateTransactionResult : uint8_t
    {
        Applied,
        RejectedBeforeMutation,
        FailedRecovered,
        FailedUnrecoverable,
    };

    template <typename State, typename Preflight, typename Restore,
        typename ValidateState>
    static inline RollbackCarriedStateTransactionResult
    RollbackExecuteCarriedStateTransaction(
        const State& authorized, const State& original,
        Preflight&& preflight, Restore&& restore,
        ValidateState&& validate_state) noexcept
    {
        if (!preflight(authorized))
            return RollbackCarriedStateTransactionResult::
                RejectedBeforeMutation;
        if (restore(authorized) && validate_state(authorized))
            return RollbackCarriedStateTransactionResult::Applied;
        if (!preflight(original))
            return RollbackCarriedStateTransactionResult::FailedUnrecoverable;
        return restore(original) && validate_state(original)
            ? RollbackCarriedStateTransactionResult::FailedRecovered
            : RollbackCarriedStateTransactionResult::FailedUnrecoverable;
    }

    template <typename State, typename Preflight, typename Restore,
        typename ValidateState>
    static inline bool RollbackRestoreAndVerifyCarriedState(
        const State& expected, Preflight&& preflight, Restore&& restore,
        ValidateState&& validate_state) noexcept
    {
        return preflight(expected) && restore(expected)
            && validate_state(expected);
    }
}
