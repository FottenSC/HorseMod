#pragma once

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>

namespace Horse
{
    // Canonical policy for the stock Player Match workflow. The live UE calls
    // remain in the game-thread harness, while this class owns every choice
    // that must agree on both clients.
    class RollbackStockOnlineLabDriver
    {
    public:
        enum class SetupPhase : uint8_t
        {
            ResetOnlineStatus,
            EnterPlayerMatch,
            CreateRoom,
            InviteGuest,
            NegotiateRollback,
            Ready,
            CharacterSelect,
            StageSelect,
            EnterBattle,
            Active,
        };

        enum class CleanupPhase : uint8_t
        {
            ExitBattle,
            AwaitLobby,
            DestroyRoom,
            ExitPlayerMatch,
            ResetOnlineStatus,
            StabilizeMainMenu,
            Complete,
        };

        static constexpr int32_t kDefaultLeftCharacter = 7;
        static constexpr int32_t kDefaultRightCharacter = 6;
        static constexpr int32_t kDefaultStage = 3;
        static constexpr int32_t kDefaultRoundsToWin = 3;
        static constexpr int32_t kMaximumCharacterCursors = 128;
        static constexpr int32_t kMaximumStageCursors = 64;
        static constexpr double kMinimumBattleTickRateHz = 58.0;
        static constexpr double kBattleRateWindowSeconds = 10.0;
        static constexpr uint32_t kBattleRateMaximumWindows = 2;

        enum class BattleRateWindowDecision : uint8_t
        {
            Passed,
            Retry,
            Failed,
        };

        static constexpr BattleRateWindowDecision battle_rate_window_decision(
            uint32_t completed_windows, double tick_rate_hz) noexcept
        {
            if (tick_rate_hz >= kMinimumBattleTickRateHz)
                return BattleRateWindowDecision::Passed;
            return completed_windows + 1 < kBattleRateMaximumWindows
                ? BattleRateWindowDecision::Retry
                : BattleRateWindowDecision::Failed;
        }

        static constexpr bool battle_rate_sample_continuous(
            bool active_player_match_scene,
            bool lifecycle_identity_valid,
            bool lifecycle_identity_matches,
            bool frame_sampled,
            uint32_t current_frame,
            uint32_t previous_frame) noexcept
        {
            return active_player_match_scene
                && lifecycle_identity_valid
                && lifecycle_identity_matches
                && (!frame_sampled || current_frame >= previous_frame);
        }

        static constexpr bool stock_lobby_ready_gate(
            bool local_session_connected,
            bool in_room_observed,
            bool stock_ui_ready,
            bool guest_dispatch_ready,
            bool rollback_contract_ready) noexcept
        {
            // Replay seed state is not an input to the session gate. Once this
            // authenticated gate passes, the caller arms the one-shot launcher
            // override immediately before the stock RequestReady call.
            return local_session_connected
                && in_room_observed
                && stock_ui_ready
                && guest_dispatch_ready
                && rollback_contract_ready;
        }

        enum class ReplaySeedStartDecision : uint8_t
        {
            PassThrough,
            Apply,
            UnexpectedLauncher,
            UnexpectedFire,
            DuplicateStart,
        };

        struct ReplaySeedStartTransition
        {
            ReplaySeedStartDecision decision {
                ReplaySeedStartDecision::PassThrough};
            bool consume_token {false};
            bool call_setter {false};
            bool fault {false};
        };

        static constexpr bool replay_seed_arm_allowed(
            bool token_live, bool consumed_arm_live) noexcept
        {
            return !token_live && !consumed_arm_live;
        }

        static constexpr ReplaySeedStartDecision
        replay_seed_start_decision(
            bool token_live,
            bool launcher_matches,
            bool fire_index_matches,
            bool consumed_arm_live) noexcept
        {
            if (token_live)
            {
                if (!launcher_matches)
                    return ReplaySeedStartDecision::UnexpectedLauncher;
                return fire_index_matches
                    ? ReplaySeedStartDecision::Apply
                    : ReplaySeedStartDecision::UnexpectedFire;
            }
            return consumed_arm_live && launcher_matches
                ? ReplaySeedStartDecision::DuplicateStart
                : ReplaySeedStartDecision::PassThrough;
        }

        static constexpr ReplaySeedStartTransition
        replay_seed_start_transition(
            bool token_live,
            bool launcher_matches,
            bool fire_index_matches,
            bool consumed_arm_live) noexcept
        {
            const ReplaySeedStartDecision decision =
                replay_seed_start_decision(
                    token_live, launcher_matches, fire_index_matches,
                    consumed_arm_live);
            return {
                decision,
                decision == ReplaySeedStartDecision::Apply
                    || decision == ReplaySeedStartDecision::UnexpectedFire,
                decision == ReplaySeedStartDecision::Apply,
                decision == ReplaySeedStartDecision::UnexpectedLauncher
                    || decision == ReplaySeedStartDecision::UnexpectedFire
                    || decision == ReplaySeedStartDecision::DuplicateStart,
            };
        }

