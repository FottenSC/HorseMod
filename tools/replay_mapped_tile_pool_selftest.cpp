#include <array>
#include <vector>
#include <span>
#include <memory>
#include <cstdint>
#include <cstring>
#include <cassert>
#include <cstdio>
enum class FailureCode {IllegalTransition,GenerationMismatch,CapacityExceeded,ContextUnavailable,UndoFailed,RestoreVerificationFailed};
struct Status {bool value;bool ok()const{return value;}static Status success(){return {true};}static Status failure(FailureCode){return {false};}};
unsigned GetCurrentThreadId(){return 7;}
bool CopyBytes(void* a,const void*,std::size_t n){assert(n==8);std::uint64_t epoch=42;std::memcpy(a,&epoch,n);return true;}
struct Lease {bool live{true};Status Validate()const{return {live};}};
static std::vector<std::uint32_t> native_pool;
struct Sc6ReplayVfxState {
 struct ReconstructionBinding {std::uintptr_t source{},target{};};
 struct PreparedTilePools {
  struct Patch {std::uintptr_t system{},pool{};std::vector<std::uint32_t> target,previous;std::array<std::uint64_t,1024> target_owned{};bool dirty{};};
  std::vector<Patch> patches_;std::vector<ReconstructionBinding> reconstruction_bindings_;
  std::uintptr_t base_{},manager_{};std::uint64_t epoch_{};unsigned thread_{};bool ready_{},in_flight_{},executing_{},execution_settled_{};
  std::size_t owned_bytes()const noexcept;
  bool published()const{return in_flight_;}
  Status CheckPool(const Patch& p,bool target)const{return {native_pool==(target?p.target:p.previous)};}
  Status WritePool(Patch& p,bool undo){native_pool=undo?p.previous:p.target;p.dirty=!undo;if(!undo)in_flight_=true;return {true};}
 };
 struct TilePool {std::uintptr_t system{100},address{200};std::unique_ptr<std::uint32_t[]> free_indices;std::size_t count{};};
 struct Owner {std::uintptr_t pool{200};std::vector<std::uint32_t> tiles;};
 std::vector<TilePool> tile_pools_;std::vector<Owner> gpu_owners_;
 bool valid_{true},mapping_valid{true},dependencies{true},partition{true};Lease* object_lease_;std::uintptr_t base_{10},manager_{20};unsigned thread_{7};
 std::size_t owned_bytes()const{return 256;}
 Status ValidateReconstructionBindings(std::span<const ReconstructionBinding> b)const{return {mapping_valid&&dependencies&&b.size()==1};}
 Status ValidateReconstructionDependencies()const{return {dependencies};}
 bool retained_owners_live()const{return object_lease_->live;}
 bool SameTilePools()const {return tile_pools_.size()==1&&native_pool==std::vector<std::uint32_t>(tile_pools_[0].free_indices.get(),tile_pools_[0].free_indices.get()+tile_pools_[0].count);}
 Status ValidateOwnerPartition()const{return {partition};}
#include "pool_partition.inl"
 static Status ValidateNativeTilePartition(std::uintptr_t,const std::array<std::uint64_t,1024>& owned,std::size_t& count) {
  count=native_pool.size();return {IsCompleteTilePartition(owned,native_pool)};
 }
 Status ValidatePoolTransaction(const Sc6ReplayVfxState&,const PreparedTilePools&)const noexcept;
 Status PrepareTilePools(const Sc6ReplayVfxState&,std::size_t,PreparedTilePools&,std::span<const ReconstructionBinding>)const noexcept;
 Status PublishTilePools(const Sc6ReplayVfxState&,PreparedTilePools&)const noexcept;
 Status ValidatePublishedTilePools(const Sc6ReplayVfxState&,const PreparedTilePools&)const noexcept;
 Status UndoTilePools(const Sc6ReplayVfxState&,PreparedTilePools&)const noexcept;
 Status CommitTilePools(const Sc6ReplayVfxState&,PreparedTilePools&)const noexcept;
 Status BeginTilePoolExecution(const Sc6ReplayVfxState&,PreparedTilePools&,std::size_t)const noexcept;
 Status SettleTilePoolExecution(const Sc6ReplayVfxState&,const Sc6ReplayVfxState&,PreparedTilePools&)const noexcept;
 Status ReopenTilePoolsForUndo(const Sc6ReplayVfxState&,PreparedTilePools&)const noexcept;
};
#include "mapped_tile_pool.inl"
void pool(Sc6ReplayVfxState& v,int owned){
 auto& p=v.tile_pools_.emplace_back();p.count=65536-(owned>=0);p.free_indices=std::make_unique<std::uint32_t[]>(p.count);
 std::size_t j{};for(unsigned i=0;i<65536;++i)if(i!=owned)p.free_indices[j++]=i;
 if(owned>=0)v.gpu_owners_.push_back({200,{unsigned(owned)}});
}
int main(){
 using V=Sc6ReplayVfxState;Lease dead{false},live;V a,b,c;a.object_lease_=&dead;b.object_lease_=c.object_lease_=&live;
 pool(a,0);pool(b,1);pool(c,-1);native_pool.assign(b.tile_pools_[0].free_indices.get(),b.tile_pools_[0].free_indices.get()+b.tile_pools_[0].count);const auto original=native_pool;
 std::array<V::ReconstructionBinding,1> bindings{{{1,9}}};V::PreparedTilePools p;
 if(!a.PrepareTilePools(b,4*1024*1024,p,bindings).ok()){std::puts("mapped tile pool requires dead A component lease");return 1;}
 assert(p.patches_[0].previous==original&&p.reconstruction_bindings_.size()==1);
 a.mapping_valid=false;assert(!a.PublishTilePools(b,p).ok()&&native_pool==original);a.mapping_valid=true;
 assert(a.PublishTilePools(b,p).ok());assert(a.BeginTilePoolExecution(b,p,4*1024*1024).ok());
 native_pool.assign(c.tile_pools_[0].free_indices.get(),c.tile_pools_[0].free_indices.get()+c.tile_pools_[0].count);
 // C's replacement died normally. Its mapping is no longer a live lease;
 // independent dependencies, actual observed partition and complete B remain.
 a.mapping_valid=false;a.dependencies=false;assert(!a.SettleTilePoolExecution(b,c,p).ok());a.dependencies=true;
 c.partition=false;assert(!a.SettleTilePoolExecution(b,c,p).ok());c.partition=true;
 live.live=false;assert(!a.SettleTilePoolExecution(b,c,p).ok());live.live=true;
 assert(a.SettleTilePoolExecution(b,c,p).ok());assert(p.patches_[0].previous==original);
 assert(a.UndoTilePools(b,p).ok()&&native_pool==original&&b.SameTilePools());
 V::PreparedTilePools rejected;assert(!a.PrepareTilePools(b,4*1024*1024,rejected,{}).ok());
 a.mapping_valid=true;a.gpu_owners_[0].tiles.push_back(0);assert(!a.PrepareTilePools(b,4*1024*1024,rejected,bindings).ok());
 assert(rejected.patches_.empty()&&native_pool==original);
}
