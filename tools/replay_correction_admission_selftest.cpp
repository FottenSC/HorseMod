#include "HorseMod/horselib/deterministic/ReplayRollingCorrections.hpp"
#include "tools/replay_qualification_mod/ReplayRollingCorrectionRequest.hpp"
#include <cassert>
#include <map>
using namespace Horse::Deterministic;
int main() {
 std::vector<ReplayRollingCorrectionRow> wire;
 assert(ParseRollingCorrectionRequest("217:0:0:169:2:0:0",wire) && wire.size()==1);
 assert(wire[0].arrival_tick==217 && wire[0].edit.sample==169 && wire[0].edit.players==2);
 for(auto malformed:{"", "217:0:0:169:2:0", "217:0:0:169:0:0:0", "217:0:0:169:2:0:0;", "217:-1:0:169:2:0:0", "217:0:0:169:2:0:4294967296"}) {
   auto before=wire;assert(!ParseRollingCorrectionRequest(malformed,wire));assert(wire.size()==before.size());
 }
 assert(ParseRollingCorrectionRequest("217:0:0:169:1:1:0;217:0:0:170:2:0:2;218:1:0:171:1:3:0",wire));
 assert(wire.size()==3 && wire[0].arrival_tick==wire[1].arrival_tick
     && wire[0].edit.sample==169 && wire[1].edit.sample==170);
 std::vector<ReplayInputOverride> grouped_edits;
 std::vector<ReplayCorrectionRequest> grouped_requests;
 assert(GroupRollingCorrectionRequests(wire,1,3,grouped_edits,grouped_requests));
 assert(grouped_requests.size()==2 && grouped_requests[0].arrival_tick==217
     && grouped_requests[0].expected_revision==0 && grouped_requests[0].overrides.size()==2
     && grouped_requests[0].overrides[0].sample==169 && grouped_requests[0].overrides[1].sample==170
     && grouped_requests[1].arrival_tick==218 && grouped_requests[1].expected_revision==1
     && grouped_requests[1].overrides.size()==1 && grouped_requests[1].overrides[0].sample==171);
 ReplayCorrectionSchedule::Handle grouped_schedule;
 assert(ReplayCorrectionSchedule::Create(grouped_requests,1024*1024,grouped_schedule).ok()
     && grouped_schedule->size()==2 && grouped_schedule->edits(0).size()==2);
 for(auto malformed:{"217:0:0:169:1:1:0;217:1:0:170:2:0:2",
                     "217:0:0:169:1:1:0;217:0:0:169:2:0:2",
                     "217:0:0:170:1:1:0;217:0:0:169:2:0:2",
                     "217:0:0:169:1:1:0;217:0:1:170:2:0:2"}) {
   const auto before=wire;assert(!ParseRollingCorrectionRequest(malformed,wire));assert(wire.size()==before.size());
 }
 auto invalid_wire=wire;invalid_wire[1].expected_revision=1;
 assert(!GroupRollingCorrectionRequests(invalid_wire,1,3,grouped_edits,grouped_requests)
     && grouped_requests.size()==2 && grouped_requests[0].overrides.size()==2);
 invalid_wire=wire;invalid_wire[2].expected_revision=0;
 assert(!GroupRollingCorrectionRequests(invalid_wire,1,3,grouped_edits,grouped_requests)
     && grouped_requests.size()==2 && grouped_requests[0].overrides.size()==2);
 wire.resize(1);wire[0].arrival_tick=217;wire[0].edit.sample=169;wire[0].edit.players=2;
 // Prepare all native authored writes before touching any recording memory.
 std::map<std::uintptr_t,std::uint64_t> memory{
   {0x1000+0x390,0x3290d20},{0x1000+0x3b8,0x2000},{0x1000+0x3c0,1},{0x1000+0x3a0,1},
   {0x2000,0x3000},{0x2008,2},{0x3000+24+4,200},{0x3000+24+16,0x4000},
   {0x4000,0x328e948},{0x4008,0x5000},{0x4010,800},{0x4014,800},{0x5000+169*4,8}};
 auto read=[&](std::uintptr_t address,auto& value) {
   auto found=memory.find(address);if(found==memory.end())return false;
   value=static_cast<std::remove_reference_t<decltype(value)>>(found->second);return true;
 };
 std::vector<ReplayRollingAuthoredWrite> writes;
 assert(PrepareRollingAuthoredControl(wire,0x1000,0,read,writes));
 assert(writes.size()==1 && writes[0].address==0x5000+169*4 && writes[0].original==8 && writes[0].value==0);
 memory[0x4014]=4;
 assert(!PrepareRollingAuthoredControl(wire,0x1000,0,read,writes) && writes.size()==1);
 memory[0x4014]=800;wire.push_back(wire[0]);wire.back().edit.sample=201;
 assert(!PrepareRollingAuthoredControl(wire,0x1000,0,read,writes) && writes.size()==1);
 wire.pop_back();
 ReplayRollingInputHistory history;
 history.Begin(1,1,0,0,0);
 assert(history.Record(1,1,0,0).ok());
 assert(history.FirstConsumption(0,0)==1);
 history.Begin(1,3,2,100,50);
 for(unsigned i=1;i<=7;++i)assert(history.Record(100+i,10+i,2,49+i).ok());
 ReplayInputOverride edit{2,50,{1,0},1};
 ReplayCorrectionRequest request{107,1,3,0,{&edit,1}};
 ReplayCorrectionSchedule::Handle schedule;
 assert(ReplayCorrectionSchedule::Create({&request,1},1024*1024,schedule).ok());
 edit.sample=999; // The immutable schedule owns its authored payload.
 assert(schedule->edits(0)[0].sample==50);
 auto admitted=schedule->Admit(0,107,1,3,0,100,history);
 assert(admitted.status.ok() && admitted.first_consumption==101);
 assert(!schedule->Admit(0,107,2,3,0,100,history).status.ok());
 assert(!schedule->Admit(0,107,1,4,0,100,history).status.ok());
 assert(!schedule->Admit(0,107,1,3,1,100,history).status.ok());
 assert(!schedule->Admit(0,108,1,3,0,100,history).status.ok());
 assert(!schedule->Admit(0,107,1,3,0,101,history).status.ok());
 auto retained=schedule;
 assert(!ReplayCorrectionSchedule::Create({&request,1},1,schedule).ok() && schedule==retained);
 // A repeated publication retains its first consumption even after ring eviction.
 history.Begin(1,3,2,100,50);
 for(unsigned i=1;i<=12;++i)assert(history.Record(100+i,11,2,50).ok());
 edit.sample=50;request.arrival_tick=112;
 assert(ReplayCorrectionSchedule::Create({&request,1},1024*1024,schedule).ok());
 assert(!schedule->Admit(0,112,1,3,0,105,history).status.ok());
 assert(history.FirstConsumption(2,50)==101);
 assert(!history.Record(113,11,2,51).ok());
 // The rolling driver admits one correction per tick. Same-tick requests must
 // be grouped into one row before any source revision can be published.
 ReplayCorrectionRequest rows[]{request,request};rows[1].expected_revision=1;
 auto prior=schedule;
 assert(!ReplayCorrectionSchedule::Create(rows,1024*1024,schedule).ok() && schedule==prior);
 rows[1].arrival_tick=113;
 assert(ReplayCorrectionSchedule::Create(rows,1024*1024,schedule).ok());
 // A later arrival cannot refer to the same or an older source revision.
 // Reject the whole schedule before a rolling host can acquire its window.
 auto valid_schedule=schedule;
 rows[1].expected_revision=0;
 assert(!ReplayCorrectionSchedule::Create(rows,1024*1024,schedule).ok() && schedule==valid_schedule);
 rows[0].expected_revision=2;rows[1].expected_revision=1;
 assert(!ReplayCorrectionSchedule::Create(rows,1024*1024,schedule).ok() && schedule==valid_schedule);
 rows[0].expected_revision=0;rows[1].expected_revision=1;
 rows[1].arrival_tick=111;
 assert(!ReplayCorrectionSchedule::Create(rows,1024*1024,schedule).ok());
}
