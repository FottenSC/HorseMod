#include "replay_qualification_mod/ReplayQualificationHealth.hpp"
#include "replay_qualification_mod/ReplaySeekReadiness.hpp"

#include <array>
#include <cstdlib>
#include <iostream>

namespace
{
using Horse::Qualification::IsTerminalReplayQualificationHealth;
using Horse::Qualification::ClassifyReplaySeekReadiness;
using Horse::Qualification::ReplaySeekReadiness;
using Horse::Qualification::replay_qualification_health_value_count;

void expect(bool condition, const char* message)
{
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}
}

int main()
{
    std::array<std::uint64_t,
        replay_qualification_health_value_count> health{};
    expect(!IsTerminalReplayQualificationHealth(health),
        "clean replay health is non-terminal");

    health[48] = 1;
    health[49] = 4;
    health[52] = 9;
    expect(!IsTerminalReplayQualificationHealth(health),
        "bounded landing-checkpoint exhaustion leaves a usable prefix");
    health[49] = 6;
    health[52] = 0;
    health[53] = 9;
    expect(!IsTerminalReplayQualificationHealth(health),
        "bounded batch-entry exhaustion leaves range validation to seek planning");

    for (const auto terminal_index :
        std::array<std::size_t, 9>{0, 1, 2, 5, 6, 21, 26, 27, 31})
    {
        health = {};
        health[terminal_index] = 1;
        expect(IsTerminalReplayQualificationHealth(health),
            "authoritative replay failure remains fail-fast terminal");
    }

    expect(ClassifyReplaySeekReadiness(
        false, 0, 0, 600, true, 1)
            == ReplaySeekReadiness::RangeUnavailable,
        "seek readiness reports an unavailable native range");
    expect(ClassifyReplaySeekReadiness(
        true, 100, 699, 600, true, 1)
            == ReplaySeekReadiness::RangeTooShort,
        "seek readiness enforces the full retained history span");
    expect(ClassifyReplaySeekReadiness(
        true, 100, 700, 600, true, 0)
            == ReplaySeekReadiness::PhaseInactive,
        "seek readiness requires the active authored replay phase");
    expect(ClassifyReplaySeekReadiness(
        true, 100, 700, 600, true, 1)
            == ReplaySeekReadiness::Ready,
        "seek readiness admits the exact 600-frame active boundary");
    return 0;
}
