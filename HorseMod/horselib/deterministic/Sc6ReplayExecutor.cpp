#include "Sc6ReplayExecutor.hpp"
#include "ReplayConsumerFailure.hpp"
#include "ReplayNiagaraObservation.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <algorithm>

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
    auto address = Field<std::uintptr_t>(Field<void*>(object, 0), slot);
    return reinterpret_cast<R(__fastcall*)(void*, A...)>(address)(object, args...);
}
}

Status Sc6ReplayExecutor::Fail(FailureCode code) noexcept
{
    state_.phase = Phase::Failed;
    return Status::failure(code);
}

bool Sc6ReplayExecutor::SetBoundaryObserver(void* owner, BoundaryObserver observer) noexcept
{
    if (!owner || state_.phase != Phase::Idle || (bindings_.thread && bindings_.thread != GetCurrentThreadId())
        || (observer_owner_ && observer_owner_ != owner)) return false;
    observer_owner_ = observer ? owner : nullptr;
    observer_ = observer;
    return true;
}

Status Sc6ReplayExecutor::Begin(std::uintptr_t image_base, void* manager,
    float delta_seconds) noexcept
{
    if (state_.phase != Phase::Idle) return Status::failure(FailureCode::IllegalTransition);
    if (!image_base || !manager) return Status::failure(FailureCode::ContextUnavailable);
    if (bindings_.manager && (bindings_.manager != manager
        || bindings_.image_base != image_base))
        return Fail(FailureCode::IdentityMismatch);
    const auto thread = ::GetCurrentThreadId();
    if (bindings_.thread && bindings_.thread != thread)
        return Fail(FailureCode::WrongThread);
    bindings_.image_base = image_base;
    bindings_.manager = static_cast<std::byte*>(manager);
    bindings_.thread = thread;
    state_.delta_seconds = delta_seconds;
    interval_entry_tick_ = state_.tick;
    ++state_.interval;
    state_.phase = Phase::OuterEntry;
    return Status::success();
}

Sc6ReplayExecutor::AdvanceResult Sc6ReplayExecutor::AdvanceToTick(std::uint64_t target) noexcept
{
    if (bindings_.thread != ::GetCurrentThreadId())
        return {AdvanceOutcome::Failed, Status::failure(FailureCode::WrongThread)};
    if (state_.phase == Phase::Failed || target < state_.tick)
        return {AdvanceOutcome::Failed, Status::failure(FailureCode::IllegalTransition)};
    __try
    {
        const auto before = state_.tick;
        const auto result = AdvanceUnchecked(target);
        if (result.ok() && state_.tick > before && state_.tick == target
            && state_.phase != Phase::Idle)
            ++statistics_.yielded_boundaries;
        return {!result.ok() ? AdvanceOutcome::Failed : tail_blocked_ ? AdvanceOutcome::PendingTraversal : state_.tick == target
            ? AdvanceOutcome::ReachedTarget : AdvanceOutcome::IntervalComplete, result};
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return {AdvanceOutcome::Failed, Fail(FailureCode::ContextUnavailable)}; }
}

Status Sc6ReplayExecutor::AdvanceOneTick() noexcept
{
    if (bindings_.thread != ::GetCurrentThreadId()) return Status::failure(FailureCode::WrongThread);
    const auto target = state_.tick + 1;
    auto advanced = AdvanceToTick(target);
    if (!advanced.status.ok() || (advanced.outcome == AdvanceOutcome::IntervalComplete || advanced.outcome == AdvanceOutcome::PendingTraversal)) return advanced.status;
    const auto boundary = state_;
    if (boundary.tick != target || (boundary.phase != Phase::MoveTail
        && boundary.phase != Phase::RepeatDecision && boundary.phase != Phase::RestartTail)) return Fail(FailureCode::AdvanceFailed);
    advanced = AdvanceToTick(target);
    if (advanced.status.ok() && (advanced.outcome != AdvanceOutcome::ReachedTarget || state_ != boundary))
        return Fail(FailureCode::AdvanceFailed);
    return advanced.status;
}

