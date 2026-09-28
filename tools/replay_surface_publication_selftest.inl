// Actual host capture/display selection policy with controlled surface leases.
namespace CheckpointDisplayTest {
struct Sc6ReplayHost {
    struct SurfaceSnapshot { struct { int image{}; } image; bool retained{}; std::uint64_t session{},tick{},epoch{}; };
    struct Checkpoint {
        std::shared_ptr<const SurfaceSnapshot> surface;
        bool rolling_display_omitted{};
        std::uint64_t session{},epoch{};
        struct { std::uint64_t tick{}; } execution;
    };
    enum class RollingPhase { Idle,Capturing,Seeking,Failed };
    struct {
        struct { RollingPhase phase{}; std::uint64_t current{}; } witness;
        std::array<std::shared_ptr<Checkpoint>,8> checkpoints;
        bool correcting{};
    } rolling_;
    std::shared_ptr<SurfaceSnapshot> held_surface_;
    bool historical_restore_{},checkpoint_restoring_{};
    bool corrected_capture_{},capture_driving_{};
    std::uint64_t checkpoint_session_{1};
    bool CaptureCheckpointDisplay(Checkpoint&) const noexcept;
    std::shared_ptr<const SurfaceSnapshot> RestoreCheckpointDisplay(const Checkpoint&) const noexcept;
};
#include "deterministic/Sc6ReplayHost.CheckpointDisplay.inl"
void Run() {
    using H=Sc6ReplayHost;
    H host;host.held_surface_=std::make_shared<H::SurfaceSnapshot>();
    auto& held=*host.held_surface_;held.retained=true;held.image.image=2;held.session=1;held.tick=217;held.epoch=19;
    H::Checkpoint b;b.session=1;b.epoch=19;b.execution.tick=217;
    expect(host.CaptureCheckpointDisplay(b) && b.surface==host.held_surface_ && !b.rolling_display_omitted,
        "ordinary checkpoint retains exact current image");
    host.rolling_.witness={H::RollingPhase::Capturing,217};
    H::Checkpoint rolling=b;
    expect(host.CaptureCheckpointDisplay(rolling) && !rolling.surface && rolling.rolling_display_omitted
        && b.surface==host.held_surface_,"rolling capture drops only its image pin, preserving B");
    host.historical_restore_=true;
    expect(host.CaptureCheckpointDisplay(b) && b.surface==host.held_surface_ && !b.rolling_display_omitted,
        "private complete B retains image even if rolling phase is capturing");
    host.historical_restore_=false;host.checkpoint_restoring_=true;
    expect(host.CaptureCheckpointDisplay(b) && b.surface==host.held_surface_,"checkpoint undo retains image");
    host.checkpoint_restoring_=false;
    for(int invalid=0;invalid<5;++invalid) {
        auto saved=held;
        if(invalid==0)held.retained=false;
        if(invalid==1)held.image.image=0;
        if(invalid==2)++held.session;
        if(invalid==3)++held.tick;
        if(invalid==4)++held.epoch;
        expect(!host.CaptureCheckpointDisplay(rolling),"omission still requires actual completed-frame identity");
        held=saved;
    }
    auto a=std::make_shared<H::Checkpoint>();a->session=1;a->epoch=12;a->execution.tick=210;a->rolling_display_omitted=true;
    host.rolling_.checkpoints[210%8]=a;host.rolling_.witness={H::RollingPhase::Seeking,217};
    expect(host.RestoreCheckpointDisplay(*a)==b.surface,"exact ring target holds complete B display");
    auto foreign=*a;
    expect(!host.RestoreCheckpointDisplay(foreign),"same-tick foreign checkpoint cannot omit image");
    for(int invalid=0;invalid<8;++invalid) {
        auto saved=held;auto phase=host.rolling_.witness.phase;
        if(invalid==0)host.rolling_.witness.phase=H::RollingPhase::Idle;
        if(invalid==1)host.rolling_.witness.phase=H::RollingPhase::Failed;
        if(invalid==2)held.retained=false;
        if(invalid==3)held.image.image=0;
        if(invalid==4)++held.session;
        if(invalid==5)++held.tick;
        if(invalid==6)a->surface=b.surface;
        if(invalid==7)++a->execution.tick;
        expect(!host.RestoreCheckpointDisplay(*a),"missing target image rejects invalid operation or B");
        held=saved;host.rolling_.witness.phase=phase;a->surface.reset();a->execution.tick=210;
    }
    a->rolling_display_omitted=false;
    expect(!host.RestoreCheckpointDisplay(*a),"unmarked missing image never gets B fallback");
    expect(host.RestoreCheckpointDisplay(b)==b.surface,"historical image remains independently valid");
    ++b.epoch;expect(!host.RestoreCheckpointDisplay(b),"historical image epoch must match");
}
}
// Compile the host's actual render-thread switch cases. This validates image
// identity/undo bookkeeping, not D3D completion or native visual coherence.
namespace SurfacePublicationTest {
enum class SurfaceCommand { RetainCheckpoint, InstallCheckpoint, UndoCheckpoint,PublishCheckpoint,FinishDisplayCommit,FinishDisplayRecovery };
struct Snapshot { int image{}; bool retained{};std::uint64_t tick{}; };
struct Target { std::shared_ptr<Snapshot> surface; };
struct Transaction {
    std::shared_ptr<Target> target;
    std::shared_ptr<Snapshot> presented_surface;
    int undo_surface{};
    bool surface_dirty{};
    void* execution{};bool display_started{},display_complete{},display_finished{};
    struct {std::uint64_t original_tick{};}witness;
};
struct Host {
    std::shared_ptr<Snapshot> held_surface_;
    std::shared_ptr<Transaction> historical_restore_;
    SurfaceCommand surface_command_{};
};
struct Surface {
    int live{2}; bool reject{};
    bool retain_replay_surface(int& out) {
        if(reject || out) return false;
        out=live;return true;
    }
    bool install_replay_surface(int target,int& undo,bool=false) {
        if(reject || !target || undo) return false;
        undo=live;live=target;return true;
    }
    void publish_replay_controls(std::uint64_t,bool,bool,bool){}
    bool publish_replay_surface(const int* expected){return !reject && expected && *expected==live;}
    bool finish_replay_display_transaction(bool){return !reject;}
    bool undo_replay_surface(int target,int& undo) {
        if(reject || target!=live || !undo) return false;
        live=undo;undo=0;return true;
    }
};
bool Execute(Host* self,Surface& surface,SurfaceCommand command) {
    self->surface_command_=command;
    bool result=false;
    switch(command) {
#include "deterministic/Sc6ReplayHost.SurfacePublication.inl"
    }
    return result;
}
void Run() {
    CheckpointDisplayTest::Run();
    for(bool rolling_display : {false,true}) for(bool completed_frame : {false,true}) {
        Host host;Surface surface;
        auto a=std::make_shared<Snapshot>();a->image=1;a->retained=true;
        auto b=std::make_shared<Snapshot>();b->image=2;b->retained=true;
        host.held_surface_=b;
        host.historical_restore_=std::make_shared<Transaction>();
        auto& tx=*host.historical_restore_;
        tx.target=std::make_shared<Target>();tx.target->surface=rolling_display?nullptr:a;tx.presented_surface=rolling_display?b:a;
        const auto installed=tx.presented_surface;const int installed_image=rolling_display?2:1;
        surface.reject=true;
        expect(!Execute(&host,surface,SurfaceCommand::InstallCheckpoint)
            && !tx.surface_dirty && !tx.undo_surface && surface.live==2,
            "failed display installation preserves B");
        surface.reject=false;
        expect(Execute(&host,surface,SurfaceCommand::InstallCheckpoint)
            && tx.surface_dirty && tx.undo_surface==2 && surface.live==installed_image,
            "A display publication retains B");
        if(completed_frame) {
            surface.live=3;host.held_surface_=std::make_shared<Snapshot>();
            surface.reject=true;
            expect(!Execute(&host,surface,SurfaceCommand::RetainCheckpoint)
                && tx.presented_surface==installed && tx.undo_surface==2,
                "failed C retention does not publish its identity or release B");
            surface.reject=false;
            expect(Execute(&host,surface,SurfaceCommand::RetainCheckpoint)
                && tx.presented_surface==host.held_surface_ && tx.presented_surface->image==3,
                "completed frame publishes the actually retained C image");
        } else {
            expect(host.held_surface_==b && tx.presented_surface==installed,
                "handoff without a completed frame leaves A published despite held B metadata");
        }
        const int correct=surface.live;surface.live=99;
        expect(!Execute(&host,surface,SurfaceCommand::UndoCheckpoint)
            && tx.surface_dirty && tx.undo_surface==2,
            "foreign current display rejects undo and retains B");
        surface.live=correct;
        expect(Execute(&host,surface,SurfaceCommand::UndoCheckpoint)
            && !tx.surface_dirty && !tx.undo_surface && surface.live==2,
            "publication identity restores B before and after completed-frame capture");
        expect(!Execute(&host,surface,SurfaceCommand::UndoCheckpoint) && surface.live==2,
            "display undo completes exactly once");
    }
}
}
