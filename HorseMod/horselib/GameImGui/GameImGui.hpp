// ============================================================================
// Horse::GameImGui — public API for the in-process ImGui overlay.
//
// Usage from a UE4SS C++ mod
// --------------------------
//
//     class MyMod : public CppUserModBase
//     {
//         uint64_t m_cb_token = 0;
//
//         auto on_unreal_init() -> void override
//         {
//             // Defer to the first game-thread tick so Steam has
//             // already installed its hooks.  Any post-init callback
//             // that runs once per frame works.
//             register_first_tick([this]{ Horse::GameImGui::initialize(); });
//
//             m_cb_token = Horse::GameImGui::register_tab(
//                 "MyMod", [this]{ this->render_tab_impl(); });
//         }
//
//         auto on_program_exit() -> void override
//         {
//             Horse::GameImGui::unregister_tab(m_cb_token);
//             Horse::GameImGui::shutdown();
//         }
//
//         void render_tab_impl()  // existing UE4SS ImGui tab body — unchanged
//         {
//             if (ImGui::Begin("MyMod"))
//             {
//                 ImGui::Text("hello");
//             }
//             ImGui::End();
//         }
//     };
//
// Visibility toggle
// -----------------
// `set_visible(false)` makes every tab invisible AND disables input
// capture (mouse/keyboard passes through to the game even if an ImGui
// widget would otherwise claim focus).  Callbacks still run — if your
// HUD draws even when "hidden", guard against that inside the callback.
//
// Thread-safety
// -------------
//   initialize() / shutdown() / set_visible() — call from the game
//      thread once at the obvious lifecycle points.
//   register_tab / unregister_tab — any thread; implemented lock-free
//      via copy-on-write std::shared_ptr<const vector<…>> snapshots.
//   The registered callback itself runs on the game's render thread
//      (on SC6 this is the game thread; Present() is synchronous).
//
// This file only pulls in the other GameImGui headers; it does not
// instantiate singletons until initialize() is called.
// ============================================================================

#pragma once

#include "DX11State.hpp"
#include "PresentHook.hpp"
#include "WndProcHook.hpp"
#include "XInputHook.hpp"

#include <DynamicOutput/DynamicOutput.hpp>

#include <cstdint>
#include <functional>

namespace Horse::GameImGui
{
    // Tab / frame-callback signature.  Called once per Present between
    // ImGui::NewFrame() and ImGui::Render().  Do your ImGui:: calls
    // here.  The `name` argument to register_tab is only used for
    // diagnostic logging; it does NOT implicitly wrap the callback in
    // an ImGui::Begin/End.
    using TabCallback = std::function<void()>;

    // ------------------------------------------------------------------
    // Lifecycle.  initialize() is idempotent; subsequent calls are
    // treated as success.  It is safe to call before Steam's overlay
    // hook is installed — our install deferred path re-tries on each
    // cockpit tick via a guard flag inside PresentHook.  The simplest
    // correct usage is to call it once on the first game-thread tick
    // and once more on on_unreal_init.
    // ------------------------------------------------------------------

