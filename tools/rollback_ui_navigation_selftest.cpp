#include "../HorseMod/horselib/RollbackUiNavigationDriver.hpp"

#include <cstdio>

namespace
{
    using Driver = Horse::RollbackUiNavigationDriver;
    using Command = Horse::RollbackUiNavigationCommand;
    using Config = Horse::RollbackUiNavigationConfig;
    using Failure = Horse::RollbackUiNavigationFailure;
    using Key = Horse::RollbackUiNavigationKey;
    using Result = Horse::RollbackUiNavigationResult;
    using Scene = Horse::RollbackUiNavigationScene;
    using StabilityGate = Horse::RollbackUiNavigationStabilityGate;
    using State = Horse::RollbackUiNavigationState;
    using Target = Horse::RollbackUiNavigationTarget;

    constexpr Key kDownDecide[] {Key::Down, Key::Decide};
    constexpr Key kResetRoute[] {Key::Up, Key::Down, Key::Decide};
    constexpr Key kDecideOnly[] {Key::Decide};
    constexpr Key kPlayerMatchHierarchyRoute[] {
        Key::Down, Key::Down, Key::Down, Key::Down,
        Key::Decide, Key::Down, Key::Decide};

    bool unavailable_dispatcher_does_not_complete()
    {
        Driver driver(Config {3, 20, 2});
        Command command {};
        return driver.begin(1, kDownDecide, 2, 100, Scene::MainMenu)
            && !driver.next_command(
                1, 100, Scene::MainMenu, false, command)
            && !driver.next_command(
                1, 101, Scene::MainMenu, false, command)
            && driver.state() == State::ReadyToDispatch
            && driver.step_index() == 0
            && driver.total_dispatches() == 0
            && !driver.sequence_complete()
            && driver.failure() == Failure::None;
    }

    bool dispatch_requires_action_acknowledgement()
    {
        Driver driver(Config {3, 20, 2});
        Command command {};
        if (!driver.begin(2, kDownDecide, 2, 0, Scene::MainMenu)
            || !driver.next_command(
                2, 0, Scene::MainMenu, true, command))
        {
            return false;
        }

        const bool dispatch_only = command.valid
            && command.step_index == 0
            && command.key == Key::Down
            && driver.state() == State::DispatchPending
            && driver.step_index() == 0
            && driver.on_dispatch_result(
                2, command.dispatch_id, 0, true)
                == Result::DispatchAccepted
            && driver.state() == State::AwaitingAcknowledgement
            && driver.step_index() == 0;
        const bool negative_ack_waits = driver.acknowledge(
            2, command.dispatch_id, 1, Scene::MainMenu, false)
                == Result::WaitingForAction
            && driver.step_index() == 0;
        const bool observed_ack_advances = driver.acknowledge(
            2, command.dispatch_id, 1, Scene::MainMenu, true)
                == Result::ActionAcknowledged
            && driver.state() == State::ReadyToDispatch
            && driver.step_index() == 1;
        return dispatch_only && negative_ack_waits
            && observed_ack_advances;
    }

    bool dropped_input_retries_same_step()
    {
        Driver driver(Config {2, 30, 3});
        Command first {};
        Command retry {};
        Command early {};
        if (!driver.begin(3, kDownDecide, 2, 0, Scene::MainMenu)
            || !driver.next_command(3, 0, Scene::MainMenu, true, first)
            || driver.on_dispatch_result(
                3, first.dispatch_id, 0, true)
                != Result::DispatchAccepted)
        {
            return false;
        }

        const bool waits_for_timeout = !driver.next_command(
            3, 1, Scene::MainMenu, true, early);
        const bool retried = driver.next_command(
                3, 2, Scene::MainMenu, true, retry)
            && retry.dispatch_id != first.dispatch_id
            && retry.step_index == first.step_index
            && retry.key == first.key
            && driver.attempts_for_step() == 2;
        const bool old_attempt_rejected = driver.acknowledge(
            3, first.dispatch_id, 2, Scene::MainMenu, true)
                == Result::IgnoredStaleDispatch
            && driver.step_index() == 0;
        const bool retry_acknowledged = driver.on_dispatch_result(
                3, retry.dispatch_id, 2, true)
                == Result::DispatchAccepted
            && driver.acknowledge(
                3, retry.dispatch_id, 3, Scene::MainMenu, true)
                == Result::ActionAcknowledged
            && driver.step_index() == 1;
        return waits_for_timeout && retried && old_attempt_rejected
            && retry_acknowledged;
    }

