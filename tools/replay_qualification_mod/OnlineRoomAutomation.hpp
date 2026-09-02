#pragma once

#include <cstdint>
#include <array>
#include <string>

namespace Horse::Qualification
{
enum class OnlineRoomState : std::uint8_t
{
    Waiting,
    Complete,
    Failed,
};

enum class OnlineAutomationAction : std::uint8_t
{
    HostRoomCreate,
    MatchSetup,
};

enum class OnlineAutomationRole : std::uint8_t
{
    Host,
    Sandbox,
};

struct OnlineAutomationRequest
{
    OnlineAutomationAction action{OnlineAutomationAction::HostRoomCreate};
    OnlineAutomationRole role{OnlineAutomationRole::Host};
    std::uint64_t lobby_id{};
    std::uint64_t local_steam_id{};
    std::uint64_t peer_steam_id{};
    std::array<std::string, 2> fighter_codes{};
    std::string stage_code{};
    std::string display_map_name{};
};

// Test-only stock-UI driver. This deliberately lives outside HorseMod's
// observer probe: the observer remains structurally read-only while this
// qualification bridge owns the visible host room-creation workflow.
class OnlineRoomAutomation final
{
public:
    bool Bind(std::uintptr_t image_base) noexcept;
    void Reset(const OnlineAutomationRequest& request = {}) noexcept;
    OnlineRoomState Tick(std::string& detail) noexcept;
    [[nodiscard]] std::uint64_t lobby_id() const noexcept { return lobby_id_; }
    [[nodiscard]] std::uint64_t local_steam_id() const noexcept
    {
        return observed_local_steam_id_;
    }

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
    OnlineAutomationRequest request_{};
    std::uintptr_t image_base_{};
    std::uint64_t lobby_id_{};
    std::uint64_t observed_local_steam_id_{};
    std::uint32_t setup_scene_ticks_{};
    std::uint32_t stage_focus_tick_{};
    bool lobby_metadata_requested_{};
    bool native_invite_queued_{};
    bool play_side_requested_{};
    bool session_connection_ready_{};
    bool peer_connect_ready_published_{};
    bool host_session_transport_ready_{};
    bool host_transport_ready_published_{};
    bool ready_channel_retry_sent_{};
    bool guest_session_ready_published_{};
    std::uint64_t ready_observation_baseline_{};
    bool ready_requested_{};
    bool character_requested_{};
    bool stage_focus_requested_{};
    bool stage_decide_requested_{};
    bool match_content_verified_{};
};
}
