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
    std::uint32_t length{1};
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
    struct SourceRange
    {
        std::uintptr_t begin{};
        std::uintptr_t end{};
    };

    static std::vector<SourceRange> merge_source_ranges(
        const HgCpuWriteTrace& trace);
    static bool directly_sourced(
        std::uintptr_t address,
        const std::vector<SourceRange>& ranges) noexcept;

    static constexpr std::size_t maximum_write_spans = 131072;
    static constexpr std::size_t maximum_unmapped_deltas = 4096;
    static constexpr std::size_t maximum_target_size = 0x100000;

    INativeMemory& memory_;
    HgCpuStreamShim shim_;
    std::array<HgCpuCoverageTarget, 2> targets_{};
    std::array<std::vector<std::byte>, 2> previous_{};
    std::vector<HgCpuWriteSpan> spans_{};
    bool bound_{};
    bool have_baseline_{};
};
}