    bool stale_generation_and_dispatch_are_ignored()
    {
        Driver driver(Config {3, 30, 2});
        Command command {};
        if (!driver.begin(10, kDownDecide, 2, 0, Scene::MainMenu)
            || !driver.next_command(
                10, 0, Scene::MainMenu, true, command)
            || driver.on_dispatch_result(
                10, command.dispatch_id, 0, true)
                != Result::DispatchAccepted)
        {
            return false;
        }

        const bool stale_generation = driver.acknowledge(
            9, command.dispatch_id, 1, Scene::MainMenu, true)
                == Result::IgnoredStaleGeneration;
        const bool stale_dispatch = driver.acknowledge(
            10, command.dispatch_id + 1, 1, Scene::MainMenu, true)
                == Result::IgnoredStaleDispatch;
        return stale_generation && stale_dispatch
            && driver.state() == State::AwaitingAcknowledgement
            && driver.step_index() == 0
            && driver.pending_dispatch_id() == command.dispatch_id;
    }

    bool scene_generation_reset_restarts_route()
    {
        Driver driver(Config {3, 30, 3});
        Command first {};
        Command second {};
        Command restarted {};
        if (!driver.begin(20, kResetRoute, 3, 0, Scene::MainMenu)
            || !driver.next_command(
                20, 0, Scene::MainMenu, true, first)
            || driver.on_dispatch_result(
                20, first.dispatch_id, 0, true)
                != Result::DispatchAccepted
            || driver.acknowledge(
                20, first.dispatch_id, 1, Scene::MainMenu, true)
                != Result::ActionAcknowledged
            || !driver.next_command(
                20, 2, Scene::MainMenu, true, second)
            || driver.on_dispatch_result(
                20, second.dispatch_id, 2, true)
                != Result::DispatchAccepted)
        {
            return false;
        }

        const bool reset = driver.observe_scene(
                21, 3, Scene::MainMenu) == Result::Restarted
            && driver.generation() == 21
            && driver.step_index() == 0
            && driver.attempts_for_step() == 0
            && driver.pending_dispatch_id() == 0
            && driver.started_tick() == 0;
        const bool old_ack_ignored = driver.acknowledge(
            20, second.dispatch_id, 3, Scene::MainMenu, true)
                == Result::IgnoredStaleGeneration;
        const bool starts_from_first_step = driver.next_command(
                21, 4, Scene::MainMenu, true, restarted)
            && restarted.generation == 21
            && restarted.dispatch_id != second.dispatch_id
            && restarted.step_index == 0
            && restarted.key == Key::Up;
        return reset && old_ack_ignored && starts_from_first_step;
    }

    bool failures_are_bounded()
    {
        Driver deadline_driver(Config {2, 5, 2});
        Command ignored {};
        const bool deadline = deadline_driver.begin(
                30, kDownDecide, 2, 100, Scene::MainMenu)
            && !deadline_driver.next_command(
                30, 104, Scene::MainMenu, false, ignored)
            && deadline_driver.state() == State::ReadyToDispatch
            && !deadline_driver.next_command(
                30, 105, Scene::MainMenu, false, ignored)
            && deadline_driver.state() == State::Failed
            && deadline_driver.failure() == Failure::DeadlineExceeded;

        Driver attempts_driver(Config {2, 50, 2});
        Command first {};
        Command second {};
        Command exhausted {};
        const bool attempts = attempts_driver.begin(
                31, kDownDecide, 2, 0, Scene::MainMenu)
            && attempts_driver.next_command(
                31, 0, Scene::MainMenu, true, first)
            && attempts_driver.on_dispatch_result(
                31, first.dispatch_id, 0, true)
                == Result::DispatchAccepted
            && attempts_driver.next_command(
                31, 2, Scene::MainMenu, true, second)
            && attempts_driver.on_dispatch_result(
                31, second.dispatch_id, 2, true)
                == Result::DispatchAccepted
            && !attempts_driver.next_command(
                31, 4, Scene::MainMenu, true, exhausted)
            && attempts_driver.state() == State::Failed
            && attempts_driver.failure()
                == Failure::AttemptLimitExceeded;
        return deadline && attempts;
    }

