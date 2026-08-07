// ============================================================================
// Horse::LuxBattleLauncherStartHook — PolyHook x64Detour on
// ULuxUIBattleLauncher::Start @ image+0x5EEB50.
//
// Why this exists
// ---------------
// The earlier OnlineRules implementation hooked the BlueprintCallable
// Set<X>Mode UFunctions on ULuxUIBattleLauncher and used pre-callbacks
// to flip the input bool.  This works ONLY if the lobby Blueprint
// actually calls those setters during match setup.  For SlipOut the BP
// demonstrably does call SetSlipOutMode (vanilla casual disables slip,
// so something is setting that), but for the other 4 rules (NoRingOut,
// EndlessMode, DamageUp, BlowUp) the vanilla casual default is "off"
// and there's no need for the BP to call the setter — leaving our
// pre-hook silent and our policy inert.
//
// The fix: hook the C++ implementation of ULuxUIBattleLauncher::Start
// directly.  Start is the chokepoint that READS the launcher's data-
// table at this+0x50 and feeds the values into the rule registrar at
// FUN_1405f6d20.  By writing our desired BattleRule.<X> values into
// the data table BEFORE the original Start runs, we guarantee the
// rule-application chain sees them — regardless of which Blueprint
// path got us here.
//
// How it works
// ------------
// 1. PolyHook x64Detour replaces the first bytes of Start
//    (FUN_1405eeb50) with a JMP to our detour.
// 2. Our detour receives `(launcher, InStartParam)` — same signature
//    as the original.
// 3. Looks at OnlineRules::current_policy().  For non-Vanilla policies,
//    calls the corresponding setter on `launcher` with the rule's
//    target_value (writes into the data-table cache).
// 4. Forwards to the trampoline (relocated prologue + jump back to
//    post-prologue Start).
// 5. Original Start now reads the freshly-written values from the
//    cache and applies them.
//
// Each setter is a regular C++ function call to the resolved native
// address — see NativeBinding's setSlipOutMode / setEndlessMode /
// setDamageUpMode / setNoRingOutMode / setBlowUpMode wrappers.
//
// Polarity per rule (target_value when the policy is selected):
//   SlipOut    -> target = false  (BattleRule.SlipOut=true SUPPRESSES slip;
//                                 we want false to leave slip available)
//   NoRingOut  -> target = true   (BattleRule.NoRingOut=true pushes
//                                 DISABLE_RINGOUT mission skill)
//   EndlessMode-> target = true   (writes BattleRule.Endless=true; the
//                                 separate handler at FUN_140594eb0 flips
//                                 the launcher's bEndless flag)
//   DamageUp   -> target = true   (push DAMAGE_UP)
//   BlowUp     -> target = true   (push HIT_POWER_UP)
//
// Threading
// ---------
// Start is called on the game thread when a match is being prepared.
// Our detour runs on the same thread.  No locking needed beyond the
// atomic policy load inside OnlineRules.
//
// Why no asm stub (unlike SetStartPositionHook)
// ----------------------------------------------
// Start's caller is the exec thunk FUN_140c41c90, which simply does
// `Start(launcher, &param); return;` — no volatile-register reads
// after the call.  So we don't need to preserve XMM0/R10/R11 like
// SetStartPosition's caller PositionCharasSymmetrically did.
// A plain C++ detour is safe here.
// ============================================================================

#pragma once

#include "NativeBinding.hpp"
#include "OnlineRules.hpp"
#include "ReplayDebugTrace.hpp"
#include "RollbackStockOnlineLabDriver.hpp"

#include <polyhook2/Detour/x64Detour.hpp>

#include <DynamicOutput/DynamicOutput.hpp>

#include <atomic>
#include <cstdint>
#include <memory>

namespace Horse
{
    class LuxBattleLauncherStartHook
    {
    public:
        enum class ReplaySeedOverrideStatus : uint8_t
        {
            None,
            Applied,
            SetterFailed,
            UnexpectedLauncher,
            UnexpectedFire,
            DuplicateStart,
        };

        struct ReplaySeedOverrideObservation
        {
            uint64_t report_serial {0};
            uint32_t arm_serial {0};
            uint64_t request_generation {0};
            uint32_t expected_seed {0};
            uintptr_t expected_launcher {0};
            uintptr_t actual_launcher {0};
            uint64_t expected_fire_index {0};
            uint64_t actual_fire_index {0};
            uint32_t fault_arm_serial {0};
            ReplaySeedOverrideStatus status {
                ReplaySeedOverrideStatus::None};
            bool token_consumed {false};
            bool setter_ok {false};
        };

