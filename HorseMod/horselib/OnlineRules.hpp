// ============================================================================
// Horse::OnlineRules — modded-lobby battle-rule overrides.
//
// Background
// ----------
// SC6's online lobbies use a state-only network protocol that exchanges
// stage / character / profile data and a sync state machine, but does NOT
// transmit any battle-rule fields between peers.  Each client constructs
// its own ULuxBattleSetup locally based on the active game mode (casual /
// ranked / etc.) and applies whatever DISABLE_PLAYER_SLIP-style mission
// skills that mode's data tables specify.
//
// See the protocol investigation in the Ghidra DB plate comments on
//   Z_Construct_UFunction_ULuxUIBattleLauncher_SetSlipOutMode  @ 140c36680
//   Z_Construct_UClass_ULuxUIBattleLauncher                    @ 140c12810
// for the full breakdown of why a host-only mod cannot push rule changes
// to a vanilla joiner.
//
// What this module does
// ---------------------
// Provides a "HorseMod Online Policy" — a single enum the user sets
// while preparing a multiplayer match.  HorseMod hooks the launcher's
// Start chokepoint and overrides the relevant rule configuration on
// the local client right before the rule-application chain runs.
//
// Required: BOTH peers must run HorseMod with the same policy selected.
// Out-of-band coordination is the user's responsibility (Discord, voice,
// pre-match agreement).  An auto-discovery layer using Steam Lobby Data
// is planned for a follow-up but not in v1.
//
// Implementation pattern (v1 + v2)
// --------------------------------
// PRIMARY mechanism (v2, all 5 rules) — PolyHook x64Detour on
// ULuxUIBattleLauncher::Start (image+0x5EEB50).  See
// horselib/LuxBattleLauncherStartHook.hpp for the hook itself.  The
// detour calls the appropriate Set<X>Mode native helper(s) on the
// launcher with rule-specific target values BEFORE the original Start
// runs, which writes our values into the launcher's data-table cache.
// Original Start then reads the cache and pushes mission skills based
// on those values.
//
// This works regardless of whether the lobby Blueprint itself called
// the Set*Mode setters — we don't depend on BP behaviour, we own the
// chokepoint on the C++ side.
//
// SECONDARY safety net (just for SlipOut) — UFunction post-hook on
// ALuxBattleMissionManager::IsSlipEnabled.  In the unlikely case the
// Start hook is bypassed (e.g. a different launcher path that doesn't
// go through ULuxUIBattleLauncher::Start), the runtime check still
// gets overridden.  No analogous Is<X>Enabled UFunction exists for
// the other 4 rules, so they rely solely on the Start hook.
//
// Threading
// ---------
// m_policy is an atomic enum.  Hooks fire on the game thread; the UI
// (which writes the policy) runs on the render thread.  The atomic
// load on every hook fire is cheap and handles the cross-thread read
// without a lock.
// ============================================================================

#pragma once

#include "HorseLib.hpp"

#include <Mod/CppUserModBase.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/UFunctionStructs.hpp>
#include <DynamicOutput/DynamicOutput.hpp>

#include <atomic>
#include <cstdint>
#include <string>
#include <utility>

namespace Horse
{
    // ------------------------------------------------------------------
    // The set of HorseMod online policies.  Vanilla = "do not touch
    // anything" (mod is inert for online play).  All other values
    // identify a single rule the user wants to override.
    //
    // Numbered explicitly so the persisted settings.cfg value remains
    // stable across HorseMod releases.  DO NOT renumber existing
    // entries; only append.  SlipOut is intentionally first
    // (immediately after Vanilla) per design.
    // ------------------------------------------------------------------
    enum class HorsePolicy : int
    {
        Vanilla     = 0,
        SlipOut     = 1,   // Force slip available
        NoRingOut   = 2,   // Force ring outs disabled
        EndlessMode = 3,   // Force endless match
        DamageUp    = 4,   // Force damage scaling
        BlowUp      = 5,   // Force always-blow-up reaction
    };

    inline constexpr int kHorsePolicyCount = 6;

    class OnlineRules
    {
    public:
        static OnlineRules& instance()
        {
            static OnlineRules s;
            return s;
        }

