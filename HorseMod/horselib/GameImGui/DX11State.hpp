// ============================================================================
// Horse::GameImGui::DX11State — shared DX11 resources for the in-process
// ImGui overlay.
//
// What this owns
// --------------
// A small bundle of *non-owning* references to the game's existing DX11
// pipeline, captured on the first hooked Present():
//
//   * ID3D11Device*        — AddRef'd
//   * ID3D11DeviceContext* — AddRef'd
//   * ID3D11RenderTargetView* for the back buffer 0 of the game's swap
//     chain — we own this one (created via CreateRenderTargetView).
//
// And one HWND (the game's main window).
//
// Lifecycle
// ---------
//   ensure_initialised(swap_chain)  — call on every hooked Present(),
//     no-op after first success.
//   release_rtv()                   — call from the hooked ResizeBuffers()
//     before chaining to the trampoline.  The RTV holds a reference to
//     the back buffer; if we don't release it, ResizeBuffers fails and
//     the game's resize path crashes.
//   shutdown()                      — call from Horse::GameImGui::shutdown()
//     on mod unload.  Releases everything, shuts down ImGui DX11+Win32
//     backends.
//
// Thread-safety
// -------------
// Present and ResizeBuffers always run on the game's render thread
// (which on SC6 is the game thread — single-threaded presentation).
// All state here is therefore effectively single-producer, single-
// consumer.  No locks.
//
// Error handling
// --------------
// Every D3D/DXGI failure path logs via UE4SS Output::send and degrades
// gracefully (we skip this frame's ImGui and chain to the trampoline
// anyway, so the game keeps rendering).  We never throw from a hook.
// ============================================================================

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>  // Microsoft::WRL::ComPtr

#include <DynamicOutput/DynamicOutput.hpp>

#include <imgui.h>
#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_win32.h>

#include <atomic>
#include <cstdint>

namespace Horse::GameImGui
{
    using Microsoft::WRL::ComPtr;

    // ------------------------------------------------------------------
    // Overlay-visibility flag.
    //
    // Owned by neither DX11State nor WndProcHook — it's a shared bit
    // that both PresentHook (on the render thread, to gate ImGui frame
    // rendering) and WndProcHook (on the UI thread, to gate input
    // consumption) need to read.  Placing it here in DX11State.hpp
    // avoids a circular include between PresentHook.hpp and
    // WndProcHook.hpp, which are otherwise independent.
    //
    // Written by WndProcHook::set_visible (driven by F2 and Back
    // button callbacks).  Read by PresentHook::on_present to decide
    // whether to run the ImGui frame, and by WndProcHook::on_message
    // to decide whether to consume input.
    //
    // Defaults to FALSE so the overlay is hidden on game startup —
    // the user opens it explicitly with F2 or Select/Back when they
    // want to see HorseMod's controls.  Keeps the first few seconds
    // of gameplay clean of the mod's UI chrome (menus, fight intros,
    // etc.).
    // ------------------------------------------------------------------
    inline std::atomic<bool> g_overlay_visible{false};

    // ------------------------------------------------------------------
    // Nav-bootstrap edge flag.  Set to true by WndProcHook::set_visible
    // whenever the overlay transitions from hidden → shown.  Consumed
    // by the mod's render_tab_impl on the next Present to trigger
    // SetNextWindowFocus() + SetKeyboardFocusHere() on the first
    // widget.
    //
    // Why at namespace scope rather than a local static in
    // render_tab_impl: render_tab_impl only runs WHILE the overlay is
    // visible (see PresentHook::on_present), so a local static can't
    // observe the hide→show transition — by the time render_tab_impl
    // runs again after a hide, "previous" and "current" are both
    // "visible" and the edge is invisible.  Making the write happen in
    // set_visible (which is called on the toggle edge itself, whether
    // hidden or visible) guarantees the flag is set correctly.
    //
    // Default false because the very first render after DLL load is
    // not a "just shown" event — that's handled by the existing
    // window-open behaviour.  WndProcHook sets it to true when a
    // transition is seen.
    // ------------------------------------------------------------------
    inline std::atomic<bool> g_overlay_just_shown{false};

