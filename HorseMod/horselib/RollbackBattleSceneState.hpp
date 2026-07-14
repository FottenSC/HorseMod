#pragma once

#include <cstdint>

namespace Horse
{
    enum class RollbackBattleSceneTransitionState : uint8_t
    {
        WaitingForLauncher,
        WaitingForResources,
        RequestNativeChange,
        WaitingForNativeTransition,
        BattleActive,
        Fatal,
    };

    struct RollbackBattleSceneTransitionInput
    {
        bool battle_active {false};
        bool transition_queued {false};
        bool setup_active {false};
        bool stock_phase_owned {false};
        bool stock_phase_requested {false};
        bool stock_phase_ok {false};
        bool launcher_requested {false};
        bool launcher_ok {false};
        bool resources_loaded_query_ok {false};
        bool resources_loaded {false};
        bool request_attempted {false};
        bool request_ok {false};
    };

    struct RollbackBattleSceneTransitionDecision
    {
        RollbackBattleSceneTransitionState state {
            RollbackBattleSceneTransitionState::Fatal};
        bool call_request_change_scene {false};
        bool call_stock_phase {false};
        bool waiting {false};
        bool battle_ready {false};
        bool fatal {false};
        const char* reason {"invalid-transition-state"};
    };

    static inline RollbackBattleSceneTransitionDecision
    EvaluateRollbackBattleSceneTransition(
        const RollbackBattleSceneTransitionInput& in) noexcept
    {
        if (in.battle_active)
            return {RollbackBattleSceneTransitionState::BattleActive,
                    false, false, false, true, false, "battle-active"};
        if (in.transition_queued)
            return {
                RollbackBattleSceneTransitionState::WaitingForNativeTransition,
                false, false, true, false, false,
                "native-transition-queued"};
        if (!in.setup_active)
            return {RollbackBattleSceneTransitionState::Fatal,
                    false, false, false, false, true,
                    "unexpected-scene-before-battle"};
        // Mirrored two-client acceptance must hand control to the stock
        // BattleSetup phase machine.  VersusInfoInit owns OnStartVersusInfo;
        // VersusInfoExec owns the animation/resource gates; MainOut alone
        // owns RequestChangeScene.  A successful ProcessEvent call to
        // RequestChangeScene is not proof that GameFlow accepted it.
        if (in.stock_phase_owned)
        {
            if (!in.stock_phase_requested)
                return {
                    RollbackBattleSceneTransitionState::WaitingForLauncher,
                    false, true, false, false, false,
                    "request-stock-versus-info-phase"};
            if (!in.stock_phase_ok)
                return {RollbackBattleSceneTransitionState::Fatal,
                        false, false, false, false, true,
                        "stock-versus-info-phase-failed"};
            return {
                RollbackBattleSceneTransitionState::WaitingForNativeTransition,
                false, false, true, false, false,
                "waiting-for-stock-battle-setup-phases"};
        }
        if (!in.launcher_requested)
            return {RollbackBattleSceneTransitionState::WaitingForLauncher,
                    false, false, true, false, false,
                    "waiting-for-launcher"};
        if (!in.launcher_ok)
            return {RollbackBattleSceneTransitionState::Fatal,
                    false, false, false, false, true,
                    "launcher-start-failed"};
        if (!in.resources_loaded_query_ok)
            return {RollbackBattleSceneTransitionState::Fatal,
                    false, false, false, false, true,
                    "loaded-resource-query-failed"};
        if (!in.resources_loaded)
            return {RollbackBattleSceneTransitionState::WaitingForResources,
                    false, false, true, false, false,
                    "waiting-for-loaded-resource"};
        if (!in.request_attempted)
            return {RollbackBattleSceneTransitionState::RequestNativeChange,
                    true, false, false, false, false,
                    "request-native-change-scene"};
        if (!in.request_ok)
            return {RollbackBattleSceneTransitionState::Fatal,
                    false, false, false, false, true,
                    "request-change-scene-failed"};
        return {
            RollbackBattleSceneTransitionState::WaitingForNativeTransition,
            false, false, true, false, false,
            "waiting-for-native-transition"};
    }
}
