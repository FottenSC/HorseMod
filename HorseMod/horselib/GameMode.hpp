// ============================================================================
// Horse::GameMode — current SC6 scene/mode tracker so the mod can self-disable
// in online matches against other human players.
//
// Why this exists
// ---------------
// A subset of HorseMod's features can affect the user's own gameplay in
// ways that aren't appropriate during a ranked / casual online match
// against another human:
//
//   - Lock camera position
//   - Free-fly camera
//   - Freeze frame (and frame stepping)
//   - Slow motion
//
// All four either change what the user sees beyond what their opponent
// sees, or affect the local simulation rate (which doesn't desync the
// netplay state machine, but does desync the user's perception of it
// in a way that's effectively a competitive advantage / cheat).
//
// SC6 doesn't expose a single "is online match" flag we can read, but it
// DOES publish a per-scene `ELuxGamePresence` enum value every time the
// player navigates between menus, training, replays, or matches.  That
// enum is the basis for Steam/Discord rich presence, set by
// `ULuxUIGamePresenceUtil::SetPresence(byte InPresence)` which the
// engine invokes once per scene transition from
// `LuxUpdateGamePresenceFromSceneData` at 0x14064a040.
//
// Hook mechanism (PolyHook x64Detour, NOT a UE4SS UFunction hook)
// ---------------------------------------------------------------
// Earlier versions of this file used `UObjectGlobals::RegisterHook` on
// the `/Script/LuxorGame.LuxUIGamePresenceUtil:SetPresence` UFunction.
// That looked attractive — it's a BlueprintCallable function — but it
// silently never fired in practice.  Ghidra cross-reference of
// `ULuxUIGamePresenceUtil_SetPresence @ 0x14064f590` showed exactly
// two callers:
//
//   1. LuxUpdateGamePresenceFromSceneData @ 0x14064a191
//      → DIRECT C++ CALL (the scene-transition driver — fires every
//        time the user navigates between menus / modes)
//   2. exec_ULuxUIGamePresenceUtil_SetPresence @ 0x140cb8e9c
//      → ProcessEvent thunk (fires when BP code calls SetPresence,
//        which doesn't happen during normal play)
//
// UE4SS's `RegisterHook` only intercepts ProcessEvent-routed dispatch
// — the direct C++ call at site 1 bypasses it entirely.  So the gate
// would sit at "presence = Unknown" forever in normal play.
//
// The fix is a PolyHook x64Detour on the C++ symbol itself, which
// catches BOTH call paths.  We use the existing PolyHook infrastructure
// in horselib/LuxBattleLauncherStartHook.hpp as the template.  The
// resolved address comes from horselib/NativeBinding.hpp which already
// computes RVAs against the live SoulcaliburVI.exe image base.
//
// Other features (hitbox overlay, character / weapon visibility, VFX
// suppression, Ansel always-allowed, reset-position override, online
// rule overrides) are NOT auto-disabled — the user explicitly opts in
// to those and they don't change the local sim rate or the user's
// camera relative to what an opponent expects.  The four gated features
// are the conservative subset that has actual competitive implications.
//
// Enum values (verified against Ghidra's
// `LuxGamePresence_FromSceneName` lookup table at 0x140652A90):
//
//     MainMenu       = 0       (no battle)
//     ShinEdgeMaster = 1       (story-mode battle, single player)
//     Chronicle      = 2       (story-mode battle, single player)
//     Creation       = 3       (chara creator, no battle)
//     Arcade         = 4       (CPU ladder, single player)
//     Versus         = 5       (offline local 2-player — see below)
//     Training       = 6       (training mode — primary use case)
//     RankMatch      = 7       (RANKED ONLINE — UNSAFE)
//     CasualMatch    = 8       (CASUAL ONLINE — UNSAFE)
//     Ranking        = 9       (leaderboards UI, no battle)
//     Replay         = 10      (replay viewer — primary use case)
//     Museum         = 11      (gallery UI, no battle)
//     Options        = 12      (settings UI, no battle)
//     Tournament     = 13      (in-game tournament bracket; offline)
//
// Versus (5) is "couch-coop" — both players are on the same machine
// with implicit consent to whatever the install has running.  We leave
// it OUT of the gated set for that reason — the user's "Auto disable
// online" toggle only fires for RankMatch / CasualMatch.
//
// Threading
// ---------
// SetPresence is called on the game thread when the user navigates
// between scenes.  Our detour runs on the same thread.  The cached
// presence is a `std::atomic<uint8_t>` so render-thread / UI-thread
// readers see consistent values without a lock.  The detour itself
// does no heavy work — read one byte, store atomic, forward to the
// trampoline.
//
// Lifecycle
// ---------
// `try_install_hook()` is idempotent and safe to call on every poll
// tick.  It depends on `Horse::NativeBinding::resolve()` having run
// successfully (which dllmain does in `on_unreal_init`); if the
// SetPresence address isn't resolved yet we just return false and
// the poll loop retries.  `uninstall_hook()` is called from the
// dllmain dtor.
// ============================================================================

#pragma once

