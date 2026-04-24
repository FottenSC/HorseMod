// ============================================================================
// Horse::GameImGui::GamepadInput — XInput polling + ImGui navigation feed.
//
// What this does
// --------------
// 1. Polls XInput controller 0 once per frame (from inside the Present
//    detour, on the render thread).
// 2. Feeds every pad button / stick direction into the ImGui context
//    via io.AddKeyEvent / AddKeyAnalogEvent, using the ImGuiKey_Gamepad*
//    enum.  Combined with ImGuiConfigFlags_NavEnableGamepad (set in
//    DX11State::ensure_initialised), this gives full gamepad navigation
//    of the overlay — A accepts, B cancels, DPad / left stick moves
//    focus between widgets.
// 3. Edge-detects the BACK (Select / View) button and calls a caller-
//    supplied toggle callback when the button is pressed.  Used by
//    GameImGui::initialize to wire Select → set_visible(!visible).
//
// Why roll our own instead of using imgui_impl_win32's gamepad path
// -----------------------------------------------------------------
// UE4SS's ImGui build passes IMGUI_IMPL_WIN32_DISABLE_GAMEPAD (see
// RE-UE4SS/deps/third/imgui/CMakeLists.txt line 77), so the built-in
// backend code-path is compiled out.  We re-implement the equivalent
// behaviour here with a direct XInput poll.  That also lets us add
// the edge-triggered toggle on BACK without monkey-patching the
// backend.
//
// Why polling from the render thread is OK
// ----------------------------------------
// XInputGetState is thread-safe, fast (microseconds), and has no
// kernel transitions on current Windows builds.  The fighting-game
// input thread also polls XInput (that's how SC6 reads the pad),
// but XInput is a read-only shared-resource API — multiple callers
// are expected and supported.
//
// Input consumption semantics
// ---------------------------
// We cannot "consume" an XInput button press the way WndProcHook can
// swallow a keyboard message — SC6 polls XInput directly and always
// sees button state.  Pressing Select therefore both toggles our
// overlay AND triggers whatever SC6 does with Select (typically
// nothing outside of character-select screens — the in-match use is
// Back button = no-op).  If this becomes a problem on some screen,
// we can expose a config flag to disable the Select binding.
// ============================================================================

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <Xinput.h>
// Match horselib/FreeCamera.hpp's linkage style: keep the lib
// dependency co-located with the code that needs it so CMakeLists
// doesn't have to know about every horselib component's deps.
#pragma comment(lib, "Xinput.lib")

// XInputHook.hpp provides get_state_raw() which bypasses our own
// XInputGetState detour — the detour zeros out controller state while
// the overlay is visible to stop the game from seeing our nav inputs
// as gameplay inputs.  Our UI code needs the REAL state, so we read
// through the trampoline directly.
#include "XInputHook.hpp"

#include <imgui.h>

#include <atomic>
#include <cstdint>
#include <functional>

namespace Horse::GameImGui
{
    class GamepadInput
    {
    public:
        static GamepadInput& instance()
        {
            static GamepadInput s;
            return s;
        }

        // Poll XInput-0 and edge-detect the BACK button.  Does NOT
        // feed any state into ImGui — that's the separate
        // feed_nav_to_imgui() below.  Splitting lets the caller poll
        // the BACK-button toggle EVERY frame (so the overlay can
        // always be reopened with Select) while only feeding the
        // nav state when the overlay is actually visible.
        //
        // `on_back_pressed` fires on the RISING edge of BACK (one
        // callback per physical press, regardless of how long the
        // button is held).  Pass a no-op lambda to disable.
        //
        // Connection state transitions are logged once each way.
        void poll_and_detect_back(const std::function<void()>& on_back_pressed)
        {
            XINPUT_STATE state{};
            // Read via the trampoline so we see the real state even
            // when the overlay is visible and our detour is zeroing
            // state for the game's callers.
            const DWORD r = XInputHook::instance().get_state_raw(0, &state);
            const bool connected = (r == ERROR_SUCCESS);

            // Cache for feed_nav_to_imgui; we stamp connected=false
            // on disconnect so nav doesn't read stale sticks.
            m_last_state        = state;
            m_last_state_valid  = connected;

            // Connection edge-trigger logging — so the log shows us
            // the exact moment a controller disappears or reappears
            // from XInput's POV.
            const bool was_connected = m_connected.exchange(
                connected, std::memory_order_relaxed);
            if (connected != was_connected)
            {
                RC::Output::send<RC::LogLevel::Default>(
                    STR("[GameImGui.Gamepad] XInput-0 {} (hr=0x{:08X}, "
                        "packet={})\n"),
                    connected ? STR("connected") : STR("disconnected"),
                    static_cast<uint32_t>(r),
                    state.dwPacketNumber);
            }

            if (!connected)
            {
                // Clear edge state so re-plug doesn't fire a phantom
                // press from stale data.
                m_prev_back = false;
                return;
            }

            // Periodic state dump — once every ~2 seconds when any
            // buttons are held, so we can confirm XInput is seeing
            // live inputs without spamming the log during idle.
            {
                static uint32_t s_log_tick = 0;
                const WORD any_button = state.Gamepad.wButtons;
                if ((++s_log_tick & 0x7F) == 0 && any_button != 0)
                {
                    RC::Output::send<RC::LogLevel::Verbose>(
                        STR("[GameImGui.Gamepad] buttons=0x{:04X} "
                            "LT={} RT={} LX={} LY={}\n"),
                        static_cast<uint32_t>(any_button),
                        static_cast<uint32_t>(state.Gamepad.bLeftTrigger),
                        static_cast<uint32_t>(state.Gamepad.bRightTrigger),
                        static_cast<int32_t>(state.Gamepad.sThumbLX),
                        static_cast<int32_t>(state.Gamepad.sThumbLY));
                }
            }

            // ---- Edge-trigger on BACK (Select / View) ------------
            const WORD btn = state.Gamepad.wButtons;
            const bool back_now = (btn & XINPUT_GAMEPAD_BACK) != 0;
            if (back_now && !m_prev_back)
            {
                if (on_back_pressed) on_back_pressed();
            }
            m_prev_back = back_now;
        }

