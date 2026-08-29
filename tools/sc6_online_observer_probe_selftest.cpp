#include "deterministic/Sc6OnlineObserverProbe.hpp"

#include <cassert>
#include <cstring>

using namespace Horse::Deterministic;

namespace
{
class FakeReadOnlyAccess final : public IOnlineObserverReadOnlyAccess
{
public:
    Status ObserveSession(Sc6OnlineSessionIdentity& output) noexcept override
    {
        ++session_reads;
        output.lobby_id = 0x1234;
        output.session_name = 0x55;
        output.session_interface = 0x1000;
        output.active_connect = 0x0fe8;
        output.online_session = 0x2000;
        output.named_session = 0x3000;
        output.session_info = 0x4000;
        output.role = 0;
        output.virtual_session_state = 4;
        output.local_player_slot = 0;
        return Status::success();
    }

    Status ObserveLobby(std::uint64_t lobby_id,
        SteamLobbyIdentity& output) noexcept override
    {
        ++lobby_reads;
        assert(lobby_id == 0x1234);
        output.members = {111, 222};
        output.local_steam_id = 111;
        output.member_count = 2;
        output.casual_player_match = true;
        return Status::success();
    }

    Status ObserveBattle(const void*,
        Sc6BattleSyncIdentity& output) noexcept override
    {
        ++battle_reads;
        output.battle_sync_object = 0x5000;
        output.characters_received = true;
        output.stage_received = true;
        std::memcpy(output.content.fighter_codes[0].data(), "rap", 4);
        std::memcpy(output.content.fighter_codes[1].data(), "max", 4);
        std::memcpy(output.content.stage_code.data(), "009", 4);
        std::memcpy(output.content.map_name.data(), "/Game/Stage/STG009", 19);
        return Status::success();
    }

    // These deliberately resemble the forbidden capability surface. They are
    // not members of IOnlineObserverReadOnlyAccess, so the probe cannot call
    // them even when its fake backing object exposes them.
    void StartTransport() noexcept { ++forbidden_calls; }
    void ArmAllowlist() noexcept { ++forbidden_calls; }
    void ConfigureGekko() noexcept { ++forbidden_calls; }
    void SuppressPresentation() noexcept { ++forbidden_calls; }
    void TakeSimulationOwnership() noexcept { ++forbidden_calls; }

    std::uint32_t session_reads{};
    std::uint32_t lobby_reads{};
    std::uint32_t battle_reads{};
    std::uint32_t forbidden_calls{};
};

OnlineObserverProbeRequest Request(const char* run_id)
{
    OnlineObserverProbeRequest request{};
    std::memcpy(request.run_id.data(), run_id, std::strlen(run_id) + 1);
    request.timeout_seconds = 180;
    return request;
}
}

int main()
{
    FakeReadOnlyAccess access{};
    Sc6OnlineObserverProbe probe{access};
    assert(probe.Arm(Request("observer-only-test"), 1000));
    constexpr std::array<std::string_view, 2> packages{
        "/Game/Stage/STG009/Maps/STG009",
        "/Game/Stage/STG009/Lighting/Day"};
    probe.Tick({reinterpret_cast<void*>(0x5000), packages}, 1200);
    OnlineObserverProbeReport report{};
    assert(probe.CopyReport(report));
    assert(report.state == OnlineObserverProbeState::Complete);
    assert(std::string_view(report.stage_package.data())
        == "/Game/Stage/STG009");
    assert(std::string_view(report.stage_display_name.data())
        == "Snow-Capped Showdown");
    assert(access.session_reads == 1);
    assert(access.lobby_reads == 1);
    assert(access.battle_reads == 1);
    assert(access.forbidden_calls == 0);

    probe.Disarm();
    assert(probe.state() == OnlineObserverProbeState::Idle);
    assert(probe.Arm(Request("observer-timeout-test"), 5000));
    probe.Tick({}, 185001);
    assert(probe.CopyReport(report));
    assert(report.state == OnlineObserverProbeState::Expired);
    assert(report.failure == FailureCode::Timeout);
    assert(access.forbidden_calls == 0);
    return 0;
}