void Sc6ReplayExecutor::CountTraversal(std::uint32_t before)
{
    const auto after = Field<std::uint32_t>(reinterpret_cast<void*>(bindings_.image_base), 0x470d0c4);
    // The native provider may do no gameplay work. More than one traversal
    // would hide a boundary and must fail, rather than inventing a tick map.
    const auto count = after - before;
    if (count > 1) { state_.phase = Phase::Failed; return; }
    state_.tick += count;
    ReplayConsumerFailure::Tick(state_.tick);
    ReplayNiagaraObservation::Tick(state_.tick);
}

Status Sc6ReplayExecutor::AdvanceUnchecked(std::uint64_t target)
{
    ReplayConsumerFailure::Tick(state_.tick);
    ReplayNiagaraObservation::Tick(state_.tick);
    auto* m = bindings_.manager;
    const auto base = bindings_.image_base;
    auto callback = [&](std::size_t offset) { Native(base, 0x3999c0, m + offset); };
    while (state_.phase != Phase::Idle && (tail_only_ || state_.tick < target))
    {
        // Stop before future work, including its input/reset side effects.
        // Decisions and owed tails are executed once; their resulting phase
        // records what explicit Resume must do next.
        if(tail_only_ && (state_.phase==Phase::PublishInput || state_.phase==Phase::SimulationTick
            || state_.phase==Phase::RestartSetup || state_.phase==Phase::RestartTick
            || state_.phase==Phase::MoveTick || state_.phase==Phase::OuterEntry)) {
            tail_blocked_=true;return Status::success();
        }
        const auto observed_tick = state_.tick;
        switch (state_.phase)
        {
        case Phase::OuterEntry:
            switch (Field<std::uint8_t>(m, 0x1461))
            {
            case 0:
                if (Virtual<std::uint8_t>(Field<void*>(m, 0x1450), 8))
                {
                    Native(base, 0x3fb400, m);
                    Native(base, 0x3fa440, m);
                    Field<std::uint8_t>(m, 0x1461) = 1;
                }
                state_.phase = Phase::OuterTail;
                break;
            case 1:
                if (Native<std::uint8_t>(base, 0x3f28f0, m))
                {
                    callback(0x720);
                    Native(base, 0x3fb660, m);
                    Field<std::uint8_t>(m, 0x1461) = 2;
                }
                state_.phase = Phase::OuterTail;
                break;
            case 2: state_.phase = Phase::WorkerEntry; break;
            default: state_.phase = Phase::OuterTail; break;
            }
            break;
        case Phase::WorkerEntry:
            if (!Field<void*>(m, 0x478) || Field<std::uint8_t>(m, 0x12f3))
                state_.phase = Phase::RoundDecision;
            else
            {
                Field<std::uint8_t>(m, 0x1465) = 0;
                state_.phase = Field<std::uint8_t>(m, 0x1463)
                    ? Phase::MoveEntry : Phase::WorkerCounters;
            }
            break;
        case Phase::MoveEntry: MoveEntry(); break;
        case Phase::MoveTick:
        {
            std::array<void*, 3> args{};
            Native<void*>(base, 0x2dd100, args.data());
            const auto before = Field<std::uint32_t>(reinterpret_cast<void*>(base), 0x470d0c4);
            Virtual(Field<void*>(m, 0x1450), 0x68, args.data());
            callback(0xa30);
            state_.phase = Phase::MoveTail;
            CountTraversal(before);
            statistics_.move_state_ticks += state_.tick - interval_entry_tick_;
            break;
        }
        case Phase::MoveTail: MoveTail(); break;
        case Phase::WorkerCounters: WorkerCounters(); break;
        case Phase::PublishInput: PublishInput(); break;
        case Phase::SimulationTick:
        {
            const auto round_state = Field<std::uint8_t>(m, 0x1480);
            if (round_state == 2 || round_state == 3) ++Field<std::uint32_t>(m, 0x1490);
            const auto before = Field<std::uint32_t>(reinterpret_cast<void*>(base), 0x470d0c4);
            Virtual(Field<void*>(m, 0x1450), 0x68, bindings_.frame_args.data());
            callback(0xa30);
            Native(base, 0x3fce80, m);
            Native(base, 0x3fca60, m);
            state_.phase = Phase::RepeatDecision;
            CountTraversal(before);
            break;
        }
        case Phase::RepeatDecision:
            if (Field<std::uint8_t>(m, 0x1462))
            {
                Field<std::uint8_t>(m, 0x1462) = 0;
                ++statistics_.repeat_requests;
                state_.phase = Phase::SimulationTick;
            }
            else state_.phase = Phase::InputTail;
            break;
        case Phase::InputTail:
            if (Field<std::int32_t>(m, 0x14f0) > 0 && --Field<std::int32_t>(m, 0x14f0) == 0)
                callback(0xf70);
            if (Field<std::uint8_t>(m, 0x1464))
            {
                if (auto* stage = Field<void*>(m, 0x500)) Native(base, 0x428d30, stage);
                Field<std::uint8_t>(m, 0x1464) = 0;
            }
            --state_.cache_offset;
            state_.phase = --state_.remaining_inputs ? Phase::PublishInput : Phase::RoundDecision;
            break;
        case Phase::RoundDecision: RoundDecision(); break;
        case Phase::RestartSetup: RestartSetup(); break;
        case Phase::RestartTick:
        {
            // 1403F705D..1403F7090: reset has already occurred in +60.
            // Count only this +68 traversal, then its actual +A30 callback.
            // Ordinary worker post-frame callbacks are absent on this path.
            auto* provider = Field<void*>(m, 0x1450);
            const auto tick_entry = Field<std::uintptr_t>(Field<void*>(provider, 0), 0x68);
            std::array<void*, 3> args{};
            auto* initialized = Native<void*>(base, 0x2dd100, args.data());
            const auto before = Field<std::uint32_t>(reinterpret_cast<void*>(base), 0x470d0c4);
            reinterpret_cast<void(__fastcall*)(void*, void*)>(tick_entry)(provider, initialized);
            callback(0xa30);
            const auto prior = state_.tick;
            CountTraversal(before);
            if (state_.phase == Phase::Failed) break;
            statistics_.restart_traversals += state_.tick - prior;
            state_.phase = Phase::RestartTail;
            break;
        }
        case Phase::RestartTail:
            Field<std::uint8_t>(m, 0x1461) = 2;
            state_.phase = Phase::OuterTail;
            break;
        case Phase::OuterTail: OuterTail(); break;
        case Phase::Failed: return Status::failure(FailureCode::IllegalTransition);
        default: return Fail(FailureCode::IllegalTransition);
        }
        if (observer_ && state_.phase != Phase::Failed
            && (state_.tick != observed_tick || state_.phase == Phase::Idle))
            observer_(observer_owner_, state_, state_.phase == Phase::Idle);
    }
    return state_.phase == Phase::Failed
        ? Status::failure(FailureCode::IllegalTransition) : Status::success();
}

