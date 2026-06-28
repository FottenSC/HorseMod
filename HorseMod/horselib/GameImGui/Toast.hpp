#pragma once

#include "PresentHook.hpp"

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cfloat>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace Horse::GameImGui
{
    enum class ToastKind : int
    {
        Info,
        Working,
        Success,
        Failure,
    };

    class ToastManager
    {
    public:
        static ToastManager& instance()
        {
            static ToastManager s;
            return s;
        }

        void show_info(const std::string& id,
                       const std::string& message,
                       double seconds = 3.0)
        {
            upsert(id, message, ToastKind::Info, false, seconds);
        }

        void show_working(const std::string& id,
                          const std::string& message)
        {
            upsert(id, message, ToastKind::Working, true, 0.0);
        }

        void show_success(const std::string& id,
                          const std::string& message,
                          double seconds = 3.0)
        {
            upsert(id, message, ToastKind::Success, false, seconds);
        }

        void show_failure(const std::string& id,
                          const std::string& message,
                          double seconds = 5.0)
        {
            upsert(id, message, ToastKind::Failure, false, seconds);
        }

        void clear(const std::string& id)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_toasts.erase(
                std::remove_if(m_toasts.begin(), m_toasts.end(),
                    [&](const Toast& toast) { return toast.id == id; }),
                m_toasts.end());
        }

        void clear_if_kind(const std::string& id, ToastKind kind)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_toasts.erase(
                std::remove_if(m_toasts.begin(), m_toasts.end(),
                    [&](const Toast& toast) {
                        return toast.id == id && toast.kind == kind;
                    }),
                m_toasts.end());
        }

        bool draw() noexcept
        {
            std::vector<Toast> snapshot;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                const auto now = Clock::now();
                m_toasts.erase(
                    std::remove_if(m_toasts.begin(), m_toasts.end(),
                        [&](const Toast& toast) {
                            return !toast.persistent
                                && toast.expires_at <= now;
                        }),
                    m_toasts.end());
                if (m_toasts.empty())
                    return false;
                snapshot = m_toasts;
            }

            ImGuiIO& io = ImGui::GetIO();
            const float margin = 22.0f;
            const float gap = 8.0f;
            const float max_width = 420.0f;
            float y = margin;

            for (const Toast& toast : snapshot)
            {
                const std::string window_name =
                    std::string("##HorseToast_") + toast.id;
                ImGui::SetNextWindowPos(
                    ImVec2(io.DisplaySize.x - margin, y),
                    ImGuiCond_Always,
                    ImVec2(1.0f, 0.0f));
                ImGui::SetNextWindowSizeConstraints(
                    ImVec2(240.0f, 0.0f),
                    ImVec2(max_width, FLT_MAX));
                ImGui::SetNextWindowBgAlpha(0.88f);

                ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                                    ImVec2(12.0f, 10.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
                ImGui::PushStyleColor(ImGuiCol_Border,
                                      accent_color(toast.kind));

                const ImGuiWindowFlags flags =
                    ImGuiWindowFlags_NoTitleBar |
                    ImGuiWindowFlags_NoResize |
                    ImGuiWindowFlags_NoScrollbar |
                    ImGuiWindowFlags_NoCollapse |
                    ImGuiWindowFlags_AlwaysAutoResize |
                    ImGuiWindowFlags_NoSavedSettings |
                    ImGuiWindowFlags_NoMove |
                    ImGuiWindowFlags_NoFocusOnAppearing |
                    ImGuiWindowFlags_NoInputs;
                if (ImGui::Begin(window_name.c_str(), nullptr, flags))
                {
                    ImGui::TextColored(accent_color(toast.kind), "%s",
                                       title(toast.kind));
                    ImGui::PushTextWrapPos(ImGui::GetCursorPosX()
                                           + max_width - 32.0f);
                    ImGui::TextUnformatted(toast.message.c_str());
                    ImGui::PopTextWrapPos();
                    y += ImGui::GetWindowSize().y + gap;
                }
                ImGui::End();
                ImGui::PopStyleColor();
                ImGui::PopStyleVar(3);
            }

            return true;
        }

    private:
        using Clock = std::chrono::steady_clock;

        struct Toast
        {
            std::string id;
            std::string message;
            ToastKind kind {ToastKind::Info};
            bool persistent {false};
            Clock::time_point expires_at {};
            Clock::time_point updated_at {};
        };

        ToastManager() = default;
        ToastManager(const ToastManager&) = delete;
        ToastManager& operator=(const ToastManager&) = delete;

        void upsert(const std::string& id,
                    const std::string& message,
                    ToastKind kind,
                    bool persistent,
                    double seconds)
        {
            const auto now = Clock::now();
            const auto expires_at = persistent
                ? (Clock::time_point::max)()
                : now + std::chrono::milliseconds(
                    static_cast<int64_t>(seconds * 1000.0));

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                auto it = std::find_if(m_toasts.begin(), m_toasts.end(),
                    [&](const Toast& toast) { return toast.id == id; });
                if (it == m_toasts.end())
                {
                    m_toasts.push_back(
                        Toast{id, message, kind, persistent, expires_at, now});
                }
                else
                {
                    it->message = message;
                    it->kind = kind;
                    it->persistent = persistent;
                    it->expires_at = expires_at;
                    it->updated_at = now;
                }
            }

            PresentHook::instance().request_passive_draw(true);
        }

        static const char* title(ToastKind kind) noexcept
        {
            switch (kind)
            {
                case ToastKind::Working: return "Working";
                case ToastKind::Success: return "Done";
                case ToastKind::Failure: return "Failed";
                case ToastKind::Info:
                default:
                    return "HorseMod";
            }
        }

        static ImVec4 accent_color(ToastKind kind) noexcept
        {
            switch (kind)
            {
                case ToastKind::Working:
                    return ImVec4(0.46f, 0.74f, 1.00f, 1.00f);
                case ToastKind::Success:
                    return ImVec4(0.36f, 0.86f, 0.54f, 1.00f);
                case ToastKind::Failure:
                    return ImVec4(1.00f, 0.39f, 0.34f, 1.00f);
                case ToastKind::Info:
                default:
                    return ImVec4(0.92f, 0.82f, 0.56f, 1.00f);
            }
        }

        std::mutex m_mutex;
        std::vector<Toast> m_toasts;
    };
}
