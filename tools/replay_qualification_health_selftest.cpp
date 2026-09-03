#include "replay_qualification_mod/ReplayQualificationHealth.hpp"

#include <array>
#include <cstdlib>
#include <iostream>

namespace
{
using Horse::Qualification::IsTerminalReplayQualificationHealth;
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
    return 0;
}
