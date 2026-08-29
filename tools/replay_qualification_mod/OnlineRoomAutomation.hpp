#pragma once

#include <cstdint>
#include <string>

namespace Horse::Qualification
{
enum class OnlineRoomState : std::uint8_t
{
    Waiting,
    Complete,
    Failed,
};

// Test-only stock-UI driver. This deliberately lives outside HorseMod's
// observer probe: the observer remains structurally read-only while this
// qualification bridge owns the visible host room-creation workflow.
class OnlineRoomAutomation final
{
public:
    bool Bind(std::uintptr_t image_base) noexcept;
    void Reset() noexcept;
    OnlineRoomState Tick(std::string& detail) noexcept;

private:
    enum class Step : std::uint8_t
    {
        NavigateToPlayerMatch,
        RequestMakeRoom,
        PollMakeRoom,
        SendCreateCommand,
        PollCreateCommand,
        WaitForInRoom,
    };

    Step step_{Step::NavigateToPlayerMatch};
    std::string last_scene_{};
    std::uint32_t scene_ticks_{};
    std::uint32_t step_ticks_{};
    std::uint32_t create_retries_{};
    std::uint8_t main_menu_route_step_{};
    bool title_top_requested_{};
    std::uint8_t title_decide_stage_{};
};
}