void Sc6ReplayExecutor::ReleaseHandler(SharedHandler& handler)
{
    auto* control = handler.controller;
    handler = {};
    if (control && --Field<std::int32_t>(control, 8) == 0)
    {
        Virtual(control, 0);
        if (--Field<std::int32_t>(control, 12) == 0) Virtual(control, 8, 1);
    }
}

void Sc6ReplayExecutor::MoveEntry()
{
    auto* m = bindings_.manager;
    Virtual(Field<void*>(m, 0x1450), 0x100, &bindings_.handler);
    auto temporary = bindings_.handler;
    if (temporary.controller) ++Field<std::int32_t>(temporary.controller, 8);
    const bool ready = temporary.object && Virtual<std::uint8_t>(temporary.object, 0);
    ReleaseHandler(temporary);
    if (!ready)
    {
        ReleaseHandler(bindings_.handler);
        state_.phase = Phase::WorkerCounters;
        return;
    }
    Native(bindings_.image_base, 0x3999c0, m + 0x950);
    state_.phase = Field<std::uint8_t>(m, 0x1463) == 3 ? Phase::MoveTick : Phase::MoveTail;
}

void Sc6ReplayExecutor::MoveTail()
{
    auto* m = bindings_.manager;
    const auto base = bindings_.image_base;
    auto* handler = bindings_.handler.object;
    Native(base, 0x3999c0, m + 0x870);
    const auto move = Field<std::uint8_t>(m, 0x1463);
    if (move == 2 || move == 3 || move == 6)
    {
        alignas(16) std::array<std::byte, 0x130> params;
        Native<void*>(base, 0x3de580, params.data());
        Native(base, 0x41f540, m + 0x1300, params.data());
        Native(base, 0x3f2c40, m, params.data());
        if (move == 6)
        {
            Field<std::uint32_t>(params.data(), 0xc4) = 1;
            Field<std::uint32_t>(params.data(), 0x104) = 1;
        }
        Virtual(handler, 0x38, params.data());
        Native(base, 0x2d2d60, params.data() + 0xf0);
        Native(base, 0x2d2d60, params.data() + 0xb0);
        Native(base, 0x2d8030, params.data() + 0x60);
        Native(base, 0x2d7930, params.data() + 0x50);
        Native(base, 0x2d72a0, params.data() + 0x38);
        Native(base, 0x2d7210, params.data());
    }
    else if (move == 4)
    {
        alignas(16) std::array<std::byte, 0x40> velocity;
        alignas(16) std::array<std::byte, 0x30> bounds;
        Native<void*>(base, 0x2dcfb0, velocity.data());
        Native<void*>(base, 0x408fd0, bounds.data());
        Native(base, 0x3f0e00, m, bounds.data(), Field<std::int32_t>(m, 0x1360));
        Native(base, 0x41f790, bounds.data(), velocity.data());
        Virtual(handler, 0xb0, velocity.data());
        Virtual(handler, 0xa8, m + 0x1360);
        Virtual(handler, 0xb8, Field<std::int32_t>(m, 0x1360));
        Field<std::uint8_t>(m, 0x1465) = 1;
    }
    Native(base, 0x3999c0, m + 0x8e0);
    if (auto* fade = Field<void*>(m, 0x518)) Native(base, 0x478d30, fade, -1);
    if (auto* actors = Field<void*>(m, 0x530)) Native(base, 0x3f7bc0, actors);
    Field<std::uint16_t>(m, 0x1463) = 0x100;
    ReleaseHandler(bindings_.handler);
    state_.phase = Phase::WorkerCounters;
}

