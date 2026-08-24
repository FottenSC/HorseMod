#include "HgCpuRuntimeDiagnostics.hpp"
#include "Schema.hpp"

#include <Windows.h>

#include <algorithm>
#include <iomanip>
#include <span>

namespace Horse::Deterministic
{
class HgCpuRuntimeDiagnostics::ProcessMemory final : public INativeMemory
{
public:
    bool Read(std::uintptr_t address, std::span<std::byte> destination) noexcept override
    {
        if (address == 0 || destination.empty()) return false;
        __try
        {
            std::copy_n(reinterpret_cast<const std::byte*>(address),
                destination.size(), destination.data());
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool Write(std::uintptr_t, std::span<const std::byte>) noexcept override
    {
        return false;
    }
};

HgCpuRuntimeDiagnostics::HgCpuRuntimeDiagnostics(std::filesystem::path report_path)
    : report_path_(std::move(report_path)),
      memory_(std::make_unique<ProcessMemory>()),
      probe_(std::make_unique<HgCpuCoverageProbe>(*memory_))
{
}

HgCpuRuntimeDiagnostics::~HgCpuRuntimeDiagnostics()
{
    Finish();
}

bool HgCpuRuntimeDiagnostics::read_fighter_roots(
    std::uintptr_t image_base,
    std::array<std::uintptr_t, 2>& roots) noexcept
{
    constexpr std::uintptr_t fighter_root_rva = 0x470DE90;
    return memory_->Read(
        image_base + fighter_root_rva,
        std::as_writable_bytes(std::span{roots}))
        && roots[0] != 0 && roots[1] != 0 && roots[0] != roots[1];
}

Status HgCpuRuntimeDiagnostics::bind_generation(
    const std::array<std::uintptr_t, 2>& roots) noexcept
{
    ++generation_;
    fighter_roots_ = roots;
    std::array<HgCpuCoverageTarget, 2> targets{{
        {roots[0], fighter_extent, roots[0]},
        {roots[1], fighter_extent, roots[1]},
    }};
    return probe_->Bind(targets);
}

void HgCpuRuntimeDiagnostics::write_header(std::uintptr_t image_base)
{
    std::filesystem::create_directories(report_path_.parent_path());
    report_.open(report_path_, std::ios::out | std::ios::trunc);
    if (!report_) return;
    report_ << "# HgCpu runtime coverage diagnostic\n\n"
            << "This file is local diagnostic evidence, not a qualification report. "
               "Raw native pointers and stream bytes are intentionally omitted.\n\n"
            << "- Writer RVA: `0x3841E0`\n"
            << "- Fighter extent sampled: `0x9751C` per lane\n"
            << "- Image base observed: `0x" << std::hex << image_base << std::dec << "`\n"
            << "- Maximum samples: " << maximum_samples << "\n\n"
            << "| Frame | Cursor | Write spans | Changed | Direct-source changed | "
               "Unmapped offsets (bounded) | Flags |\n"
            << "|---:|---:|---:|---:|---:|---|---|\n";
    header_written_ = true;
}

void HgCpuRuntimeDiagnostics::write_sample(const HgCpuCoverageSample& sample)
{
    if (!report_) return;
    for (std::size_t lane = 0; lane < unmapped_changed_by_page_.size(); ++lane)
    {
        for (std::size_t page = 0;
             page < unmapped_changed_by_page_[lane].size(); ++page)
        {
            unmapped_changed_by_page_[lane][page] +=
                sample.unmapped_changed_by_page[lane][page];
        }
    }
    report_ << "| " << sample.frame << " | " << sample.stream_cursor
            << " | " << sample.write_span_count << " | " << sample.changed_bytes
            << " | " << sample.directly_sourced_changed_bytes << " | ";
    const auto shown = std::min<std::size_t>(sample.unmapped_deltas.size(), 32);
    for (std::size_t i = 0; i < shown; ++i)
    {
        if (i != 0) report_ << ", ";
        const auto& delta = sample.unmapped_deltas[i];
        report_ << 'P' << static_cast<unsigned>(delta.target + 1)
                << "+0x" << std::hex << delta.offset << std::dec;
        if (delta.length > 1) report_ << "/0x" << std::hex << delta.length << std::dec;
    }
    if (sample.unmapped_deltas.size() > shown) report_ << ", …";
    report_ << " | ";
    if (sample.write_spans_truncated) report_ << "write-spans-truncated ";
    if (sample.unmapped_deltas_truncated) report_ << "deltas-truncated ";
    report_ << "|\n";
    if ((samples_ % 30) == 0) report_.flush();
}

void HgCpuRuntimeDiagnostics::write_page_summary()
{
    struct PageTotal
    {
        std::size_t lane{};
        std::size_t page{};
        std::uint64_t changed{};
    };
    std::vector<PageTotal> totals;
    for (std::size_t lane = 0; lane < unmapped_changed_by_page_.size(); ++lane)
    {
        for (std::size_t page = 0;
             page < unmapped_changed_by_page_[lane].size(); ++page)
        {
            const auto changed = unmapped_changed_by_page_[lane][page];
            if (changed != 0) totals.push_back({lane, page, changed});
        }
    }
    std::sort(totals.begin(), totals.end(), [](const PageTotal& left,
        const PageTotal& right) { return left.changed > right.changed; });
    report_ << "\n## Complete unmapped-change heat map\n\n"
            << "Every unmapped changed byte contributes to one 4-KiB page; "
               "this summary is not bounded by the per-frame range display.\n\n"
            << "| Rank | Lane | Offset | Changed-byte observations |\n"
            << "|---:|---:|---:|---:|\n";
    for (std::size_t rank = 0; rank < totals.size(); ++rank)
    {
        const auto& total = totals[rank];
        report_ << "| " << rank + 1 << " | P" << total.lane + 1
                << " | 0x" << std::hex
                << total.page * hgcpu_coverage_page_size << std::dec
                << " | " << total.changed << " |\n";
    }
}

Status HgCpuRuntimeDiagnostics::Observe(
    std::uintptr_t image_base, std::uint64_t frame) noexcept
{
    if (finished_ || samples_ >= maximum_samples) return Status::success();
    if (image_base == 0 || frame == last_frame_)
        return Status::failure(FailureCode::ContextUnavailable);
    std::array<std::uintptr_t, 2> roots{};
    if (!read_fighter_roots(image_base, roots))
        return Status::failure(FailureCode::ContextUnavailable);
    if (roots != fighter_roots_)
    {
        const auto status = bind_generation(roots);
        if (!status.ok()) return status;
    }
    if (!header_written_) write_header(image_base);
    if (!report_) return Status::failure(FailureCode::CaptureFailed);

    HgCpuGenerationContext context{
        0xF8904E4B04BCA3B4ull,
        Schema::snapshot_schema_version,
        1,
        generation_,
        {roots[0], roots[1]},
        1,
    };
    HgCpuCoverageSample sample{};
    const auto writer = reinterpret_cast<HgCpuExecFn>(image_base + 0x3841E0);
    const auto status = probe_->Observe(writer, context, frame, sample);
    if (!status.ok()) return status;
    last_frame_ = frame;
    ++samples_;
    write_sample(sample);
    if (samples_ >= maximum_samples) Finish();
    return Status::success();
}

void HgCpuRuntimeDiagnostics::Finish() noexcept
{
    if (finished_) return;
    finished_ = true;
    if (report_)
    {
        write_page_summary();
        report_ << "\nCaptured samples: " << samples_ << ".\n";
        report_.flush();
        report_.close();
    }
    probe_->Reset();
}
}
