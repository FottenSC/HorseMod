#pragma once

#include "HgCpuCoverageProbe.hpp"

#include <filesystem>
#include <fstream>
#include <memory>

namespace Horse::Deterministic
{
class HgCpuRuntimeDiagnostics
{
public:
    explicit HgCpuRuntimeDiagnostics(std::filesystem::path report_path);
    ~HgCpuRuntimeDiagnostics();

    Status Observe(std::uintptr_t image_base, std::uint64_t frame) noexcept;
    void Finish() noexcept;
    [[nodiscard]] bool complete() const noexcept { return samples_ >= maximum_samples; }
    [[nodiscard]] const std::filesystem::path& report_path() const noexcept { return report_path_; }

private:
    class ProcessMemory;

    bool read_fighter_roots(
        std::uintptr_t image_base,
        std::array<std::uintptr_t, 2>& roots) noexcept;
    Status bind_generation(const std::array<std::uintptr_t, 2>& roots) noexcept;
    void write_header(std::uintptr_t image_base);
    void write_sample(const HgCpuCoverageSample& sample);
    void write_page_summary();

    static constexpr std::size_t fighter_extent = 0x9751C;
    static constexpr std::size_t maximum_samples = 600;

    std::filesystem::path report_path_;
    std::unique_ptr<ProcessMemory> memory_;
    std::unique_ptr<HgCpuCoverageProbe> probe_;
    std::ofstream report_;
    std::array<std::uintptr_t, 2> fighter_roots_{};
    std::array<std::array<std::uint64_t,
        hgcpu_coverage_pages_per_target>, 2> unmapped_changed_by_page_{};
    std::uint64_t generation_{1};
    std::uint64_t last_frame_{~std::uint64_t{0}};
    std::size_t samples_{};
    bool header_written_{};
    bool finished_{};
};
}
