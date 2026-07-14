#include "RollbackBattleSceneState.hpp"

#include <cassert>
#include <iostream>

using namespace Horse;

int main()
{
    RollbackBattleSceneTransitionInput in {};
    in.setup_active = true;

    auto decision = EvaluateRollbackBattleSceneTransition(in);
    assert(decision.state ==
           RollbackBattleSceneTransitionState::WaitingForLauncher);
    assert(decision.waiting && !decision.call_request_change_scene);

    in.launcher_requested = true;
    decision = EvaluateRollbackBattleSceneTransition(in);
    assert(decision.fatal);

    in.launcher_ok = true;
    in.resources_loaded_query_ok = true;
    decision = EvaluateRollbackBattleSceneTransition(in);
    assert(decision.state ==
           RollbackBattleSceneTransitionState::WaitingForResources);

    in.resources_loaded = true;
    decision = EvaluateRollbackBattleSceneTransition(in);
    assert(decision.call_request_change_scene);
    assert(!decision.waiting && !decision.fatal);

    in.request_attempted = true;
    in.request_ok = true;
    decision = EvaluateRollbackBattleSceneTransition(in);
    assert(decision.state ==
           RollbackBattleSceneTransitionState::WaitingForNativeTransition);
    assert(!decision.call_request_change_scene);

    in.transition_queued = true;
    decision = EvaluateRollbackBattleSceneTransition(in);
    assert(decision.waiting && !decision.fatal);

    in.battle_active = true;
    decision = EvaluateRollbackBattleSceneTransition(in);
    assert(decision.battle_ready && !decision.waiting);

    in = {};
    in.setup_active = true;
    in.stock_phase_owned = true;
    decision = EvaluateRollbackBattleSceneTransition(in);
    assert(decision.call_stock_phase);
    assert(!decision.call_request_change_scene);
    assert(!decision.fatal);

    in.stock_phase_requested = true;
    in.stock_phase_ok = true;
    decision = EvaluateRollbackBattleSceneTransition(in);
    assert(decision.waiting);
    assert(!decision.call_request_change_scene);
    assert(!decision.call_stock_phase);

    in.stock_phase_ok = false;
    decision = EvaluateRollbackBattleSceneTransition(in);
    assert(decision.fatal);
    assert(!decision.call_request_change_scene);

    in = {};
    in.setup_active = true;
    in.launcher_requested = true;
    in.launcher_ok = true;
    decision = EvaluateRollbackBattleSceneTransition(in);
    assert(decision.fatal);

    in.resources_loaded_query_ok = true;
    in.resources_loaded = true;
    in.request_attempted = true;
    decision = EvaluateRollbackBattleSceneTransition(in);
    assert(decision.fatal);

    in = {};
    decision = EvaluateRollbackBattleSceneTransition(in);
    assert(decision.fatal);

    std::cout << "rollback_battle_scene_state_selftest: ok\n";
    return 0;
}
