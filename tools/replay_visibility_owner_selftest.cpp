#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <vector>
namespace RC {
enum class LogLevel {Warning};
namespace Output {template<LogLevel, class... T> void send(const char*,T...) {}}
}
#define STR(s) s
#define VISIBILITY_REJECT() return false
template<class T> T& Field(std::uintptr_t address,unsigned offset=0) {
    return *reinterpret_cast<T*>(address+offset);
}
struct Fixture {
    struct LightingPrimitive {
        enum class Binding {ScenePrimitive,DormantCreation,DormantStage,PendingStageTarget,PendingTraceTarget,DormantTrace} binding{};
        unsigned id{},slot_count{};std::uintptr_t component{},weak{},proxy_type{};
        std::array<std::uintptr_t,1> proxy_inputs{};
    };
    struct Image {std::vector<LightingPrimitive> primitives;} lighting_a_,lighting_b_;
    struct Owner {unsigned primitive_id{};std::uintptr_t component{},weak{},mesh{};};
    std::array<Owner,2> creation_render_owners_{};unsigned creation_render_owner_count_{};
    std::uintptr_t base_{};bool live_bindings{};unsigned binding_checks{};
    // Explicitly controlled dependency: presence in a captured row does not
    // grant live native ownership. The production call must ask for it.
    bool LightingBindings(const Image& image) {++binding_checks;return &image==&lighting_b_ && live_bindings;}
    bool DormantCreationBinding(const LightingPrimitive&,bool=true) {return false;}
    bool TraceRenderBinding(const LightingPrimitive&) {return false;}
    bool PendingTraceTargetBinding(const LightingPrimitive&) {return false;}
    bool PendingStageTargetBinding(const LightingPrimitive&) {return false;}
    bool Check(unsigned id,unsigned subquery=0) {
        std::array<unsigned,18> entry{};entry[0]=id;entry[16]=subquery;
        const auto ae=reinterpret_cast<std::uintptr_t>(entry.data());bool found=false;
#include "visibility_owner_method.inl"
        return found;
    }
};
#define REQUIRE(x) do {if(!(x)){std::fprintf(stderr,"visibility owner contract failed line=%d\n",__LINE__);return 1;}} while(false)
int main() {
    Fixture f;Fixture::LightingPrimitive row{};row.id=442;row.component=0x100;row.weak=0x200;
    f.lighting_b_.primitives.push_back(row);
    REQUIRE(!f.Check(442)); // A cached B row alone is insufficient.
    f.live_bindings=true;
    REQUIRE(f.Check(442) && f.binding_checks>=2);
    REQUIRE(!f.Check(443) && !f.Check(442,1));
    row.binding=Fixture::LightingPrimitive::Binding::DormantCreation;
    f.lighting_a_.primitives.push_back(row);
    REQUIRE(!f.Check(442)); // Do not override a rejected A owner with B.
    f.lighting_a_.primitives.clear();
    f.lighting_b_.primitives[0].binding=Fixture::LightingPrimitive::Binding::DormantCreation;
    REQUIRE(!f.Check(442)); // Current-B route cannot certify dormant bindings.
    std::puts("production visibility owner contract passed");
}
