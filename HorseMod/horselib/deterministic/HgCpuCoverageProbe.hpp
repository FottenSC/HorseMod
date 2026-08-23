#pragma once

#include "HgCpuStream.hpp"
#include "NativeCandidateRegions.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Horse::Deterministic
{
struct HgCpuCoverageTarget
{
    std::uintptr_t base{};
    std::size_t size{};
    std::uint64_t generation{};
};

struct HgCpuUnmappedDelta
{
    std::uint8_t target{};
    std::uint32_t offset{};
    std::byte before{};
    std::byte after{};
};

struct HgCpuCoverageSample
{
    std::uint64_t frame{};
    std::size_t stream_cursor{};
    std::size_t write_span_count{};
    std::size_t changed_bytes{};
    std::size_t directly_sourced_changed_bytes{};
    bool write_spans_truncated{};
    bool unmapped_deltas_truncated{};
    std::vector<HgCpuUnmappedDelta> unmapped_deltas;
};

class HgCpuCoverageProbe
{
public:
    explicit HgCpuCoverageProbe(INativeMemory& memory) noexcept;

    Status Bind(const std::array<HgCpuCoverageTarget, 2>& targets);
    void Reset() noexcept;
    Status Observe(
        HgCpuExecFn writer,
        const HgCpuGenerationContext& context,
        std::uint64_t frame,
        HgCpuCoverageSample& output) noexcept;

private:
    bool capture_targets(std::array<std::vector<std::byte>, 2>& output) noexcept;
    bool directly_sourced(std::uintptr_t address, const HgCpuWriteTrace& trace) const noexcept;

    static constexpr std::size_t maximum_write_spans = 2048;
    static constexpr std::size_t maximum_unmapped_deltas = 4096;
    static constexpr std::size_t maximum_target_size = 0x100000;

    INativeMemory& memory_;
    HgCpuStreamShim shim_;
    std::array<HgCpuCoverageTarget, 2> targets_{};
    std::array<std::vector<std::byte>, 2> previous_{};
    std::array<HgCpuWriteSpan, maximum_write_spans> spans_{};
    bool bound_{};
    bool have_baseline_{};
};
}
