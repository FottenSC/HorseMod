#pragma once
#include "Types.hpp"
#include <memory>
#include <new>
#include <span>

namespace Horse::Deterministic
{
// An observation-only, contiguous map. No entry is ever installed in the game.
// Storage is reserved before recording; exhaustion preserves a failed partial
// map, never a shortened successful timeline. Checkpoint storage is separate.
class ReplayTickIndex final
{
public:
    enum class Phase : std::uint8_t { Empty, Recording, Complete, Cancelled, Failed, Releasing, SuspendedRevision };
    enum class EndPolicy : std::uint8_t { NativeFinish, RetainedSourceStop };
    struct Entry {
        std::uint64_t tick{}, interval{}, publications{};
        std::uint32_t native_tick{}, round_tick{};
        std::int32_t round{}, source_cursor{}, cache_offset{};
        std::uint32_t remaining_inputs{};
        std::uint8_t execution_phase{}, round_state{}, source_active{}, move_state{};
        friend bool operator==(const Entry&, const Entry&) = default;
    };
    struct Witness {
        Phase phase{};
        FailureCode failure{};
        std::uint64_t entries{}, bytes{}, completed_intervals{}, zero_tick_intervals{}, multi_tick_intervals{};
        bool native_finish{}, final_tail{};
        EndPolicy end_policy{};
        bool source_stopped{}, unsupported_native_tail{};
    };
    Status Begin(const Entry& baseline, std::size_t budget, std::uint64_t input_revision=0,
        EndPolicy end_policy=EndPolicy::NativeFinish) noexcept {
        if (state_.phase != Phase::Empty || baseline.tick || baseline.native_tick)
            return Status::failure(FailureCode::IllegalTransition);
        const auto capacity = budget / sizeof(Entry);
        if (capacity < 2) return Status::failure(FailureCode::CapacityExceeded);
        auto rows = std::unique_ptr<Entry[]>(new(std::nothrow) Entry[capacity]);
        if (!rows) return Status::failure(FailureCode::CapacityExceeded);
        entries_ = std::move(rows); capacity_ = capacity;
        entries_[0] = baseline;
        state_ = {Phase::Recording, FailureCode::None, 1, capacity * sizeof(Entry)};
        state_.end_policy=end_policy;
        last_interval_ = baseline.interval; interval_tick_ = 0;
        indexed_revision_=input_revision;
        return Status::success();
    }
    Status Append(const Entry& entry) noexcept {
        if (state_.phase != Phase::Recording) return Status::failure(FailureCode::IllegalTransition);
        const auto& previous = entries_[state_.entries - 1];
        if (entry.tick != state_.entries || entry.native_tick != previous.native_tick + 1
            || entry.interval < previous.interval || entry.publications < previous.publications)
            return Fail(FailureCode::AdvanceFailed);
        if(state_.final_tail && (entry.source_active || entry.round_state!=10
            || entry.round!=entries_[source_end_tick_].round)) return Fail(FailureCode::GenerationMismatch);
        if(state_.source_stopped && (entry.source_active || entry.round_state!=5
            || entry.round!=entries_[source_end_tick_].round)) return Fail(FailureCode::GenerationMismatch);
        if (state_.entries == capacity_) return Fail(FailureCode::CapacityExceeded);
        entries_[state_.entries++] = entry;
        return Status::success();
    }
    Status CompleteInterval(std::uint64_t interval, std::uint64_t tick, bool final_tail) noexcept {
        if (state_.phase != Phase::Recording) return Status::failure(FailureCode::IllegalTransition);
        if (interval != last_interval_ + 1 || tick != state_.entries - 1 || tick < interval_tick_)
            return Fail(FailureCode::AdvanceFailed);
        ++state_.completed_intervals;
        state_.zero_tick_intervals += tick == interval_tick_;
        state_.multi_tick_intervals += tick - interval_tick_ > 1;
        last_interval_ = interval; interval_tick_ = tick;
        if(final_tail && !state_.final_tail) source_end_tick_=tick;
        state_.final_tail |= final_tail;
        return Status::success();
    }
    // These are separate native witnesses. Finish must not infer completion
    // from a winner, source length, cursor exhaustion, or a UI poll timeout.
    void ObserveNativeFinish() noexcept {
        if(state_.end_policy==EndPolicy::RetainedSourceStop
            && (state_.phase==Phase::Recording || state_.phase==Phase::Complete)) {
            state_.native_finish=true;Fail(FailureCode::GenerationMismatch);
        } else if(state_.phase==Phase::Recording)state_.native_finish=true;
    }
    // This records an observed native stop, not exhaustion inferred from a
    // recording length. The host separately verifies the native finish
    // predicate and session bindings before arming and after completing hold.
    Status ObserveRetainedSourceStop(std::uint64_t tick) noexcept {
        if(state_.phase!=Phase::Recording || state_.end_policy!=EndPolicy::RetainedSourceStop
            || state_.native_finish || state_.final_tail || tick!=interval_tick_
            || tick!=state_.entries-1 || entries_[tick].source_active || entries_[tick].round_state!=5)
            return Fail(FailureCode::IllegalTransition);
        if(!state_.source_stopped)source_end_tick_=tick;
        state_.source_stopped=true;
        return Status::success();
    }
    Status FinishRetainedSourceStop(std::uint64_t tick) noexcept {
        if(state_.phase!=Phase::Recording || state_.end_policy!=EndPolicy::RetainedSourceStop
            || !state_.source_stopped || state_.native_finish || state_.final_tail
            || tick<=source_end_tick_ || tick!=interval_tick_ || tick!=state_.entries-1)
            return Fail(FailureCode::IllegalTransition);
        state_.unsupported_native_tail=true;
        state_.phase=Phase::Complete;
        return Status::success();
    }
    Status FinishAtApplicationBoundary(std::uint64_t current_tick) noexcept {
        if (state_.phase != Phase::Recording || !state_.native_finish || !state_.final_tail)
            return Status::failure(FailureCode::IllegalTransition);
        // Source exhaustion is a marker, not permission to omit later native
        // traversals. The complete map ends at the retained application boundary.
        if (current_tick != state_.entries - 1 || current_tick!=interval_tick_)
            return Fail(FailureCode::AdvanceFailed);
        state_.phase = Phase::Complete;
        return Status::success();
    }
    Status Fail(FailureCode reason) noexcept {
        if (state_.phase == Phase::Recording || state_.phase == Phase::Releasing || state_.phase==Phase::Complete) {
            state_.phase = Phase::Failed; state_.failure = reason;
        }
        return Status::failure(reason);
    }
    // A completed timeline describes one authored input history. A committed
    // revision can change repeats, rounds and even its endpoint. Keep its
    // bounded evidence/storage, but never continue advertising it as complete.
    void InvalidateSourceRevision() noexcept {
        if (state_.phase == Phase::Complete || state_.phase==Phase::SuspendedRevision) {
            state_.phase = Phase::Failed;
            state_.failure = FailureCode::GenerationMismatch;
        }
    }
    // A provisional history cannot use this map. Installing B's input source
    // alone does not restore validity: CPU/GPU/native retirement must finish.
    void SuspendSourceRevision(std::uint64_t active_revision) noexcept {
        if(state_.phase==Phase::Complete && active_revision!=indexed_revision_)
            state_.phase=Phase::SuspendedRevision;
    }
    // Called at completed recovery or commit, using the actual installed
    // revision (which may have been edited after checkpoint A publication).
    void SettleSourceRevision(std::uint64_t active_revision) noexcept {
        if(state_.phase!=Phase::Complete && state_.phase!=Phase::SuspendedRevision) return;
        if(active_revision==indexed_revision_) {
            state_.phase=Phase::Complete;state_.failure=FailureCode::None;
        } else InvalidateSourceRevision();
    }
    bool Cancel() noexcept {
        if (state_.phase != Phase::Recording) return false;
        state_.phase = Phase::Cancelled; state_.failure = FailureCode::Cancelled; return true;
    }
    Status ReleaseAfterOwners(bool retired) noexcept {
        if(state_.phase==Phase::Recording) return Status::failure(FailureCode::IllegalTransition);
        if(state_.phase==Phase::Empty) return Status::success();
        // Releasing remains observable while native/GPU owners are in flight.
        // Keep the map and its reservation until retirement actually completes.
        state_.phase=Phase::Releasing;
        if(retired) Clear();
        return Status::success();
    }
    void Clear() noexcept { entries_.reset(); capacity_ = 0; state_ = {}; last_interval_ = interval_tick_ = indexed_revision_ = source_end_tick_ = 0; }
    Witness witness() const noexcept { return state_; }
    // The final indexed boundary is a retained session hold. Releasing its
    // index for native teardown is explicit; playback must not escape it.
    bool AllowsPlaybackFrom(std::uint64_t tick) const noexcept {
        return state_.phase!=Phase::Complete || (state_.entries>0 && tick<state_.entries-1);
    }
    std::uint64_t source_end_tick() const noexcept { return source_end_tick_; }
    std::span<const Entry> entries() const noexcept { return {entries_.get(), static_cast<std::size_t>(state_.entries)}; }
    std::size_t storage_bytes() const noexcept { return state_.bytes; }
private:
    std::unique_ptr<Entry[]> entries_;
    std::size_t capacity_{};
    Witness state_{};
    std::uint64_t last_interval_{}, interval_tick_{};
    std::uint64_t indexed_revision_{}, source_end_tick_{};
};
}
