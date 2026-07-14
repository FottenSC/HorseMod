#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Horse
{
    enum class RollbackUiNavigationKey : uint8_t
    {
        Up,
        Down,
        Left,
        Right,
        Decide,
        Cancel,
    };

    enum class RollbackUiNavigationScene : uint8_t
    {
        Unknown,
        Title,
        MainMenu,
        PlayerMatchInvite,
        PlayerMatchLobby,
        PlayerMatchSetup,
        PlayerMatchBattle,
        Other,
    };

    enum class RollbackUiNavigationState : uint8_t
    {
        Idle,
        ReadyToDispatch,
        DispatchPending,
        AwaitingAcknowledgement,
        Succeeded,
        Failed,
    };

    enum class RollbackUiNavigationTarget : uint8_t
    {
        PlayerMatch,
        MainMenu,
    };

    enum class RollbackUiNavigationFailure : uint8_t
    {
        None,
        InvalidConfiguration,
        InvalidRoute,
        DeadlineExceeded,
        AttemptLimitExceeded,
    };

    enum class RollbackUiNavigationResult : uint8_t
    {
        None,
        Restarted,
        DispatchAccepted,
        DispatchRejected,
        ActionAcknowledged,
        WaitingForAction,
        WaitingForTargetScene,
        WaitingForPlayerMatchScene = WaitingForTargetScene,
        Succeeded,
        Failed,
        IgnoredStaleGeneration,
        IgnoredStaleDispatch,
        IgnoredState,
    };

    enum class RollbackUiNavigationEvidence : uint8_t
    {
        None,
        // The native input consumer read or accepted the command. This proves
        // transport only; it is never sufficient to settle a menu action.
        InputConsumed,
        // The caller independently observed the requested focus/hierarchy
        // change after the command.
        FocusChanged,
        // A semantic, non-idempotent menu action was invoked and may now be
        // completing asynchronous privilege/login work. This suppresses
        // replay but does not count as target success.
        SemanticActionStarted,
        // The destination scene is present in the game-flow next-scene slot.
        TargetSceneQueued,
    };

    struct RollbackUiNavigationConfig
    {
        uint64_t acknowledgement_timeout_ticks {30};
        uint64_t total_deadline_ticks {600};
        uint8_t max_attempts_per_step {3};
        RollbackUiNavigationScene origin_scene {
            RollbackUiNavigationScene::MainMenu};
        RollbackUiNavigationTarget target {
            RollbackUiNavigationTarget::PlayerMatch};
        uint64_t semantic_settle_ticks {1};
    };

    struct RollbackUiNavigationCommand
    {
        bool valid {false};
        uint64_t generation {0};
        uint64_t dispatch_id {0};
        uint8_t step_index {0};
        RollbackUiNavigationKey key {RollbackUiNavigationKey::Down};

        explicit operator bool() const noexcept { return valid; }
    };

    // A scene pointer becoming visible is not, by itself, proof that its
    // input hierarchy is ready.  Keep the stability policy separate from the
    // transaction driver so callers can gate the first non-idempotent input
    // without teaching the generic driver about SC6-specific scene classes.
    class RollbackUiNavigationStabilityGate
    {
    public:
        bool observe(uint64_t generation,
                     bool eligible,
                     uint64_t required_ticks) noexcept
        {
            if (!eligible || generation == 0 || required_ticks == 0)
            {
                reset();
                return false;
            }
            if (generation != m_generation)
            {
                m_generation = generation;
                m_stable_ticks = 1;
            }
            else if (m_stable_ticks != UINT64_MAX)
            {
                ++m_stable_ticks;
            }
            return m_stable_ticks >= required_ticks;
        }

        void reset() noexcept
        {
            m_generation = 0;
            m_stable_ticks = 0;
        }

        uint64_t generation() const noexcept { return m_generation; }
        uint64_t stable_ticks() const noexcept { return m_stable_ticks; }

    private:
        uint64_t m_generation {0};
        uint64_t m_stable_ticks {0};
    };

    class RollbackUiNavigationDriver
    {
    public:
        static constexpr size_t kMaxRouteSteps = 16;

        explicit RollbackUiNavigationDriver(
            RollbackUiNavigationConfig config = {}) noexcept
            : m_config(config)
        {
        }

        bool begin(uint64_t generation,
                   const RollbackUiNavigationKey* route,
                   size_t route_length,
                   uint64_t tick,
                   RollbackUiNavigationScene scene) noexcept
        {
            reset_runtime();
            m_generation = generation;
            m_started_tick = tick;
            m_last_progress_tick = tick;
            m_last_scene = scene;

            if (!valid_config())
            {
                fail(RollbackUiNavigationFailure::InvalidConfiguration);
                return false;
            }
            if (!route || route_length == 0
                || route_length > kMaxRouteSteps
                || route[route_length - 1]
                    != RollbackUiNavigationKey::Decide)
            {
                fail(RollbackUiNavigationFailure::InvalidRoute);
                return false;
            }

            for (size_t i = 0; i < route_length; ++i)
                m_route[i] = route[i];
            m_route_length = static_cast<uint8_t>(route_length);

            if (target_reached(scene))
            {
                succeed();
                return true;
            }

            m_state = RollbackUiNavigationState::ReadyToDispatch;
            return true;
        }

        RollbackUiNavigationResult observe_scene(
            uint64_t generation,
            uint64_t tick,
            RollbackUiNavigationScene scene) noexcept
        {
            if (m_state == RollbackUiNavigationState::Idle)
                return RollbackUiNavigationResult::IgnoredState;
            if (m_state == RollbackUiNavigationState::Failed)
                return RollbackUiNavigationResult::Failed;
            if (m_state == RollbackUiNavigationState::Succeeded)
                return RollbackUiNavigationResult::Succeeded;

            // Reaching the requested destination is authoritative.  In
            // particular, a scene-lifecycle generation change that accompanies
            // MainMenu -> PlayerMatch must not reset an otherwise successful
            // Decide action before the transition is observed.
            if (target_reached(scene))
            {
                m_generation = generation;
                m_last_scene = scene;
                m_last_progress_tick = tick;
                succeed();
                return RollbackUiNavigationResult::Succeeded;
            }

            if (generation != m_generation)
            {
                if (scene != m_config.origin_scene)
                {
                    // A scene UObject change is the generation token used by
                    // the live harness.  Non-target overlays (notably the
                    // invite scene) must not bypass the transaction deadline
                    // merely because their pointer differs from the origin.
                    if (deadline_expired(tick))
                    {
                        fail(RollbackUiNavigationFailure::DeadlineExceeded);
                        return RollbackUiNavigationResult::Failed;
                    }
                    return RollbackUiNavigationResult::IgnoredStaleGeneration;
                }

                m_generation = generation;
                m_last_scene = scene;
                m_step_index = 0;
                m_attempts_for_step = 0;
                m_next_dispatch_not_before_tick = 0;
                clear_pending();
                m_state = RollbackUiNavigationState::ReadyToDispatch;
                m_last_progress_tick = tick;
                if (deadline_expired(tick))
                {
                    fail(RollbackUiNavigationFailure::DeadlineExceeded);
                    return RollbackUiNavigationResult::Failed;
                }
                return RollbackUiNavigationResult::Restarted;
            }

            m_last_scene = scene;
            if (deadline_expired(tick))
            {
                fail(RollbackUiNavigationFailure::DeadlineExceeded);
                return RollbackUiNavigationResult::Failed;
            }
            return RollbackUiNavigationResult::None;
        }

        bool next_command(uint64_t generation,
                          uint64_t tick,
                          RollbackUiNavigationScene scene,
                          bool dispatcher_available,
                          RollbackUiNavigationCommand& out) noexcept
        {
            out = {};
            const RollbackUiNavigationResult observed =
                observe_scene(generation, tick, scene);
            if (observed == RollbackUiNavigationResult::Failed
                || observed == RollbackUiNavigationResult::Succeeded
                || generation != m_generation)
            {
                return false;
            }

            if ((m_state == RollbackUiNavigationState::DispatchPending
                 || m_state
                    == RollbackUiNavigationState::AwaitingAcknowledgement)
                // Only semantic target evidence suppresses a retry. Merely
                // delivering/consuming a command leaves this timeout armed.
                && !m_pending_action_observed
                && elapsed(tick, m_pending_since_tick)
                    >= m_config.acknowledgement_timeout_ticks)
            {
                clear_pending();
                if (m_attempts_for_step
                    >= m_config.max_attempts_per_step)
                {
                    fail(RollbackUiNavigationFailure::AttemptLimitExceeded);
                    return false;
                }
                m_state = RollbackUiNavigationState::ReadyToDispatch;
            }

            if (m_state != RollbackUiNavigationState::ReadyToDispatch
                || scene != m_config.origin_scene
                || !dispatcher_available
                || tick < m_next_dispatch_not_before_tick)
            {
                return false;
            }

            if (m_step_index >= m_route_length)
            {
                fail(RollbackUiNavigationFailure::InvalidRoute);
                return false;
            }

            ++m_attempts_for_step;
            ++m_total_dispatches;
            ++m_next_dispatch_id;
            if (m_next_dispatch_id == 0)
                ++m_next_dispatch_id;

            m_pending_dispatch_id = m_next_dispatch_id;
            m_pending_since_tick = tick;
            m_state = RollbackUiNavigationState::DispatchPending;

            out.valid = true;
            out.generation = m_generation;
            out.dispatch_id = m_pending_dispatch_id;
            out.step_index = m_step_index;
            out.key = m_route[m_step_index];
            return true;
        }

        RollbackUiNavigationResult on_dispatch_result(
            uint64_t generation,
            uint64_t dispatch_id,
            uint64_t tick,
            bool delivered) noexcept
        {
            if (generation != m_generation)
                return RollbackUiNavigationResult::IgnoredStaleGeneration;
            if (dispatch_id == 0 || dispatch_id != m_pending_dispatch_id)
                return RollbackUiNavigationResult::IgnoredStaleDispatch;
            if (m_state != RollbackUiNavigationState::DispatchPending)
                return RollbackUiNavigationResult::IgnoredState;
            if (deadline_expired(tick))
            {
                fail(RollbackUiNavigationFailure::DeadlineExceeded);
                return RollbackUiNavigationResult::Failed;
            }

            if (delivered)
            {
                m_pending_since_tick = tick;
                m_state =
                    RollbackUiNavigationState::AwaitingAcknowledgement;
                return RollbackUiNavigationResult::DispatchAccepted;
            }

            clear_pending();
            if (m_attempts_for_step >= m_config.max_attempts_per_step)
            {
                fail(RollbackUiNavigationFailure::AttemptLimitExceeded);
                return RollbackUiNavigationResult::Failed;
            }
            m_state = RollbackUiNavigationState::ReadyToDispatch;
            return RollbackUiNavigationResult::DispatchRejected;
        }

        RollbackUiNavigationResult acknowledge(
            uint64_t generation,
            uint64_t dispatch_id,
            uint64_t tick,
            RollbackUiNavigationScene scene,
            bool semantic_focus_changed) noexcept
        {
            // Compatibility overload: true means the caller independently
            // confirmed a semantic focus/hierarchy change. Do not pass XInput
            // consumption here; use acknowledge_input_consumed instead.
            return acknowledge(
                generation,
                dispatch_id,
                tick,
                scene,
                semantic_focus_changed
                    ? RollbackUiNavigationEvidence::FocusChanged
                    : RollbackUiNavigationEvidence::None);
        }

        RollbackUiNavigationResult acknowledge_input_consumed(
            uint64_t generation,
            uint64_t dispatch_id,
            uint64_t tick,
            RollbackUiNavigationScene scene) noexcept
        {
            return acknowledge(
                generation, dispatch_id, tick, scene,
                RollbackUiNavigationEvidence::InputConsumed);
        }

        RollbackUiNavigationResult acknowledge_focus_change(
            uint64_t generation,
            uint64_t dispatch_id,
            uint64_t tick,
            RollbackUiNavigationScene scene,
            bool focus_change_confirmed) noexcept
        {
            return acknowledge(
                generation, dispatch_id, tick, scene,
                focus_change_confirmed
                    ? RollbackUiNavigationEvidence::FocusChanged
                    : RollbackUiNavigationEvidence::None);
        }

        RollbackUiNavigationResult acknowledge_target_scene_queued(
            uint64_t generation,
            uint64_t dispatch_id,
            uint64_t tick,
            RollbackUiNavigationScene scene) noexcept
        {
            return acknowledge(
                generation, dispatch_id, tick, scene,
                RollbackUiNavigationEvidence::TargetSceneQueued);
        }

        RollbackUiNavigationResult acknowledge_semantic_action_started(
            uint64_t generation,
            uint64_t dispatch_id,
            uint64_t tick,
            RollbackUiNavigationScene scene) noexcept
        {
            return acknowledge(
                generation, dispatch_id, tick, scene,
                RollbackUiNavigationEvidence::SemanticActionStarted);
        }

        RollbackUiNavigationResult acknowledge(
            uint64_t generation,
            uint64_t dispatch_id,
            uint64_t tick,
            RollbackUiNavigationScene scene,
            RollbackUiNavigationEvidence evidence) noexcept
        {
            if (generation != m_generation)
                return RollbackUiNavigationResult::IgnoredStaleGeneration;
            if (dispatch_id == 0 || dispatch_id != m_pending_dispatch_id)
                return RollbackUiNavigationResult::IgnoredStaleDispatch;
            if (m_state
                != RollbackUiNavigationState::AwaitingAcknowledgement)
            {
                return RollbackUiNavigationResult::IgnoredState;
            }

            if (target_reached(scene))
            {
                m_last_scene = scene;
                m_last_progress_tick = tick;
                succeed();
                return RollbackUiNavigationResult::Succeeded;
            }
            if (deadline_expired(tick))
            {
                fail(RollbackUiNavigationFailure::DeadlineExceeded);
                return RollbackUiNavigationResult::Failed;
            }

            m_last_scene = scene;
            const bool final_decide =
                m_route[m_step_index] == RollbackUiNavigationKey::Decide
                && m_step_index + 1 >= m_route_length;
            if (evidence == RollbackUiNavigationEvidence::TargetSceneQueued ||
                evidence ==
                    RollbackUiNavigationEvidence::SemanticActionStarted)
            {
                if (!final_decide)
                    return RollbackUiNavigationResult::WaitingForAction;
                if (!m_pending_action_observed)
                {
                    m_pending_action_observed = true;
                    m_pending_since_tick = tick;
                    m_last_progress_tick = tick;
                }
                return RollbackUiNavigationResult::WaitingForTargetScene;
            }
            if (final_decide)
            {
                // Neither a native input read nor a focus animation proves
                // that a final Decide selected the requested destination.
                // Without current/queued target-scene evidence the normal
                // acknowledgement timeout remains armed and will retry.
                return RollbackUiNavigationResult::WaitingForAction;
            }
            // A hierarchy commit (for example Main Menu -> Network) is an
            // intermediate Decide.  It is acknowledged like a directional
            // step because the scene remains MainMenu and more route commands
            // still have to be dispatched.
            if (evidence != RollbackUiNavigationEvidence::FocusChanged
                || scene != m_config.origin_scene)
            {
                return RollbackUiNavigationResult::WaitingForAction;
            }

            clear_pending();
            ++m_step_index;
            m_attempts_for_step = 0;
            m_last_progress_tick = tick;
            m_next_dispatch_not_before_tick =
                tick + m_config.semantic_settle_ticks;
            if (m_step_index >= m_route_length)
            {
                fail(RollbackUiNavigationFailure::InvalidRoute);
                return RollbackUiNavigationResult::Failed;
            }
            m_state = RollbackUiNavigationState::ReadyToDispatch;
            return RollbackUiNavigationResult::ActionAcknowledged;
        }

        RollbackUiNavigationState state() const noexcept { return m_state; }
        RollbackUiNavigationFailure failure() const noexcept
        {
            return m_failure;
        }
        uint64_t generation() const noexcept { return m_generation; }
        uint64_t pending_dispatch_id() const noexcept
        {
            return m_pending_dispatch_id;
        }
        bool pending_action_observed() const noexcept
        {
            return m_pending_action_observed;
        }
        uint64_t total_dispatches() const noexcept
        {
            return m_total_dispatches;
        }
        uint64_t started_tick() const noexcept { return m_started_tick; }
        uint64_t last_progress_tick() const noexcept
        {
            return m_last_progress_tick;
        }
        uint64_t next_dispatch_not_before_tick() const noexcept
        {
            return m_next_dispatch_not_before_tick;
        }
        uint8_t route_length() const noexcept { return m_route_length; }
        uint8_t step_index() const noexcept { return m_step_index; }
        uint8_t attempts_for_step() const noexcept
        {
            return m_attempts_for_step;
        }
        RollbackUiNavigationScene last_scene() const noexcept
        {
            return m_last_scene;
        }
        bool sequence_complete() const noexcept
        {
            return m_state == RollbackUiNavigationState::Succeeded;
        }

        static const char* failure_name(
            RollbackUiNavigationFailure failure) noexcept
        {
            switch (failure)
            {
            case RollbackUiNavigationFailure::None:
                return "none";
            case RollbackUiNavigationFailure::InvalidConfiguration:
                return "invalid-configuration";
            case RollbackUiNavigationFailure::InvalidRoute:
                return "invalid-route";
            case RollbackUiNavigationFailure::DeadlineExceeded:
                return "deadline-exceeded";
            case RollbackUiNavigationFailure::AttemptLimitExceeded:
                return "attempt-limit-exceeded";
            }
            return "unknown";
        }

        static bool is_player_match_scene(
            RollbackUiNavigationScene scene) noexcept
        {
            // The production two-client harness is invite-free.  Reaching the
            // invite overlay is observable, but it is not the requested
            // Player Match destination and must never satisfy navigation.
            return scene == RollbackUiNavigationScene::PlayerMatchLobby
                || scene == RollbackUiNavigationScene::PlayerMatchSetup
                || scene == RollbackUiNavigationScene::PlayerMatchBattle;
        }

    private:
        static uint64_t elapsed(uint64_t now, uint64_t then) noexcept
        {
            return now - then;
        }

        bool valid_config() const noexcept
        {
            return m_config.acknowledgement_timeout_ticks != 0
                && m_config.total_deadline_ticks != 0
                && m_config.max_attempts_per_step != 0;
        }

        bool deadline_expired(uint64_t tick) const noexcept
        {
            return elapsed(tick, m_started_tick)
                >= m_config.total_deadline_ticks;
        }

        void clear_pending() noexcept
        {
            m_pending_dispatch_id = 0;
            m_pending_since_tick = 0;
            m_pending_action_observed = false;
            m_next_dispatch_not_before_tick = 0;
        }

        bool target_reached(RollbackUiNavigationScene scene) const noexcept
        {
            return m_config.target
                    == RollbackUiNavigationTarget::MainMenu
                ? scene == RollbackUiNavigationScene::MainMenu
                : is_player_match_scene(scene);
        }

        void fail(RollbackUiNavigationFailure failure) noexcept
        {
            clear_pending();
            m_failure = failure;
            m_state = RollbackUiNavigationState::Failed;
        }

        void succeed() noexcept
        {
            clear_pending();
            m_failure = RollbackUiNavigationFailure::None;
            m_step_index = m_route_length;
            m_state = RollbackUiNavigationState::Succeeded;
        }

        void reset_runtime() noexcept
        {
            m_route = {};
            m_route_length = 0;
            m_step_index = 0;
            m_attempts_for_step = 0;
            m_generation = 0;
            m_next_dispatch_id = 0;
            m_pending_dispatch_id = 0;
            m_pending_since_tick = 0;
            m_pending_action_observed = false;
            m_next_dispatch_not_before_tick = 0;
            m_started_tick = 0;
            m_last_progress_tick = 0;
            m_total_dispatches = 0;
            m_last_scene = RollbackUiNavigationScene::Unknown;
            m_state = RollbackUiNavigationState::Idle;
            m_failure = RollbackUiNavigationFailure::None;
        }

        RollbackUiNavigationConfig m_config {};
        std::array<RollbackUiNavigationKey, kMaxRouteSteps> m_route {};
        uint8_t m_route_length {0};
        uint8_t m_step_index {0};
        uint8_t m_attempts_for_step {0};
        uint64_t m_generation {0};
        uint64_t m_next_dispatch_id {0};
        uint64_t m_pending_dispatch_id {0};
        uint64_t m_pending_since_tick {0};
        bool m_pending_action_observed {false};
        uint64_t m_started_tick {0};
        uint64_t m_last_progress_tick {0};
        uint64_t m_next_dispatch_not_before_tick {0};
        uint64_t m_total_dispatches {0};
        RollbackUiNavigationScene m_last_scene {
            RollbackUiNavigationScene::Unknown};
        RollbackUiNavigationState m_state {
            RollbackUiNavigationState::Idle};
        RollbackUiNavigationFailure m_failure {
            RollbackUiNavigationFailure::None};
    };
}
