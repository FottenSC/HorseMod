#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace Horse::Qualification
{
inline constexpr std::size_t replay_qualification_health_value_count = 54;

[[nodiscard]] constexpr bool IsTerminalReplayQualificationHealth(
    std::span<const std::uint64_t,
        replay_qualification_health_value_count> health) noexcept
{
    // The replay timeline has a deliberate 512 MiB bound. A checkpoint store
    // reaching that bound sets timeline_partial (48) and its role-specific
    // diagnostic (52/53), but leaves the retained prefix usable. The seek API
    // separately rejects targets that cannot be planned from that prefix.
    // Actual allocation failures, post-arm growth, accounting, canonical,
    // presentation, identity, and gameplay-RNG failures remain terminal.
    return health[0] != 0 || health[1] != 0 || health[2] != 0
        || health[5] != 0 || health[6] != 0 || health[21] != 0
        || health[26] != 0 || health[27] != 0 || health[31] != 0;
}
}
