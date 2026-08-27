#pragma once

#include <cstdint>
#include <string>

namespace Horse::Qualification
{
enum class NavigationState : std::uint8_t
{
    Waiting,
    ReplayListReady,
    Ready,
    Failed,
};

class ReplaySceneNavigator final
{
public:
    bool Bind(std::uintptr_t image_base) noexcept;
    NavigationState Tick(bool playback_context_staged, std::string& detail);

private:
    std::string last_scene_{};
    std::uint32_t retry_frames_{};
    bool title_top_requested_{};
    bool title_user_forced_{};
};
}
