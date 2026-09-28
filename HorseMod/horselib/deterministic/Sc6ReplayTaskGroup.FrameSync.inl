// 1421876D0 and 1415EFEC0/1415ED580. Application tail stays on its original
// stack. Replace only the native GT drains with the existing admitted pump.
void Sc6ReplayTaskGroup::DrainFrameTasks()
{
    if (phase_ != Phase::FrameSync || pumping_ || pending_task_ || return_task_ || consumer_task_.task)
        __fastfail(FAST_FAIL_INVALID_ARG);
    BeginPump();
    while (pumping_) {
        PumpOne(true);
        // This synchronous tail cannot hand a consumer hold to a later app.
        // Fail with the task/protection still owned; never execute or cancel it.
        if (consumer_task_.task) __fastfail(FAST_FAIL_INVALID_ARG);
    }
    if (pending_task_ || return_task_) __fastfail(FAST_FAIL_INVALID_ARG);
}

void Sc6ReplayTaskGroup::WaitFrameFence(void** slot, bool process_tasks)
{
    auto* image = reinterpret_cast<void*>(base_);
    auto& depth = Field<int>(image, 0x4351b88);
    if (depth < 0 || depth == INT_MAX) __fastfail(FAST_FAIL_INVALID_ARG);
    if (!process_tasks || depth) {
        // Native disables pumping for an already busy GT or nested fence wait.
        Native(base_, 0x15efec0, slot, false);
        return;
    }
    if (!Field<bool>(image, 0x4351840)) return;
    Native(base_, 0x15e9e30); // Native health, exception and Windows message work.
    if (!*slot || EventComplete(*slot)) {
        auto* completed = *slot;
        *slot = nullptr;
        ReleaseEvent(completed);
        return;
    }
    Native(base_, 0x15ef6f0); // Flush the shared immediate fence batch, if any.
    auto* fence = *slot;
    if (!fence) __fastfail(FAST_FAIL_INVALID_ARG);
    // The native public Wait tail-jumps to its helper on this branch. Even if
    // completion races the flush, this path retains the fence slot/reference.
    if (EventComplete(fence)) return;
    ++depth;
    process_tasks = !Virtual<bool>(Native<void*>(base_, 0xd24400), 0x20, 2);
    auto* event = Native<void*>(base_, 0xd28aa0, false);
    if (!event) __fastfail(FAST_FAIL_INVALID_ARG);
    // Native bridge adds the actual fence prerequisite and signals this pooled
    // OS event on completion. Submission/polling never substitutes a signal.
    Native(base_, 0x15efd70, Native<void*>(base_, 0xd24400), event, slot, 2);
    const auto configured_slice = Field<unsigned>(image, 0x4090840);
    const auto slice = configured_slice < 33u ? configured_slice : 33u;
    const double start = FrameWaitClock();
    const double deadline = start + static_cast<double>(Field<int>(image, 0x4090844))
        * Field<double>(image, 0x34d0c40);
    for (;;) {
        Native(base_, 0x15e9e30);
        if (*slot != fence) __fastfail(FAST_FAIL_INVALID_ARG);
        if (process_tasks) DrainFrameTasks();
        if (*slot != fence) __fastfail(FAST_FAIL_INVALID_ARG);
        if (Virtual<bool>(event, 0x20, slice, false)) break;
        // Preserve the native process-lifetime nothreadtimeout cache and its
        // own CRT initialization, rather than conflating it with nothreading.
        auto& guard = Field<int>(image, 0x4351b90);
        const auto epoch = InterlockedCompareExchange(&Field<LONG>(image, 0x4351b90), 0, 0);
        // Completed MSVC epochs can be negative; only 0/-1 are uninitialized
        // or in progress. The atomic read publishes the cached value as well.
        if (epoch == 0 || epoch == -1) {
            Native(base_, 0x3119d9c, &guard);
            if (guard == -1) {
                Field<bool>(image, 0x4351b8c) = Native<bool>(base_, 0xdd02f0,
                    Native<const wchar_t*>(base_, 0xda6170), L"nothreadtimeout");
                Native(base_, 0x3119d3c, &guard);
            }
        }
        if (FrameWaitClock() >= deadline
            && Native<bool>(base_, 0xd4ebe0, Native<void*>(base_, 0xd470f0))
            && !Field<bool>(image, 0x4351b8c)) ReportFrameWaitTimeout(start);
    }
    // No timeout, empty queue or task return can release this event. Both its
    // native signal and actual fence completion are required before pool return.
    if (*slot != fence || !EventComplete(fence)) __fastfail(FAST_FAIL_INVALID_ARG);
    Native(base_, 0xd31540, event);
    --depth;
}

void Sc6ReplayTaskGroup::SyncFrame(std::uintptr_t base, void* state, bool allow_one_frame_lag)
{
    if (!idle() || pumping_ || pending_task_ || release_event_ || return_task_
        || consumer_task_.task || consumer_executing_) __fastfail(FAST_FAIL_INVALID_ARG);
    base_ = base;
    auto& cursor = Field<int>(state, 0x10);
    if (cursor < 0 || cursor > 1) __fastfail(FAST_FAIL_INVALID_ARG);
    phase_ = Phase::FrameSync;
    auto** slots = static_cast<void**>(state);
    Native(base_, 0x15e9510, slots + cursor, true);
    const bool busy = Virtual<bool>(Native<void*>(base_, 0xd24400), 0x20, 2);
    if (!busy) DrainFrameTasks();
    if (cursor < 0 || cursor > 1) __fastfail(FAST_FAIL_INVALID_ARG);
    if (allow_one_frame_lag) cursor = (cursor + 1) & 1;
    WaitFrameFence(slots + cursor, !busy);
    phase_ = Phase::Idle;
}