#include "NativeBinding.hpp"

#include <polyhook2/Detour/x64Detour.hpp>

#include <DynamicOutput/DynamicOutput.hpp>

#include <atomic>
#include <cstdint>
#include <memory>

namespace Horse
{
    // Sentinel `Unknown` is reported until the SetPresence detour has
    // fired at least once.  In practice that only happens during the
    // brief window between `on_unreal_init` and the first scene
    // transition (commonly the splash → MainMenu transition), but
    // accessors must not crash if asked too early.  The
    // is_in_pvp_match() predicate treats `Unknown` as NOT-pvp so the
    // gate is OFF until we have a confirmed scene — this matches the
    // user-visible "all features available" state and avoids false
    // positives in the weird case where the hook never installs.
    enum class GamePresence : uint8_t
    {
        MainMenu       = 0,
        ShinEdgeMaster = 1,
        Chronicle      = 2,
        Creation       = 3,
        Arcade         = 4,
        Versus         = 5,
        Training       = 6,
        RankMatch      = 7,
        CasualMatch    = 8,
        Ranking        = 9,
        Replay         = 10,
        Museum         = 11,
        Options        = 12,
        Tournament     = 13,
        Unknown        = 0xFF,
    };

    // Human-readable label for the UI status line.  Matches Ghidra's
    // ELuxGamePresence enum name.  L"" return is intentional for the
    // Unknown case so the UI shows nothing instead of "Unknown" before
    // the first scene transition.
    inline const wchar_t* presence_name(GamePresence p) noexcept
    {
        switch (p)
        {
            case GamePresence::MainMenu:       return L"MainMenu";
            case GamePresence::ShinEdgeMaster: return L"ShinEdgeMaster";
            case GamePresence::Chronicle:      return L"Chronicle";
            case GamePresence::Creation:       return L"Creation";
            case GamePresence::Arcade:         return L"Arcade";
            case GamePresence::Versus:         return L"Versus (local 2P)";
            case GamePresence::Training:       return L"Training";
            case GamePresence::RankMatch:      return L"RankMatch (online)";
            case GamePresence::CasualMatch:    return L"CasualMatch (online)";
            case GamePresence::Ranking:        return L"Ranking";
            case GamePresence::Replay:         return L"Replay";
            case GamePresence::Museum:         return L"Museum";
            case GamePresence::Options:        return L"Options";
            case GamePresence::Tournament:     return L"Tournament";
            case GamePresence::Unknown:        return L"";
        }
        return L"";
    }

    class GameMode
    {
    public:
        static GameMode& instance()
        {
            static GameMode s;
            return s;
        }

