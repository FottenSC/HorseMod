#include "HgCpuCoverageProbe.hpp"

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

bool HgCpuCoverageProbe::directly_sourced(
    std::uintptr_t address, const HgCpuWriteTrace& trace) const noexcept
{
    for (std::size_t i = 0; i < trace.count; ++i)
    {
        const auto& span = trace.storage[i];
        if (span.source_address <= address
            && address - span.source_address < span.size)
        {
            return true;
        }
    }
    return false;
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
    HgCpuWriteTrace trace{spans_};
    const auto capture = shim_.Capture(writer, context, stream, &trace);
    if (!capture.ok()) return capture;
    output.stream_cursor = stream.cursor;
    output.write_span_count = trace.count;
    output.write_spans_truncated = trace.truncated;
    output.unmapped_deltas.reserve(maximum_unmapped_deltas);

    if (have_baseline_)
    {
        for (std::size_t lane = 0; lane < targets_.size(); ++lane)
        {
            for (std::size_t offset = 0; offset < current[lane].size(); ++offset)
            {
                if (current[lane][offset] == previous_[lane][offset]) continue;
                ++output.changed_bytes;
                if (directly_sourced(targets_[lane].base + offset, trace))
                {
                    ++output.directly_sourced_changed_bytes;
                }
                else if (output.unmapped_deltas.size() < maximum_unmapped_deltas)
                {
                    output.unmapped_deltas.push_back({
                        static_cast<std::uint8_t>(lane),
                        static_cast<std::uint32_t>(offset),
                        previous_[lane][offset], current[lane][offset]});
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
