#include "RollbackHistoricalCameraArgs.hpp"

#include <cstdint>
#include <iostream>

namespace
{
    using History = Horse::RollbackHistoricalCameraArgs<128>;

    History::Bytes bytes(uint8_t seed)
    {
        History::Bytes out {};
        for (size_t index = 0; index < out.size(); ++index)
            out[index] = static_cast<uint8_t>(seed + index);
        return out;
    }

    bool equal(const History::Bytes& left, const History::Bytes& right)
    {
        return left == right;
    }
}

int main()
{
    History history {};
    History::Bytes selected {};
    const History::Bytes first = bytes(0x10);
    const History::Bytes changed = bytes(0x80);

    auto report = history.select(7, 10, false, &first, selected);
    if (!report.ok || !report.captured || report.replayed
        || !equal(selected, first))
        return 1;

    report = history.select(7, 10, false, &changed, selected);
    if (!report.ok || report.captured || !report.replayed
        || !equal(selected, first))
        return 2;

    report = history.select(7, 10, true, nullptr, selected);
    if (!report.ok || !report.replayed || !equal(selected, first))
        return 3;

    report = history.select(7, 11, true, nullptr, selected);
    if (report.ok || report.failure != Horse::
            RollbackHistoricalCameraArgsFailure::MissingRollbackFrame)
        return 4;

    const History::Bytes next = bytes(0x20);
    report = history.select(7, 138, false, &next, selected);
    if (!report.ok || !report.captured || !equal(selected, next))
        return 5;

    report = history.select(7, 10, true, nullptr, selected);
    if (report.ok || report.failure != Horse::
            RollbackHistoricalCameraArgsFailure::MissingRollbackFrame)
        return 6;

    const History::Bytes stale = bytes(0x30);
    report = history.select(7, 10, false, &stale, selected);
    if (report.ok || report.failure != Horse::
            RollbackHistoricalCameraArgsFailure::RetentionCollision)
        return 7;

    report = history.select(8, 10, false, &stale, selected);
    if (!report.ok || !report.captured || !equal(selected, stale))
        return 8;

    history.clear();
    report = history.select(8, 10, true, nullptr, selected);
    if (report.ok || report.failure != Horse::
            RollbackHistoricalCameraArgsFailure::MissingRollbackFrame)
        return 9;

    report = history.select(0, 0, false, &first, selected);
    if (report.ok || report.failure != Horse::
            RollbackHistoricalCameraArgsFailure::InvalidArgument)
        return 10;

    std::cout << "rollback historical camera args self-test passed\n";
    return 0;
}