        // ---- Policy state ------------------------------------------------

        HorsePolicy current_policy() const noexcept
        {
            return m_policy.load(std::memory_order_acquire);
        }

        void set_policy(HorsePolicy v) noexcept
        {
            m_policy.store(v, std::memory_order_release);
        }

        bool policy_active(HorsePolicy p) const noexcept
        {
            return current_policy() == p;
        }

        // ---- UI labels ---------------------------------------------------

        static const char* policy_name(HorsePolicy p) noexcept
        {
            switch (p)
            {
                case HorsePolicy::Vanilla:     return "Vanilla";
                case HorsePolicy::SlipOut:     return "SlipOut";
                case HorsePolicy::NoRingOut:   return "No Ring Out";
                case HorsePolicy::EndlessMode: return "Endless Mode";
                case HorsePolicy::DamageUp:    return "Damage Up";
                case HorsePolicy::BlowUp:      return "Blow Up";
            }
            return "?";
        }

        static const char* policy_tooltip(HorsePolicy p) noexcept
        {
            switch (p)
            {
                case HorsePolicy::Vanilla:
                    return "No HorseMod intervention; play vanilla online.";
                case HorsePolicy::SlipOut:
                    return "Allow SlipOut in casual lobbies. Both peers "
                           "need this policy or the connection drops.";
                case HorsePolicy::NoRingOut:
                    return "Disable ring-outs. Both peers need this "
                           "policy or matches desync.";
                case HorsePolicy::EndlessMode:
                    return "Endless match (round counter doesn't end). "
                           "Both peers need this policy or matches desync.";
                case HorsePolicy::DamageUp:
                    return "Global damage scaling. Both peers need this "
                           "policy or matches desync.";
                case HorsePolicy::BlowUp:
                    return "Every counterhit triggers max-launch blow-up. "
                           "Both peers need this policy or matches desync.";
            }
            return "";
        }

        // ---- Hook lifecycle ----------------------------------------------
        //
        // The Start-chokepoint hook is owned by LuxBattleLauncherStartHook
        // (a separate file because it's a PolyHook x64Detour, not a
        // UE4SS UFunction hook).  This OnlineRules class only owns the
        // safety-net IsSlipEnabled UFunction post-hook.

        bool hooks_installed() const noexcept
        {
            return m_slipout_runtime_hook_registered;
        }

        bool try_install_hooks()
        {
            install_slipout_runtime_hook();
            return hooks_installed();
        }

        void uninstall_hooks()
        {
            if (m_slipout_runtime_hook_registered &&
                !m_slipout_runtime_hook_path.empty())
            {
                RC::Unreal::UObjectGlobals::UnregisterHook(
                    m_slipout_runtime_hook_path, m_slipout_runtime_hook_ids);
                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("[OnlineRules] dtor unregistered IsSlipEnabled hook "
                        "pre={} post={}\n"),
                    m_slipout_runtime_hook_ids.first,
                    m_slipout_runtime_hook_ids.second);
                m_slipout_runtime_hook_registered = false;
            }
        }

    private:
        OnlineRules() = default;
        ~OnlineRules() { uninstall_hooks(); }
        OnlineRules(const OnlineRules&)            = delete;
        OnlineRules& operator=(const OnlineRules&) = delete;

        // ------------------------------------------------------------------
        // SlipOut runtime safety-net hook.
        //
        // ALuxBattleMissionManager::IsSlipEnabled(int32 inPlayerIndex)
        // is a BlueprintPure const UFunction returning bool.  Hook the
        // post-callback to override its return to true when the SlipOut
        // policy is active.  This is REDUNDANT with the Start-hook path
        // (which makes BattleRule.SlipOut=false → no DISABLE skill push
        // → IsSlipEnabled naturally returns true) but provides a fallback
        // if Start is somehow bypassed.
        //
        // Defensive null check on RESULT_DECL — the previous build
        // crashed on first launch when this was a plain
        // SetReturnValue<bool>(true) without the null guard.  See the
        // git history for the full investigation.
        // ------------------------------------------------------------------
        void install_slipout_runtime_hook()
        {
            using namespace RC;
            using namespace RC::Unreal;

            if (m_slipout_runtime_hook_registered) return;

            UClass* klass = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr,
                STR("/Script/LuxorGame.LuxBattleMissionManager"));
            if (!klass) return;

