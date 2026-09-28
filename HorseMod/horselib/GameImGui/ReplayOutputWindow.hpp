#pragma once

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <mutex>

namespace Horse::GameImGui
{
    // Window messages must not share the suspended game/render task queues.
    // This thread owns the held display/input window: no D3D, engine
    // calls, input synthesis, or simulation state. The render owner keeps all
    // GPU resources until their completion witness, then closes this window.
    class ReplayOutputWindow
    {
    public:
        static constexpr std::uint64_t ownership_allowance = 1024 * 1024;
        bool open(UINT width, UINT height)
        {
            if (thread_) return hwnd_.load() != nullptr;
            ready_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (!ready_) return false;
            width_ = width; height_ = height;
            thread_ = CreateThread(nullptr, 256 * 1024, &Run, this,
                STACK_SIZE_PARAM_IS_A_RESERVATION, &thread_id_);
            if (!thread_) { CloseHandle(ready_); ready_ = nullptr; return false; }
            // Timeout leaves the thread/state owned. close() can finish later;
            // the caller must not free this object or report retirement.
            return WaitForSingleObject(ready_, 500) == WAIT_OBJECT_0 && hwnd_.load();
        }

        HWND hwnd() const noexcept { return hwnd_.load(); }
        bool active() const noexcept { return thread_ != nullptr; }

        bool display(bool visible, const RECT& rectangle = {}, HWND return_window = nullptr)
        {
            const auto window = hwnd_.load();
            if (!window) return false;
            {
                std::lock_guard lock(config_mutex_);
                visible_ = visible; rectangle_ = rectangle;
                if(return_window) return_window_ = return_window;
            }
            return PostMessageW(window, WM_APP + 91, 0, 0) != FALSE;
        }

        bool close()
        {
            if (!thread_) return true;
            stop_.store(true);
            if (auto window = hwnd_.load()) PostMessageW(window, WM_CLOSE, 0, 0);
            if (WaitForSingleObject(thread_, 500) != WAIT_OBJECT_0) return false;
            CloseHandle(thread_); CloseHandle(ready_);
            thread_ = ready_ = nullptr; thread_id_ = 0; stop_.store(false);
            return hwnd_.load() == nullptr;
        }

    private:
        static DWORD WINAPI Run(void* context)
        {
            auto& self = *static_cast<ReplayOutputWindow*>(context);
            WNDCLASSEXW wc{};
            wc.cbSize = sizeof(wc); wc.lpfnWndProc = &WindowProc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpszClassName = L"HorseModReplayOutputUi";
            if (!RegisterClassExW(&wc)) {
                WNDCLASSEXW existing{}; existing.cbSize=sizeof(existing);
                if(GetLastError()!=ERROR_CLASS_ALREADY_EXISTS
                    || !GetClassInfoExW(wc.hInstance,wc.lpszClassName,&existing) || existing.lpfnWndProc!=&WindowProc)
                { SetEvent(self.ready_); return 0; }
            }
            auto window = CreateWindowExW(WS_EX_APPWINDOW,
                wc.lpszClassName, L"Replay playback", WS_POPUP,
                0, 0, self.width_, self.height_, nullptr, nullptr, wc.hInstance, &self);
            self.hwnd_.store(window); SetEvent(self.ready_);
            if (!window) { UnregisterClassW(wc.lpszClassName,wc.hInstance); return 0; }
            if (self.stop_.load()) DestroyWindow(window);
            MSG message{};
            while (self.hwnd_.load() && GetMessageW(&message, nullptr, 0, 0) > 0)
            { TranslateMessage(&message); DispatchMessageW(&message); }
            if (IsWindow(window)) DestroyWindow(window);
            self.hwnd_.store(nullptr);
            UnregisterClassW(wc.lpszClassName,wc.hInstance);
            return 0;
        }

        static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
        {
            if (message == WM_NCCREATE)
                SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(
                    reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams));
            auto* self = reinterpret_cast<ReplayOutputWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
            if (message == WM_APP + 91 && self)
            {
                RECT rectangle{}; bool visible{};HWND return_window{};
                { std::lock_guard lock(self->config_mutex_); rectangle = self->rectangle_; visible = self->visible_; return_window=self->return_window_; }
                if (visible)
                    SetWindowPos(window, HWND_TOP, rectangle.left, rectangle.top,
                        rectangle.right - rectangle.left, rectangle.bottom - rectangle.top,
                        SWP_NOACTIVATE | SWP_NOZORDER | SWP_SHOWWINDOW);
                else {
                    // A user resume can return activation to the original
                    // window once its native queue resumes. Never activate it
                    // when the user has already switched to another app.
                    DWORD pid{};
                    if(GetForegroundWindow()==window && return_window && IsWindow(return_window)
                        && GetWindowThreadProcessId(return_window,&pid) && pid==GetCurrentProcessId())
                        SetForegroundWindow(return_window);
                    ShowWindow(window, SW_HIDE);
                }
                return 0;
            }
            if (message == WM_MOUSEACTIVATE) return MA_ACTIVATE;
            if (message == WM_NCHITTEST) return HTCLIENT;
            // The GPU owner closes only after completion. Alt-F4 must not
            // destroy an in-flight swapchain destination behind its back.
            if (message == WM_CLOSE && self && !self->stop_.load()) return 0;
            if (message == WM_DESTROY)
            { if (self) self->hwnd_.store(nullptr); PostQuitMessage(0); return 0; }
            return DefWindowProcW(window, message, wparam, lparam);
        }

        HANDLE thread_{}, ready_{};
        DWORD thread_id_{};
        UINT width_{}, height_{};
        std::atomic<HWND> hwnd_{};
        std::atomic<bool> stop_{};
        std::mutex config_mutex_;
        RECT rectangle_{};
        HWND return_window_{};
        bool visible_{};
    };
}
