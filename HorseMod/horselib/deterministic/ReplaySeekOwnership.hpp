#pragma once
#include <cstdint>

namespace Horse::Deterministic {
// Seek-wide ownership, separate from UI progress and individual participant
// journals. Only the host supplies completed native boundary observations.
// CSettled means the requested traversal is complete; an interior boundary
// may still own application/render tails. Holding there never retires B.
class ReplaySeekOwnership final {
public:
    enum class Phase : std::uint8_t {
        BRetained, APublished, ExecutionActive, CSettled, CompletingTargetTails, TargetTailsCompleted,
        Committing, Committed, RecoveryQuiescing, Recovering, Recovered,
        FailedRecoverable, FailedTerminal, CompletingResumedTails
    };
    ReplaySeekOwnership(std::uint64_t original_tick,std::uint64_t target_tick) noexcept
        : original_tick_(original_tick),target_tick_(target_tick) {}
    Phase phase() const noexcept { return phase_; }
    static constexpr const char* Name(Phase phase) noexcept {
        switch(phase) {
        case Phase::BRetained:return "BRetained";
        case Phase::APublished:return "APublished";
        case Phase::ExecutionActive:return "ExecutionActive";
        case Phase::CSettled:return "CSettled";
        case Phase::CompletingTargetTails:return "CompletingTargetTails";
        case Phase::CompletingResumedTails:return "CompletingResumedTails";
        case Phase::TargetTailsCompleted:return "TargetTailsCompleted";
        case Phase::Committing:return "Committing";
        case Phase::Committed:return "Committed";
        case Phase::RecoveryQuiescing:return "RecoveryQuiescing";
        case Phase::Recovering:return "Recovering";
        case Phase::Recovered:return "Recovered";
        case Phase::FailedRecoverable:return "FailedRecoverable";
        case Phase::FailedTerminal:return "FailedTerminal";
        }
        return "Invalid";
    }
    std::uint64_t original_tick() const noexcept { return original_tick_; }
    std::uint64_t target_tick() const noexcept { return target_tick_; }
    std::uint64_t completed_tail_tick() const noexcept { return completed_tail_tick_; }
    bool retains_undo() const noexcept {
        return phase_!=Phase::Committing && phase_!=Phase::Committed
            && phase_!=Phase::Recovered && phase_!=Phase::FailedTerminal;
    }
    bool execution_active() const noexcept { return phase_==Phase::ExecutionActive; }
    bool commit_decided() const noexcept {
        return phase_==Phase::Committing || phase_==Phase::Committed || phase_==Phase::FailedTerminal;
    }
    bool PublishA() noexcept { return Move(Phase::BRetained,Phase::APublished); }
    bool ActivateExecution() noexcept { return Move(Phase::APublished,Phase::ExecutionActive); }
    bool SettleC(std::uint64_t observed_tick) noexcept {
        if(observed_tick!=target_tick_) return false;
        return Move(Phase::ExecutionActive,Phase::CSettled);
    }
    bool CompleteTargetTails(std::uint64_t observed_tick) noexcept {
        if(phase_==Phase::CompletingResumedTails) { if(observed_tick<target_tick_) return false; }
        else if((phase_!=Phase::CSettled && phase_!=Phase::CompletingTargetTails) || observed_tick!=target_tick_) return false;
        completed_tail_tick_=observed_tick;phase_=Phase::TargetTailsCompleted;return true;
    }
    // A user step continues the same reversible C branch. It must not finish
    // an interval merely to retire B; repeats may contain further exact ticks.
    bool StepFromSettled(std::uint64_t observed_tick) noexcept {
        if(phase_!=Phase::CSettled || observed_tick!=target_tick_ || target_tick_==UINT64_MAX) return false;
        ++target_tick_;phase_=Phase::ExecutionActive;return true;
    }
    bool DeferTargetTails() noexcept { return Move(Phase::CompletingTargetTails,Phase::CSettled); }
    bool BeginResumedTails() noexcept { return Move(Phase::CSettled,Phase::CompletingResumedTails); }
    bool BeginTargetTails() noexcept { return Move(Phase::CSettled,Phase::CompletingTargetTails); }
    bool BeginCommit() noexcept { return Move(Phase::TargetTailsCompleted,Phase::Committing); }
    bool CompleteCommit() noexcept { return Move(Phase::Committing,Phase::Committed); }
    bool BeginRecovery() noexcept {
        if(phase_==Phase::RecoveryQuiescing || phase_==Phase::Recovering) return true;
        if(!retains_undo()) return false;
        phase_=Phase::RecoveryQuiescing;return true;
    }
    enum class Cancellation : std::uint8_t { Rejected, Quiesce, Undo, Recovered };
    Cancellation RequestCancellation() noexcept {
        if(phase_==Phase::Recovered) return Cancellation::Recovered;
        if(phase_==Phase::Recovering) return Cancellation::Undo;
        return BeginRecovery()?Cancellation::Quiesce:Cancellation::Rejected;
    }
    bool CompleteRecoveryTails(std::uint64_t observed_tick) noexcept {
        if(phase_!=Phase::RecoveryQuiescing) return false;
        completed_tail_tick_=observed_tick;phase_=Phase::Recovering;return true;
    }
    bool CompleteRecovery(std::uint64_t observed_tick) noexcept {
        if(observed_tick!=original_tick_) return false;
        return Move(Phase::Recovering,Phase::Recovered);
    }
    void Fail() noexcept {
        if(phase_==Phase::Committed || phase_==Phase::Recovered) return;
        phase_=retains_undo()?Phase::FailedRecoverable:Phase::FailedTerminal;
    }
private:
    Phase phase_{Phase::BRetained};
    std::uint64_t original_tick_{},target_tick_{},completed_tail_tick_{};
    bool Move(Phase from,Phase to) noexcept {
        if(phase_!=from) return false;
        phase_=to;return true;
    }
};
}
