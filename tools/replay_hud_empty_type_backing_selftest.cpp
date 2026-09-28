#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#define REQUIRE(x) do{if(!(x)){std::printf("failure line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
struct Object { std::array<std::byte,0x780> bytes{}; };
struct ObjectId{Object* object{};};
struct Array{Object** data{};int count{},capacity{};};
struct Status{bool value;bool ok()const{return value;}static Status success(){return{true};}};
struct PlayerOperation{bool prepared{};bool Prepare(){if(prepared)return false;prepared=true;return true;}};
struct Sc6ReplayObjectLease{};
struct Effect{bool reconstructible{true};Array active_image{};Array* active{};Array* stopped{};std::array<ObjectId,4> players{};};
static unsigned allocations{},frees{};static bool fail_allocate{},fail_free{};
static void Resize(void* ptr,int capacity){auto& a=*static_cast<Array*>(ptr);
 if(capacity){++allocations;if(fail_allocate)return;a.data=static_cast<Object**>(std::malloc(4*sizeof(Object*)));a.capacity=4;}
 else{++frees;if(fail_free)return;std::free(a.data);a.data=nullptr;a.capacity=0;}}
struct Sc6ReplayHudState{
 struct Prepared{const Sc6ReplayHudState* target{};const Sc6ReplayHudState* private_hud{};std::array<PlayerOperation,16> players{};
 enum class TypeBacking : unsigned char { Empty, Allocating, Owned, Recovering, Recovered, Adopted };
 std::array<TypeBacking,16> type_backings{};
 struct DamagePlayer {enum class Phase {Empty,Constructing,Constructed,Starting,Published,Finishing,Finished,Released};Phase phase{};ObjectId identity{};std::shared_ptr<Sc6ReplayObjectLease> lease;};
 std::array<DamagePlayer,16> type_players{};};
 std::uintptr_t base_=reinterpret_cast<std::uintptr_t>(&Resize)-0x1764ac0;
 Array type_pool_image_{nullptr,1,1},pool_image_{};std::array<Effect,16> type_effects_{},effects_{};
 template<class T>static T& At(Object* p,std::size_t n){return *reinterpret_cast<T*>(p->bytes.data()+n);}
 bool SameTypes(const Sc6ReplayHudState&)const{return true;}
 bool TypePlayerBinding(const Effect&)const{return true;}bool TypeAssetReconstructible(const Effect&)const{return true;}
 static bool ValidArray(const Array& a,int max){return a.count>=0&&a.count<=max&&a.capacity>=a.count&&a.capacity<=16&&((a.capacity==0)==(a.data==nullptr));}
 static Status Fail(){return{false};}static bool TypeReconstructionRejected(const char*){return false;}
#include "hud_empty_type_methods.inl"
};
int main(){
 Object player;Array active{},stopped{};Sc6ReplayHudState a,b;Sc6ReplayHudState::Prepared op;
 a.type_effects_[0].active=&active;a.type_effects_[0].stopped=&stopped;a.type_effects_[0].active_image.count=1;a.type_effects_[0].players[0]={&player};
 b.type_effects_[0].active=&active;b.type_effects_[0].stopped=&stopped;
 REQUIRE(a.PrepareTypePlayers(b,op).ok());REQUIRE(op.target==&a&&op.players[0].prepared);
 op={};Object* rows[]{&player};active={rows,1,1};
 Sc6ReplayHudState::At<int>(&player,0x6d8)=1;const auto before=player.bytes;
 REQUIRE(a.PrepareTypePlayers(b,op).ok());REQUIRE(op.target==&a&&op.type_players[0].lease);
 REQUIRE(player.bytes==before&&active.count==1&&active.data[0]==&player);
 op={};Sc6ReplayHudState::At<int>(&player,0x6d8)=0;
 active={};stopped={rows,1,1};REQUIRE(!a.PrepareTypePlayers(b,op).ok());
 stopped={};active={nullptr,0,1};REQUIRE(!a.PrepareTypePlayers(b,op).ok());
 active={};stopped={};op={};REQUIRE(a.PrepareTypePlayers(b,op).ok());op.private_hud=&b;
 REQUIRE(a.PublishTypeBacking(op,0));REQUIRE(active.data&&active.capacity==4&&!active.count&&allocations==1);
 REQUIRE(op.type_backings[0]==Sc6ReplayHudState::Prepared::TypeBacking::Owned);
 REQUIRE(!a.PublishTypeBacking(op,0));REQUIRE(allocations==1);
 // Actual C may finish and native-shrink the array before undo.
 Resize(&active,0);REQUIRE(b.RecoverTypeBackings(op));REQUIRE(!active.data&&!active.capacity&&!active.count);
 auto done=frees;REQUIRE(b.RecoverTypeBackings(op));REQUIRE(frees==done);REQUIRE(!a.CommitTypeBackings(op));
 op={};op.target=&a;op.private_hud=&b;fail_allocate=true;
 REQUIRE(!a.PublishTypeBacking(op,0));REQUIRE(op.type_backings[0]==Sc6ReplayHudState::Prepared::TypeBacking::Allocating);
 fail_allocate=false;REQUIRE(b.RecoverTypeBackings(op));
 op={};op.target=&a;op.private_hud=&b;REQUIRE(a.PublishTypeBacking(op,0));
 fail_free=true;REQUIRE(!b.RecoverTypeBackings(op));done=frees;REQUIRE(!b.RecoverTypeBackings(op));REQUIRE(frees==done);
 fail_free=false;Resize(&active,0); // fixture cleanup of deliberately poisoned ownership
 op={};op.target=&a;op.private_hud=&b;REQUIRE(a.PublishTypeBacking(op,0));
 auto* allocation=active.data;REQUIRE(a.CommitTypeBackings(op));REQUIRE(a.CommitTypeBackings(op));
 REQUIRE(active.data==allocation&&!b.RecoverTypeBackings(op));Resize(&active,0);
 op={};op.target=&a;op.private_hud=&b;b.type_effects_[0].active_image={rows,1,1};
 REQUIRE(!a.PublishTypeBacking(op,0));REQUIRE(!active.data);b.type_effects_[0].active_image={};
 op.private_hud=nullptr;REQUIRE(!a.PublishTypeBacking(op,0));
 std::puts("empty type backing admission, native allocation, complete empty B undo and adoption passed");
}