void Sc6ReplayExecutor::WorkerCounters()
{
    auto* m = bindings_.manager;
    const auto base = bindings_.image_base;
    state_.entry_press_timer_positive = Field<std::int32_t>(m, 0x1468) > 0;
    state_.entry_down_timer_positive = Field<std::int32_t>(m, 0x146c) > 0;
    if (state_.entry_press_timer_positive) --Field<std::int32_t>(m, 0x1468);
    if (state_.entry_down_timer_positive) --Field<std::int32_t>(m, 0x146c);
    if (Field<std::uint8_t>(m, 0x1465))
    {
        alignas(16) std::array<std::byte, 0x58> move;
        Native<void*>(base, 0x3de2a0, move.data());
        Native<void*>(base, 0x3df880, m + 0x1488, move.data());
        for (auto offset : {0x30, 0x20, 0x10})
            if (auto* memory = Field<void*>(move.data(), offset)) Native(base, 0xd46a00, memory);
        Field<std::int32_t>(m, 0x1488) = Field<std::int32_t>(m, 0x1360) - 1;
        Field<std::uint32_t>(m, 0x148c) = 0;
        state_.remaining_inputs = 1;
    }
    else
    {
        auto* input = Field<void*>(m, 0x478);
        const auto round = Field<std::int32_t>(input, 0x3a0);
        if (Field<std::int32_t>(m, 0x1488) != round) Field<std::uint32_t>(m, 0x148c) = 0;
        Field<std::int32_t>(m, 0x1488) = round;
        const auto current = Field<std::int32_t>(input, 0x3a4);
        const auto previous = std::min(current, Field<std::int32_t>(m, 0x148c));
        Field<std::int32_t>(m, 0x148c) = current;
        state_.remaining_inputs = static_cast<std::uint32_t>(current) - static_cast<std::uint32_t>(previous);
    }
    Native(base, 0x3fe960, m);
    Native(base, 0x3fdec0, m, state_.remaining_inputs);
    state_.cache_offset = static_cast<std::int32_t>(state_.remaining_inputs) - 1;
    state_.phase = static_cast<std::int32_t>(state_.remaining_inputs) > 0
        ? Phase::PublishInput : Phase::RoundDecision;
}