    bool decide_requires_player_match_scene()
    {
        Driver driver(Config {5, 30, 2});
        Command decide {};
        if (!driver.begin(40, kDecideOnly, 1, 0, Scene::MainMenu)
            || !driver.next_command(
                40, 0, Scene::MainMenu, true, decide)
            || driver.on_dispatch_result(
                40, decide.dispatch_id, 0, true)
                != Result::DispatchAccepted)
        {
            return false;
        }

        const bool generic_ack_is_insufficient =
            driver.acknowledge_input_consumed(
                40, decide.dispatch_id, 1, Scene::MainMenu)
                == Result::WaitingForAction
            && driver.state() == State::AwaitingAcknowledgement
            && driver.step_index() == 0
            && !driver.pending_action_observed()
            && !driver.sequence_complete();
        const bool invite_is_not_a_destination =
            driver.observe_scene(40, 2, Scene::PlayerMatchInvite)
                == Result::None
            && driver.state() == State::AwaitingAcknowledgement
            && !driver.sequence_complete();
        const bool destination_precedes_generation_reset =
            driver.observe_scene(41, 3, Scene::PlayerMatchLobby)
                == Result::Succeeded
            && driver.generation() == 41
            && driver.state() == State::Succeeded
            && driver.sequence_complete();

        Driver acknowledgement_driver(Config {5, 30, 2});
        Command second_decide {};
        const bool destination_acknowledges =
            acknowledgement_driver.begin(
                42, kDecideOnly, 1, 0, Scene::MainMenu)
            && acknowledgement_driver.next_command(
                42, 0, Scene::MainMenu, true, second_decide)
            && acknowledgement_driver.on_dispatch_result(
                42, second_decide.dispatch_id, 0, true)
                == Result::DispatchAccepted
            && acknowledgement_driver.acknowledge(
                42, second_decide.dispatch_id, 1,
                Scene::PlayerMatchLobby, false)
                == Result::Succeeded
            && acknowledgement_driver.sequence_complete();
        return generic_ack_is_insufficient
            && invite_is_not_a_destination
            && destination_precedes_generation_reset
            && destination_acknowledges;
    }

    bool closed_loop_happy_path()
    {
        Driver driver(Config {5, 30, 2});
        Command down {};
        Command decide {};
        return driver.begin(50, kDownDecide, 2, 0, Scene::MainMenu)
            && driver.next_command(50, 0, Scene::MainMenu, true, down)
            && driver.on_dispatch_result(50, down.dispatch_id, 0, true)
                == Result::DispatchAccepted
            && driver.acknowledge(
                50, down.dispatch_id, 1, Scene::MainMenu, true)
                == Result::ActionAcknowledged
            && driver.next_command(50, 2, Scene::MainMenu, true, decide)
            && driver.on_dispatch_result(
                50, decide.dispatch_id, 2, true)
                == Result::DispatchAccepted
            && driver.acknowledge_target_scene_queued(
                50, decide.dispatch_id, 3, Scene::MainMenu)
                == Result::WaitingForTargetScene
            && driver.observe_scene(50, 4, Scene::PlayerMatchSetup)
                == Result::Succeeded
            && driver.sequence_complete();
    }

    bool intermediate_decide_enters_network_submenu()
    {
        Driver driver(Config {5, 60, 2});
        if (!driver.begin(
                55,
                kPlayerMatchHierarchyRoute,
                sizeof(kPlayerMatchHierarchyRoute)
                    / sizeof(kPlayerMatchHierarchyRoute[0]),
                0,
                Scene::MainMenu))
        {
            return false;
        }

        uint64_t tick = 0;
        for (uint8_t step = 0; step < 6; ++step)
        {
            Command command {};
            if (!driver.next_command(
                    55, tick, Scene::MainMenu, true, command)
                || command.step_index != step
                || driver.on_dispatch_result(
                       55, command.dispatch_id, tick, true)
                    != Result::DispatchAccepted
                || driver.acknowledge(
                       55, command.dispatch_id, ++tick,
                       Scene::MainMenu, true)
                    != Result::ActionAcknowledged)
            {
                return false;
            }
            ++tick;
        }

        Command final_decide {};
        return driver.next_command(
                   55, tick, Scene::MainMenu, true, final_decide)
            && final_decide.step_index == 6
            && final_decide.key == Key::Decide
            && driver.on_dispatch_result(
                   55, final_decide.dispatch_id, tick, true)
                == Result::DispatchAccepted
            && driver.acknowledge_target_scene_queued(
                   55, final_decide.dispatch_id, ++tick,
                   Scene::MainMenu)
                == Result::WaitingForTargetScene
            && driver.observe_scene(
                   56, ++tick, Scene::PlayerMatchLobby)
                == Result::Succeeded
            && driver.sequence_complete();
    }

