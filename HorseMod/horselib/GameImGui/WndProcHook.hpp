// ============================================================================
// Horse::GameImGui::WndProcHook — SetWindowLongPtr chain for the game
// window, routing input into ImGui while remaining a polite neighbour
// to Steam and the game.
//
// The rules this hook follows
// ---------------------------
// 1. EVERY message is eventually forwarded to the original WndProc via
//    CallWindowProc.  We never swallow a message permanently.  The
//    game's input stays functional whether the overlay is open or not.
//
// 2. When ImGui declares WantCaptureMouse / WantCaptureKeyboard (i.e.
//    the user is actively interacting with an ImGui widget), we still
//    forward to ImGui first for its own state update, then we
//    conditionally skip forwarding pointer / keyboard messages to the
//    game so clicks on ImGui buttons don't also activate in-game
//    bindings.  We continue to forward *non*-pointer/keyboard messages
//    (WM_SIZE, WM_ACTIVATE, WM_SYSCOMMAND, …) unchanged.
//
// 3. Shift+Tab is ALWAYS forwarded to the game regardless of ImGui
//    focus.  Steam's input hook needs to see it to open the overlay.
//    Same treatment for the other Steam default hotkeys (if a user
//    ever rebinds them we don't try to enumerate; Shift+Tab is the
//    overwhelmingly common default).
//
// 4. We never call SetCapture, never change cursor state, never
//    reparent the window.  Any cursor visibility changes go through
//    ImGui's own code paths, which use ShowCursor's reference counter
//    and therefore compose with Steam's own cursor management.
//
// Install / uninstall
// -------------------
// install(hwnd) is called once after the game window exists (first
// hooked Present is a good trigger — we know the HWND from the swap
// chain desc).  uninstall() puts the original WndProc back via
// SetWindowLongPtr; safe even if install failed.
//
// Toggle behaviour
// ---------------
// Horse::GameImGui exposes set_visible(bool) so HorseMod can drive
// a show/hide toggle via its existing hotkey framework.  When hidden:
//
//   * frame callbacks still run (they can decide to early-out if the
//     overlay is hidden, or keep drawing non-interactive HUD elements);
//   * WndProcHook short-circuits WantCaptureMouse / WantCaptureKeyboard
//     to false, so no input is ever eaten.
// ============================================================================

#pragma once

#include "DX11State.hpp"
#include "PresentHook.hpp"

#include <Windows.h>
#include <imgui.h>

#include <DynamicOutput/DynamicOutput.hpp>

#include <atomic>

