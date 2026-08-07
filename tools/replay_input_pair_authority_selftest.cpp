#include "ReplayInputPairAuthority.hpp"

#include <cassert>
#include <iostream>

int main()
{
    using Horse::ReconcileReplayLatestEngineInputPair;
    using Horse::ReplayInputPairRepairPhaseActive;
    using Horse::ReplayInputPairRepairResult;
    using Horse::ReplayLatestEngineInputPairMatches;

    assert(ReplayInputPairRepairPhaseActive(true, false, false, false));
    assert(ReplayInputPairRepairPhaseActive(false, true, true, true));
    assert(!ReplayInputPairRepairPhaseActive(false, true, true, false));
    assert(!ReplayInputPairRepairPhaseActive(false, true, false, true));

    assert(ReplayLatestEngineInputPairMatches(2, 4, 2, 4));
    assert(!ReplayLatestEngineInputPairMatches(
        2, 0x0000000200000002ull, 2, 2));
    assert(!ReplayLatestEngineInputPairMatches(
        0x0000000200000002ull, 4, 2, 4));
    assert(!ReplayLatestEngineInputPairMatches(2, 3, 2, 4));

    uint64_t live[2] {2, 0x0000000200000002ull};
    int writes = 0;
    auto write_pair = [&](uint64_t p1, uint64_t p2) noexcept
    {
        ++writes;
        live[0] = p1;
        live[1] = p2;
        return true;
    };
    assert(ReconcileReplayLatestEngineInputPair(
               live[0], live[1], 2, 2, write_pair)
           == ReplayInputPairRepairResult::Repaired);
    assert(writes == 1 && live[0] == 2 && live[1] == 2);
    assert(ReconcileReplayLatestEngineInputPair(
               live[0], live[1], 2, 2, write_pair)
           == ReplayInputPairRepairResult::Exact);
    assert(writes == 1);

    const auto fail_write = [](uint64_t, uint64_t) noexcept
    {
        return false;
    };
    assert(ReconcileReplayLatestEngineInputPair(
               0x0000000200000002ull, 4, 2, 4, fail_write)
           == ReplayInputPairRepairResult::Failed);

    std::cout << "replay input pair authority selftest passed\n";
    return 0;
}
