// Actual production HUD installation preflight/membership and damage backing
// methods. UObject validation and the synchronous native allocator are explicit
// controlled dependencies; neither grants array ownership or recovery.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <set>
#define STR(x) x
#define REQUIRE(x) do {if(!(x)){std::printf("failure line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
namespace RC {enum class LogLevel{Warning,Default};struct Output {template<LogLevel,class... T>static void send(const char*,T...) {}};}
struct Status {bool valid{true};bool ok()const{return valid;}static Status success(){return {};}};
struct Object {unsigned value{};};
struct ObjectId {Object* object{};friend bool operator==(const ObjectId&,const ObjectId&)=default;};
struct Array {Object** data{};int count{},capacity{};};
enum class DamageBackingPhase {Attached,Private,Recovered,Retiring,Retired,Poisoned};
struct PlayerOperation {enum class Phase{Empty,Prepared,Private};Phase phase()const{return Phase::Empty;}};
using ReplayHudPrivatePlayer=PlayerOperation;
using ReplayHudPlayerOperation=PlayerOperation;
struct Effect {
    ObjectId widget,image,slot,animation;Array *active{},*stopped{};
    Array active_image{},stopped_image{};std::array<ObjectId,4> players{};
    PlayerOperation private_player;mutable Array private_backing{};
    mutable std::array<bool,4> evaluation_finished{};
    mutable DamageBackingPhase damage_backing{DamageBackingPhase::Attached};
};
static std::set<Object**> allocations;
static unsigned frees{},resize_calls{},finished_players{};static bool poison_resize{};
static void resize(Array& a,int count) {
    ++resize_calls;REQUIRE(count==0);
    if(poison_resize)return;
    if(a.data){REQUIRE(allocations.erase(a.data)==1);delete[] a.data;++frees;}
    a.data=nullptr;a.capacity=0;
}
struct Sc6ReplayHudState {
    struct Prepared {const Sc6ReplayHudState* target{};const Sc6ReplayHudState* private_hud{};std::array<PlayerOperation,16> players{};struct DamagePlayer {ObjectId identity;};std::array<DamagePlayer,16> damage_players{},type_players{};
 enum class TypeBacking : unsigned char { Empty, Allocating, Owned, Recovering, Recovered, Adopted };
 std::array<TypeBacking,16> type_backings{};};
 std::uintptr_t base_=reinterpret_cast<std::uintptr_t>(&resize)-0x1764ac0;
    Array pool_image_{nullptr,1,1},type_pool_image_{};
    std::array<Effect,16> effects_{},type_effects_{};
    ObjectId damage_listener_{};bool bindings{true},players_unchanged{true};
    Status Fail()const{return {false};}
    Status ValidateBindings(bool=true,bool=false)const{return {bindings&&players_unchanged};}
    bool SameTypes(const Sc6ReplayHudState&)const{return true;}
    static bool ValidArray(const Array& a,int n){return a.count>=0&&a.count<=n&&a.capacity>=a.count&&(!a.capacity||a.data);}
    bool ValidateDiscardedDamage(const Sc6ReplayHudState&,const Prepared&,int i)const{return !effects_[i].active->count && !effects_[i].stopped->count;}
    bool UndoDamagePlayers(const Sc6ReplayHudState&,Prepared&)const{return true;}
    Status FinishDiscardedTypes(const Sc6ReplayHudState*)const{return {};}
    Status UndoTypePlayers(const Sc6ReplayHudState&,Prepared&)const{return {};}
    Status RetireAnnouncements(const Sc6ReplayHudState*)const{return {};}
    Status ParkAnnouncements()const{return {};}
    Status ParkPrivateTypes()const{return {};}
    bool FinishDamagePlayer(const Effect&,int)const{++finished_players;return true;}
    static void ResizeDamageBacking(Array& a){resize(a,0);}
#include "hud_damage_methods.inl"
};
struct Case {
    Object widget{},image{},slot{},animation{},player{123};
    Array active{},stopped{};Sc6ReplayHudState a,b,c;Sc6ReplayHudState::Prepared prepared;
    Object** original{};
    Case() {
        original=new Object*[4]{};allocations.insert(original);original[0]=&player;
        active={original,1,4};
        auto& e=b.effects_[0];e.widget={&widget};e.image={&image};e.slot={&slot};e.animation={&animation};
        e.active=&active;e.stopped=&stopped;e.active_image=active;e.stopped_image=stopped;e.players[0]={&player};
        a.effects_[0]=e;a.effects_[0].active_image.count=0;prepared={&a,&b};
    }
    void publish(){REQUIRE(a.Install(b,prepared).ok());REQUIRE(active.count==0);}
    void native_shrink(){resize(active,0);capture_c();}
    void capture_c(){c.effects_[0]=b.effects_[0];c.effects_[0].active_image=active;c.effects_[0].players={};
        c.effects_[0].damage_backing=DamageBackingPhase::Attached;c.effects_[0].private_backing={};}
};
int main() {
    {
        Case f;f.publish();f.native_shrink();
        REQUIRE(f.b.Install(f.c,f.prepared,true).ok());
        REQUIRE(f.active.data==f.original&&f.active.count==1&&f.active.data[0]==&f.player&&f.player.value==123);
        const auto before=frees;REQUIRE(f.b.Install(f.c,f.prepared,true).ok());REQUIRE(frees==before);
        resize(f.active,0);
    }
    {
        Case f;f.publish();f.native_shrink();
        f.b.players_unchanged=false;REQUIRE(!f.b.Install(f.c,f.prepared,true).ok());
        REQUIRE(allocations.contains(f.original)&&f.active.count==0);
        f.b.players_unchanged=true;Object foreign{};f.c.effects_[0].image={&foreign};
        REQUIRE(!f.b.Install(f.c,f.prepared,true).ok());f.c.effects_[0].image=f.b.effects_[0].image;
        REQUIRE(f.b.Install(f.c,f.prepared,true).ok());resize(f.active,0);
    }
    {
        Case f;f.publish();
        auto* c=new Object*[4]{};allocations.insert(c);c[0]=&f.image;f.active={c,1,4};f.capture_c();
        // A live C evaluator cannot be silently dropped to return B.
        REQUIRE(!f.b.Install(f.c,f.prepared,true).ok());REQUIRE(allocations.contains(f.original)&&allocations.contains(c));
        f.active.count=0;f.capture_c();REQUIRE(f.b.Install(f.c,f.prepared,true).ok());
        REQUIRE(!allocations.contains(c));resize(f.active,0);
    }
    {
        Case f;f.publish();f.native_shrink();
        const auto before=frees;const auto finished_before=finished_players;REQUIRE(f.b.CommitDamageBackings().ok());REQUIRE(frees==before+1);
        REQUIRE(finished_players==finished_before+1);
        REQUIRE(f.b.CommitDamageBackings().ok());REQUIRE(frees==before+1);
        REQUIRE(!f.b.Install(f.c,f.prepared,true).ok());
    }
    {
        Case f;f.publish();auto* c=new Object*[4]{};allocations.insert(c);f.active={c,0,4};f.capture_c();
        poison_resize=true;REQUIRE(!f.b.Install(f.c,f.prepared,true).ok());
        const auto calls=resize_calls;poison_resize=false;
        REQUIRE(!f.b.Install(f.c,f.prepared,true).ok());REQUIRE(resize_calls==calls);
        REQUIRE(allocations.contains(f.original)&&allocations.contains(c));
        REQUIRE(!f.b.CommitDamageBackings().ok());
        // Test teardown owns these fake allocations; production must retain
        // this poisoned transaction and cannot perform another native free.
        resize(f.active,0);resize(f.b.effects_[0].private_backing,0);
    }
    {
        Case f;auto second=f.b.effects_[0];Array invalid=f.active;invalid.count=0;second.active=&invalid;
        f.b.pool_image_.count=2;f.b.effects_[1]=second;
        REQUIRE(!f.b.ParkDamageBackings().ok());REQUIRE(f.active.data==f.original&&f.active.count==1);
        REQUIRE(f.b.effects_[0].damage_backing==DamageBackingPhase::Attached);
        resize(f.active,0);
    }
    {
        Case f;auto alias=f.active;f.b.effects_[1]=f.b.effects_[0];f.b.effects_[1].active=&alias;f.b.pool_image_.count=2;
        REQUIRE(!f.b.ParkDamageBackings().ok());REQUIRE(f.active.count==1&&alias.count==1);
        resize(f.active,0);
    }
    {
        Case f;f.publish();f.native_shrink();f.original[0]=&f.image;
        const auto calls=resize_calls;REQUIRE(!f.b.Install(f.c,f.prepared,true).ok());
        REQUIRE(resize_calls==calls);f.original[0]=&f.player;
        REQUIRE(f.b.Install(f.c,f.prepared,true).ok());resize(f.active,0);
    }
    REQUIRE(allocations.empty());
    std::puts("HUD damage backing native-shrink recovery passed");
}