        // Stateful hermetic model of the same transition helper used by the
        // atomic Start hook. It owns no native pointers and exists so consume,
        // failure, duplicate, clear, and no-rearm semantics are executable.
        struct ReplaySeedOneShotModel
        {
            bool token_live {false};
            bool consumed_arm_live {false};
            bool sticky_fault {false};
            bool setter_failed {false};

            constexpr bool arm() noexcept
            {
                if (!replay_seed_arm_allowed(
                        token_live, consumed_arm_live))
                    return false;
                token_live = true;
                sticky_fault = false;
                setter_failed = false;
                return true;
            }

            constexpr void clear() noexcept
            {
                token_live = false;
                consumed_arm_live = false;
                sticky_fault = false;
                setter_failed = false;
            }

            constexpr ReplaySeedStartDecision start(
                bool launcher_matches,
                bool fire_index_matches,
                bool setter_ok = true) noexcept
            {
                const ReplaySeedStartTransition transition =
                    replay_seed_start_transition(
                        token_live, launcher_matches, fire_index_matches,
                        consumed_arm_live);
                if (transition.consume_token)
                {
                    token_live = false;
                    consumed_arm_live = true;
                }
                sticky_fault = sticky_fault || transition.fault;
                if (transition.call_setter && !setter_ok)
                {
                    setter_failed = true;
                    sticky_fault = true;
                }
                return transition.decision;
            }
        };

        static constexpr bool replay_seed_allows_battle_release(
            bool replay_input_requested,
            bool replay_seed_applied) noexcept
        {
            return !replay_input_requested || replay_seed_applied;
        }

        struct BattleRateGate
        {
            uint32_t completed_windows {0};
            bool window_active {false};
            bool complete {false};
            bool ok {false};

            constexpr void reset() noexcept
            {
                completed_windows = 0;
                window_active = false;
                complete = false;
                ok = false;
            }

            constexpr void begin_window() noexcept
            {
                window_active = true;
            }

            constexpr void interrupt_window() noexcept
            {
                window_active = false;
                complete = false;
                ok = false;
            }

            constexpr BattleRateWindowDecision finish_window(
                double tick_rate_hz) noexcept
            {
                const auto decision = battle_rate_window_decision(
                    completed_windows, tick_rate_hz);
                ++completed_windows;
                window_active = false;
                complete = decision != BattleRateWindowDecision::Retry;
                ok = decision == BattleRateWindowDecision::Passed;
                return decision;
            }
        };

        struct Selection
        {
            std::string left_character;
            std::string right_character;
            std::string stage;
            int32_t rounds_to_win {kDefaultRoundsToWin};
        };

        struct SetupObservation
        {
            bool in_title {false};
            bool in_main_menu {false};
            bool in_lobby {false};
            bool in_setup {false};
            bool in_battle {false};
            bool room_created {false};
            bool guest_joined {false};
            bool rollback_contract_ready {false};
            bool ready {false};
            bool characters_synchronized {false};
            bool stage_synchronized {false};
            bool selection_bilateral {false};
            bool native_battle_running {false};
        };

        struct CleanupObservation
        {
            bool scene_identity_valid {false};
            bool in_battle {false};
            bool in_lobby {false};
            bool in_title {false};
            bool in_main_menu {false};
            bool battle_exit_requested {false};
            bool room_destroyed {false};
            bool title_reset_seen {false};
            bool main_menu_stable {false};
        };

        void reset(int32_t left_character = kDefaultLeftCharacter,
                   int32_t right_character = kDefaultRightCharacter,
                   int32_t stage = kDefaultStage,
                   int32_t rounds_to_win = kDefaultRoundsToWin)
        {
            m_selection = desired(
                left_character, right_character, stage, rounds_to_win);
            reset_phases();
        }

        void reset(const std::string& left_character,
                   const std::string& right_character,
                   int32_t stage = kDefaultStage,
                   int32_t rounds_to_win = kDefaultRoundsToWin)
        {
            m_selection = {
                left_character.empty()
                    ? character_code(kDefaultLeftCharacter)
                    : left_character,
                right_character.empty()
                    ? character_code(kDefaultRightCharacter)
                    : right_character,
                stage_code(stage >= 0 ? stage : kDefaultStage),
                rounds_to_win > 0
                    ? rounds_to_win : kDefaultRoundsToWin,
            };
            reset_phases();
        }