    inline bool initialize()
    {
        // Kill-switch for diagnosis.  If a file named
        //   disable_gameimgui.txt
        // exists NEXT TO the game's .exe (so typically at
        //   <GameDir>/Binaries/Win64/disable_gameimgui.txt )
        // we skip all of the overlay install steps.  Useful for
        // A/B-testing whether the Present hook / WndProc subclass /
        // ImGui backends are responsible for some game-side
        // regression.  HorseMod still loads and every horselib
        // feature (KHit walker, SpeedControl, CamLock, etc.) still
        // works — you just don't get an in-game ImGui panel and
        // F2 / Back do nothing.
        //
        // Why a file and not an env var: Steam restarts the game via
        // its own launcher whenever the .exe is started outside of
        // Steam, and that restart loses env vars set by the parent
        // shell.  A file on disk is immune to that.  Create the
        // file via `type nul > disable_gameimgui.txt` or just drop
        // an empty file next to SoulcaliburVI.exe.
        {
            // Use GetModuleFileName on the running .exe to find the
            // game directory regardless of where the DLL lives.
            wchar_t exe_path[MAX_PATH]{};
            DWORD plen = GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
            if (plen > 0 && plen < MAX_PATH)
            {
                // Strip filename, append "disable_gameimgui.txt".
                wchar_t* slash = wcsrchr(exe_path, L'\\');
                if (slash)
                {
                    slash[1] = L'\0';
                    wchar_t check_path[MAX_PATH]{};
                    wcscpy_s(check_path, MAX_PATH, exe_path);
                    wcscat_s(check_path, MAX_PATH, L"disable_gameimgui.txt");
                    DWORD attr = GetFileAttributesW(check_path);
                    if (attr != INVALID_FILE_ATTRIBUTES &&
                        !(attr & FILE_ATTRIBUTE_DIRECTORY))
                    {
                        RC::Output::send<RC::LogLevel::Default>(
                            STR("[GameImGui] disable_gameimgui.txt found next "
                                "to the game .exe — SKIPPING all overlay "
                                "install.  Delete the file to re-enable.\n"));
                        return false;
                    }
                }
            }
        }

        auto& hook = PresentHook::instance();
        if (!hook.install())
        {
            return false;
        }

        // Install the XInput gate — when the overlay is visible,
        // XInputGetState returns zeroed state to everyone except our
        // own UI poll, which reads the trampoline directly.  Best-
        // effort: if the hook fails, UI still works but controller
        // inputs will "leak" into gameplay while the overlay is open.
        XInputHook::instance().install();

        // We can't install WndProcHook yet — we don't have the game's
        // HWND until after the first Present() fires and DX11State
        // captures it.  Instead we register a once-only frame
        // callback that installs the WndProc hook the moment HWND is
        // known and then unregisters itself.
        //
        // This callback runs inside the Present detour between
        // NewFrame and Render, which is exactly when DX11State::ready
        // flips to true, so the HWND is guaranteed valid.
        static uint64_t wndproc_bootstrap_token = 0;
        wndproc_bootstrap_token = hook.register_frame_callback([]{
            auto& state = DX11State::instance();
            if (!state.ready() || !state.hwnd()) return;
            auto& wp = WndProcHook::instance();
            if (wp.install(state.hwnd()))
            {
                PresentHook::instance().unregister_frame_callback(
                    wndproc_bootstrap_token);
            }
        });

        // Wire the gamepad BACK (Select / View) button to a visibility
        // toggle.  Rising-edge only so holding Select doesn't rapidly
        // flip the overlay.  Runs on the render thread — set_visible
        // is atomic, no locking needed.
        hook.set_on_gamepad_back([]{
            WndProcHook::instance().set_visible(!WndProcHook::instance().visible());
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[GameImGui] gamepad BACK pressed — overlay {}\n"),
                WndProcHook::instance().visible() ? STR("SHOWN") : STR("HIDDEN"));
        });

        RC::Output::send<RC::LogLevel::Default>(
            STR("[GameImGui] initialize() complete (WndProcHook will "
                "attach on first Present)\n"));
        return true;
    }

    inline void shutdown()
    {
        XInputHook::instance().uninstall();
        WndProcHook::instance().uninstall();
        PresentHook::instance().uninstall();
        DX11State::instance().shutdown();
        RC::Output::send<RC::LogLevel::Default>(
            STR("[GameImGui] shutdown() complete\n"));
    }

    // ------------------------------------------------------------------
    // Tab registration.  `name` is free-form (wide string to match the
    // rest of UE4SS's APIs — it's the same StringViewType used by
    // CppUserModBase::register_tab); we log it at register / unregister.
    // Returns a token; pass it to unregister_tab() to remove the
    // callback.
    // ------------------------------------------------------------------

    inline uint64_t register_tab(const wchar_t* name, TabCallback cb)
    {
        const uint64_t token = PresentHook::instance()
            .register_frame_callback(std::move(cb));
        RC::Output::send<RC::LogLevel::Verbose>(
            STR("[GameImGui] registered tab '{}' token={}\n"),
            name ? name : L"(unnamed)", token);
        return token;
    }

    inline void unregister_tab(uint64_t token)
    {
        if (!token) return;
        PresentHook::instance().unregister_frame_callback(token);
        RC::Output::send<RC::LogLevel::Verbose>(
            STR("[GameImGui] unregistered tab token={}\n"), token);
    }

    // ------------------------------------------------------------------
    // Visibility toggle.  When false, input is fully passed through
    // and the overlay is effectively invisible even if callbacks are
    // still firing (they are — it's up to the callback to early-out
    // if it doesn't want to draw while hidden).
    // ------------------------------------------------------------------

    inline void set_visible(bool v) noexcept
    {
        WndProcHook::instance().set_visible(v);
    }

    inline bool visible() noexcept
    {
        return WndProcHook::instance().visible();
    }

    // ------------------------------------------------------------------
    // Diagnostic accessors exposed for the mod's own debug UI.
    // ------------------------------------------------------------------

    inline uint64_t present_count() noexcept
    {
        return PresentHook::instance().present_count();
    }
}
