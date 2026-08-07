#include "ReplayMatrixBankRing.hpp"

#include <cassert>

int main()
{
    using namespace Horse;

    for (uint8_t slot = 0; slot < 3; ++slot)
    {
        ReplayMatrixBankRingState state {
            slot,
            slot,
            PreviousReplayMatrixBankSlot(slot),
            0,
        };
        assert(ValidateReplayMatrixBankRingState(state));
    }

    assert(PreviousReplayMatrixBankSlot(0) == 1);
    assert(PreviousReplayMatrixBankSlot(1) == 2);
    assert(PreviousReplayMatrixBankSlot(2) == 0);

    assert(!ValidateReplayMatrixBankRingState({3, 0, 1, 0}));
    assert(!ValidateReplayMatrixBankRingState({0, 1, 1, 0}));
    assert(!ValidateReplayMatrixBankRingState({0, 0, 2, 0}));

    assert(ReplayMatrixBankHistorySourceTick(1101, 1) == 1100);
    assert(ReplayMatrixBankHistorySourceTick(1101, 2) == 1099);
    assert(ReplayMatrixBankHistorySourceTick(1085, 1) == 1084);
    assert(ReplayMatrixBankHistorySourceTick(1085, 2) == 1083);
    assert(ReplayMatrixBankHistorySourceTick(-1, 1) == -1);
    assert(ReplayMatrixBankHistorySourceTick(0, 1) == -1);
    assert(ReplayMatrixBankHistorySourceTick(1, 2) == -1);
    assert(ReplayMatrixBankHistorySourceTick(1101, 0) == -1);
    assert(ReplayMatrixBankHistorySourceTick(1101, 3) == -1);

    const ReplayMatrixBankFrameIdentity restored_origin {
        1085, 1085, 0, 1088,
    };
    const ReplayMatrixBankFrameIdentity origin_age1 {
        1084, 1084, 0, 1087,
    };
    const ReplayMatrixBankFrameIdentity origin_age2 {
        1083, 1083, 0, 1086,
    };
    const ReplayMatrixBankFrameIdentity final_target_age1 {
        1100, 1100, 0, 1103,
    };
    assert(ClassifyReplayMatrixBankHistorySource(
               restored_origin, origin_age1, 1)
           == ReplayMatrixBankHistorySourceDisposition::Required);
    assert(ClassifyReplayMatrixBankHistorySource(
               restored_origin, origin_age2, 2)
           == ReplayMatrixBankHistorySourceDisposition::Required);
    assert(ClassifyReplayMatrixBankHistorySource(
               restored_origin, final_target_age1, 1)
           == ReplayMatrixBankHistorySourceDisposition::Invalid);

    const ReplayMatrixBankFrameIdentity round_start {
        2500, 2500, 1, 0,
    };
    const ReplayMatrixBankFrameIdentity prior_round {
        2499, 2499, 0, 2400,
    };
    assert(ClassifyReplayMatrixBankHistorySource(
               round_start, prior_round, 1)
           == ReplayMatrixBankHistorySourceDisposition::RoundBoundary);

    const ReplayMatrixBankFrameIdentity round_second_frame {
        2501, 2501, 1, 1,
    };
    const ReplayMatrixBankFrameIdentity same_round_age1 {
        2500, 2500, 1, 0,
    };
    assert(ClassifyReplayMatrixBankHistorySource(
               round_second_frame, same_round_age1, 1)
           == ReplayMatrixBankHistorySourceDisposition::Required);
    assert(ClassifyReplayMatrixBankHistorySource(
               round_second_frame, prior_round, 2)
           == ReplayMatrixBankHistorySourceDisposition::RoundBoundary);

    const ReplayMatrixBankFrameIdentity missing_source {};
    assert(ClassifyReplayMatrixBankHistorySource(
               restored_origin, missing_source, 1)
           == ReplayMatrixBankHistorySourceDisposition::Invalid);
    assert(ClassifyReplayMatrixBankHistorySource(
               round_start, missing_source, 1)
           == ReplayMatrixBankHistorySourceDisposition::RoundBoundary);

    const ReplayMatrixBankFrameIdentity mid_round_capture_origin {
        0, 500, 1, 100,
    };
    assert(ClassifyReplayMatrixBankHistorySource(
               mid_round_capture_origin, missing_source, 1)
           == ReplayMatrixBankHistorySourceDisposition::Invalid);
    return 0;
}