void Sc6ReplayExecutor::PublishInput()
{
    auto* m = bindings_.manager;
    const auto base = bindings_.image_base;
    Native<void*>(base, 0x2dd100, bindings_.frame_args.data());
    for (std::int32_t slot = 0; slot < Field<std::int32_t>(m, 0x14b0); ++slot)
    {
        if (slot >= 2) { state_.phase = Phase::Failed; return; }
        Native(base, 0x3fcd10, m, slot, state_.cache_offset,
            state_.entry_press_timer_positive, state_.entry_down_timer_positive);
        bindings_.frame_args[slot] = Field<std::byte*>(m, 0x14a8) + slot * 8;
    }
    Native(base, 0x1d38300, m + 0x1210, m + 0x14a8);
    bindings_.frame_args[2] = m + 0x14c8;
    if (Virtual<std::uint8_t>(Field<void*>(m, 0x1450), 0x80) && Field<std::uint8_t>(m, 0x12f0))
        for (std::int32_t slot = 0; slot < Field<std::int32_t>(m, 0x14b0); ++slot)
            if (Field<std::uint8_t>(Field<void*>(m, 0x14a8), slot * 8 + 4) & 0x10)
            {
                Virtual(Field<void*>(m, 0x1450), 0x88);
                break;
            }
    // Preserve the native class test even in replay mode. No online state is
    // manufactured; this is the worker's original conditional provider call.
    if (auto* input = Field<void*>(m, 0x478))
    {
        auto* sync_class = Native<std::byte*>(base, 0x914490);
        auto* actual_class = Field<void*>(input, 0x10);
        const auto index = Field<std::int32_t>(sync_class, 0x90);
        if (index >= 0 && index <= Field<std::int32_t>(actual_class, 0x90)
            && Field<void*>(Field<void*>(actual_class, 0x88), index * 8) == sync_class + 0x88
            && Field<std::uint32_t>(input, 0x39c) == 0 && Field<std::uint8_t>(input, 0x4404)
            && !Field<std::uint8_t>(m, 0x1640)
            && Virtual<std::uint8_t>(Field<void*>(m, 0x1450), 0x80)
            && Field<std::uint8_t>(m, 0x12f0))
            Virtual(Field<void*>(m, 0x1450), 0x88);
    }
    ++state_.publications;
    state_.phase = Phase::SimulationTick;
}

void Sc6ReplayExecutor::RoundDecision()
{
    auto* m = bindings_.manager;
    const auto base = bindings_.image_base;
    if (Native<std::uint8_t>(base, 0x3f2840, m))
    {
        auto* move_provider = Native<void*>(base, 0x3f00b0, m);
        if (move_provider && Field<std::int32_t>(move_provider, 0x150) == 1)
        {
            state_.phase = Phase::RestartSetup;
            return;
        }
        else if (Field<std::uint8_t>(m, 0x12f2))
        {
            Native(base, 0x3eec20, m);
            Field<std::uint8_t>(m, 0x1461) = 3;
        }
    }
    state_.phase = Phase::OuterTail;
}

