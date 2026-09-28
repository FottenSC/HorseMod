#pragma once
#include "Types.hpp"
#include <algorithm>
#include <memory>
#include <span>

namespace Horse::Deterministic {
// Replay sample coordinates are authored data, never native simulation ticks.
struct ReplayInputOverride {
    std::int32_t round{};
    std::uint32_t sample{};
    std::array<std::uint32_t,2> raw{};
    std::uint8_t players{};
};
struct ReplayCorrectionRequest {
    std::uint64_t arrival_tick{},session{},epoch{},expected_revision{};
    std::span<const ReplayInputOverride> overrides;
};

// Eight native traversals, including each publication's first consumption.
// A repeat carries that first tick even when its original ring row expires.
class ReplayRollingInputHistory final {
public:
    void Begin(std::uint64_t session,std::uint64_t epoch,std::int32_t round,
        std::uint64_t tick,std::uint32_t sample_floor) noexcept {
        *this={};session_=session;epoch_=epoch;round_=round;last_tick_=tick;sample_floor_=sample_floor;
    }
    Status Record(std::uint64_t tick,std::uint64_t publication,std::int32_t round,std::uint32_t sample) noexcept {
        if(!session_ || last_tick_==UINT64_MAX || tick!=last_tick_+1 || round!=round_ || sample<sample_floor_ || !publication)
            return Status::failure(FailureCode::GenerationMismatch);
        const auto& previous=rows_[last_tick_%rows_.size()];
        std::uint64_t first=tick;
        if(previous.tick && previous.tick==last_tick_) {
            if(publication<previous.publication || (publication==previous.publication && sample!=previous.sample)
                || (publication>previous.publication && sample<previous.sample))
                return Status::failure(FailureCode::GenerationMismatch);
            if(sample==previous.sample)first=previous.first;
        }
        rows_[tick%rows_.size()]={tick,publication,first,sample};last_tick_=tick;
        return Status::success();
    }
    std::uint64_t FirstConsumption(std::int32_t round,std::uint32_t sample) const noexcept {
        if(round!=round_)return 0;
        for(const auto& row:rows_)if(row.tick && row.sample==sample)return row.first;
        return 0;
    }
    bool Matches(std::uint64_t session,std::uint64_t epoch,std::uint64_t tick)const noexcept {
        return session_==session && epoch_==epoch && last_tick_==tick;
    }
private:
    struct Row {std::uint64_t tick{},publication{},first{};std::uint32_t sample{};};
    std::array<Row,8> rows_{};
    std::uint64_t session_{},epoch_{},last_tick_{};
    std::int32_t round_{};
    std::uint32_t sample_floor_{};
};

class ReplayCorrectionSchedule final {
public:
    using Handle=std::shared_ptr<const ReplayCorrectionSchedule>;
    struct Row {std::uint64_t arrival_tick{},session{},epoch{},expected_revision{};std::size_t offset{},count{};};
    static constexpr std::size_t maximum_corrections=600,maximum_overrides=4096;
    static Status Create(std::span<const ReplayCorrectionRequest> requests,std::size_t available,Handle& output) noexcept {
        if(requests.empty())return Status::failure(FailureCode::InvalidConfiguration);
        if(requests.size()>maximum_corrections)return Status::failure(FailureCode::CapacityExceeded);
        std::size_t count{};
        for(std::size_t i=0;i<requests.size();++i) {
            const auto& request=requests[i];
            if(!request.session || !request.epoch || !request.arrival_tick || request.overrides.empty()
                || (i && (requests[i-1].arrival_tick>=request.arrival_tick
                    || requests[i-1].expected_revision>=request.expected_revision)))
                return Status::failure(FailureCode::InvalidConfiguration);
            if(request.overrides.size()>maximum_overrides-count)return Status::failure(FailureCode::CapacityExceeded);
            count+=request.overrides.size();
            for(std::size_t j=0;j<request.overrides.size();++j) {
                const auto& edit=request.overrides[j];
                if(edit.round<0 || !edit.players || (edit.players&~3)
                    || (j && (request.overrides[j-1].round!=edit.round || request.overrides[j-1].sample>=edit.sample)))
                    return Status::failure(FailureCode::InvalidConfiguration);
            }
        }
        const auto bytes=sizeof(ReplayCorrectionSchedule)+64+requests.size()*sizeof(Row)+count*sizeof(ReplayInputOverride);
        if(bytes>available)return Status::failure(FailureCode::CapacityExceeded);
        try {
            auto next=std::shared_ptr<ReplayCorrectionSchedule>(new ReplayCorrectionSchedule);
            next->rows_.reserve(requests.size());next->edits_.reserve(count);
            for(const auto& request:requests) {
                next->rows_.push_back({request.arrival_tick,request.session,request.epoch,request.expected_revision,next->edits_.size(),request.overrides.size()});
                next->edits_.insert(next->edits_.end(),request.overrides.begin(),request.overrides.end());
            }
            if(next->owned_bytes()>available)return Status::failure(FailureCode::CapacityExceeded);
            output=std::move(next);return Status::success();
        }catch(...){return Status::failure(FailureCode::CapacityExceeded);}
    }
    std::size_t size()const noexcept{return rows_.size();}
    const Row& row(std::size_t index)const noexcept{return rows_[index];}
    std::span<const ReplayInputOverride> edits(std::size_t index)const noexcept {
        const auto& r=rows_[index];return {edits_.data()+r.offset,r.count};
    }
    std::size_t owned_bytes()const noexcept {
        return sizeof(*this)+64+rows_.capacity()*sizeof(Row)+edits_.capacity()*sizeof(ReplayInputOverride);
    }
    struct Admission {Status status;std::uint64_t first_consumption{};};
    Admission Admit(std::size_t index,std::uint64_t tick,std::uint64_t session,std::uint64_t epoch,
        std::uint64_t revision,std::uint64_t boundary,const ReplayRollingInputHistory& history)const noexcept {
        if(index>=rows_.size())return {Status::failure(FailureCode::InvalidConfiguration)};
        const auto& r=rows_[index];
        if(r.arrival_tick!=tick || r.session!=session || r.epoch!=epoch || r.expected_revision!=revision
            || !history.Matches(session,epoch,tick))return {Status::failure(FailureCode::GenerationMismatch)};
        if(boundary>=tick || tick-boundary!=7)return {Status::failure(FailureCode::IllegalTransition)};
        auto first=tick;
        for(const auto& edit:edits(index)) {
            const auto consumed=history.FirstConsumption(edit.round,edit.sample);
            if(!consumed || consumed<=boundary || consumed>tick)return {Status::failure(FailureCode::UnsupportedContent)};
            first=(std::min)(first,consumed);
        }
        return {Status::success(),first};
    }
private:
    ReplayCorrectionSchedule()=default;
    std::vector<Row> rows_;
    std::vector<ReplayInputOverride> edits_;
};
}
