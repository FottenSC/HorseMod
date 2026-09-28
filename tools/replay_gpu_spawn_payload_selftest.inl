// Exercise actual capture, preparation, publication and undo. Only native weak
// handles, allocation and authored offset lookup are controlled dependencies.
static void test_gpu_spawn_payload_ownership()
{
#ifdef _WIN32
    using Fixture=EmitterComponentReplacementFixture;
    auto* memory=static_cast<std::byte*>(VirtualAlloc(nullptr,0x3960000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    expect(memory!=nullptr,"GPU spawn dependency storage");if(!memory)return;
    const auto base=reinterpret_cast<std::uintptr_t>(memory);
    const auto put=[](void* p,std::size_t off,auto v){std::memcpy(static_cast<std::byte*>(p)+off,&v,sizeof(v));};
    const auto thunk=[&](std::size_t off,auto fn) {
        const unsigned char jump[]{0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0};
        std::memcpy(memory+off,jump,sizeof(jump));put(memory,off+2,reinterpret_cast<std::uintptr_t>(fn));
        DWORD old{};expect(VirtualProtect(memory+(off&~4095ull),4096,PAGE_EXECUTE_READ,&old)!=0,"GPU spawn dependency executable");
    };
    const auto bind=+[](void* weak,const void* object){std::memcpy(weak,&object,8);};
    const auto lookup=+[](const void* root,const void* module)->std::uintptr_t {
        std::uintptr_t data{};int off{};std::memcpy(&data,static_cast<const std::byte*>(root)+0x100,8);
        std::memcpy(&off,static_cast<const std::byte*>(module)+0x20,4);return data+off;
    };
    thunk(0xf7bad0,bind);thunk(0xf823f0,&Fixture::Resolve);thunk(0x1f9a380,lookup);
    thunk(0x4a61c0,&Fixture::Allocate);thunk(0xd46a00,&Fixture::Free);thunk(0xd50dc0,&Fixture::Charge);
    FlushInstructionCache(GetCurrentProcess(),memory,0x3960000);
    std::array<std::byte,0x170> asset{};std::array<std::byte,0xc0> lod{};
    std::array<std::byte,0x460> type{},module{};std::array<std::byte,0x2a0> root{};
    std::array<std::byte,0xad0> component{};std::array<std::byte,0x260> render{};
    std::uintptr_t system=base+0x3941830;
    void* lods[]{lod.data()};void* modules[]{module.data()};void* slots[]{root.data()};
    float a=1.25f,b=7.5f;
    put(asset.data(),0x38,lods);put(asset.data(),0x40,1);
    put(asset.data(),0x158,modules);put(asset.data(),0x160,1);put(asset.data(),0x164,1);
    put(lod.data(),0x48,type.data());put(type.data(),0,base+0x394b9e0);
    put(type.data(),0x40,module.data()); // GPU descriptor+10 native spawn module
    put(module.data(),0,base+0x395ed20);
    for(auto [slot,rva]:std::array<std::pair<int,int>,3>{{{0x268,0x301490},{0x270,0x1e030c0},{0x328,0x1fcee40}}})put(memory+0x395ed20,slot,base+rva);
    put(root.data(),0,base+0x394c100);put(root.data(),0x10,asset.data());put(root.data(),0x18,component.data());
    put(root.data(),0x28,lod.data());put(root.data(),0x100,&a);put(root.data(),0x108,4);put(root.data(),0x114,128);
    put(root.data(),0x1d0,&system);put(root.data(),0x1d8,type.data()+0x30);put(root.data(),0x1e0,render.data());
    put(render.data(),0,base+0x394bfc0);put(render.data(),0x1f0,base+0x394c000);
    put(component.data(),0,base+0x335db28);put(component.data(),0x808,asset.data());
    put(component.data(),0xa50,slots);put(component.data(),0xa58,1);put(component.data(),0xa5c,1);
    Fixture::retired=Fixture::retire_on_allocation=0;Fixture::allocated=Fixture::freed=0;
    Sc6ReplayCpuEmitterState captured;
    const auto capture=ReplayEmitterStorageTestAccess::CaptureGpu(captured,base,component.data(),root.data());
    expect(capture.ok(),"GPU capture admits verified four-byte SpawnPerUnit payload");
    if(capture.ok()) {
        a=99.f; // Preparation must use retained bytes, not changed native A.
        put(root.data(),0x100,&b);const auto before=root;
        Sc6ReplayCpuEmitterState original;
        expect(ReplayEmitterStorageTestAccess::CaptureGpu(original,base,component.data(),root.data()).ok(),"capture independent complete B payload");
        {
            Sc6ReplayCpuEmitterState::Prepared prepared;
            expect(captured.PrepareReplacement(1024*1024,prepared).ok(),"prepare independently owned GPU scalar payload");
            if(prepared.get()) {
                float* clone{};std::memcpy(&clone,static_cast<std::byte*>(prepared.get())+0x100,8);
                expect(clone && clone!=&a && clone!=&b && *clone==1.25f,"GPU clone retains historical scalar independently of A and B");
                put(root.data(),0x108,8);
                expect(!prepared.ValidateGpuDestination(0,root.data()).ok(),"unsupported B payload rejects before publication");
                root=before;
                expect(prepared.PublishGpuStorage(0,root.data()).ok(),"publish scalar graph while preserving complete B");
                expect(b==7.5f && original.ValidateDisplaced(prepared).ok(),"displaced B scalar remains independently owned and unchanged");
                expect(prepared.UndoPublication().ok() && root==before && b==7.5f,"GPU undo restores exact B header and payload");
            }
        }
        expect(Fixture::allocated==Fixture::freed && Fixture::allocated==2,"undo frees private header and scalar exactly once");
    }
    // The same production capture must continue to reject unsupported graphs.
    put(root.data(),0x100,&b);put(root.data(),0x108,8);
    Sc6ReplayCpuEmitterState bad_size;
    expect(!ReplayEmitterStorageTestAccess::CaptureGpu(bad_size,base,component.data(),root.data()).ok(),"GPU rejects wrong scalar extent");
    put(root.data(),0x108,4);put(module.data(),0x20,1);
    Sc6ReplayCpuEmitterState bad_range;
    expect(!ReplayEmitterStorageTestAccess::CaptureGpu(bad_range,base,component.data(),root.data()).ok(),"GPU rejects module lookup outside captured range");
    put(module.data(),0x20,0);put(root.data(),0x1c8,1);
    Sc6ReplayCpuEmitterState attached;
    expect(!ReplayEmitterStorageTestAccess::CaptureGpu(attached,base,component.data(),root.data()).ok(),"GPU attached callback exclusion remains enforced");
    VirtualFree(memory,0,MEM_RELEASE);
#endif
}
