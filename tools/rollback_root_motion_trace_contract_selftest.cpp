#include "RollbackRootMotionTraceContract.hpp"

#include <cassert>

using namespace Horse;

int main()
{
    assert(ClassifyRollbackRootMotionSampleReturnRva(0x33D030)
        == RollbackRootMotionSamplePhase::PushOverrideEarly);
    assert(ClassifyRollbackRootMotionSampleReturnRva(0x33D6E0)
        == RollbackRootMotionSamplePhase::CommonP1);
    assert(ClassifyRollbackRootMotionSampleReturnRva(0x33D6EC)
        == RollbackRootMotionSamplePhase::CommonP2);
    assert(ClassifyRollbackRootMotionSampleReturnRva(0x33D6E1)
        == RollbackRootMotionSamplePhase::Unknown);

    RollbackRootMotionTraceLedger ordinary;
    auto p1 = ordinary.admit(
        RollbackRootMotionSamplePhase::CommonP1, 0, 10);
    assert(p1.began_transaction);
    assert(p1.transaction_active);
    assert(p1.transaction_id == 10);
    assert(p1.call_ordinal == 1);
    auto p2 = ordinary.admit(
        RollbackRootMotionSamplePhase::CommonP2, 1, 0);
    assert(!p2.began_transaction);
    assert(p2.transaction_active);
    assert(p2.transaction_id == 10);
    assert(p2.call_ordinal == 1);
    assert(p2.ended_transaction);
    assert(!ordinary.active());

    RollbackRootMotionTraceLedger push_override;
    auto early = push_override.admit(
        RollbackRootMotionSamplePhase::PushOverrideEarly, 0, 20);
    assert(early.began_transaction);
    assert(early.call_ordinal == 1);
    p1 = push_override.admit(
        RollbackRootMotionSamplePhase::CommonP1, 0, 0);
    assert(!p1.began_transaction);
    assert(p1.transaction_id == 20);
    assert(p1.call_ordinal == 2);
    p2 = push_override.admit(
        RollbackRootMotionSamplePhase::CommonP2, 1, 0);
    assert(p2.call_ordinal == 1);
    assert(p2.ended_transaction);

    RollbackRootMotionTraceLedger outer_owned;
    outer_owned.begin_outer_transaction(30);
    p1 = outer_owned.admit(
        RollbackRootMotionSamplePhase::CommonP1, 0, 0);
    assert(!p1.began_transaction);
    assert(p1.transaction_id == 30);
    assert(p1.call_ordinal == 1);
    outer_owned.finish_outer_transaction();
    assert(!outer_owned.active());

    RollbackRootMotionTraceLedger unknown;
    auto invalid = unknown.admit(
        RollbackRootMotionSamplePhase::Unknown, 0, 40);
    assert(!invalid.transaction_active);
    assert(!invalid.began_transaction);
    assert(invalid.transaction_id == 0);
    assert(invalid.call_ordinal == 0);
    return 0;
}
