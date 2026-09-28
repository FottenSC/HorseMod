// Actual production type-player undo/release and membership admission.
// Native Finish and GC registry are controlled edges; B membership and the
// operation phase transitions are not supplied by the fixture.
#include "horselib/deterministic/ReplayHudPlayback.hpp"
using namespace Horse::Deterministic;
#include <array>
#include <memory>
#include <cstdio>
#include <cstdlib>
#define REQUIRE(x) do{if(!(x)){std::printf("failure line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
struct Object {bool live{true};bool evaluated{true};unsigned finishes{};};
struct ObjectId {Object* object{};bool operator==(const ObjectId&)const=default;};
struct Array {Object** data{};int count{},capacity{};};
enum class DamageBackingPhase {Attached,Private,Recovered};
struct Lease {bool fail{};unsigned calls{};struct Status{bool valid;bool ok()const{return valid;}};Status Release(){++calls;return {!fail};}};
struct PrivatePlayer { ReplayHudPrivatePlayer::Phase value{ReplayHudPrivatePlayer::Phase::Private}; auto phase()const{return value;} };
struct Effect {enum class Retirement {None,Finished};std::array<Retirement,4> retirement{};PrivatePlayer private_player;ObjectId animation;Array* active{};Array* stopped{};Array active_image{};std::array<ObjectId,4> players{};DamageBackingPhase damage_backing{DamageBackingPhase::Attached};};
struct Sc6ReplayHudState {
    struct Prepared {
        const Sc6ReplayHudState* target{};const Sc6ReplayHudState* private_hud{};
        struct DamagePlayer {
            enum class Phase {Empty,Constructing,Constructed,Starting,Published,Finishing,Finished,Released};
            Phase phase{};ObjectId identity{};std::shared_ptr<Lease> lease;
        };
        std::array<DamagePlayer,16> type_players{};std::array<ReplayHudPlayerOperation,16> players{};
    };
    using TypeEffect=Effect;
    struct Status{bool valid;bool ok()const{return valid;} explicit operator bool()const{return valid;}static Status success(){return {true};}};
    static Status Fail(){return {false};}
    Array type_pool_image_{nullptr,1,1};std::array<Effect,16> type_effects_{},effects_{};
    static bool ValidArray(const Array& a,int max){return a.count>=0&&a.count<=max&&a.capacity>=a.count&&(!a.count||a.data);}
    static bool Live(ObjectId id){return id.object&&id.object->live;}
    static ObjectId Id(Object* p){return p&&p->live?ObjectId{p}:ObjectId{};}
    bool FinishReconstructedTypePlayer(const Effect& e)const {
        auto* p=e.players[0].object;if(!p||!p->live)return false;
        if(p->evaluated){++p->finishes;p->evaluated=false;}return true;
    }
#include "hud_fresh_type_methods.inl"
};
using Phase=Sc6ReplayHudState::Prepared::DamagePlayer::Phase;
struct Case {
    Object original{},fresh{},born{};Object* rows[4]{&fresh};Array active{rows,1,4},stopped{};
    Sc6ReplayHudState a,b,c;Sc6ReplayHudState::Prepared op{&a,&b};
    Case(){auto& e=b.type_effects_[0];e.active=&active;e.stopped=&stopped;e.active_image.count=1;e.players[0]={&original};e.damage_backing=DamageBackingPhase::Private;
        c.type_effects_[0]=e;c.type_effects_[0].players[0]={&fresh};
        op.type_players[0].phase=Phase::Published;op.type_players[0].identity={&fresh};op.type_players[0].lease=std::make_shared<Lease>();}
};
int main(){
    {Case f;REQUIRE(f.b.UndoTypePlayers(f.c,f.op));REQUIRE(!f.active.count&&f.fresh.finishes==1&&f.original.finishes==0&&f.original.evaluated);
        f.active.data[0]=&f.original;f.active.count=1;f.b.type_effects_[0].private_player.value=ReplayHudPrivatePlayer::Phase::Recovered;
        REQUIRE(f.b.UndoTypePlayers(f.c,f.op));REQUIRE(f.original.finishes==0);}
    {Case f;f.rows[0]=&f.original;REQUIRE(!f.b.UndoTypePlayers(f.c,f.op));REQUIRE(f.original.finishes==0&&f.active.count==1);}
    {Case f;f.rows[0]=&f.born;REQUIRE(!f.b.UndoTypePlayers(f.b,f.op));REQUIRE(f.born.finishes==0&&f.fresh.finishes==0);}
    {Case f;f.rows[0]=&f.born;f.c.type_effects_[0].players[0]={&f.born};REQUIRE(f.b.UndoTypePlayers(f.c,f.op));REQUIRE(f.born.finishes==1&&f.fresh.finishes==1&&f.original.finishes==0);}
    {Case f;f.active.count=0;REQUIRE(f.b.UndoTypePlayers(f.c,f.op));REQUIRE(f.fresh.finishes==1);}
    {Case f;f.op.type_players[0].phase=Phase::Constructing;REQUIRE(!f.b.UndoTypePlayers(f.c,f.op));REQUIRE(f.fresh.finishes==0&&f.active.count==1);}
    {Case f;auto lease=f.op.type_players[0].lease;lease->fail=true;REQUIRE(!f.b.UndoTypePlayers(f.c,f.op));REQUIRE(f.op.type_players[0].phase==Phase::Finished&&f.fresh.finishes==1);
        lease->fail=false;REQUIRE(f.b.UndoTypePlayers(f.c,f.op));REQUIRE(f.fresh.finishes==1&&lease->calls==2);}
    {Case f;REQUIRE(!f.a.ReleaseTypePlayers(f.op,true));REQUIRE(f.op.type_players[0].lease);REQUIRE(f.a.ReleaseTypePlayers(f.op));REQUIRE(f.fresh.evaluated&&f.original.evaluated);}
    {Case f;f.op.type_players[0].phase=Phase::Empty;f.op.type_players[0].identity={};f.b.type_effects_[0].private_player.value=ReplayHudPrivatePlayer::Phase::Prepared;f.rows[0]=&f.original;
        REQUIRE(f.b.UndoTypePlayers(f.c,f.op));REQUIRE(f.original.finishes==0&&f.active.count==1);}
    {Case f;f.fresh.evaluated=false;f.fresh.finishes=1;f.c.type_effects_[0].retirement[0]=Effect::Retirement::Finished;
        REQUIRE(f.b.UndoTypePlayers(f.c,f.op));REQUIRE(f.fresh.finishes==1);}
    std::puts("fresh type owner undo/release passed");
}
