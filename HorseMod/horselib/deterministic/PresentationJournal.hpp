#pragma once

#include "Interfaces.hpp"

#include <compare>
#include <memory>
#include <span>

namespace Horse::Deterministic
{
class PresentationJournal final : public IPresentationJournal
{
public:
    struct Statistics
    {
        std::uint64_t attempted{};
        std::uint64_t recorded{};
        std::uint64_t duplicates{};
        std::uint64_t capacity_failures{};
        std::uint64_t discarded{};
        std::uint64_t committed{};
        std::uint64_t speculative_presented{};
        std::uint64_t speculative_reused{};
        std::uint64_t publish_failures{};
        FailureCode first_publish_failure{FailureCode::None};
        PresentationEvent first_failed_event{};
        FailureCode last_publish_failure{FailureCode::None};
        PresentationEvent last_failed_event{};
    };

    PresentationJournal(
        std::size_t maximum_events,
        std::size_t maximum_payload_bytes) noexcept;

    Status Record(PresentationEvent event) noexcept override;
    // Record an event whose native terminal already ran during speculative
    // authoritative/predicted simulation. Confirmation advances exactly-once
    // metadata without publishing the terminal a second time.
    Status RecordPresented(PresentationEvent event) noexcept;
    void DiscardFrom(FrameCoordinate coordinate) noexcept override;
    Status CommitThrough(
        FrameCoordinate confirmed,
        IPresentationSink& sink) noexcept override;
    // Atomically capacity-check and replace an uncommitted same-generation
    // suffix. No slot is changed when validation or capacity preflight fails.
    Status ReplaceFrom(FrameCoordinate coordinate,
        std::span<const PresentationEvent> replacement) noexcept;
    void InvalidateGeneration(std::uint64_t generation) noexcept override;

    [[nodiscard]] std::size_t pending_count() const noexcept;
    [[nodiscard]] std::size_t payload_bytes() const noexcept;
    [[nodiscard]] std::size_t capacity() const noexcept;
    [[nodiscard]] std::size_t allocated_bytes() const noexcept
    {
        return maximum_events_
            * (sizeof(Slot) + sizeof(Watermark) + sizeof(bool));
    }
    [[nodiscard]] Statistics statistics() const noexcept;
    [[nodiscard]] Status ResetStatistics() noexcept;

private:
    struct EventKey
    {
        FrameCoordinate coordinate{};
        std::uint32_t source_ordinal{};
        std::uint32_t kind{};
        std::uint64_t identity{};

        friend constexpr auto operator<=>(const EventKey&, const EventKey&) = default;
    };

    struct Slot
    {
        bool occupied{};
        bool presented{};
        PresentationEvent event{};
    };

    struct Watermark
    {
        bool occupied{};
        std::uint64_t generation{};
        std::uint64_t frame{};
        std::uint32_t source_ordinal{};
    };

    [[nodiscard]] Watermark* FindWatermark(std::uint64_t generation) noexcept;
    [[nodiscard]] const Watermark* FindWatermark(
        std::uint64_t generation) const noexcept;
    [[nodiscard]] Watermark* EnsureWatermark(std::uint64_t generation) noexcept;
    [[nodiscard]] bool IsCommitted(const PresentationEvent& event) const noexcept;
    [[nodiscard]] static EventKey Key(const PresentationEvent& event) noexcept;
    [[nodiscard]] static bool Valid(const PresentationEvent& event) noexcept;
    Status RecordInternal(PresentationEvent event, bool presented) noexcept;
    // Occupied journal slots are kept in [0, pending_count_). Removal swaps
    // the final occupied slot into the hole. Event ordering is derived from
    // EventKey, never physical slot order, so this preserves semantics while
    // keeping the hot-path work proportional to pending events.
    void ClearSlot(std::size_t index) noexcept;

    std::size_t maximum_events_{};
    std::size_t maximum_payload_bytes_{};
    std::size_t payload_bytes_{};
    std::size_t pending_count_{};
    std::size_t watermark_count_{};
    std::unique_ptr<Slot[]> slots_;
    std::unique_ptr<Watermark[]> watermarks_;
    std::unique_ptr<bool[]> replacement_presented_;
    Statistics statistics_{};
};
}
