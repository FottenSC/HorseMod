void Sc6ReplayTaskGroup::ValidatePendingTask()
{
    const auto phase = simulation_->continuation().phase;
    if (!pending_task_ || !Sc6ReplayExecutor::IsPendingBoundary(phase)
        || Field<void*>(Field<void*>(pending_task_, 0x10), 0x18) != pending_task_
        || !Field<std::uint8_t>(pending_task_, 0x38)
        || EventComplete(Field<void*>(pending_task_, 0x40)))
        __fastfail(FAST_FAIL_INVALID_ARG);
}

void Sc6ReplayTaskGroup::ResumeManagerTask()
{
    ValidatePendingTask();
    ReplayVfxExecutionScope vfx_execution(3,pending_task_,pending_task_+0x40,
        Field<std::uint32_t>(named_thread_,0x10),_ReturnAddress());
    const auto status = yield_each_tick_ ? simulation_->AdvanceOneTick() : simulation_->DrainInterval();
    if (!status.ok()) __fastfail(FAST_FAIL_INVALID_ARG);
    if (simulation_->interval_complete()) FinishManagerTask();
    else {
        ValidatePendingTask();
        // This counter is compared with completed-traversal yields. A bounded
        // tail deferral retains the task but has not traversed another tick.
        if(!simulation_->tail_blocked()) ++manager_yields_;
    }
}

