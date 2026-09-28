#pragma once
#include "../../HorseMod/horselib/deterministic/ReplayRollingCorrections.hpp"
#include <charconv>
#include <limits>
#include <string_view>
#include <vector>

struct ReplayRollingCorrectionRow {
    std::uint64_t arrival_tick{}, expected_revision{};
    Horse::Deterministic::ReplayInputOverride edit{};
};

// Protocol 14: decimal arrival:revision:round:sample:players:raw0:raw1 rows.
// Parse into private storage so malformed requests cannot publish partial edits.
inline bool ParseRollingCorrectionRequest(std::string_view text,
    std::vector<ReplayRollingCorrectionRow>& output)
{
    if(text.empty() || text.size()>128*600)return false;
    std::vector<ReplayRollingCorrectionRow> parsed;
    while(!text.empty()) {
        if(parsed.size()==600)return false;
        const auto separator=text.find(';');
        auto row=text.substr(0,separator);
        std::uint64_t values[7]{};
        for(unsigned i=0;i<7;++i) {
            const auto colon=row.find(':');
            if((i<6)==(colon==std::string_view::npos))return false;
            const auto field=row.substr(0,colon);
            if(field.empty())return false;
            const auto result=std::from_chars(field.data(),field.data()+field.size(),values[i]);
            if(result.ec!=std::errc{} || result.ptr!=field.data()+field.size())return false;
            if(i<6)row.remove_prefix(colon+1);
        }
        if(!values[0] || values[2]>INT32_MAX || values[3]>UINT32_MAX
            || !values[4] || values[4]>3 || values[5]>UINT32_MAX || values[6]>UINT32_MAX)return false;
        if(!parsed.empty()) {
            const auto& previous=parsed.back();
            if(values[0]<previous.arrival_tick
                || (values[0]==previous.arrival_tick
                    && (values[1]!=previous.expected_revision
                        || values[2]!=static_cast<std::uint64_t>(previous.edit.round)
                        || values[3]<=previous.edit.sample))
                || (values[0]>previous.arrival_tick && values[1]<=previous.expected_revision))return false;
        }
        parsed.push_back({values[0],values[1],{static_cast<std::int32_t>(values[2]),
            static_cast<std::uint32_t>(values[3]),{static_cast<std::uint32_t>(values[5]),
            static_cast<std::uint32_t>(values[6])},static_cast<std::uint8_t>(values[4])}});
        if(separator==std::string_view::npos)break;
        text.remove_prefix(separator+1);
        if(text.empty())return false;
    }
    output.swap(parsed);
    return true;
}

// The wire format has one edit per row. A shared arrival/revision is one host
// request with ordered edits; the host accepts only one request per native tick.
inline bool GroupRollingCorrectionRequests(std::span<const ReplayRollingCorrectionRow> wire,
    std::uint64_t session,std::uint64_t epoch,
    std::vector<Horse::Deterministic::ReplayInputOverride>& output_edits,
    std::vector<Horse::Deterministic::ReplayCorrectionRequest>& output_requests)
{
    using Horse::Deterministic::ReplayCorrectionRequest;
    using Horse::Deterministic::ReplayInputOverride;
    if(wire.empty() || wire.size()>600 || !session || !epoch)return false;
    std::vector<ReplayInputOverride> edits;
    std::vector<ReplayCorrectionRequest> requests;
    edits.reserve(wire.size());requests.reserve(wire.size());
    for(const auto& row:wire) {
        if(!row.arrival_tick || row.edit.round<0 || !row.edit.players || (row.edit.players&~3))return false;
        if(!requests.empty()) {
            const auto& prior=requests.back();
            if(row.arrival_tick<prior.arrival_tick)return false;
            if(row.arrival_tick>prior.arrival_tick && row.expected_revision<=prior.expected_revision)return false;
            if(row.arrival_tick==prior.arrival_tick
                && (row.expected_revision!=prior.expected_revision
                    || row.edit.round!=edits.back().round || row.edit.sample<=edits.back().sample))return false;
        }
        edits.push_back(row.edit);
        if(requests.empty() || row.arrival_tick!=requests.back().arrival_tick)
            requests.push_back({row.arrival_tick,session,epoch,row.expected_revision,{}});
        auto& request=requests.back();
        const auto offset=edits.size()-1;
        const auto count=request.overrides.size()+1;
        request.overrides={edits.data()+offset+1-count,count};
    }
    output_edits=std::move(edits);
    output_requests=std::move(requests);
    return true;
}

struct ReplayRollingAuthoredWrite {
    std::uintptr_t address{};
    std::uint32_t original{},value{};
};

// Native authored recorder layout, shared with the existing source-revision
// control. This prepares values only; every binding must pass before writes.
template<class Read>
bool PrepareRollingAuthoredControl(std::span<const ReplayRollingCorrectionRow> rows,
    std::uintptr_t owner,std::uintptr_t base,Read&& read,
    std::vector<ReplayRollingAuthoredWrite>& output)
{
    if(rows.empty() || rows.size()>600 || !owner)return false;
    std::uintptr_t tracker{},rounds{};int round_count{},cursor{};
    if(!read(owner+0x390,tracker) || tracker!=base+0x3290d20
        || !read(owner+0x3b8,rounds) || !rounds
        || !read(owner+0x3c0,round_count) || round_count<1 || round_count>32
        || !read(owner+0x3a0,cursor) || cursor<0)return false;
    std::vector<ReplayRollingAuthoredWrite> writes;
    writes.reserve(rows.size()*2);
    for(const auto& row:rows) {
        const auto& edit=row.edit;
        if(edit.round<0 || edit.round>=round_count || !edit.players || edit.players>3
            || (edit.round==0 && edit.sample<static_cast<unsigned>(cursor)))return false;
        std::uintptr_t recorders{};int count{};
        const auto round=rounds+static_cast<std::uintptr_t>(edit.round)*16;
        if(!read(round,recorders) || !recorders || !read(round+8,count) || count!=2)return false;
        for(unsigned player=0;player<2;++player)if(edit.players&(1u<<player)) {
            const auto recorder=recorders+player*24;
            std::uintptr_t object{},vtable{},data{};unsigned recorded{};int size{},capacity{};
            if(!read(recorder+4,recorded) || edit.sample>=recorded
                || !read(recorder+16,object) || !object
                || !read(object,vtable) || vtable!=base+0x328e948
                || !read(object+8,data) || !data
                || !read(object+16,size) || size<0 || (size&3)
                || edit.sample>=static_cast<unsigned>(size)/4
                || !read(object+20,capacity) || capacity<size)return false;
            ReplayRollingAuthoredWrite write{data+static_cast<std::uintptr_t>(edit.sample)*4,0,edit.raw[player]};
            if(!read(write.address,write.original))return false;
            writes.push_back(write);
        }
    }
    output.swap(writes);
    return true;
}