    bool invite_generation_change_remains_bounded()
    {
        Driver driver(Config {5, 10, 2});
        Command decide {};
        return driver.begin(57, kDecideOnly, 1, 0, Scene::MainMenu)
            && driver.next_command(57, 0, Scene::MainMenu, true, decide)
            && driver.on_dispatch_result(
                   57, decide.dispatch_id, 0, true)
                == Result::DispatchAccepted
            && driver.acknowledge_input_consumed(
                   57, decide.dispatch_id, 1, Scene::MainMenu)
                == Result::WaitingForAction
            && driver.observe_scene(58, 2, Scene::PlayerMatchInvite)
                == Result::IgnoredStaleGeneration
            && driver.state() == State::AwaitingAcknowledgement
            && driver.observe_scene(58, 10, Scene::PlayerMatchInvite)
                == Result::Failed
            && driver.state() == State::Failed
            && driver.failure() == Failure::DeadlineExceeded;
    }

    bool observed_final_decide_is_not_replayed_while_scene_loads()
    {
        Driver driver(Config {
            5, 30, 2, Scene::Title, Target::MainMenu});
        Command first {};
        Command retry {};
        return driver.begin(60, kDecideOnly, 1, 0, Scene::Title)
            && driver.next_command(60, 0, Scene::Title, true, first)
            && driver.on_dispatch_result(
                60, first.dispatch_id, 0, true)
                == Result::DispatchAccepted
            && driver.acknowledge_target_scene_queued(
                60, first.dispatch_id, 1, Scene::Title)
                == Result::WaitingForTargetScene
            && !driver.next_command(60, 5, Scene::Title, true, retry)
            && !driver.next_command(60, 6, Scene::Title, true, retry)
            && !driver.next_command(60, 29, Scene::Title, true, retry)
            && driver.observe_scene(61, 30, Scene::MainMenu)
                == Result::Succeeded
            && driver.sequence_complete();
    }

    bool consumed_input_without_scene_evidence_retries()
    {
        Driver driver(Config {
            5, 40, 3, Scene::Title, Target::MainMenu});
        Command first {};
        Command early {};
        Command retry {};
        return driver.begin(70, kDecideOnly, 1, 0, Scene::Title)
            && driver.next_command(70, 0, Scene::Title, true, first)
            && driver.on_dispatch_result(
                   70, first.dispatch_id, 0, true)
                == Result::DispatchAccepted
            && driver.acknowledge_input_consumed(
                   70, first.dispatch_id, 1, Scene::Title)
                == Result::WaitingForAction
            // XInput consumption is transport evidence only.  The harness
            // deliberately does not call acknowledge(true) until MainMenu is
            // current or queued, so the generic timeout must remain armed.
            && !driver.next_command(70, 4, Scene::Title, true, early)
            && driver.next_command(70, 5, Scene::Title, true, retry)
            && retry.dispatch_id != first.dispatch_id
            && retry.key == Key::Decide
            && driver.attempts_for_step() == 2;
    }

    bool semantic_final_action_waits_without_replay()
    {
        Driver driver(Config {5, 40, 3});
        Command decide {};
        Command replay {};
        return driver.begin(71, kDecideOnly, 1, 0, Scene::MainMenu)
            && driver.next_command(
                71, 0, Scene::MainMenu, true, decide)
            && driver.on_dispatch_result(
                   71, decide.dispatch_id, 0, true)
                == Result::DispatchAccepted
            && driver.acknowledge_semantic_action_started(
                   71, decide.dispatch_id, 1, Scene::MainMenu)
                == Result::WaitingForTargetScene
            && driver.pending_action_observed()
            && !driver.next_command(
                71, 5, Scene::MainMenu, true, replay)
            && !driver.next_command(
                71, 20, Scene::MainMenu, true, replay)
            && !driver.sequence_complete()
            && driver.observe_scene(
                   72, 21, Scene::PlayerMatchLobby)
                == Result::Succeeded
            && driver.sequence_complete();
    }

    bool intermediate_semantic_action_waits_without_replay()
    {
        Driver driver(Config {5, 40, 3});
        Command down {};
        Command replay {};
        return driver.begin(72, kDownDecide, 2, 0, Scene::MainMenu)
            && driver.next_command(
                72, 0, Scene::MainMenu, true, down)
            && driver.on_dispatch_result(
                   72, down.dispatch_id, 0, true)
                == Result::DispatchAccepted
            && driver.acknowledge_semantic_action_started(
                   72, down.dispatch_id, 1, Scene::MainMenu)
                == Result::WaitingForAction
            && driver.pending_action_observed()
            && !driver.next_command(
                72, 5, Scene::MainMenu, true, replay)
            && driver.state() == State::AwaitingAcknowledgement
            && driver.step_index() == 0;
    }

