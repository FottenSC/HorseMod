#include "replay_qualification_mod/ReplayStartupLoading.hpp"
namespace StartupLoadingTest {
struct Native {
    bool owner=true,signatures=true,ready=true,suspended{},pending=true,advance{},incomplete{},reenter{};
    unsigned tick{},flushes{},callbacks{};float registration_budget=5.0f;
    int raw_pending=2;unsigned raw_waits{},raw_callbacks{};std::uint64_t milliseconds{};
    bool raw_stuck{},raw_advance{};unsigned raw_busy{};
    int RawPending(){if(raw_busy){--raw_busy;return -2;}return raw_pending;}
    std::uint64_t Milliseconds(){return milliseconds;}
    void WaitOne(){++raw_waits;milliseconds+=1000;if(!raw_stuck && raw_pending>0){--raw_pending;++raw_callbacks;}if(raw_advance)++tick;}
    float& RegistrationBudget(){return registration_budget;}
    Horse::Qualification::ReplayStartupLoading* policy{};
    bool Owner(){return owner;} bool Signatures(){return signatures;}
    bool Ready(){return ready;} bool Suspended(){return suspended;}
    unsigned Tick(){return tick;} bool Pending(){return pending;}
    void Flush(){++flushes;++callbacks;if(advance)++tick;pending=incomplete;if(reenter)policy->Poll(*this,true);}
};
template<class Policy> const char* RawAfter(Policy& policy,Native& native,bool eligible) {
    if constexpr(requires {policy.AfterEngine(native,eligible);})return policy.AfterEngine(native,eligible);
    else return "production startup raw completion boundary missing";
}
void Run() {
    using Policy=Horse::Qualification::ReplayStartupLoading;
    {Policy q;Native m;m.pending=false;
        expect(!RawAfter(q,m,false) && m.raw_waits==0,"raw startup wait respects setup eligibility");
        expect(!q.Poll(m,true),"registration ownership acquired before raw completion");
        expect(!RawAfter(q,m,true) && m.raw_pending==0 && m.raw_callbacks==2 && m.raw_waits==2,
            "native raw completion callbacks finish before next engine interval");
        expect(!m.flushes && m.tick==0,"raw wait does not manufacture packages or simulation ticks");
        m.tick=1;m.raw_pending=1;expect(!RawAfter(q,m,true) && m.raw_waits==2,"raw wait ends at native simulation");}
    {Policy q;Native m;m.pending=false;m.raw_stuck=true;expect(!q.Poll(m,true),"raw timeout setup");
        expect(RawAfter(q,m,true)!=nullptr && m.raw_pending==2 && m.raw_callbacks==0 && m.registration_budget==5.0f,
            "timeout retains native in-flight requests and restores owned setup budget");
        const auto waits=m.raw_waits;expect(RawAfter(q,m,true)!=nullptr && waits==m.raw_waits,"raw failure is terminal");}
    {Policy q;Native m;m.raw_busy=1;expect(!RawAfter(q,m,true) && m.raw_pending==0,"busy native service lock is retried without holding it across wait");}
    {Policy q;Native m;m.raw_pending=-1;expect(RawAfter(q,m,true)!=nullptr && !m.raw_waits,"invalid native queue rejects before waiting");}
    {Policy q;Native m;m.raw_advance=true;expect(RawAfter(q,m,true)!=nullptr,"unexpected simulation advancement during raw wait rejects");}
    Policy p;Native n;n.policy=&p;
    expect(!p.Poll(n,false) && n.flushes==0,"startup flush disabled outside staged replay");
    expect(!p.Poll(n,true) && p.calls==1 && n.callbacks==1 && !n.pending && n.tick==0,
        "startup flush executes native drain callback without advancing simulation");
    expect(n.registration_budget==60000.0f,"native registration budget is raised before startup work");
    expect(!p.Poll(n,true) && n.flushes==1,"no pending work is not flushed again");
    n.tick=1;expect(!p.Poll(n,true) && p.complete,"policy ends permanently at first simulation tick");
    expect(n.registration_budget==5.0f,"startup completion restores the exact original registration budget");
    n.tick=0;n.pending=true;expect(!p.Poll(n,true) && n.flushes==1,"later tick rewind cannot rearm startup policy");
    for(int fault=0;fault<8;++fault) {
        Policy q;Native m;m.policy=&q;
        if(fault==0)m.owner=false;
        if(fault==1)m.signatures=false;
        if(fault==2)m.suspended=true;
        if(fault==3)q.calls=64;
        if(fault==4)m.advance=true;
        if(fault==5)m.incomplete=true;
        if(fault==6)m.reenter=true;
        if(fault==7)q.busy=true;
        expect(q.Poll(m,true)!=nullptr,"startup invalid binding, suspension, capacity, advancement, pending or reentry rejects");
        expect(m.registration_budget==5.0f,"failed startup restores acquired registration budget");
        const auto before=m.flushes;
        expect(q.Poll(m,true)!=nullptr && before==m.flushes,"startup failure is terminal and never retries native work");
        if(fault<4 || fault==7)expect(m.flushes==0,"preflight rejection precedes native drain");
    }
    {Policy q;Native m;m.registration_budget=2.5f;m.pending=false;
        expect(!q.Poll(m,true) && m.registration_budget==60000.0f && !m.flushes,"registration ownership does not depend on pending packages");
        m.tick=1;expect(!q.Poll(m,true) && m.registration_budget==2.5f && q.registration_restored,"exact custom predecessor restored");}
    {Policy q;Native m;m.registration_budget=-1;
        expect(q.Poll(m,true)!=nullptr && m.registration_budget==-1 && !m.flushes,"invalid budget rejects without mutation");}
    {Policy q;Native m;m.pending=false;expect(!q.Poll(m,true),"budget acquired");m.registration_budget=12;
        expect(q.Poll(m,true)!=nullptr && m.registration_budget==12 && q.registration_owned,"changed budget is contained without overwriting another owner");}
    Policy waiting;Native loader;loader.ready=false;
    expect(!waiting.Poll(loader,true) && !loader.flushes,"uninitialized loader is not invoked");
}
}
