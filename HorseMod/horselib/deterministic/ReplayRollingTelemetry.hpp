#pragma once
#include <array>
#include <chrono>
#include <cstdint>
#include <algorithm>

namespace Horse::Deterministic {
// Fixed storage is part of sizeof(Sc6ReplayHost), charged by AdmissionBytes.
// No output, allocation, hooks, completion decisions or ownership changes here.
struct ReplayRollingTelemetry {
    enum class Phase : unsigned { Preparation, Publication, Resimulation, Application, Forward, Capture, Retirement, Count };
    enum class Status : unsigned { Active, Complete, Aborted, Terminal };
    struct Row {
        std::uint64_t ordinal{},tick{},elapsed_us{},backlog{};
        std::array<std::uint64_t,static_cast<unsigned>(Phase::Count)> phases{};
        Status status{Status::Active};
    };
    std::array<Row,600> rows{};
    unsigned size{},emitted{};
    bool active{};
    std::uint64_t started{},changed{},warmup_started{},warmup_us{},teardown_started{},teardown_us{};
    Phase phase{Phase::Preparation};
    static std::uint64_t Now() noexcept {
        return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    bool Begin(std::uint64_t ordinal,std::uint64_t tick,std::uint64_t now) noexcept {
        if(active || size==rows.size())return false;
        rows[size++]={};rows[size-1].ordinal=ordinal;rows[size-1].tick=tick;
        started=changed=now;phase=Phase::Preparation;active=true;
        if(size==1 && warmup_started)warmup_us=now-warmup_started;
        return true;
    }
    void Mark(Phase next,std::uint64_t now) noexcept {
        if(!active)return;
        rows[size-1].phases[static_cast<unsigned>(phase)]+=now-changed;
        changed=now;phase=next;
    }
    void Backlog(std::uint64_t count) noexcept {
        if(active)rows[size-1].backlog=(std::max)(rows[size-1].backlog,count);
    }
    void Finish(Status status,std::uint64_t now) noexcept {
        if(!active)return;
        Mark(phase,now);rows[size-1].elapsed_us=now-started;rows[size-1].status=status;active=false;
    }
    bool Complete(std::uint64_t now,bool next_checkpoint,bool retirement_complete) noexcept {
        if(!active || !next_checkpoint || !retirement_complete)return false;
        Finish(Status::Complete,now);return true;
    }
    void Abort(std::uint64_t now) noexcept {Finish(Status::Aborted,now);}
    void Terminal(std::uint64_t now) noexcept {Finish(Status::Terminal,now);teardown_started=now;}
};
}