    bool confirmed_focus_change_has_semantic_settle()
    {
        Driver driver(Config {
            5, 50, 2, Scene::MainMenu, Target::PlayerMatch, 3});
        Command first {};
        Command too_early {};
        Command settled {};
        if (!driver.begin(80, kDownDecide, 2, 0, Scene::MainMenu)
            || !driver.next_command(
                80, 0, Scene::MainMenu, true, first)
            || driver.on_dispatch_result(
                   80, first.dispatch_id, 0, true)
                != Result::DispatchAccepted)
        {
            return false;
        }

        const bool unconfirmed_does_not_advance =
            driver.acknowledge_focus_change(
                80, first.dispatch_id, 1, Scene::MainMenu, false)
                == Result::WaitingForAction
            && driver.step_index() == 0;
        const bool confirmed_advances = driver.acknowledge_focus_change(
                80, first.dispatch_id, 1, Scene::MainMenu, true)
                == Result::ActionAcknowledged
            && driver.step_index() == 1
            && driver.next_dispatch_not_before_tick() == 4;
        const bool settle_blocks = !driver.next_command(
                80, 3, Scene::MainMenu, true, too_early)
            && driver.state() == State::ReadyToDispatch;
        const bool settled_dispatch = driver.next_command(
                80, 4, Scene::MainMenu, true, settled)
            && settled.step_index == 1
            && settled.key == Key::Decide;
        return unconfirmed_does_not_advance && confirmed_advances
            && settle_blocks && settled_dispatch;
    }

    bool title_scene_requires_stability_before_dispatch()
    {
        StabilityGate gate;
        bool ready = false;
        for (uint64_t tick = 1; tick < 30; ++tick)
            ready = ready || gate.observe(100, true, 30);
        const bool exactly_ready = gate.observe(100, true, 30);
        const bool reset_on_ineligible = !gate.observe(100, false, 30)
            && gate.stable_ticks() == 0;
        const bool new_generation_restarts = !gate.observe(101, true, 30)
            && gate.stable_ticks() == 1;
        return !ready && exactly_ready && reset_on_ineligible
            && new_generation_restarts;
    }
}

int main()
{
    const bool unavailable = unavailable_dispatcher_does_not_complete();
    const bool dispatch_ack = dispatch_requires_action_acknowledgement();
    const bool dropped_retry = dropped_input_retries_same_step();
    const bool stale = stale_generation_and_dispatch_are_ignored();
    const bool reset = scene_generation_reset_restarts_route();
    const bool bounded = failures_are_bounded();
    const bool decide_scene = decide_requires_player_match_scene();
    const bool happy = closed_loop_happy_path();
    const bool hierarchy = intermediate_decide_enters_network_submenu();
    const bool invite_bounded = invite_generation_change_remains_bounded();
    const bool title =
        observed_final_decide_is_not_replayed_while_scene_loads();
    const bool title_retry =
        consumed_input_without_scene_evidence_retries();
    const bool semantic_final =
        semantic_final_action_waits_without_replay();
    const bool semantic_final_only =
        intermediate_semantic_action_waits_without_replay();
    const bool title_stable =
        title_scene_requires_stability_before_dispatch();
    const bool semantic_settle =
        confirmed_focus_change_has_semantic_settle();
    const bool ok = unavailable && dispatch_ack && dropped_retry && stale
        && reset && bounded && decide_scene && happy && hierarchy
        && invite_bounded && title && title_retry && semantic_final
        && semantic_final_only
        && title_stable
        && semantic_settle;

    std::printf(
        "rollback UI-navigation self-test %s unavailable=%d "
        "dispatch_ack=%d dropped_retry=%d stale=%d reset=%d bounded=%d "
        "decide_scene=%d happy=%d hierarchy=%d invite_bounded=%d title=%d "
        "title_retry=%d semantic_final=%d title_stable=%d "
        "semantic_final_only=%d semantic_settle=%d\n",
        ok ? "passed" : "failed",
        unavailable ? 1 : 0,
        dispatch_ack ? 1 : 0,
        dropped_retry ? 1 : 0,
        stale ? 1 : 0,
        reset ? 1 : 0,
        bounded ? 1 : 0,
        decide_scene ? 1 : 0,
        happy ? 1 : 0,
        hierarchy ? 1 : 0,
        invite_bounded ? 1 : 0,
        title ? 1 : 0,
        title_retry ? 1 : 0,
        semantic_final ? 1 : 0,
        title_stable ? 1 : 0,
        semantic_final_only ? 1 : 0,
        semantic_settle ? 1 : 0);
    return ok ? 0 : 1;
}