// Forward declaration — ImGui's Win32 backend exposes this.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace Horse::GameImGui
{
    class WndProcHook
    {
    public:
        static WndProcHook& instance()
        {
            static WndProcHook s;
            return s;
        }

        bool install(HWND hwnd)
        {
            if (m_installed.load(std::memory_order_acquire)) return true;
            if (!hwnd) return false;

            m_hwnd = hwnd;
            m_original = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
                hwnd, GWLP_WNDPROC,
                reinterpret_cast<LONG_PTR>(&WndProcHook::wndproc_thunk)));
            if (!m_original)
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[GameImGui] SetWindowLongPtrW(GWLP_WNDPROC) failed err={}\n"),
                    GetLastError());
                return false;
            }
            m_installed.store(true, std::memory_order_release);
            RC::Output::send<RC::LogLevel::Default>(
                STR("[GameImGui] WndProcHook installed on HWND={}\n"),
                reinterpret_cast<uintptr_t>(hwnd));
            return true;
        }

        void uninstall()
        {
            if (!m_installed.exchange(false)) return;
            if (m_hwnd && m_original)
            {
                // Only swap back if our thunk is still the active proc.
                // If something else chained over us, leaving m_original
                // in place is the safer option.
                auto current = reinterpret_cast<WNDPROC>(
                    GetWindowLongPtrW(m_hwnd, GWLP_WNDPROC));
                if (current == &WndProcHook::wndproc_thunk)
                {
                    SetWindowLongPtrW(m_hwnd, GWLP_WNDPROC,
                                       reinterpret_cast<LONG_PTR>(m_original));
                }
            }
            m_hwnd     = nullptr;
            m_original = nullptr;
        }

        // Driven by the mod's visibility toggle.  When false, the
        // overlay is hidden: PresentHook skips the entire ImGui frame
        // (no window drawn, no rendering cost) AND WndProcHook stops
        // consuming input even if ImGui would otherwise claim it.
        //
        // On a false→true transition (overlay becoming visible), we
        // raise g_overlay_just_shown so the mod's render_tab_impl can
        // re-bootstrap nav focus on the next frame.  This needs to
        // happen HERE rather than in render_tab_impl itself because
        // render_tab_impl only runs while visible — a static "prev"
        // inside it can't observe the hide→show edge.
        //
        // The flags live at namespace scope (in DX11State.hpp) so
        // PresentHook can read them without a circular include.
        void set_visible(bool v) noexcept
        {
            const bool was_visible = g_overlay_visible.exchange(
                v, std::memory_order_relaxed);
            if (v && !was_visible)
            {
                g_overlay_just_shown.store(true, std::memory_order_relaxed);
                // Bring the system mouse cursor back so the user can
                // click overlay widgets — SC6 normally hides it for
                // the duration of a match.  See
                // set_overlay_cursor_visible doc in DX11State.hpp.
                set_overlay_cursor_visible(true);
            }
            else if (!v && was_visible)
            {
                // Restore game's cursor-hidden state.
                set_overlay_cursor_visible(false);
            }
        }
        bool visible() const noexcept
        {
            return g_overlay_visible.load(std::memory_order_relaxed);
        }

    private:
        WndProcHook() = default;
        ~WndProcHook() { uninstall(); }
        WndProcHook(const WndProcHook&)            = delete;
        WndProcHook& operator=(const WndProcHook&) = delete;

        static LRESULT CALLBACK wndproc_thunk(HWND hwnd, UINT msg,
                                              WPARAM wparam, LPARAM lparam)
        {
            return instance().on_message(hwnd, msg, wparam, lparam);
        }

        LRESULT on_message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
        {
            // NOTE: F2 toggling is NOT handled here.
            //
            // SC6 registers its input devices with RawInput's
            // RIDEV_NOLEGACY flag, which suppresses WM_KEYDOWN /
            // WM_SYSKEYDOWN messages for the game HWND.  That means
            // we will NEVER see F2's WM_KEYDOWN from this WndProc,
            // and trying to toggle here is a dead code path.  The
            // actual F2 binding is installed by HorseMod's
            // dllmain.cpp via UE4SS's register_keydown_event(), which
            // uses a WH_KEYBOARD_LL low-level hook underneath — the
            // only reliable way to catch keys past RIDEV_NOLEGACY.
            // See horselib/FreeCamera.hpp's LowLevelKeyInput comment
            // for the full explanation.

            // 1. Always feed to ImGui first so its state tracks
            //    everything (hover highlights, cursor position, etc.)
            //    — safe no-op until DX11State is ready.  Bind OUR
            //    context before calling the Win32 backend; UE4SS's
            //    own ImGui context may be the current one if UE4SS
            //    also set one up.
            auto& state = DX11State::instance();
            if (state.ready() && state.imgui_context())
            {
                ImGuiContext* prev = ImGui::GetCurrentContext();
                state.bind_imgui_context();
                ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam);
                ImGui::SetCurrentContext(prev);
            }

            // 2. If the overlay is hidden, pass through unconditionally.
            if (!g_overlay_visible.load(std::memory_order_relaxed))
            {
                return forward(hwnd, msg, wparam, lparam);
            }

            // 3. ALWAYS forward Shift+Tab so Steam overlay can toggle.
            //    Detected as Tab keydown while either shift is held.
            if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) &&
                wparam == VK_TAB &&
                (GetAsyncKeyState(VK_SHIFT) & 0x8000))
            {
                return forward(hwnd, msg, wparam, lparam);
            }

            // 4. Conditionally swallow input when ImGui wants it.
            auto& hook = PresentHook::instance();
            const bool want_mouse    = hook.imgui_wants_mouse();
            const bool want_keyboard = hook.imgui_wants_keyboard();

            if (want_mouse && is_mouse_message(msg))
            {
                return 0;
            }
            if (want_keyboard && is_keyboard_message(msg))
            {
                return 0;
            }

            return forward(hwnd, msg, wparam, lparam);
        }

        LRESULT forward(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) const
        {
            return m_original
                ? CallWindowProcW(m_original, hwnd, msg, wparam, lparam)
                : DefWindowProcW(hwnd, msg, wparam, lparam);
        }

        static bool is_mouse_message(UINT msg) noexcept
        {
            switch (msg)
            {
                case WM_MOUSEMOVE:
                case WM_LBUTTONDOWN:  case WM_LBUTTONUP:  case WM_LBUTTONDBLCLK:
                case WM_RBUTTONDOWN:  case WM_RBUTTONUP:  case WM_RBUTTONDBLCLK:
                case WM_MBUTTONDOWN:  case WM_MBUTTONUP:  case WM_MBUTTONDBLCLK:
                case WM_XBUTTONDOWN:  case WM_XBUTTONUP:  case WM_XBUTTONDBLCLK:
                case WM_MOUSEWHEEL:   case WM_MOUSEHWHEEL:
                    return true;
                default:
                    return false;
            }
        }

        static bool is_keyboard_message(UINT msg) noexcept
        {
            switch (msg)
            {
                case WM_KEYDOWN: case WM_KEYUP:
                case WM_SYSKEYDOWN: case WM_SYSKEYUP:
                case WM_CHAR:    case WM_SYSCHAR:
                case WM_UNICHAR:
                    return true;
                default:
                    return false;
            }
        }

        HWND              m_hwnd     {nullptr};
        WNDPROC           m_original {nullptr};
        std::atomic<bool> m_installed{false};
        // m_visible removed — visibility now lives at namespace scope
        // as Horse::GameImGui::g_overlay_visible (see DX11State.hpp)
        // so PresentHook can read it without a circular include.
    };
}