    // ------------------------------------------------------------------
    // Mouse-cursor visibility helper.
    //
    // SC6 hides the system cursor during gameplay (typical fighting-game
    // behaviour: ShowCursor is dropped to a negative reference count at
    // startup so the cursor stays gone for the whole match).  When our
    // overlay opens, the user reasonably expects the cursor back so they
    // can click the UI with a mouse.
    //
    // Windows' ShowCursor(BOOL) is a per-process REFERENCE COUNTER:
    //   ShowCursor(TRUE)  -> ++count, returns new count
    //   ShowCursor(FALSE) -> --count, returns new count
    // Cursor is visible iff count >= 0.
    //
    // To keep the game's count balanced, we count exactly how many
    // TRUE-calls we made on the visible-edge and unwind that many
    // FALSE-calls on the hidden-edge.  s_raised holds the count.
    //
    // Called from WndProcHook::set_visible on every visibility flip
    // (F2 / Back).  Idempotent — repeated calls with the same v are
    // no-ops.
    // ------------------------------------------------------------------
    inline void set_overlay_cursor_visible(bool v)
    {
        // Static-local int is fine here: set_visible only fires from
        // the UI / input thread and isn't re-entered.  No locking
        // required.
        static int s_raised = 0;
        if (v && s_raised == 0)
        {
            int raised = 0;
            int result;
            do
            {
                result = ShowCursor(TRUE);
                ++raised;
            } while (result < 0);
            s_raised = raised;
        }
        else if (!v && s_raised > 0)
        {
            for (int i = 0; i < s_raised; ++i) ShowCursor(FALSE);
            s_raised = 0;
        }
    }

    // ------------------------------------------------------------------
    // DX11State is a process-wide singleton.  Access via instance().
    // ------------------------------------------------------------------
    class DX11State
    {
    public:
        static DX11State& instance()
        {
            static DX11State s;
            return s;
        }

        // ---- Lifecycle ----

