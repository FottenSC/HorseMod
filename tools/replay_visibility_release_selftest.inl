namespace VisibilityReleaseTest {
struct Resource {int references=2;unsigned calls=0;bool fault=false;};
static void NativeRelease(void** slot) {
    auto& resource=*static_cast<Resource*>(*slot);++resource.calls;--resource.references;
    if(resource.fault)RaiseException(0xe0424242,0,0,nullptr);
}
struct Sc6ReplayParticleCopy {
    struct VisibilityImage {void* material_uniform{};void* material_scene_uniform{};
        std::array<void*,2> queries{};std::size_t leases{};};
    std::uintptr_t base_=reinterpret_cast<std::uintptr_t>(&NativeRelease)-0x11b9260;
    bool capture_owner_fault_=false;
    bool ReleaseVisibilityImage(VisibilityImage&)noexcept;
};
#include "../HorseMod/horselib/deterministic/Sc6ReplayParticleCopy.VisibilityRelease.inl"
static void Run() {
    for(unsigned position=0;position<3;++position) {
        Sc6ReplayParticleCopy owner;Resource first,second,last;
        Sc6ReplayParticleCopy::VisibilityImage image{&first,&second,{{&last,nullptr}},1};
        (position==0?first:position==1?second:last).fault=true;
        expect(!owner.ReleaseVisibilityImage(image) && owner.capture_owner_fault_,
            "post-decrement native fault records uncertain visibility ownership");
        const auto calls=first.calls+second.calls+last.calls;
        const auto references=first.references+second.references+last.references;
        expect(!owner.ReleaseVisibilityImage(image) && !owner.ReleaseVisibilityImage(image)
            && calls==first.calls+second.calls+last.calls
            && references==first.references+second.references+last.references,
            "uncertain release never decrements again on retries");
        Resource b;Sc6ReplayParticleCopy::VisibilityImage undo{&b};
        expect(!owner.ReleaseVisibilityImage(undo) && b.references==2 && b.calls==0,
            "uncertain C release preserves complete B leases");
    }
    Sc6ReplayParticleCopy owner;Resource first,second,last;
    Sc6ReplayParticleCopy::VisibilityImage image{&first,&second,{{&last,nullptr}},1};
    expect(owner.ReleaseVisibilityImage(image) && owner.ReleaseVisibilityImage(image)
        && !owner.capture_owner_fault_ && first.references==1 && second.references==1 && last.references==1
        && first.calls==1 && second.calls==1 && last.calls==1 && image.leases==0,
        "successful visibility release remains once-only and idempotent");
}
}