        void reset_phases() noexcept
        {
            m_setup_phase = SetupPhase::ResetOnlineStatus;
            m_cleanup_phase = CleanupPhase::ExitBattle;
            m_cleanup_battle_seen = false;
            m_cleanup_out_of_battle_observations = 0;
        }

        SetupPhase observe_setup(const SetupObservation& observation) noexcept
        {
            if (observation.native_battle_running)
                m_setup_phase = SetupPhase::Active;
            else if (observation.in_battle)
                m_setup_phase = SetupPhase::EnterBattle;
            else if (observation.in_setup
                && !observation.characters_synchronized)
                m_setup_phase = SetupPhase::CharacterSelect;
            else if (observation.in_setup
                && (!observation.stage_synchronized
                    || !observation.selection_bilateral))
                m_setup_phase = SetupPhase::StageSelect;
            else if (observation.in_setup)
                m_setup_phase = SetupPhase::EnterBattle;
            else if (observation.guest_joined
                && !observation.rollback_contract_ready)
                m_setup_phase = SetupPhase::NegotiateRollback;
            else if (observation.guest_joined && !observation.ready)
                m_setup_phase = SetupPhase::Ready;
            else if (observation.room_created && !observation.guest_joined)
                m_setup_phase = SetupPhase::InviteGuest;
            else if (observation.in_lobby)
                m_setup_phase = SetupPhase::CreateRoom;
            else if (observation.in_main_menu)
                m_setup_phase = SetupPhase::EnterPlayerMatch;
            else
                m_setup_phase = SetupPhase::ResetOnlineStatus;
            return m_setup_phase;
        }

        CleanupPhase observe_cleanup(
            const CleanupObservation& observation) noexcept
        {
            if (!observation.scene_identity_valid)
            {
                m_cleanup_out_of_battle_observations = 0;
            }
            else if (observation.in_battle)
            {
                m_cleanup_battle_seen = true;
                m_cleanup_out_of_battle_observations = 0;
            }
            else if (m_cleanup_battle_seen
                && m_cleanup_out_of_battle_observations < 3)
            {
                ++m_cleanup_out_of_battle_observations;
            }
            m_cleanup_phase = cleanup_phase(
                observation.in_battle,
                observation.in_lobby,
                observation.in_title,
                observation.in_main_menu,
                observation.battle_exit_requested,
                observation.room_destroyed,
                observation.title_reset_seen,
                observation.main_menu_stable);
            return m_cleanup_phase;
        }

        const Selection& selection() const noexcept { return m_selection; }
        SetupPhase setup_phase() const noexcept { return m_setup_phase; }
        CleanupPhase current_cleanup_phase() const noexcept
        {
            return m_cleanup_phase;
        }
        bool cleanup_battle_seen() const noexcept
        {
            return m_cleanup_battle_seen;
        }
        uint8_t cleanup_out_of_battle_observations() const noexcept
        {
            return m_cleanup_out_of_battle_observations;
        }
        bool cleanup_out_of_battle_stable() const noexcept
        {
            return m_cleanup_out_of_battle_observations >= 3;
        }

        static Selection desired(int32_t left_character,
                                 int32_t right_character,
                                 int32_t stage,
                                 int32_t rounds_to_win = kDefaultRoundsToWin)
        {
            return {
                character_code(left_character >= 0
                    ? left_character : kDefaultLeftCharacter),
                character_code(right_character >= 0
                    ? right_character : kDefaultRightCharacter),
                stage_code(stage >= 0 ? stage : kDefaultStage),
                rounds_to_win > 0
                    ? rounds_to_win : kDefaultRoundsToWin,
            };
        }

        static bool accepts(const Selection& expected,
                            const std::string& left,
                            const std::string& right,
                            const std::string& stage) noexcept
        {
            return left == expected.left_character
                && right == expected.right_character
                && stage == expected.stage;
        }

        static uint64_t selection_hash(const Selection& selection) noexcept
        {
            uint64_t value = 1469598103934665603ull;
            const auto add = [&value](const std::string& text) noexcept {
                for (const unsigned char byte : text)
                {
                    value ^= byte;
                    value *= 1099511628211ull;
                }
                value ^= 0xffu;
                value *= 1099511628211ull;
            };
            add(selection.left_character);
            add(selection.right_character);
            add(selection.stage);
            value ^= static_cast<uint32_t>(selection.rounds_to_win);
            value *= 1099511628211ull;
            return value ? value : 1ull;
        }

        static constexpr bool steam_connection_complete(
            bool room_host, uint32_t luxor_state,
            uintptr_t connection, bool peer_join_seen) noexcept
        {
            return connection != 0
                && (room_host
                    ? luxor_state == 1u && peer_join_seen
                    : luxor_state == 3u);
        }