        static LuxBattleLauncherStartHook& instance()
        {
            static LuxBattleLauncherStartHook s;
            return s;
        }

        bool install()
        {
            if (m_installed.load(std::memory_order_acquire)) return true;

            if (!NativeBinding::hasLauncherStart()
                || !NativeBinding::validateLauncherStartSignature())
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[LuxBattleLauncherStartHook] Start target missing "
                        "or signature mismatch; cannot install\n"));
                return false;
            }

            const uintptr_t target = NativeBinding::launcherStartAddress();

            m_trampoline = 0;
            m_detour = std::make_unique<PLH::x64Detour>(
                static_cast<uint64_t>(target),
                reinterpret_cast<uint64_t>(&LuxBattleLauncherStartHook::detour),
                &m_trampoline);

            if (!m_detour->hook())
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[LuxBattleLauncherStartHook] x64Detour::hook() "
                        "failed on Start (target=0x{:X}). "
                        "Online rules will be inert.\n"),
                    target);
                m_detour.reset();
                return false;
            }

            m_installed.store(true, std::memory_order_release);
            RC::Output::send<RC::LogLevel::Default>(
                STR("[LuxBattleLauncherStartHook] installed (target=0x{:X}, "
                    "trampoline=0x{:X})\n"),
                target, static_cast<uintptr_t>(m_trampoline));
            return true;
        }

        void uninstall()
        {
            clear_replay_seed_override();
            if (!m_installed.exchange(false)) return;
            if (m_detour)
            {
                m_detour->unHook();
                m_detour.reset();
            }
            m_trampoline = 0;
        }

        bool installed() const noexcept
        {
            return m_installed.load(std::memory_order_acquire);
        }

        uint64_t fire_count() const noexcept
        {
            return m_total_fires.load(std::memory_order_acquire);
        }

        bool replay_seed_setter_signature_valid() const noexcept
        {
            return NativeBinding::validateLauncherRandomSeedSignature();
        }

        bool replay_seed_override_idle() const noexcept
        {
            return RollbackStockOnlineLabDriver::replay_seed_arm_allowed(
                m_replay_seed_token.load(std::memory_order_acquire) != 0,
                m_replay_seed_consumed_arm.load(
                    std::memory_order_acquire) != 0);
        }

        void set_rounds_to_win_override(int32_t rounds) noexcept
        {
            m_rounds_to_win_override.store(
                rounds > 0 && rounds <= 9 ? rounds : 0,
                std::memory_order_release);
        }

        uint32_t arm_replay_seed_override(
            uintptr_t expected_launcher,
            uint64_t expected_fire_index,
            uint64_t request_generation,
            uint32_t seed) noexcept
        {
            if (!installed() || expected_launcher == 0
                || expected_fire_index == 0 || request_generation == 0
                || seed == 0
                || !NativeBinding::validateLauncherRandomSeedSignature()
                || !RollbackStockOnlineLabDriver::replay_seed_arm_allowed(
                    m_replay_seed_token.load(std::memory_order_acquire) != 0,
                    m_replay_seed_consumed_arm.load(
                        std::memory_order_acquire) != 0))
                return 0;

            uint32_t arm_serial = m_replay_seed_arm_counter.fetch_add(
                1, std::memory_order_acq_rel) + 1;
            if (arm_serial == 0)
            {
                arm_serial = m_replay_seed_arm_counter.fetch_add(
                    1, std::memory_order_acq_rel) + 1;
                if (arm_serial == 0) return 0;
            }

            // The release-published packed token is the sole permission to
            // consume the metadata written before it.
            m_replay_seed_expected_launcher.store(
                expected_launcher, std::memory_order_relaxed);
            m_replay_seed_expected_fire.store(
                expected_fire_index, std::memory_order_relaxed);
            m_replay_seed_request_generation.store(
                request_generation, std::memory_order_relaxed);
            m_replay_seed_fault_arm.store(0, std::memory_order_relaxed);
            const uint64_t token =
                (static_cast<uint64_t>(arm_serial) << 32)
                | static_cast<uint64_t>(seed);
            uint64_t empty = 0;
            if (!m_replay_seed_token.compare_exchange_strong(
                    empty, token, std::memory_order_release,
                    std::memory_order_relaxed))
                return 0;
            return arm_serial;
        }

        void clear_replay_seed_override() noexcept
        {
            m_replay_seed_token.store(0, std::memory_order_release);
            m_replay_seed_consumed_arm.store(0, std::memory_order_release);
            m_replay_seed_consumed_value.store(0, std::memory_order_release);
            m_replay_seed_fault_arm.store(0, std::memory_order_release);
            m_replay_seed_expected_launcher.store(
                0, std::memory_order_relaxed);
            m_replay_seed_expected_fire.store(0, std::memory_order_relaxed);
            m_replay_seed_request_generation.store(
                0, std::memory_order_relaxed);
        }

        ReplaySeedOverrideObservation replay_seed_observation() const noexcept
        {
            ReplaySeedOverrideObservation result {};
            for (;;)
            {
                const uint64_t begin = m_replay_seed_report_sequence.load(
                    std::memory_order_acquire);
                if ((begin & 1u) != 0) continue;
                result.report_serial = begin;
                result.arm_serial = m_replay_seed_report_arm.load(
                    std::memory_order_relaxed);
                result.request_generation =
                    m_replay_seed_report_request_generation.load(
                        std::memory_order_relaxed);
                result.expected_seed = m_replay_seed_report_seed.load(
                    std::memory_order_relaxed);
                result.expected_launcher =
                    m_replay_seed_report_expected_launcher.load(
                        std::memory_order_relaxed);
                result.actual_launcher =
                    m_replay_seed_report_actual_launcher.load(
                        std::memory_order_relaxed);
                result.expected_fire_index =
                    m_replay_seed_report_expected_fire.load(
                        std::memory_order_relaxed);
                result.actual_fire_index =
                    m_replay_seed_report_actual_fire.load(
                        std::memory_order_relaxed);
                result.fault_arm_serial = m_replay_seed_fault_arm.load(
                    std::memory_order_relaxed);
                result.status = static_cast<ReplaySeedOverrideStatus>(
                    m_replay_seed_report_status.load(
                        std::memory_order_relaxed));
                result.token_consumed =
                    m_replay_seed_report_consumed.load(
                        std::memory_order_relaxed);
                result.setter_ok = m_replay_seed_report_setter_ok.load(
                    std::memory_order_relaxed);
                const uint64_t end = m_replay_seed_report_sequence.load(
                    std::memory_order_acquire);
                if (begin == end && (end & 1u) == 0)
                    return result;
            }
        }

    private:
        LuxBattleLauncherStartHook() = default;
        ~LuxBattleLauncherStartHook() { uninstall(); }
        LuxBattleLauncherStartHook(const LuxBattleLauncherStartHook&) = delete;
        LuxBattleLauncherStartHook& operator=(
            const LuxBattleLauncherStartHook&) = delete;

        // ---- The detour --------------------------------------------------
        //
        // Same signature as ULuxUIBattleLauncher::Start.  param2 is an
        // FUIBattleLauncherStartParam* — opaque to us; we pass it through
        // unchanged.
        //
        // For each non-Vanilla policy the user may have selected, we call
        // the corresponding setter on `launcher` with the rule's target
        // value.  This writes BattleRule.<X> into the launcher's data-
        // table cache.  We then forward to the trampoline; original Start
        // reads the cache and applies the rules.
        //
        // We log only on the FIRST fire per session per policy, so the
        // log doesn't flood across repeated lobby setups.
        static void __fastcall detour(void* launcher, void* InStartParam)
        {
            using Fn = void(__fastcall*)(void*, void*);
            LuxBattleLauncherStartHook& hook = instance();
            Fn orig = reinterpret_cast<Fn>(hook.m_trampoline);
            const uint64_t n = hook.m_total_fires.fetch_add(
                1, std::memory_order_acq_rel) + 1;
            const uint64_t seed_token_at_entry =
                hook.m_replay_seed_token.load(std::memory_order_acquire);

            if (!launcher)
            {
                if (seed_token_at_entry != 0)
                {
                    const uint32_t report_arm = static_cast<uint32_t>(
                        seed_token_at_entry >> 32);
                    hook.m_replay_seed_fault_arm.store(
                        report_arm, std::memory_order_release);
                    hook.publish_replay_seed_report(
                        report_arm,
                        hook.m_replay_seed_request_generation.load(
                            std::memory_order_relaxed),
                        static_cast<uint32_t>(seed_token_at_entry),
                        hook.m_replay_seed_expected_launcher.load(
                            std::memory_order_relaxed),
                        0,
                        hook.m_replay_seed_expected_fire.load(
                            std::memory_order_relaxed),
                        n,
                        ReplaySeedOverrideStatus::UnexpectedLauncher,
                        false, false);
                }
                if (orig) orig(launcher, InStartParam);
                return;
            }

            const auto policy = OnlineRules::instance().current_policy();

            // ALWAYS run apply_policy_to_launcher — even for the Vanilla
            // policy.  apply_policy_to_launcher's Phase 1 unconditionally
            // resets the 4 carry-over-prone rules (NoRingOut/Endless/
            // DamageUp/BlowUp) so a previous match's override doesn't
            // leak into the new match when the user switches back to
            // Vanilla or to a different non-Vanilla policy.  See the
            // function's doc-comment for the full rationale.
            apply_policy_to_launcher(launcher, policy);
            const int32_t rounds_to_win = hook
                .m_rounds_to_win_override.load(std::memory_order_acquire);
            const bool rounds_applied = rounds_to_win == 0
                || NativeBinding::changeBattleRounds(
                    launcher, rounds_to_win);

            // RandomSeed is the final HorseMod cache mutation. Original
            // Start consumes the launcher table immediately after this.
            const uint64_t token = seed_token_at_entry;
            const uint32_t arm_serial = static_cast<uint32_t>(token >> 32);
            const uint32_t replay_seed = token != 0
                ? static_cast<uint32_t>(token)
                : hook.m_replay_seed_consumed_value.load(
                    std::memory_order_acquire);
            const uintptr_t expected_launcher =
                hook.m_replay_seed_expected_launcher.load(
                    std::memory_order_relaxed);
            const uint64_t expected_fire =
                hook.m_replay_seed_expected_fire.load(
                    std::memory_order_relaxed);
            const uint64_t request_generation =
                hook.m_replay_seed_request_generation.load(
                    std::memory_order_relaxed);
            const bool consumed_arm_live =
                hook.m_replay_seed_consumed_arm.load(
                    std::memory_order_acquire) != 0;
            const auto seed_transition = RollbackStockOnlineLabDriver::
                replay_seed_start_transition(
                    token != 0,
                    reinterpret_cast<uintptr_t>(launcher)
                        == expected_launcher,
                    n == expected_fire,
                    consumed_arm_live);
            const auto seed_decision = seed_transition.decision;
            bool seed_consumed = false;
            bool seed_setter_ok = false;
            ReplaySeedOverrideStatus seed_status =
                ReplaySeedOverrideStatus::None;
            if (seed_transition.consume_token)
            {
                uint64_t expected_token = token;
                seed_consumed = token != 0
                    && hook.m_replay_seed_token.compare_exchange_strong(
                        expected_token, 0, std::memory_order_acq_rel,
                        std::memory_order_acquire);
                if (seed_consumed)
                {
                    hook.m_replay_seed_consumed_arm.store(
                        arm_serial, std::memory_order_release);
                    hook.m_replay_seed_consumed_value.store(
                        replay_seed, std::memory_order_release);
                    if (seed_transition.call_setter)
                    {
                        seed_setter_ok = NativeBinding::setRandomSeed(
                            launcher, replay_seed);
                        seed_status = seed_setter_ok
                            ? ReplaySeedOverrideStatus::Applied
                            : ReplaySeedOverrideStatus::SetterFailed;
                    }
                    else
                    {
                        seed_status = ReplaySeedOverrideStatus::UnexpectedFire;
                    }
                }
            }
            else if (seed_decision == RollbackStockOnlineLabDriver::
                         ReplaySeedStartDecision::UnexpectedLauncher)
            {
                seed_status = ReplaySeedOverrideStatus::UnexpectedLauncher;
            }
            else if (seed_decision == RollbackStockOnlineLabDriver::
                         ReplaySeedStartDecision::DuplicateStart)
            {
                seed_status = ReplaySeedOverrideStatus::DuplicateStart;
            }
            if (seed_status != ReplaySeedOverrideStatus::None)
            {
                const uint32_t report_arm_serial = arm_serial != 0
                    ? arm_serial
                    : hook.m_replay_seed_consumed_arm.load(
                        std::memory_order_acquire);
                if (seed_status != ReplaySeedOverrideStatus::Applied)
                {
                    hook.m_replay_seed_fault_arm.store(
                        report_arm_serial, std::memory_order_release);
                }
                hook.publish_replay_seed_report(
                    report_arm_serial, request_generation, replay_seed,
                    expected_launcher,
                    reinterpret_cast<uintptr_t>(launcher),
                    expected_fire, n, seed_status,
                    seed_consumed, seed_setter_ok);
            }

            // Log every fire for triage, but keep Vanilla-policy noise at
            // Verbose so a fresh install does not spam normal logs.
            // The 2026-04-28 desync investigation needs to know:
            //  * how many times Start fires per match (1? several?)
            //  * whether host AND joiner both fire it, or just host
            //  * if launcher pointer differs between host/joiner runs
            // Without per-fire logging we can't answer those questions
            // from a post-test log.
            ReplayTraceFields f;
            f.integer("fire_index", n)
             .integer("policy", static_cast<int>(policy))
             .integer("rounds_to_win_override", rounds_to_win)
             .boolean("rounds_to_win_applied", rounds_applied)
             .integer("replay_seed_arm_serial", arm_serial)
             .integer("replay_seed_request_generation", request_generation)
             .hex("replay_seed_expected", replay_seed)
             .integer("replay_seed_expected_fire_index", expected_fire)
             .integer("replay_seed_status",
                      static_cast<int>(seed_status))
             .boolean("replay_seed_token_consumed", seed_consumed)
             .boolean("replay_seed_setter_ok", seed_setter_ok)
             .hex("launcher", reinterpret_cast<uintptr_t>(launcher))
             .hex("start_param", reinterpret_cast<uintptr_t>(InStartParam));
            ReplayDebugTrace::instance().event(
                "native_replay_ui_battle_launcher_start", f);
            if (policy == HorsePolicy::Vanilla)
            {
                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("[LuxBattleLauncherStartHook] fire #{} policy={} "
                        "launcher=0x{:X} start_param=0x{:X}\n"),
                    n,
                    static_cast<int>(policy),
                    reinterpret_cast<uintptr_t>(launcher),
                    reinterpret_cast<uintptr_t>(InStartParam));
            }
            else
            {
                RC::Output::send<RC::LogLevel::Default>(
                    STR("[LuxBattleLauncherStartHook] fire #{} policy={} "
                        "launcher=0x{:X} start_param=0x{:X}\n"),
                    n,
                    static_cast<int>(policy),
                    reinterpret_cast<uintptr_t>(launcher),
                    reinterpret_cast<uintptr_t>(InStartParam));
            }

            if (orig) orig(launcher, InStartParam);
        }

        void publish_replay_seed_report(
            uint32_t arm_serial,
            uint64_t request_generation,
            uint32_t seed,
            uintptr_t expected_launcher,
            uintptr_t actual_launcher,
            uint64_t expected_fire,
            uint64_t actual_fire,
            ReplaySeedOverrideStatus status,
            bool consumed,
            bool setter_ok) noexcept
        {
            m_replay_seed_report_sequence.fetch_add(
                1, std::memory_order_acq_rel);
            m_replay_seed_report_arm.store(
                arm_serial, std::memory_order_relaxed);
            m_replay_seed_report_request_generation.store(
                request_generation, std::memory_order_relaxed);
            m_replay_seed_report_seed.store(seed, std::memory_order_relaxed);
            m_replay_seed_report_expected_launcher.store(
                expected_launcher, std::memory_order_relaxed);
            m_replay_seed_report_actual_launcher.store(
                actual_launcher, std::memory_order_relaxed);
            m_replay_seed_report_expected_fire.store(
                expected_fire, std::memory_order_relaxed);
            m_replay_seed_report_actual_fire.store(
                actual_fire, std::memory_order_relaxed);
            m_replay_seed_report_status.store(
                static_cast<uint8_t>(status), std::memory_order_relaxed);
            m_replay_seed_report_consumed.store(
                consumed, std::memory_order_relaxed);
            m_replay_seed_report_setter_ok.store(
                setter_ok, std::memory_order_relaxed);
            m_replay_seed_report_sequence.fetch_add(
                1, std::memory_order_release);
        }

        // For each policy we know about, call the matching setter on the
        // launcher with the target value that ENABLES the user-facing
        // rule.  See file-header doc for the polarity table.
        //
        // Carry-over fix (clean policy switching across matches)
        // -------------------------------------------------------
        // The launcher's data-table cache at this+0x50 PERSISTS our
        // writes across matches in the same session (the launcher
        // object is typically a long-lived UI controller).  Without
        // intervention, a previous match's NoRingOut override would
        // leak into the user's next match even if they switched to
        // Vanilla — because the lobby BP for casual matches doesn't
        // call SetNoRingOutMode (no need; vanilla default is "off"),
        // so nothing overwrites our stale `true`.
        //
        // Fix: at the START of every apply, RESET the 4 rules whose
        // vanilla default is universally "off" — NoRingOut, Endless,
        // DamageUp, BlowUp.  Their off-state is `false`, which is the
        // same regardless of game mode (casual / training / story /
        // ranked all have these off by default in vanilla SC6).  So
        // unconditionally writing false to the cache before applying
        // the user's selection guarantees:
        //   * Vanilla policy -> all 4 rules reset to off, plus we
        //     don't touch SlipOut so the BP's mode-specific value
        //     stands.  Match runs vanilla.
        //   * Non-Vanilla policy -> 4 rules reset to off, then the
        //     selected one is overridden to its target.  Only the
        //     user's chosen rule is active.
        //
        // SlipOut is intentionally NOT reset here — its vanilla
        // default differs per mode (suppressed in casual, allowed in
        // training) and the lobby BP demonstrably calls
        // SetSlipOutMode every match init for online lobbies, so the
        // BP's call is the authoritative reset.  Touching SlipOut
        // here would risk corrupting training-mode defaults.
        static void apply_policy_to_launcher(void* launcher, HorsePolicy p)
        {
            // Phase 1 — universal reset for the 4 "off-by-default" rules.
            NativeBinding::setNoRingOutMode(launcher, false);
            NativeBinding::setEndlessMode  (launcher, false);
            NativeBinding::setDamageUpMode (launcher, false);
            NativeBinding::setBlowUpMode   (launcher, false);

            // Phase 2 — apply the user's selected policy on top.
            switch (p)
            {
                case HorsePolicy::Vanilla:
                    // Phase 1's resets are sufficient; no further override.
                    // SlipOut is left to the BP's mode-specific call.
                    break;
                case HorsePolicy::SlipOut:
                    // Inverted polarity: BattleRule.SlipOut=true PUSHES
                    // DISABLE_PLAYER_SLIP.  We want false so slip stays
                    // available to both players.
                    NativeBinding::setSlipOutMode(launcher, false);
                    break;
                case HorsePolicy::NoRingOut:
                    NativeBinding::setNoRingOutMode(launcher, true);
                    break;
                case HorsePolicy::EndlessMode:
                    NativeBinding::setEndlessMode(launcher, true);
                    break;
                case HorsePolicy::DamageUp:
                    NativeBinding::setDamageUpMode(launcher, true);
                    break;
                case HorsePolicy::BlowUp:
                    NativeBinding::setBlowUpMode(launcher, true);
                    break;
            }
        }

        std::unique_ptr<PLH::x64Detour> m_detour;
        uint64_t                        m_trampoline{0};
        std::atomic<bool>               m_installed{false};
        std::atomic<uint64_t>           m_total_fires{0};
        std::atomic<int32_t>            m_rounds_to_win_override{0};
        std::atomic<uint32_t>           m_replay_seed_arm_counter{0};
        std::atomic<uint64_t>           m_replay_seed_token{0};
        std::atomic<uintptr_t>          m_replay_seed_expected_launcher{0};
        std::atomic<uint64_t>           m_replay_seed_expected_fire{0};
        std::atomic<uint64_t>           m_replay_seed_request_generation{0};
        std::atomic<uint32_t>           m_replay_seed_consumed_arm{0};
        std::atomic<uint32_t>           m_replay_seed_consumed_value{0};
        std::atomic<uint32_t>           m_replay_seed_fault_arm{0};
        std::atomic<uint64_t>           m_replay_seed_report_sequence{0};
        std::atomic<uint32_t>           m_replay_seed_report_arm{0};
        std::atomic<uint64_t>
            m_replay_seed_report_request_generation{0};
        std::atomic<uint32_t>           m_replay_seed_report_seed{0};
        std::atomic<uintptr_t>
            m_replay_seed_report_expected_launcher{0};
        std::atomic<uintptr_t>
            m_replay_seed_report_actual_launcher{0};
        std::atomic<uint64_t>           m_replay_seed_report_expected_fire{0};
        std::atomic<uint64_t>           m_replay_seed_report_actual_fire{0};
        std::atomic<uint8_t>            m_replay_seed_report_status{0};
        std::atomic<bool>               m_replay_seed_report_consumed{false};
        std::atomic<bool>               m_replay_seed_report_setter_ok{false};
    };
}