        // Try to install the SetPresence detour.  Idempotent.
        //
        // Returns true if the detour is now (or was already) installed.
        // Safe to call repeatedly from on_update — failure usually
        // means NativeBinding::resolve() hasn't run yet, which the
        // dllmain ctor / on_unreal_init does early.
        //
        // The detour itself does no game-state mutation — purely
        // observational, so even if the user has the safety gate
        // disabled the detour stays installed (cheap one-byte read).
        bool try_install_hook()
        {
            if (m_installed.load(std::memory_order_acquire)) return true;

            if (!NativeBinding::hasSetPresence())
            {
                // NativeBinding hasn't resolved yet (or the RVA was
                // wrong and the pointer stayed null).  Caller will
                // retry on the next poll tick.
                return false;
            }

            const uintptr_t target = NativeBinding::setPresenceAddress();

            m_trampoline = 0;
            m_detour = std::make_unique<PLH::x64Detour>(
                static_cast<uint64_t>(target),
                reinterpret_cast<uint64_t>(&GameMode::detour),
                &m_trampoline);

            if (!m_detour->hook())
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[Horse.GameMode] x64Detour::hook() failed on "
                        "SetPresence (target=0x{:X}). The 'Auto "
                        "disable online' gate will never engage.\n"),
                    target);
                m_detour.reset();
                return false;
            }

            m_installed.store(true, std::memory_order_release);
            RC::Output::send<RC::LogLevel::Default>(
                STR("[Horse.GameMode] SetPresence hook installed "
                    "(target=0x{:X}, trampoline=0x{:X})\n"),
                target, static_cast<uintptr_t>(m_trampoline));
            return true;
        }

        // Tear down the detour for a clean dllmain dtor.  Idempotent.
        void uninstall_hook()
        {
            if (!m_installed.exchange(false)) return;
            if (m_detour)
            {
                m_detour->unHook();
                m_detour.reset();
            }
            m_trampoline = 0;
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[Horse.GameMode] SetPresence hook uninstalled\n"));
        }

        bool hook_installed() const noexcept
        {
            return m_installed.load(std::memory_order_acquire);
        }

        // Latest observed presence value, or `Unknown` if the hook has
        // not fired yet this session.
        GamePresence current_presence() const noexcept
        {
            return static_cast<GamePresence>(
                m_presence.load(std::memory_order_acquire));
        }

        // -----------------------------------------------------------
        // Pure scene-classifier predicates.  No user-toggle inputs —
        // these answer "what kind of scene are we in?" only.  The
        // user's "Auto disable online" toggle is layered on top via
        // `should_force_disable_features()` below.
        // -----------------------------------------------------------

        // True iff the current scene is a PvP online match (Ranked
        // or Casual).  Versus (offline 2P) and Tournament (offline
        // bracket) are NOT included — only the modes that involve a
        // remote opponent.  Returns false for `Unknown` so a
        // never-installed hook leaves the gate OFF (fail-open) rather
        // than locking the user out of features they might want.
        bool is_in_pvp_match() const noexcept
        {
            const auto p = current_presence();
            return p == GamePresence::RankMatch
                || p == GamePresence::CasualMatch;
        }

        // -----------------------------------------------------------
        // Force-disable predicate — the actual gate the rest of the
        // mod consults.  Returns true when the user's "Auto disable
        // online" toggle is on AND we're in a PvP online match.
        //
        // The set of features dllmain.cpp force-disables when this
        // returns true:
        //   - Lock camera position (Camera tab)
        //   - Free-fly camera (Camera tab, F7)
        //   - Freeze frame (Time tab, F6)
        //   - Slow motion (Time tab)
        //
        // For each, dllmain wraps the UI checkbox in
        // ImGui::BeginDisabled() and force-calls disable() once per
        // tick to prevent the user from re-enabling.  See the
        // "force-disable" block in on_cockpit_update_pre.
        //
        // Performance: two atomic_loads + an enum compare.  Cheap to
        // call on every cockpit tick + every UI render frame.
        // -----------------------------------------------------------
        bool should_force_disable_features() const noexcept
        {
            return m_auto_disable_online.load(std::memory_order_acquire)
                && is_in_pvp_match();
        }

        // -------- Settings (UI-driven) --------------------------------
        // The "Auto disable online" master toggle.  Default ON.
        // Persisted via dllmain's ModSettings under the key
        // `gamemode_auto_disable_online`.
        void set_auto_disable_online(bool v) noexcept
        {
            m_auto_disable_online.store(v, std::memory_order_release);
        }
        bool auto_disable_online() const noexcept
        {
            return m_auto_disable_online.load(std::memory_order_acquire);
        }

    private:
        GameMode() = default;
        ~GameMode() { uninstall_hook(); }
        GameMode(const GameMode&) = delete;
        GameMode& operator=(const GameMode&) = delete;

        // ---- The detour -------------------------------------------
        //
        // Same signature as ULuxUIGamePresenceUtil_SetPresence: a
        // single byte parameter (the new ELuxGamePresence enum value).
        // Microsoft x64 ABI passes the byte in CL (low byte of RCX),
        // zero-extended.
        //
        // Just record the value, log on transitions, then forward to
        // the trampoline so the engine's own presence-string-update
        // path runs unchanged.  We must call the trampoline even
        // when the value matches the cached one — the engine's logic
        // there is what publishes the string to Steam / Discord, and
        // skipping it would break rich presence.
        static void __fastcall detour(uint8_t InPresence)
        {
            using Fn = void(__fastcall*)(uint8_t);
            Fn orig = reinterpret_cast<Fn>(instance().m_trampoline);

            instance().on_set_presence(InPresence);

            if (orig) orig(InPresence);
        }

        // Called from the detour with the raw byte param.  Updates
        // the cached presence atom and emits a one-line transition log.
        void on_set_presence(uint8_t raw) noexcept
        {
            const uint8_t prev = m_presence.exchange(
                raw, std::memory_order_acq_rel);
            if (prev == raw) return;

            // First-fire and transition logging at Default level so the
            // user can confirm the gate is responding to scene changes
            // without enabling Verbose.  One log per transition; any
            // SetPresence call with the same value as last time (rare
            // but possible during init churn) is silent.
            const auto from = static_cast<GamePresence>(prev);
            const auto to   = static_cast<GamePresence>(raw);
            RC::Output::send<RC::LogLevel::Default>(
                STR("[Horse.GameMode] presence {} -> {} "
                    "(force_disable={})\n"),
                presence_name(from),
                presence_name(to),
                should_force_disable_features() ? STR("YES") : STR("no"));
        }

        // Latest presence observed.  Atomic for cross-thread reads.
        // Initialised to `Unknown` (0xFF) so accessors return a
        // distinguishable "haven't seen anything yet" value rather
        // than the legitimate value 0 (MainMenu).
        std::atomic<uint8_t> m_presence{
            static_cast<uint8_t>(GamePresence::Unknown)};

        // PolyHook bookkeeping.  Mirrors LuxBattleLauncherStartHook.
        std::unique_ptr<PLH::x64Detour> m_detour;
        uint64_t                        m_trampoline {0};
        std::atomic<bool>               m_installed  {false};

        // User-facing setting — the "Auto disable online" toggle in
        // the General tab.  Default ON.  When ON, RankMatch /
        // CasualMatch force-disables the four gated features (lock
        // camera, free-fly camera, freeze frame, slow motion).
        std::atomic<bool> m_auto_disable_online {true};
    };

} // namespace Horse
