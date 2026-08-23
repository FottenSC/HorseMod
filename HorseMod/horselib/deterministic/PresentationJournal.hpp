#pragma once

#include "Interfaces.hpp"

#include <map>
#include <set>
#include <compare>

namespace Horse::Deterministic
{
class PresentationJournal final : public IPresentationJournal
{
public:
    PresentationJournal(
        std::size_t maximum_events,
        std::size_t maximum_payload_bytes) noexcept;

    Status Record(PresentationEvent event) noexcept override;
    void DiscardFrom(FrameCoordinate coordinate) noexcept override;
    Status CommitThrough(
        FrameCoordinate confirmed,
        IPresentationSink& sink) noexcept override;
    void InvalidateGeneration(std::uint64_t generation) noexcept override;

    [[nodiscard]] std::size_t pending_count() const noexcept;

private:
    struct EventKey
    {
        FrameCoordinate coordinate{};
        std::uint32_t kind{};
        std::uint64_t identity{};

        friend constexpr auto operator<=>(const EventKey&, const EventKey&) = default;
    };

    std::size_t maximum_events_{};
    std::size_t maximum_payload_bytes_{};
    std::size_t payload_bytes_{};
    std::map<EventKey, PresentationEvent> pending_;
    std::set<EventKey> committed_;
};
}
