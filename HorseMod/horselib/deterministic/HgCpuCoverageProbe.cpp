#include "HgCpuCoverageProbe.hpp"

#include <algorithm>
#include <limits>

namespace Horse::Deterministic
{
HgCpuCoverageProbe::HgCpuCoverageProbe(INativeMemory& memory) noexcept
    : memory_(memory)
{
}

Status HgCpuCoverageProbe::Bind(const std::array<HgCpuCoverageTarget, 2>& targets)
{
    Reset();
    for (const auto& target : targets)
    {
        if (target.base == 0 || target.size == 0
            || target.size > maximum_target_size || target.generation == 0)
        {
            return Status::failure(FailureCode::ContextUnavailable);
        }
    }
    targets_ = targets;
    try
    {
        for (std::size_t lane = 0; lane < targets_.size(); ++lane)
        {
            previous_[lane].resize(targets_[lane].size);
        }
        spans_.resize(maximum_write_spans);
    }
    catch (...)
    {
        Reset();
        return Status::failure(FailureCode::CapacityExceeded);
    }
    bound_ = true;
    return Status::success();
}

void HgCpuCoverageProbe::Reset() noexcept
{
    targets_ = {};
    for (auto& bytes : previous_) bytes.clear();
    spans_.clear();
    bound_ = false;
    have_baseline_ = false;
}

bool HgCpuCoverageProbe::capture_targets(
    std::array<std::vector<std::byte>, 2>& output) noexcept
{
    for (std::size_t lane = 0; lane < targets_.size(); ++lane)
    {
        try
        {
            output[lane].resize(targets_[lane].size);
        }
        catch (...)
        {
            return false;
        }
        if (!memory_.Read(targets_[lane].base, output[lane])) return false;
    }
    return true;
}

std::vector<HgCpuCoverageProbe::SourceRange>
HgCpuCoverageProbe::merge_source_ranges(const HgCpuWriteTrace& trace)
{
    std::vector<SourceRange> ranges;
    ranges.reserve(trace.count);
    for (std::size_t i = 0; i < trace.count; ++i)
    {
        const auto& span = trace.storage[i];
        if (span.source_address != 0 && span.size != 0
            && span.size <= std::numeric_limits<std::uintptr_t>::max()
                - span.source_address)
        {
            ranges.push_back({span.source_address, span.source_address + span.size});
        }
    }
    std::sort(ranges.begin(), ranges.end(), [](const SourceRange& left,
        const SourceRange& right) { return left.begin < right.begin; });
    std::size_t merged = 0;
    for (const auto range : ranges)
    {
        if (merged != 0 && range.begin <= ranges[merged - 1].end)
        {
            ranges[merged - 1].end = std::max(ranges[merged - 1].end, range.end);
        }
        else
        {
            ranges[merged++] = range;
        }
    }
    ranges.resize(merged);
    return ranges;
}

bool HgCpuCoverageProbe::directly_sourced(
    std::uintptr_t address, const std::vector<SourceRange>& ranges) noexcept
{
    const auto found = std::upper_bound(
        ranges.begin(), ranges.end(), address,
        [](std::uintptr_t value, const SourceRange& range) {
            return value < range.begin;
        });
    return found != ranges.begin() && address < std::prev(found)->end;
}

Status HgCpuCoverageProbe::Observe(
    HgCpuExecFn writer,
    const HgCpuGenerationContext& context,
    std::uint64_t frame,
    HgCpuCoverageSample& output) noexcept
{
    output = {};
    output.frame = frame;
    if (!bound_ || context.fighter_generations[0] != targets_[0].generation
        || context.fighter_generations[1] != targets_[1].generation)
    {
        return Status::failure(FailureCode::GenerationMismatch);
    }

    std::array<std::vector<std::byte>, 2> current{};
    if (!capture_targets(current))
        return Status::failure(FailureCode::CaptureFailed);

    HgCpuLocalImage stream{};
    HgCpuWriteTrace trace{std::span{spans_}};
    const auto capture = shim_.Capture(writer, context, stream, &trace);
    if (!capture.ok()) return capture;
    output.stream_cursor = stream.cursor;
    output.write_span_count = trace.count;
    output.write_spans_truncated = trace.truncated;
    output.unmapped_deltas.reserve(maximum_unmapped_deltas);
    std::vector<SourceRange> source_ranges;
    try
    {
        source_ranges = merge_source_ranges(trace);
    }
    catch (...)
    {
        return Status::failure(FailureCode::CapacityExceeded);
    }

    if (have_baseline_)
    {
        for (std::size_t lane = 0; lane < targets_.size(); ++lane)
        {
            for (std::size_t offset = 0; offset < current[lane].size(); ++offset)
            {
                if (current[lane][offset] == previous_[lane][offset]) continue;
                ++output.changed_bytes;
                if (directly_sourced(targets_[lane].base + offset, source_ranges))
                {
                    ++output.directly_sourced_changed_bytes;
                }
                else if (output.unmapped_deltas.size() < maximum_unmapped_deltas)
                {
                    auto* previous_range = output.unmapped_deltas.empty()
                        ? nullptr : &output.unmapped_deltas.back();
                    if (previous_range != nullptr
                        && previous_range->target == lane
                        && previous_range->offset + previous_range->length == offset)
                    {
                        ++previous_range->length;
                        previous_range->after = current[lane][offset];
                    }
                    else
                    {
                        output.unmapped_deltas.push_back({
                            static_cast<std::uint8_t>(lane),
                            static_cast<std::uint32_t>(offset),
                            1,
                            previous_[lane][offset], current[lane][offset]});
                    }
                }
                else
                {
                    output.unmapped_deltas_truncated = true;
                }
            }
        }
    }
    previous_ = std::move(current);
    have_baseline_ = true;
    return Status::success();
}
}