        // Feed the cached controller state into the ImGui context for
        // gamepad nav.  ONLY call this when the overlay is visible and
        // inside an ImGui frame — otherwise the AddKeyEvent events
        // would queue into a context that never NewFrame's them out
        // (leaking events forever).
        //
        // Must be called AFTER poll_and_detect_back() in the same
        // frame, so the cached state is fresh.
        void feed_nav_to_imgui(ImGuiContext* target_ctx)
        {
            if (!target_ctx)        return;
            if (!m_last_state_valid) return;

            const XINPUT_GAMEPAD& pad = m_last_state.Gamepad;
            const WORD btn            = pad.wButtons;

            ImGuiContext* prev = ImGui::GetCurrentContext();
            ImGui::SetCurrentContext(target_ctx);
            ImGuiIO& io = ImGui::GetIO();

            auto button = [&](ImGuiKey key, WORD mask) {
                io.AddKeyEvent(key, (btn & mask) != 0);
            };

            button(ImGuiKey_GamepadFaceDown,  XINPUT_GAMEPAD_A);
            button(ImGuiKey_GamepadFaceRight, XINPUT_GAMEPAD_B);
            button(ImGuiKey_GamepadFaceLeft,  XINPUT_GAMEPAD_X);
            button(ImGuiKey_GamepadFaceUp,    XINPUT_GAMEPAD_Y);
            button(ImGuiKey_GamepadDpadDown,  XINPUT_GAMEPAD_DPAD_DOWN);
            button(ImGuiKey_GamepadDpadUp,    XINPUT_GAMEPAD_DPAD_UP);
            button(ImGuiKey_GamepadDpadLeft,  XINPUT_GAMEPAD_DPAD_LEFT);
            button(ImGuiKey_GamepadDpadRight, XINPUT_GAMEPAD_DPAD_RIGHT);
            button(ImGuiKey_GamepadL1,        XINPUT_GAMEPAD_LEFT_SHOULDER);
            button(ImGuiKey_GamepadR1,        XINPUT_GAMEPAD_RIGHT_SHOULDER);
            button(ImGuiKey_GamepadL3,        XINPUT_GAMEPAD_LEFT_THUMB);
            button(ImGuiKey_GamepadR3,        XINPUT_GAMEPAD_RIGHT_THUMB);
            button(ImGuiKey_GamepadStart,     XINPUT_GAMEPAD_START);
            button(ImGuiKey_GamepadBack,      XINPUT_GAMEPAD_BACK);

            const float lt = pad.bLeftTrigger  / 255.0f;
            const float rt = pad.bRightTrigger / 255.0f;
            io.AddKeyAnalogEvent(ImGuiKey_GamepadL2, lt > 0.1f, lt);
            io.AddKeyAnalogEvent(ImGuiKey_GamepadR2, rt > 0.1f, rt);

            auto normalise = [](SHORT raw, SHORT deadzone) -> float {
                if (raw >  deadzone)
                    return float(raw - deadzone) / float(32767 - deadzone);
                if (raw < -deadzone)
                    return float(raw + deadzone) / float(32768 - deadzone);
                return 0.0f;
            };

            const float lx = normalise(pad.sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
            const float ly = normalise(pad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
            io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickLeft,  lx < 0.0f, -lx);
            io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickRight, lx > 0.0f,  lx);
            io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickUp,    ly > 0.0f,  ly);
            io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickDown,  ly < 0.0f, -ly);

            const float rx = normalise(pad.sThumbRX, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
            const float ry = normalise(pad.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
            io.AddKeyAnalogEvent(ImGuiKey_GamepadRStickLeft,  rx < 0.0f, -rx);
            io.AddKeyAnalogEvent(ImGuiKey_GamepadRStickRight, rx > 0.0f,  rx);
            io.AddKeyAnalogEvent(ImGuiKey_GamepadRStickUp,    ry > 0.0f,  ry);
            io.AddKeyAnalogEvent(ImGuiKey_GamepadRStickDown,  ry < 0.0f, -ry);

            ImGui::SetCurrentContext(prev);
        }

        bool connected() const noexcept
        {
            return m_connected.load(std::memory_order_relaxed);
        }

    private:
        GamepadInput() = default;
        ~GamepadInput() = default;
        GamepadInput(const GamepadInput&)            = delete;
        GamepadInput& operator=(const GamepadInput&) = delete;

        bool              m_prev_back{false};
        std::atomic<bool> m_connected{false};

        // Cached XInput state from the most recent poll_and_detect_back
        // call.  Used by feed_nav_to_imgui so we don't call
        // XInputGetState twice per frame.  Valid flag mirrors the
        // connected state from the last poll.
        XINPUT_STATE m_last_state{};
        bool         m_last_state_valid{false};
    };
}