void Sc6ReplayExecutor::RestartSetup()
{
    // Exact native order from 1403F6FD0, before its default-argument traversal.
    // Reload +1450 after callbacks, just as the native function does.
    auto* m = bindings_.manager;
    const auto base = bindings_.image_base;
    if (Virtual<std::uint8_t>(Field<void*>(m, 0x1450), 0x78)) {
        Native(base, 0x3999c0, m + 0x800);
        Virtual(Field<void*>(m, 0x1450), 0x70);
    }
    Native(base, 0x3999c0, m + 0x9c0);
    auto* provider = Native<void*>(base, 0x3f00b0, m);
    Native(base, 0x426210, m + 0x1300, provider);
    Native(base, 0x3f0e00, m, m + 0x1320, 1);
    Virtual(Field<void*>(m, 0x1450), 0x60);
    Native(base, 0x3999c0, m + 0x790);
    state_.phase = Phase::RestartTick;
}

void Sc6ReplayExecutor::OuterTail()
{
    auto* m = bindings_.manager;
    auto& sync = Field<std::uint32_t>(m, 0x1440);
    if (Field<std::uint8_t>(m, 0x1480) == 2)
    {
        ++sync;
        if (static_cast<std::int32_t>(sync) > 0x2a3a) sync = 0;
        if (static_cast<std::int32_t>(sync) <= 0x2a30)
            if (auto* world = Virtual<void*>(m, 0x138)) Native(bindings_.image_base, 0x1eecde0, world);
    }
    else sync = 0;
    Native(bindings_.image_base, 0x562320, m, state_.delta_seconds);
    ++statistics_.completed_intervals;
    const auto ticks = state_.tick - interval_entry_tick_;
    statistics_.zero_tick_intervals += ticks == 0;
    statistics_.multi_tick_intervals += ticks > 1;
    state_.phase = Phase::Idle;
}

Status Sc6ReplayExecutor::RestoreContinuation(const Continuation& expected_current,
    const Continuation& target) noexcept
{
    if (bindings_.thread != ::GetCurrentThreadId()) return Status::failure(FailureCode::WrongThread);
    auto same_boundary = target;
    same_boundary.tick = expected_current.tick;
    if (state_ != expected_current || expected_current.phase != Phase::RepeatDecision
        || same_boundary != expected_current)
        return Status::failure(FailureCode::RestorePreflightFailed);
    state_ = target;
    // Physical traversal/dispatch statistics are intentionally not rewound.
    return Status::success();
}

Status Sc6ReplayExecutor::Stop() noexcept
{
    if (bindings_.thread && bindings_.thread != ::GetCurrentThreadId())
        return Status::failure(FailureCode::WrongThread);
    // Stop is teardown, never a request to resume stock with pending work.
    __try { ReleaseHandler(bindings_.handler); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return Fail(FailureCode::ContextUnavailable); }
    bindings_ = {};
    tail_only_=tail_blocked_=false;
    state_ = {};
    statistics_ = {};
    interval_entry_tick_ = 0;
    observer_ = nullptr;
    observer_owner_ = nullptr;
    return Status::success();
}

Status Sc6ReplayExecutor::RestoreIdleContinuation(const Continuation& expected_current,
    const Continuation& target) noexcept
{
    if (bindings_.thread != ::GetCurrentThreadId()) return Status::failure(FailureCode::WrongThread);
    if (state_ != expected_current || state_.phase != Phase::Idle || target.phase != Phase::Idle
        || bindings_.handler.object || bindings_.handler.controller || !bindings_.manager || !bindings_.image_base)
        return Status::failure(FailureCode::RestorePreflightFailed);
    // Begin establishes the next interval's delta/entry tick; WorkerCounters
    // rebuilds input admission and PublishInput rebinds all frame arguments.
    // No handler, pending traversal or epilogue may survive this boundary.
    state_ = target;
    bindings_.frame_args = {};
    interval_entry_tick_ = target.tick;
    return Status::success();
}
}
