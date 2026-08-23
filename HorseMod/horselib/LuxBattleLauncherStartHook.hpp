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
        static LuxBattleLauncherStartHook& instance()
        {
            static LuxBattleLauncherStartHook s;
            return s;
        }

        bool install()
        {
            if (m_installed.load(std::memory_order_acquire)) return true;

            if (!NativeBinding::hasLauncherStart())
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[LuxBattleLauncherStartHook] NativeBinding not "
                        "resolved — cannot install\n"));
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
            Fn orig = reinterpret_cast<Fn>(instance().m_trampoline);

            if (!launcher)
            {
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

            // Log EVERY fire (with a per-process counter for triage).
            // The 2026-04-28 desync investigation needs to know:
            //  * how many times Start fires per match (1? several?)
            //  * whether host AND joiner both fire it, or just host
            //  * if launcher pointer differs between host/joiner runs
            // Without per-fire logging we can't answer those questions
            // from a post-test log.
            static std::atomic<int> s_total_fires{0};
            const int n = s_total_fires.fetch_add(1) + 1;
            RC::Output::send<RC::LogLevel::Default>(
                STR("[LuxBattleLauncherStartHook] fire #{} policy={} "
                    "launcher=0x{:X}\n"),
                n,
                static_cast<int>(policy),
                reinterpret_cast<uintptr_t>(launcher));

            if (orig) orig(launcher, InStartParam);
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
    };
}