        // Title reset must call ULuxInputUtil.EmulateTitleDecide. Directly
        // changing to MainMenu bypasses SC6's sign-in and online-status reset;
        // later room creation then hangs at "Creating a room..." and can crash
        // on the next title/main-menu transition.
        static constexpr const char* title_reset_function() noexcept
        {
            return "ULuxInputUtil.EmulateTitleDecide";
        }

        static constexpr const wchar_t* battle_exit_menu() noexcept
        {
            return L"BattleMenu";
        }

        static constexpr const wchar_t* battle_exit_command() noexcept
        {
            return L"LuxResultMenu::GoBackToLobby";
        }

        static constexpr const wchar_t* battle_exit_confirm_menu() noexcept
        {
            return L"Result";
        }

        static constexpr const wchar_t* battle_exit_confirm_command() noexcept
        {
            return L"OnDecide";
        }

        static constexpr CleanupPhase cleanup_phase(
            bool in_battle,
            bool in_lobby,
            bool in_title,
            bool in_main_menu,
            bool battle_exit_requested,
            bool room_destroyed,
            bool title_reset_seen,
            bool main_menu_stable) noexcept
        {
            if (main_menu_stable) return CleanupPhase::Complete;
            if (in_battle)
                return battle_exit_requested
                    ? CleanupPhase::AwaitLobby
                    : CleanupPhase::ExitBattle;
            if (in_lobby)
                return room_destroyed
                    ? CleanupPhase::ExitPlayerMatch
                    : CleanupPhase::DestroyRoom;
            if (in_title) return CleanupPhase::ResetOnlineStatus;
            if (in_main_menu)
                return title_reset_seen
                    ? CleanupPhase::StabilizeMainMenu
                    : CleanupPhase::ResetOnlineStatus;
            return CleanupPhase::AwaitLobby;
        }

        // Each process owns a local stock session object. The room owner closes
        // the room and the guest destroys its joined session through the same
        // InRoom Blueprint method before either exits Player Match.
        static constexpr bool destroys_room(bool is_host) noexcept
        {
            (void)is_host;
            return true;
        }

        static constexpr bool ownership_ready(
            bool native_battle_tick_seen,
            bool battle_rate_complete,
            bool battle_rate_ok,
            bool immediate_replay_ownership) noexcept
        {
            return native_battle_tick_seen
                && (immediate_replay_ownership
                    || (battle_rate_complete && battle_rate_ok));
        }

        static constexpr const char* cleanup_phase_name(
            CleanupPhase phase) noexcept
        {
            switch (phase)
            {
            case CleanupPhase::ExitBattle: return "exit-battle";
            case CleanupPhase::AwaitLobby: return "await-lobby";
            case CleanupPhase::DestroyRoom: return "destroy-room";
            case CleanupPhase::ExitPlayerMatch: return "exit-player-match";
            case CleanupPhase::ResetOnlineStatus: return "reset-online-status";
            case CleanupPhase::StabilizeMainMenu: return "stabilize-main-menu";
            case CleanupPhase::Complete: return "complete";
            }
            return "unknown";
        }

        static constexpr const char* setup_phase_name(
            SetupPhase phase) noexcept
        {
            switch (phase)
            {
            case SetupPhase::ResetOnlineStatus: return "reset-online-status";
            case SetupPhase::EnterPlayerMatch: return "enter-player-match";
            case SetupPhase::CreateRoom: return "create-room";
            case SetupPhase::InviteGuest: return "invite-guest";
            case SetupPhase::NegotiateRollback:
                return "negotiate-rollback";
            case SetupPhase::Ready: return "ready";
            case SetupPhase::CharacterSelect: return "character-select";
            case SetupPhase::StageSelect: return "stage-select";
            case SetupPhase::EnterBattle: return "enter-battle";
            case SetupPhase::Active: return "active";
            }
            return "unknown";
        }

    private:
        Selection m_selection {};
        SetupPhase m_setup_phase {SetupPhase::ResetOnlineStatus};
        CleanupPhase m_cleanup_phase {CleanupPhase::ExitBattle};
        bool m_cleanup_battle_seen {false};
        uint8_t m_cleanup_out_of_battle_observations {0};

        static std::string character_code(int32_t value)
        {
            std::array<char, 16> text {};
            std::snprintf(text.data(), text.size(), "%03d", value);
            return text.data();
        }

        static std::string stage_code(int32_t value)
        {
            switch (value)
            {
            case 0x106: return "STG006_R";
            case 0x111: return "STG011_R";
            case 0x115: return "STG015_R";
            case 0x201: return "STG001_V";
            case 0x206: return "STG006_V";
            case 0x217: return "STG017_V";
            case 0x311: return "STG011_R_V";
            default: break;
            }
            std::array<char, 16> text {};
            std::snprintf(text.data(), text.size(), "STG%03X", value);
            return text.data();
        }
    };
}
