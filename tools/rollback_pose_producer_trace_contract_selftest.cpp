#include "RollbackPoseProducerTraceContract.hpp"

#include <cassert>

using namespace Horse;

static RollbackPoseProducerTraceLedger begin()
{
    RollbackPoseProducerTraceLedger ledger;
    assert(ledger.admit(
        RollbackPoseProducerCheckpoint::TickMainEnter));
    return ledger;
}

int main()
{
    auto early = begin();
    assert(early.admit(
        RollbackPoseProducerCheckpoint::TickMainExit));
    assert(early.complete());

    auto base = begin();
    assert(base.admit(
        RollbackPoseProducerCheckpoint::FinalizeEnter));
    assert(base.admit(
        RollbackPoseProducerCheckpoint::FinalizeExit));
    assert(base.admit(
        RollbackPoseProducerCheckpoint::TickMainExit));
    assert(base.complete());

    auto overlay = begin();
    assert(overlay.admit(
        RollbackPoseProducerCheckpoint::FinalizeEnter));
    assert(overlay.admit(
        RollbackPoseProducerCheckpoint::FinalizeExit));
    assert(overlay.admit(
        RollbackPoseProducerCheckpoint::EvaluateEnter));
    assert(overlay.admit(
        RollbackPoseProducerCheckpoint::EvaluateExit));
    assert(overlay.admit(
        RollbackPoseProducerCheckpoint::TickMainExit));
    assert(overlay.complete());

    auto duplicate = begin();
    assert(duplicate.admit(
        RollbackPoseProducerCheckpoint::FinalizeEnter));
    assert(!duplicate.admit(
        RollbackPoseProducerCheckpoint::FinalizeEnter));
    assert(!duplicate.complete());

    auto reordered = begin();
    assert(!reordered.admit(
        RollbackPoseProducerCheckpoint::EvaluateEnter));
    assert(!reordered.complete());

    auto missing_exit = begin();
    assert(missing_exit.admit(
        RollbackPoseProducerCheckpoint::FinalizeEnter));
    assert(!missing_exit.admit(
        RollbackPoseProducerCheckpoint::TickMainExit));
    assert(!missing_exit.complete());

    auto post_complete = begin();
    assert(post_complete.admit(
        RollbackPoseProducerCheckpoint::TickMainExit));
    assert(post_complete.complete());
    assert(!post_complete.admit(
        RollbackPoseProducerCheckpoint::EvaluateEnter));
    assert(!post_complete.complete());
    return 0;
}