        // Called from the hooked Present() every frame.  Returns false
        // if initialisation is incomplete (caller should skip ImGui
        // drawing this frame and go straight to the trampoline).
        bool ensure_initialised(IDXGISwapChain* swap_chain)
        {
            if (m_ready.load(std::memory_order_acquire))
            {
                return true;
            }
            if (!swap_chain)
            {
                return false;
            }

            // Query device + context.
            HRESULT hr = swap_chain->GetDevice(__uuidof(ID3D11Device),
                                               reinterpret_cast<void**>(m_device.GetAddressOf()));
            if (FAILED(hr) || !m_device)
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[GameImGui] swap_chain->GetDevice failed hr=0x{:08X}\n"),
                    static_cast<uint32_t>(hr));
                return false;
            }
            m_device->GetImmediateContext(m_context.GetAddressOf());
            if (!m_context)
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[GameImGui] GetImmediateContext returned null\n"));
                m_device.Reset();
                return false;
            }

            // Look up the HWND from the swap chain.
            DXGI_SWAP_CHAIN_DESC desc{};
            if (FAILED(swap_chain->GetDesc(&desc)) || !desc.OutputWindow)
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[GameImGui] swap_chain->GetDesc gave no HWND\n"));
                m_device.Reset();
                m_context.Reset();
                return false;
            }
            m_hwnd = desc.OutputWindow;

            if (!create_rtv(swap_chain))
            {
                m_device.Reset();
                m_context.Reset();
                m_hwnd = nullptr;
                return false;
            }

            // Always create our OWN ImGui context.  Sharing with
            // UE4SS's context is unsafe because ImGui back-end state
            // (vertex buffers, font texture, active render target)
            // belongs to the context; two renderers on one context
            // trample each other.  If UE4SS's `UE4SS_ENABLE_IMGUI()`
            // macro runs (in on_ui_init) and sets its own context as
            // the "current", we override it for OUR render path and
            // restore it if needed.  The allocator functions were
            // likely already set globally by UE4SS_ENABLE_IMGUI; our
            // context inherits them, which is what we want so objects
            // allocated inside ImGui match whatever freer is active.
            IMGUI_CHECKVERSION();
            m_owned_context = ImGui::CreateContext();
            ImGui::SetCurrentContext(m_owned_context);
            ImGuiIO& io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

            // ImGuiBackendFlags_HasGamepad is REQUIRED for gamepad nav
            // to activate.  ImGui's NavUpdate() gates nav_gamepad_active
            // on (NavEnableGamepad && HasGamepad) — without the backend
            // flag, AddKeyEvent(ImGuiKey_Gamepad*) calls are accepted
            // but never consulted for nav.  Stock imgui_impl_win32.cpp
            // sets this flag in its Init(), but only when
            // IMGUI_IMPL_WIN32_DISABLE_GAMEPAD is NOT defined — UE4SS
            // builds ImGui WITH that define (see
            // RE-UE4SS/deps/third/imgui/CMakeLists.txt:77), so the
            // backend compiles out its gamepad path entirely.  We take
            // over gamepad input in GamepadInput.hpp via direct XInput
            // polling, so we own both the "has gamepad" assertion and
            // the per-frame state feed.
            io.BackendFlags |= ImGuiBackendFlags_HasGamepad;

            io.IniFilename = nullptr;  // no imgui.ini side effects
            ImGui::StyleColorsDark();

            // Init backends against the game's D3D11.
            if (!ImGui_ImplWin32_Init(m_hwnd))
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[GameImGui] ImGui_ImplWin32_Init failed\n"));
                destroy_owned_context_if_any();
                release_rtv();
                m_device.Reset();
                m_context.Reset();
                m_hwnd = nullptr;
                return false;
            }
            if (!ImGui_ImplDX11_Init(m_device.Get(), m_context.Get()))
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[GameImGui] ImGui_ImplDX11_Init failed\n"));
                ImGui_ImplWin32_Shutdown();
                destroy_owned_context_if_any();
                release_rtv();
                m_device.Reset();
                m_context.Reset();
                m_hwnd = nullptr;
                return false;
            }

            m_ready.store(true, std::memory_order_release);
            RC::Output::send<RC::LogLevel::Default>(
                STR("[GameImGui] DX11 overlay initialised (hwnd={}, dev={}, ctx={})\n"),
                reinterpret_cast<uintptr_t>(m_hwnd),
                reinterpret_cast<uintptr_t>(m_device.Get()),
                reinterpret_cast<uintptr_t>(m_context.Get()));
            return true;
        }

        // Called from the hooked ResizeBuffers() BEFORE chaining to the
        // trampoline.  The RTV holds a ref on back-buffer 0, which the
        // resize must release.  Rebuilt on next Present via
        // ensure_rtv_after_resize().
        void release_rtv()
        {
            m_back_buffer_rtv.Reset();
        }

        // Called from the hooked Present() after a prior ResizeBuffers
        // invalidated the RTV.  Safe to call every frame; fast-paths
        // when m_back_buffer_rtv is already live.
        bool ensure_rtv_after_resize(IDXGISwapChain* swap_chain)
        {
            if (m_back_buffer_rtv) return true;
            return create_rtv(swap_chain);
        }

        // Full teardown (mod unload).  After this, ready() returns
        // false and ensure_initialised() will re-run a full init on
        // the next hooked Present.
        void shutdown()
        {
            if (!m_ready.exchange(false))
            {
                return;
            }
            ImGui_ImplDX11_Shutdown();
            ImGui_ImplWin32_Shutdown();
            destroy_owned_context_if_any();
            release_rtv();
            m_context.Reset();
            m_device.Reset();
            m_hwnd = nullptr;
        }

        // ---- Accessors for the rendering path ----

        bool ready() const noexcept
        {
            return m_ready.load(std::memory_order_acquire);
        }

        ID3D11Device*        device()  const noexcept { return m_device.Get();  }
        ID3D11DeviceContext* context() const noexcept { return m_context.Get(); }
        ID3D11RenderTargetView* back_buffer_rtv() const noexcept
        {
            return m_back_buffer_rtv.Get();
        }
        HWND          hwnd()           const noexcept { return m_hwnd; }
        ImGuiContext* imgui_context()  const noexcept { return m_owned_context; }

        // Convenience: make OUR ImGui context the current one.
        // Called from PresentHook at the start of each frame so
        // subsequent ImGui:: calls (including register-tab callbacks)
        // route to our context even if UE4SS flipped it underneath.
        void bind_imgui_context() const noexcept
        {
            if (m_owned_context) ImGui::SetCurrentContext(m_owned_context);
        }

    private:
        DX11State() = default;
        ~DX11State() { shutdown(); }
        DX11State(const DX11State&)            = delete;
        DX11State& operator=(const DX11State&) = delete;

        bool create_rtv(IDXGISwapChain* swap_chain)
        {
            ComPtr<ID3D11Texture2D> back_buffer;
            HRESULT hr = swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                               reinterpret_cast<void**>(back_buffer.GetAddressOf()));
            if (FAILED(hr) || !back_buffer)
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[GameImGui] swap_chain->GetBuffer(0) failed hr=0x{:08X}\n"),
                    static_cast<uint32_t>(hr));
                return false;
            }
            hr = m_device->CreateRenderTargetView(back_buffer.Get(), nullptr,
                                                  m_back_buffer_rtv.GetAddressOf());
            if (FAILED(hr))
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[GameImGui] CreateRenderTargetView failed hr=0x{:08X}\n"),
                    static_cast<uint32_t>(hr));
                return false;
            }
            return true;
        }

        void destroy_owned_context_if_any()
        {
            if (m_owned_context)
            {
                // Only destroy if *we* created it.  If UE4SS owns the
                // context, leave it alone.
                ImGui::DestroyContext(m_owned_context);
                m_owned_context = nullptr;
            }
        }

        ComPtr<ID3D11Device>           m_device;
        ComPtr<ID3D11DeviceContext>    m_context;
        ComPtr<ID3D11RenderTargetView> m_back_buffer_rtv;
        HWND                           m_hwnd{nullptr};
        ImGuiContext*                  m_owned_context{nullptr};
        std::atomic<bool>              m_ready{false};
    };
}
