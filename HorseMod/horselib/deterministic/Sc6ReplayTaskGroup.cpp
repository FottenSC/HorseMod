#include "Sc6ReplayTaskGroup.hpp"
#include "Sc6ReplayExecutor.hpp"
#include "ReplayComponentTickConsumer.hpp"
#include "ReplayConsumerFailure.hpp"
#include "ReplayPhysicsStepConsumer.hpp"
#include "ReplayVfxExecutionScope.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <intrin.h>
#include <cstring>

namespace Horse::Deterministic
{
namespace
{
template<class T> T& Field(void* object, std::size_t offset)
{
    return *reinterpret_cast<T*>(static_cast<std::byte*>(object) + offset);
}
template<class R = void, class... A>
R Native(std::uintptr_t base, std::uintptr_t rva, A... args)
{
    return reinterpret_cast<R(__fastcall*)(A...)>(base + rva)(args...);
}
template<class R = void, class... A>
R Virtual(void* object, std::size_t slot, A... args)
{
    return reinterpret_cast<R(__fastcall*)(void*, A...)>(
        Field<std::uintptr_t>(Field<void*>(object, 0), slot))(object, args...);
}
bool EventComplete(void* event)
{
    return (InterlockedCompareExchange64(&Field<LONG64>(event, 8), 0, 0) & (1ll << 26)) != 0;
}
struct PumpDepth
{
    int& depth;
    std::uint8_t& native_return;
    std::uint8_t& owned_return;
    std::uint8_t saved_return;
    PumpDepth(int& value, std::uint8_t& native, std::uint8_t& owned)
        : depth(value), native_return(native), owned_return(owned), saved_return(native)
    {
        ++depth;
        native_return = owned_return;
    }
    ~PumpDepth()
    {
        owned_return = native_return;
        native_return = saved_return;
        --depth;
    }
};
}

bool Sc6ReplayTaskGroup::ParallelTasks() const
{
    return Native<bool>(base_, 0xdb7b80) && Native<int>(base_, 0xe08e20) >= 3;
}

double Sc6ReplayTaskGroup::FrameWaitClock() const
{
    auto* image = reinterpret_cast<void*>(base_);
    LARGE_INTEGER counter{};
    if (!Field<BOOL (WINAPI*)(LARGE_INTEGER*)>(image, 0x322c678)(&counter))
        __fastfail(FAST_FAIL_INVALID_ARG);
    return static_cast<double>(counter.QuadPart) * Field<double>(image, 0x415cd90)
        + Field<double>(image, 0x325bcc0);
}

void Sc6ReplayTaskGroup::ReportFrameWaitTimeout(double start)
{
    // Exact native log/error calls from 1415ED727..7B7. Varargs must use the
    // variadic ABI, including the duplicate floating argument in a GP register.
    using Log = void(__cdecl*)(const char*, int, const wchar_t*, ...);
    using Error = void(__cdecl*)(const void*, const char*, int, const wchar_t*, ...);
    reinterpret_cast<Log>(base_ + 0xdadc60)(reinterpret_cast<const char*>(base_ + 0x36bb1e0),
        0x428, reinterpret_cast<const wchar_t*>(base_ + 0x36bb160), FrameWaitClock() - start);
    reinterpret_cast<Error>(base_ + 0xd9d430)(reinterpret_cast<const void*>(base_ + 0x36b93f6),
        reinterpret_cast<const char*>(base_ + 0x36bb2c0), 0x428,
        reinterpret_cast<const wchar_t*>(base_ + 0x36bb240), FrameWaitClock() - start);
}

#include "Sc6ReplayTaskGroup.FrameSync.inl"

void Sc6ReplayTaskGroup::BindSimulation(void* actor, Sc6ReplayExecutor* simulation, bool yield_each_tick)
{
    if (!idle() || pending_task_ || return_task_ || consumer_task_.task) __fastfail(FAST_FAIL_INVALID_ARG);
    actor_ = actor;
    simulation_ = simulation;
    yield_each_tick_ = yield_each_tick;
    manager_tasks_ = manager_yields_ = 0;
}

void Sc6ReplayTaskGroup::BindConsumerAdmission(ConsumerAdmissionHooks hooks)
{
    if (!idle() || pumping_ || pending_task_ || consumer_task_.task
        || (hooks.acquire && (!hooks.validate || !hooks.begin_execution || !hooks.completed))
        || (!hooks.acquire && (hooks.validate || hooks.begin_execution || hooks.completed)))
        __fastfail(FAST_FAIL_INVALID_ARG);
    consumer_hooks_ = hooks;
}

bool Sc6ReplayTaskGroup::RequestConsumerResume() noexcept
{
    if (consumer_executing_ || !consumer_task_.task || GetCurrentThreadId() != consumer_task_.operating_thread
        || !consumer_hooks_.validate
        || !consumer_hooks_.validate(consumer_hooks_.context, consumer_task_)) return false;
    consumer_resume_requested_ = true;
    return true;
}

Sc6ReplayTaskGroup::DispatchOutcome Sc6ReplayTaskGroup::ResumeConsumerTask()
{
    if (!consumer_resume_requested_) return DispatchOutcome::ConsumerHeld;
    // Validate before touching borrowed payload. Native lifetime/execution
    // protection must remain owned through completed(), including this call.
    if (!consumer_hooks_.validate
        || GetCurrentThreadId() != consumer_task_.operating_thread
        || !consumer_hooks_.validate(consumer_hooks_.context, consumer_task_)) {
        consumer_resume_requested_ = false;
        return DispatchOutcome::ConsumerHeld;
    }
    const auto retained = consumer_task_;
    if (Field<std::uint32_t>(named_thread_, 0x10) != retained.native_thread
        || Field<std::uintptr_t>(retained.task, 0) != base_ + 0x39cd9c0
        || Field<void*>(retained.task, 0x10) != retained.function
        || Field<void*>(retained.task, 0x28) != retained.world
        || Field<void*>(retained.task, 0x40) != retained.completion
        || Field<void*>(retained.function, 0x18) != retained.task
        || !Field<std::uint8_t>(retained.task, 0x38)
        || !retained.completion
        || (InterlockedCompareExchange64(&Field<LONG64>(retained.completion, 8), 0, 0) & (1ll << 26)))
        __fastfail(FAST_FAIL_INVALID_ARG);
    if (!consumer_hooks_.begin_execution
        || !consumer_hooks_.begin_execution(consumer_hooks_.context, retained)) {
        consumer_resume_requested_ = false;
        return DispatchOutcome::ConsumerHeld;
    }
    consumer_executing_ = true;
    consumer_resume_requested_ = false;
    // Preserve the ordinary native branch and its event/refcount/TLS lifecycle.
    // No task storage may be inspected after the wrapper returns.
    Virtual(retained.task, 8, named_thread_ + 0x20, retained.native_thread);
    consumer_hooks_.completed(consumer_hooks_.context, retained);
    consumer_executing_ = false;
    consumer_task_ = {};
    consumer_resume_requested_ = false;
    return DispatchOutcome::Completed;
}

void Sc6ReplayTaskGroup::ReleaseEvent(void* event)
{
    if (event && InterlockedDecrement(&Field<LONG>(event, 0x48)) == 0)
        Native(base_, 0xd2ebe0, event);
}

void Sc6ReplayTaskGroup::Begin(std::uintptr_t base, void* manager, int group, bool wait)
{
    if (!idle() || pumping_ || release_event_ || return_task_ || consumer_task_.task || group < 0 || group > 6)
        __fastfail(FAST_FAIL_INVALID_ARG);
    base_ = base;
    manager_ = static_cast<std::byte*>(manager);
    sequencer_ = Field<std::byte*>(manager, 8);
    group_ = group;
    requested_wait_ = wait;
    spawn_sequence_ = false;
    spawn_passes_ = 0;
    // Use the same native command-line parser as 140D2D160. ONETHREAD and
    // nothreading are different options; do not conflate their two predicates.
    allow_stall_ = !Native<bool>(base_, 0xdd02f0,
        Native<const wchar_t*>(base_, 0xda6170), L"nothreading");
    phase_ = Phase::Release;
}

std::array<std::uint8_t,3> Sc6ReplayTaskGroup::PriorCleanupFlags() const
{
    auto* image = reinterpret_cast<void*>(base_);
    // 142167D10 publishes these before waiting on the retained cleanup array.
    const auto batching = std::uint8_t(*Field<int*>(image, 0x43b2070) != 0);
    const auto logging = std::uint8_t(*Field<int*>(image, 0x43b2088) != 0);
    const auto parallel = std::uint8_t(ParallelTasks() && *Field<int*>(image, 0x43b20a0) != 0);
    return {parallel, batching, logging};
}

bool Sc6ReplayTaskGroup::BeginPriorCleanup(std::uintptr_t base, void* manager,
    void* world, float delta, int tick_type)
{
    if (!idle() || pumping_ || pending_task_ || release_event_ || return_task_
        || consumer_task_.task || consumer_executing_) __fastfail(FAST_FAIL_INVALID_ARG);
    base_ = base;
    manager_ = static_cast<std::byte*>(manager);
    sequencer_ = Field<std::byte*>(manager, 8);
    auto* events = reinterpret_cast<EventArray*>(sequencer_ + 0x980);
    if (events->count == 0) return false;
    // There are eight native group arrays. A cleanup event is retained once
    // per released group until the next StartTasks. Reject a corrupt descriptor.
    if (events->count < 0 || events->count > int(prior_events_.size())
        || events->capacity < events->count || (!events->heap && events->count > 4))
        __fastfail(FAST_FAIL_INVALID_ARG);

    // Exact prefix of 142167C50, followed by 142167D10's flag publication.
    // Queued work must observe the same context as the original blocking wait.
    Field<float>(manager_, 0x20) = delta;
    Field<int>(manager_, 0x24) = tick_type;
    Field<int>(manager_, 0x28) = 0;
    Field<int>(manager_, 0x2c) = 2;
    Field<void*>(manager_, 0x30) = world;
    Field<std::uint8_t>(manager_, 0x38) = 1;
    auto* image = reinterpret_cast<void*>(base_);
    prior_flags_[1] = Field<std::uint8_t>(sequencer_, 0x9b5) = *Field<int*>(image, 0x43b2070) != 0;
    prior_flags_[2] = Field<std::uint8_t>(sequencer_, 0x9b6) = *Field<int*>(image, 0x43b2088) != 0;
    prior_flags_[0] = Field<std::uint8_t>(sequencer_, 0x9b4) = ParallelTasks() && *Field<int*>(image, 0x43b20a0) != 0;
    std::memcpy(prior_context_.data(), manager_ + 0x20, prior_context_.size());
    prior_array_ = *events;
    for (int i = 0; i < events->count; ++i) {
        prior_events_[i] = events->data()[i];
        if (!prior_events_[i]) __fastfail(FAST_FAIL_INVALID_ARG);
    }
    allow_stall_ = !Native<bool>(base_, 0xdd02f0,
        Native<const wchar_t*>(base_, 0xda6170), L"nothreading");
    BeginWait(events);
    phase_ = Phase::PriorCleanup;
    return true;
}

bool Sc6ReplayTaskGroup::PriorCleanupStable() const
{
    auto* events = reinterpret_cast<EventArray*>(sequencer_ + 0x980);
    if (waiting_events_ != events || Field<std::byte*>(manager_, 8) != sequencer_
        || std::memcmp(prior_context_.data(), manager_ + 0x20, prior_context_.size())
        || std::memcmp(prior_flags_.data(), sequencer_ + 0x9b4, prior_flags_.size())
        || prior_flags_ != PriorCleanupFlags()
        || std::memcmp(&prior_array_, events, sizeof(EventArray))) return false;
    for (int i = 0; i < events->count; ++i)
        if (events->data()[i] != prior_events_[i]) return false;
    return true;
}

bool Sc6ReplayTaskGroup::AdvancePriorCleanup()
{
    if (phase_ != Phase::PriorCleanup || !PriorCleanupStable()) __fastfail(FAST_FAIL_INVALID_ARG);
    PumpOne(false);
    if (pumping_) return false;
    auto* events = reinterpret_cast<EventArray*>(sequencer_ + 0x980);
    // Returning from an empty poll is insufficient. Require the real return
    // task, stable borrowed array/context, and actual cleanup-event completion.
    // This also makes repeating the native StartTasks prefix observationally
    // unchanged; callbacks/config changes cannot be silently overwritten.
    if (return_task_ || pending_task_ || consumer_task_.task || consumer_executing_
        || !PriorCleanupStable())
        __fastfail(FAST_FAIL_INVALID_ARG);
    for (int i = 0; i < events->count; ++i)
        if (events->data()[i] != prior_events_[i] || !EventComplete(prior_events_[i]))
            __fastfail(FAST_FAIL_INVALID_ARG);

    // Original 142167D10 release, after the exact native wait. This releases
    // retained references; it does not synthesize any event/task completion.
    Native(base_, 0x15e3830, events, 0);
    if (events->count != 0) __fastfail(FAST_FAIL_INVALID_ARG);
    waiting_events_ = nullptr;
    prior_events_ = {};
    prior_array_ = {};
    phase_ = Phase::Idle;
    return true;
}

void Sc6ReplayTaskGroup::ReleaseGroup()
{
    if (!ParallelTasks() || !*Field<int*>(reinterpret_cast<void*>(base_), 0x43b20d0))
    {
        Native(base_, 0x215d110, sequencer_, 2, group_);
        phase_ = Phase::SelectWait;
        return;
    }
    std::uintptr_t storage[3]{};
    auto* builder = Native<std::uintptr_t*>(base_, 0x215c730, storage, 0, 2);
    auto* task = reinterpret_cast<void*>(builder[0]);
    Field<void*>(task, 0x10) = sequencer_;
    Field<int>(task, 0x18) = group_;
    release_event_ = Field<void*>(task, 0x28);
    if (release_event_) InterlockedIncrement(&Field<LONG>(release_event_, 0x48));
    Native(base_, 0x2167760, task, reinterpret_cast<void*>(builder[1]), static_cast<int>(builder[2]), true);

    // 1403A2340 adds a separate reference for its one-element wait array.
    // Both leases survive an Advance return and are released after the wait.
    single_event_ = {};
    single_event_.count = 1;
    single_event_.capacity = 4;
    single_event_.inline_events[0] = release_event_;
    if (release_event_) InterlockedIncrement(&Field<LONG>(release_event_, 0x48));
    BeginWait(&single_event_);
    phase_ = Phase::WaitRelease;
}

void Sc6ReplayTaskGroup::BeginWait(EventArray* events)
{
    if (pumping_) __fastfail(FAST_FAIL_INVALID_ARG);
    waiting_events_ = events;
    if (events->count > 8)
    {
        bool complete = true;
        for (int i = 0; i < events->count; ++i)
            if (!EventComplete(events->data()[i])) { complete = false; break; }
        if (complete) return;
    }
    auto* graph = Native<void*>(base_, 0xd24400);
    named_thread_ = Field<std::byte*>(graph, 8 + 2 * 0x18);
    // The replay world is called directly by UGameEngine, outside a native
    // named-thread pump. A nested native blocking wait is a different owner.
    if (Field<int>(named_thread_, 0x3e0) != 0) __fastfail(FAST_FAIL_INVALID_ARG);

    // Keep native return-task ordering, including for already complete arrays
    // of up to eight events. Its prerequisites are actual completion events.
    std::uintptr_t storage[3]{};
    auto* builder = Native<std::uintptr_t*>(base_, 0x39a7b0, storage, events, 2);
    auto* task = reinterpret_cast<void*>(builder[0]);
    if (return_task_) __fastfail(FAST_FAIL_INVALID_ARG);
    return_task_ = task;
    Field<int>(task, 0x10) = 2;
    auto* event = Field<void*>(task, 0x18);
    if (event) InterlockedIncrement(&Field<LONG>(event, 0x48));
    Native(base_, 0x3a1ff0, task, reinterpret_cast<void*>(builder[1]), static_cast<int>(builder[2]), true);
    ReleaseEvent(event);
    BeginPump();
}

void Sc6ReplayTaskGroup::BeginPump()
{
    auto* graph = Native<void*>(base_, 0xd24400);
    named_thread_ = Field<std::byte*>(graph, 8 + 2 * 0x18);
    if (Field<int>(named_thread_, 0x3e0) != 0) __fastfail(FAST_FAIL_INVALID_ARG);
    pump_return_requested_ = 0;
    pumping_ = true;
}

void Sc6ReplayTaskGroup::PumpOne(bool until_idle)
{
    if (consumer_executing_) __fastfail(FAST_FAIL_INVALID_ARG);
    if (!pumping_) return;
    // A later application pump has its own return flag. Preserve ours as
    // continuation data, just as the world preserves its temporary arena.
    PumpDepth depth(Field<int>(named_thread_, 0x3e0),
        Field<std::uint8_t>(named_thread_, 0x3e4), pump_return_requested_);
    if (consumer_task_.task)
    {
        if (pending_task_) __fastfail(FAST_FAIL_INVALID_ARG);
        dispatch_outcome_ = ResumeConsumerTask();
        if (dispatch_outcome_ == DispatchOutcome::Completed) ++dispatched_tasks_;
        return; // Never dequeue later work while this task is retained.
    }
    if (pending_task_)
    {
        ResumeManagerTask();
        dispatch_outcome_ = pending_task_ ? DispatchOutcome::ManagerPending : DispatchOutcome::Completed;
        if (dispatch_outcome_ == DispatchOutcome::Completed) ++dispatched_tasks_;
        return;
    }
    if (Field<std::uint8_t>(named_thread_, 0x3e4)) { pumping_ = false; return; }
    auto* queue = named_thread_ + 0x38;
    for (;;)
    {
        auto& state = Field<LONG64>(queue, 0x320);
        const auto before = static_cast<std::uint64_t>(InterlockedCompareExchange64(&state, 0, 0));
        void* task{};
        for (int priority = 0; priority < 2 && !task; ++priority) {
            struct PopScope {
                const ConsumerAdmissionHooks& hooks;
                explicit PopScope(const ConsumerAdmissionHooks& h):hooks(h) {
                    if(hooks.pop_scope)hooks.pop_scope(hooks.context,true);
                }
                ~PopScope(){if(hooks.pop_scope)hooks.pop_scope(hooks.context,false);}
            } pop(consumer_hooks_);
            task = Native<void*>(base_, 0xd2b9f0, queue + priority * 0x190);
        }
        if (task)
        {
            dispatch_outcome_ = DispatchTask(task);
            if (dispatch_outcome_ == DispatchOutcome::TerminalFailure) {
                terminal_task_ = task;
                __fastfail(FAST_FAIL_INVALID_ARG);
            }
            if (dispatch_outcome_ == DispatchOutcome::Completed) ++dispatched_tasks_;
            else if (dispatch_outcome_ != DispatchOutcome::ManagerPending
                && dispatch_outcome_ != DispatchOutcome::ConsumerHeld)
                __fastfail(FAST_FAIL_INVALID_ARG);
            if (Field<std::uint8_t>(named_thread_, 0x3e4)) pumping_ = false;
            return;
        }
        if (until_idle || !allow_stall_) { pumping_ = false; return; }
        // Native 140D2CED0's empty-queue handshake preserves low bits 1..25,
        // increments the upper generation, and sets the stalled bit. The
        // apparently uninitialized decompiler bits are all overwritten.
        constexpr std::uint64_t generation_mask = ~0x3ffffffull;
        const auto generation = (before & generation_mask) + 0x4000000ull;
        if (generation < (before & generation_mask))
            Native(base_, 0xe1fa80, Field<float>(reinterpret_cast<void*>(base_), 0x3e89fd4));
        const auto stalled = generation | (before & 0x3fffffeull) | 1;
        if (static_cast<std::uint64_t>(InterlockedCompareExchange64(&state,
                static_cast<LONG64>(stalled), static_cast<LONG64>(before))) != before) continue;
        Virtual<bool>(Field<void*>(named_thread_, 0x3e8), 0x20, UINT32_MAX, false);
        if (Field<std::uint8_t>(named_thread_, 0x3e5) || Field<std::uint8_t>(named_thread_, 0x3e4))
        { pumping_ = false; return; }
    }
}

Sc6ReplayTaskGroup::DispatchOutcome Sc6ReplayTaskGroup::DispatchTask(void* task)
{
    const auto current_thread = Field<std::uint32_t>(named_thread_, 0x10);
    if (task == return_task_)
    {
        // 14039B310 sets the named-pump return flag, completes its own event,
        // and returns this storage to the TLS pool. Forget it before dispatch.
        if (Field<std::uintptr_t>(task, 0) != base_ + 0x325b948)
            __fastfail(FAST_FAIL_INVALID_ARG);
        return_task_ = nullptr;
    }
    if (Field<std::uintptr_t>(task, 0) != base_ + 0x39cd9c0)
    {
        // Initial/repeat PhysX substep delegates run on named thread 2. The
        // initial wrapper publishes B0; the repeat releases C8 before C40
        // schedules another event/task. Reject before either wrapper, without
        // signaling, releasing or abandoning any of those obligations.
        ReplayPhysicsStepConsumer::SubstepDiagnostic diagnostic;
        const auto reader=[](std::uintptr_t address,auto& value)noexcept {
            __try {if(!address)return false;std::memcpy(&value,reinterpret_cast<const void*>(address),sizeof(value));return true;}
            __except(EXCEPTION_EXECUTE_HANDLER){return false;}
        };
        if(!ReplayPhysicsStepConsumer::SubstepTask(base_,reinterpret_cast<std::uintptr_t>(task),reader,diagnostic)) {
            ReplayConsumerFailure::Record failure{};failure.site=ReplayConsumerFailure::Site::PhysicsSubstepConsumer;
            failure.reason=static_cast<unsigned>(diagnostic.check);failure.task=reinterpret_cast<std::uintptr_t>(task);
            failure.function=diagnostic.method;failure.owner=diagnostic.scene;
            failure.original_thread=GetCurrentThreadId();failure.native_thread=current_thread;
            failure.operands[0]=diagnostic.task_table;failure.operands[1]=diagnostic.callable;
            failure.operands[2]=diagnostic.field;failure.operands[3]=diagnostic.context;
            failure.operands[4]=diagnostic.completion;failure.operands[5]=diagnostic.count;failure.operands[6]=diagnostic.limit;
            ReplayConsumerFailure::Write(failure);return DispatchOutcome::TerminalFailure;
        }
        Virtual(task, 8, named_thread_ + 0x20, current_thread); return DispatchOutcome::Completed;
    }
    auto* function = Field<void*>(task, 0x10);
    ConsumerTask candidate{};
    auto admission=ConsumerAdmission::Forward;
    if (consumer_hooks_.acquire) {
        candidate.task = task;
        candidate.function = function;
        candidate.world = Field<void*>(task, 0x28);
        candidate.completion = Field<void*>(task, 0x40);
        candidate.native_thread = current_thread;
        candidate.operating_thread = GetCurrentThreadId();
        admission = consumer_hooks_.acquire(consumer_hooks_.context, candidate);
        if (admission == ConsumerAdmission::Reject) {
            ReplayConsumerFailure::Record failure{};failure.site=ReplayConsumerFailure::Site::DispatchAdmission;
            failure.task=reinterpret_cast<std::uintptr_t>(task);failure.function=reinterpret_cast<std::uintptr_t>(function);
            failure.owner=candidate.owner;failure.owner_index=candidate.owner_index;failure.owner_generation=candidate.owner_generation;
            failure.epoch=candidate.application_epoch;failure.original_thread=candidate.operating_thread;failure.native_thread=current_thread;
            ReplayConsumerFailure::Write(failure);
            return DispatchOutcome::TerminalFailure;
        }
        if (admission == ConsumerAdmission::Hold || admission == ConsumerAdmission::ForwardObserved) {
            if (consumer_task_.task || pending_task_ || !candidate.owner
                || candidate.owner_generation <= 0 || candidate.owner_index < 0 || !candidate.application_epoch
                || candidate.task != task || candidate.function != function
                || candidate.world != Field<void*>(task, 0x28)
                || candidate.completion != Field<void*>(task, 0x40)
                || candidate.native_thread != current_thread
                || candidate.operating_thread != GetCurrentThreadId())
                __fastfail(FAST_FAIL_INVALID_ARG);
        }
        if (admission == ConsumerAdmission::Hold) {
            consumer_task_ = candidate;
            consumer_resume_requested_ = false;
            return DispatchOutcome::ConsumerHeld;
        }
    }
    // No native payload entry or completion has occurred. Without an owned
    // suspension/abandonment protocol, a newly unsupported component consumer
    // is terminal. Do not dispatch, signal its event, recycle it or count it
    // completed. Fail-fast terminates the process without C++ teardown; this
    // is not complete-B recovery or a resumable consumer hold.
    const auto tick_type = Field<std::uintptr_t>(function, 0);
    if(tick_type==base_+0x39efa78) {
        ReplayPhysicsStepConsumer::Diagnostic diagnostic;
        const auto reader=[](std::uintptr_t address,auto& value)noexcept {
            __try {if(!address)return false;std::memcpy(&value,reinterpret_cast<const void*>(address),sizeof(value));return true;}
            __except(EXCEPTION_EXECUTE_HANDLER){return false;}
        };
        if(!ReplayPhysicsStepConsumer::StartTask(base_,reinterpret_cast<std::uintptr_t>(function),reader,diagnostic)) {
            ReplayConsumerFailure::Record failure{};failure.site=ReplayConsumerFailure::Site::PhysicsConsumer;
            failure.reason=static_cast<unsigned>(diagnostic.check);failure.task=reinterpret_cast<std::uintptr_t>(task);
            failure.function=reinterpret_cast<std::uintptr_t>(function);failure.owner=diagnostic.scene;
            failure.original_thread=GetCurrentThreadId();failure.native_thread=current_thread;
            failure.operands[0]=tick_type;failure.operands[1]=diagnostic.world;failure.operands[2]=diagnostic.field;
            ReplayConsumerFailure::Write(failure);return DispatchOutcome::TerminalFailure;
        }
    }
    ReplayComponentTickFailure component_failure{};
    if (tick_type == base_ + 0x3865f98
        && !ReplayComponentTickConsumerAdmitted(base_, tick_type,
            Field<std::uintptr_t>(function, 0x50),&component_failure))
    {
        ReplayConsumerFailure::Record failure{};failure.site=ReplayConsumerFailure::Site::ComponentConsumer;
        failure.task=reinterpret_cast<std::uintptr_t>(task);failure.function=reinterpret_cast<std::uintptr_t>(function);
        failure.owner=Field<std::uintptr_t>(function,0x50);failure.native_thread=current_thread;
        failure.original_thread=GetCurrentThreadId();failure.operands[0]=tick_type;
        failure.reason=component_failure.reason;
        failure.operands[1]=component_failure.table;failure.operands[2]=component_failure.dispatch;
        ReplayConsumerFailure::Write(failure);
        return DispatchOutcome::TerminalFailure;
    }
    const auto execute = Field<std::uintptr_t>(Field<void*>(function, 0), 8);
    if (execute != base_ + 0x1c15c90 || Field<void*>(function, 0x50) != actor_)
    {
        struct ActiveTaskScope {
            const ConsumerTask*& slot;const ConsumerTask* previous;
            ActiveTaskScope(const ConsumerTask*& s,const ConsumerTask& t):slot(s),previous(s){slot=&t;}
            ~ActiveTaskScope(){slot=previous;}
        };
        {ActiveTaskScope scope(executing_consumer_task_,candidate);
            Virtual(task,8,named_thread_+0x20,current_thread);}
        if(admission==ConsumerAdmission::ForwardObserved)consumer_hooks_.completed(consumer_hooks_.context,candidate);
        return DispatchOutcome::Completed;
    }
    if(admission==ConsumerAdmission::ForwardObserved)__fastfail(FAST_FAIL_INVALID_ARG);
    if (!simulation_ || !simulation_->interval_complete()) __fastfail(FAST_FAIL_INVALID_ARG);
    pending_task_ = static_cast<std::byte*>(task);
    ++manager_tasks_;
    ReplayVfxExecutionScope vfx_execution(2,pending_task_,pending_task_+0x40,current_thread,_ReturnAddress());

    // Native 14215D250 payload entry. Actor dispatch 141C15C90 tail-jumps
    // to the verified manager +3C0 implementation (141C2D330), which has no
    // work after manager +418 returns. Only these concrete wrappers may yield.
    auto* payload = pending_task_ + 0x10;
    if (Field<std::uint8_t>(payload, 0x20) && Field<std::uint8_t>(payload, 0x21))
    {
        auto* prerequisites = Field<std::byte*>(function, 0x20);
        const int count = Field<int>(function, 0x28);
        for (int i = 0; i < count; ++i)
            if (auto* prerequisite = Field<void*>(prerequisites, i * 16 + 8))
                Native(base_, 0x2167ad0, prerequisite, 2);
    }
    if (Field<std::uint8_t>(function, 0xd))
    {
        float delta = Field<float>(payload, 8);
        auto& previous = Field<float>(function, 0x3c);
        if (Field<float>(function, 0x40) == 0.0f) previous = -1.0f;
        else
        {
            const auto time = Field<float>(Field<void*>(payload, 0x18),
                (Field<std::uint8_t>(function, 0xc) & 1) ? 0x934 : 0x930);
            if (previous >= 0.0f) delta = time - previous;
            previous = time;
        }
    entry_active_ = true;
        Virtual(function, 8, delta, Field<int>(payload, 0xc), current_thread, pending_task_ + 0x40);
        entry_active_ = false;
    }
    if (simulation_->interval_complete()) FinishManagerTask();
    else {
        ValidatePendingTask();
        // This counter is compared with completed-traversal yields. A bounded
        // tail deferral retains the task but has not traversed another tick.
        if(!simulation_->tail_blocked()) ++manager_yields_;
    }
    return pending_task_ ? DispatchOutcome::ManagerPending : DispatchOutcome::Completed;
}

#include "Sc6ReplayTaskGroup.Resume.inl"

void Sc6ReplayTaskGroup::FinishManagerTask()
{
    auto* task = pending_task_;
    Field<void*>(Field<void*>(task, 0x10), 0x18) = nullptr;
    Field<std::uint8_t>(task, 0x38) = 0;
    Native(base_, 0x2d2bc0);
    Native(base_, 0xd20a70, Field<void*>(task, 0x40), named_thread_ + 0x20,
        Field<std::uint32_t>(named_thread_, 0x10));
    Field<std::uintptr_t>(task, 0) = base_ + 0x39cd9c0;
    ReleaseEvent(Field<void*>(task, 0x40));
    Field<std::uintptr_t>(task, 0) = base_ + 0x3714118;

    // Native 14215ED20 retires to a two-batch TLS free list, 32 tasks per
    // batch. This allocation is invalid after linking it into that list.
    auto* tls = Native<std::byte*>(base_, 0xd25ee0);
    auto* pool = TlsGetValue(Field<DWORD>(tls, 0));
    if (!pool)
    {
        pool = Native<void*>(base_, 0x4a61c0, std::size_t{0x18});
        if (!pool) __fastfail(FAST_FAIL_FATAL_APP_EXIT);
        Field<void*>(pool, 0) = Field<void*>(pool, 8) = nullptr;
        Field<int>(pool, 0x10) = 0;
        TlsSetValue(Field<DWORD>(tls, 0), pool);
    }
    if (Field<int>(pool, 0x10) >= 32)
    {
        if (auto* older = Field<void*>(pool, 0)) Native(base_, 0x3a1200, tls + 8, older);
        Field<void*>(pool, 0) = Field<void*>(pool, 8);
        Field<void*>(pool, 8) = nullptr;
        Field<int>(pool, 0x10) = 0;
    }
    Field<void*>(task, 0) = Field<void*>(pool, 8);
    ++Field<int>(pool, 0x10);
    Field<void*>(pool, 8) = task;
    pending_task_ = nullptr;
}

void Sc6ReplayTaskGroup::CleanupGroup()
{
    auto* events = reinterpret_cast<EventArray*>(sequencer_ + cleanup_group_ * 0x30);
    if (!ParallelTasks() || cleanup_group_ == 7
        || !*Field<int*>(reinterpret_cast<void*>(base_), 0x43b20e8))
    {
        if (events->capacity < 0) Native(base_, 0x15d8c70, events, 0);
        else
        {
            const auto count = events->count;
            auto** data = events->data();
            for (int i = 0; i < count; ++i) ReleaseEvent(data[i]);
            events->count = 0;
        }
        return;
    }
    std::uintptr_t storage[3]{};
    auto* builder = Native<std::uintptr_t*>(base_, 0x215c8d0, storage, 0, 2);
    auto* task = reinterpret_cast<void*>(builder[0]);
    Field<void*>(task, 0x10) = sequencer_;
    Field<int>(task, 0x18) = cleanup_group_;
    auto* event = Field<void*>(task, 0x28);
    if (event) InterlockedIncrement(&Field<LONG>(event, 0x48));
    Native(base_, 0x2167890, task, reinterpret_cast<void*>(builder[1]), static_cast<int>(builder[2]), true);
    auto* cleanup_events = reinterpret_cast<EventArray*>(sequencer_ + 0x980);
    const int index = cleanup_events->count++;
    if (cleanup_events->count > cleanup_events->capacity) Native(base_, 0x3a1bd0, cleanup_events, index);
    cleanup_events->data()[index] = event; // Transfer the retained reference to the native array.
}

void Sc6ReplayTaskGroup::FinishSequence()
{
    if (spawn_sequence_)
    {
        if (++spawn_passes_ >= 101) { level_ = 0; phase_ = Phase::DropSpawned; return; }
    }
    else
    {
        ++Field<int>(manager_, 0x28);
        if (!requested_wait_) { phase_ = Phase::Finish; return; }
    }
    level_ = 0;
    spawned_ = 0;
    phase_ = Phase::SpawnLevel;
}

void Sc6ReplayTaskGroup::Advance()
{
    switch (phase_)
    {
    case Phase::Idle: break;
    case Phase::Release: ReleaseGroup(); break;
    case Phase::WaitRelease:
        PumpOne(false);
        if (!pumping_)
        {
            ReleaseEvent(single_event_.inline_events[0]);
            single_event_ = {};
            ReleaseEvent(release_event_);
            release_event_ = nullptr;
            waiting_events_ = nullptr;
            phase_ = Phase::SelectWait;
        }
        break;
    case Phase::SelectWait:
        if (!requested_wait_ && ParallelTasks())
        { waiting_events_ = nullptr; BeginPump(); phase_ = Phase::PumpIdle; }
        else
        { cleanup_group_ = Field<int>(sequencer_, 0x9b0); phase_ = Phase::NextGroup; }
        break;
    case Phase::PumpIdle:
        PumpOne(true);
        if (!pumping_) FinishSequence(); // Native does not update +9B0 on this branch.
        break;
    case Phase::NextGroup:
        if (cleanup_group_ > group_)
        {
            Field<int>(sequencer_, 0x9b0) = group_ + (group_ != 7);
            phase_ = Phase::FinishSequence;
        }
        else
        {
            auto* events = reinterpret_cast<EventArray*>(sequencer_ + cleanup_group_ * 0x30);
            if (events->count == 0) { ++cleanup_group_; break; }
            BeginWait(events); phase_ = Phase::WaitGroup;
        }
        break;
    case Phase::WaitGroup:
        PumpOne(false);
        if (!pumping_) { waiting_events_ = nullptr; phase_ = Phase::CleanupGroup; }
        break;
    case Phase::CleanupGroup: CleanupGroup(); ++cleanup_group_; phase_ = Phase::NextGroup; break;
    case Phase::FinishSequence: FinishSequence(); break;
    case Phase::SpawnLevel:
        if (level_ >= Field<int>(manager_, 0x18)) { phase_ = Phase::SpawnDecision; break; }
        spawned_ += static_cast<std::uint32_t>(Native<int>(base_, 0x21638c0,
            Field<void**>(manager_, 0x10)[level_++], Field<int>(manager_, 0x28)));
        break;
    case Phase::SpawnDecision:
        if (!spawned_ || Field<int>(manager_, 0x28) != 7) { phase_ = Phase::Finish; break; }
        group_ = 7; spawn_sequence_ = true; phase_ = Phase::Release; break;
    case Phase::DropSpawned:
        if (level_ >= Field<int>(manager_, 0x18)) { phase_ = Phase::Finish; break; }
        Native(base_, 0x2162ad0, Field<void**>(manager_, 0x10)[level_++], Field<int>(manager_, 0x28));
        break;
    case Phase::Finish: ++completed_groups_; phase_ = Phase::Idle; break;
    }
}
}
