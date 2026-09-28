#pragma once
#include <cstdint>
#include <cstddef>
#include <array>
namespace Horse::Deterministic {
// Logical view publications, not simulation ticks or physical task epochs.
struct ReplayRenderState {
    std::uint64_t publications{};
    double elapsed{};
    std::uint32_t random_state{1};
    std::array<std::byte,0x36c> last_view_matrices{};
    bool history_valid{};
    friend bool operator==(const ReplayRenderState&, const ReplayRenderState&) = default;
};
}
