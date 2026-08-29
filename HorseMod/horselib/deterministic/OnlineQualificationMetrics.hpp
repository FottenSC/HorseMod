#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace Horse::Deterministic
{
class OnlineQualificationMetrics final
{
public:
    struct Status
    {
        std::uint64_t correction_samples{};
        std::uint64_t correction_p50_ns{};
        std::uint64_t correction_p95_ns{};
        std::uint64_t correction_p99_ns{};
        std::uint64_t correction_max_ns{};
        std::size_t pre_match_owned_bytes{};
        std::size_t status4_owned_bytes{};
        std::size_t maximum_owned_bytes{};
        std::uint64_t post_status4_growth_events{};
        std::uint64_t capacity_failures{};
    };

    void Reset() noexcept { *this = {}; }

    void BeginStatus4(std::size_t owned_bytes) noexcept
    {
        if (status4_owned_bytes_ == 0) status4_owned_bytes_ = owned_bytes;
        ObserveOwnedBytes(owned_bytes);
    }

    void SetPreMatchOwnedBytes(std::size_t owned_bytes) noexcept
    {
        pre_match_owned_bytes_ = owned_bytes;
        ObserveOwnedBytes(owned_bytes);
    }

    void ObserveOwnedBytes(std::size_t owned_bytes) noexcept
    {
        maximum_owned_bytes_ = (std::max)(maximum_owned_bytes_, owned_bytes);
        if (status4_owned_bytes_ != 0 && owned_bytes > status4_owned_bytes_)
            ++post_status4_growth_events_;
    }

    void RecordCorrection(std::uint64_t nanoseconds) noexcept
    {
        const auto bucket = static_cast<std::size_t>((std::min)(
            nanoseconds / bucket_width_ns,
            static_cast<std::uint64_t>(bucket_count - 1)));
        ++buckets_[bucket];
        ++correction_samples_;
        correction_max_ns_ = (std::max)(correction_max_ns_, nanoseconds);
    }

    void RecordCapacityFailure() noexcept { ++capacity_failures_; }

    [[nodiscard]] Status status() const noexcept
    {
        return {correction_samples_, Percentile(50), Percentile(95),
            Percentile(99), correction_max_ns_, pre_match_owned_bytes_,
            status4_owned_bytes_,
            maximum_owned_bytes_, post_status4_growth_events_,
            capacity_failures_};
    }

private:
    [[nodiscard]] std::uint64_t Percentile(std::uint64_t percentile) const noexcept
    {
        if (correction_samples_ == 0) return 0;
        const auto target = (correction_samples_ * percentile + 99) / 100;
        std::uint64_t cumulative{};
        for (std::size_t index = 0; index < buckets_.size(); ++index)
        {
            cumulative += buckets_[index];
            if (cumulative >= target) return (index + 1) * bucket_width_ns;
        }
        return bucket_count * bucket_width_ns;
    }

    static constexpr std::uint64_t bucket_width_ns = 10'000;
    static constexpr std::size_t bucket_count = 10'002;
    std::array<std::uint64_t, bucket_count> buckets_{};
    std::uint64_t correction_samples_{};
    std::uint64_t correction_max_ns_{};
    std::size_t pre_match_owned_bytes_{};
    std::size_t status4_owned_bytes_{};
    std::size_t maximum_owned_bytes_{};
    std::uint64_t post_status4_growth_events_{};
    std::uint64_t capacity_failures_{};
};
}