            m_slipout_runtime_hook_path =
                std::wstring(STR("/Script/LuxorGame.LuxBattleMissionManager"))
                + STR(":IsSlipEnabled");

            UFunction* fn = UObjectGlobals::StaticFindObject<UFunction*>(
                nullptr, nullptr, m_slipout_runtime_hook_path);
            if (!fn)
            {
                Output::send<LogLevel::Warning>(
                    STR("[OnlineRules] IsSlipEnabled not found at {} — "
                        "runtime safety-net inert.\n"),
                    m_slipout_runtime_hook_path);
                m_slipout_runtime_hook_registered = true;  // stop retrying
                return;
            }

            UnrealScriptFunctionCallable pre_cb =
                [](UnrealScriptFunctionCallableContext&, void*) {};
            UnrealScriptFunctionCallable post_cb =
                [](UnrealScriptFunctionCallableContext& ctx, void*) {
                    if (instance().current_policy() != HorsePolicy::SlipOut)
                        return;

                    // Defensive: skip if the result slot is null.
                    if (!ctx.RESULT_DECL) return;

                    // BoolProperty for a UFunction return value is 1
                    // byte in UE4.17 (SC6's engine version).  Write
                    // explicitly via uint8_t so the size is unambiguous.
                    *static_cast<uint8_t*>(ctx.RESULT_DECL) = 1;

                    // First-fire log only — no per-frame spam.
                    static std::atomic<int> s_seen{0};
                    if (s_seen.fetch_add(1) == 0)
                    {
                        RC::Output::send<RC::LogLevel::Default>(
                            STR("[OnlineRules] FIRST IsSlipEnabled "
                                "runtime override fired\n"));
                    }
                };

            // RegisterHook can throw std::runtime_error if the resolved
            // UFunction isn't a hookable shape (UObjectGlobals.cpp:855
            // throws on FUNC_Native + non-ProcessInternal mismatch).
            // Pre-validation above only guards the not-loaded-yet case;
            // the throw path needs explicit handling so an unexpected
            // engine-version skew doesn't tear down the mod via an
            // uncaught DLL-boundary exception.
            try
            {
                m_slipout_runtime_hook_ids = UObjectGlobals::RegisterHook(
                    m_slipout_runtime_hook_path, pre_cb, post_cb, nullptr);
            }
            catch (const std::exception& e)
            {
                Output::send<LogLevel::Error>(
                    STR("[OnlineRules] IsSlipEnabled RegisterHook threw: {}\n"),
                    RC::to_generic_string(e.what()));
                // Mark registered=true to stop the retry loop — if it
                // threw once it's structurally going to keep throwing,
                // and spinning every poll-tick logs nothing useful.
                m_slipout_runtime_hook_registered = true;
                return;
            }
            // (0,0) is the only sentinel UE4SS uses to signal a
            // silent-no-op in the global-script-hook path; treat as
            // failure and don't mark registered so we can retry later.
            if (m_slipout_runtime_hook_ids.first == 0
                && m_slipout_runtime_hook_ids.second == 0)
            {
                Output::send<LogLevel::Warning>(
                    STR("[OnlineRules] IsSlipEnabled RegisterHook returned "
                        "(0,0) — treating as failure.\n"));
                return;
            }
            m_slipout_runtime_hook_registered = true;

            Output::send<LogLevel::Default>(
                STR("[OnlineRules] IsSlipEnabled safety-net hook registered: "
                    "{} (pre={} post={})\n"),
                m_slipout_runtime_hook_path,
                m_slipout_runtime_hook_ids.first,
                m_slipout_runtime_hook_ids.second);
        }

        std::atomic<HorsePolicy>           m_policy {HorsePolicy::Vanilla};

        // SlipOut runtime safety net.
        bool                               m_slipout_runtime_hook_registered = false;
        std::pair<int32_t, int32_t>        m_slipout_runtime_hook_ids{};
        std::wstring                       m_slipout_runtime_hook_path;
    };
}
