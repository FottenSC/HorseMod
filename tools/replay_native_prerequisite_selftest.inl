// Included by the existing mapped component-task fixture. Only heap, TLS,
// object-array storage and graph enqueue are external services. Native code
// constructs weak references, removes edges, links dependencies and completes
// tasks. This is not a UObject lifetime or complete-B recovery proof.
namespace NativePrerequisiteTest {
struct QueueItem { void* task; unsigned desired, current; };
static std::array<QueueItem,4> queued;
static unsigned queue_count, frees;
static std::uintptr_t mapped_base;
static const char* phase;
static LONG CALLBACK Fault(EXCEPTION_POINTERS* info) {
    if(info->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION) {
        const auto* c=info->ContextRecord;
        std::fprintf(stderr,"native prerequisite fault phase=%s rva=%llx rcx=%llx rdx=%llx r8=%llx r9=%llx address=%llx\n",
            phase,static_cast<unsigned long long>(c->Rip-mapped_base),c->Rcx,c->Rdx,c->R8,c->R9,
            static_cast<unsigned long long>(info->ExceptionRecord->ExceptionInformation[1]));
        std::fflush(stderr);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
static void Enqueue(void*,void* task,unsigned desired,unsigned current) {
    expect(queue_count<queued.size(),"bounded native graph enqueue capture");
    if(queue_count<queued.size())queued[queue_count++]={task,desired,current};
}
static void* Realloc(void*,void* allocation,std::size_t bytes,unsigned alignment) {
    expect(alignment==0,"native prerequisite array uses default alignment");
    if(!bytes){std::free(allocation);++frees;return nullptr;}
    return std::realloc(allocation,bytes);
}
static std::size_t Quantize(void*,std::size_t bytes,unsigned alignment) {
    expect(alignment==0,"native prerequisite quantization alignment");return bytes;
}
static void Run(std::uintptr_t game,void* named,void* task_pool) {
    constexpr unsigned char remove_sig[]{0x48,0x85,0xd2,0x74,0x13,0x4c,0x8d,0x82,0x10,1,0,0,0x48,0x81,0xc1,0x10};
    constexpr unsigned char link_sig[]{0x41,0x54,0x41,0x56,0x48,0x83,0xec,0x38,0x48,0x89,0x6c,0x24,0x58,0x45,0x8b,0xe0};
    if(std::memcmp(reinterpret_cast<void*>(game+0x1d58ed0),remove_sig,sizeof remove_sig)
        || std::memcmp(reinterpret_cast<void*>(game+0x21679c0),link_sig,sizeof link_sig)) {
        expect(false,"shipped public prerequisite removal/link signatures");return;
    }
    const DWORD link_slot=TlsAlloc();
    expect(link_slot!=TLS_OUT_OF_INDEXES,"allocate dependency-node TLS service");
    if(link_slot==TLS_OUT_OF_INDEXES)return;
    const auto put=[](void* p,std::size_t offset,auto value){std::memcpy(static_cast<std::byte*>(p)+offset,&value,sizeof value);};
    const auto pointer=[](void* p,std::size_t offset){return *reinterpret_cast<void**>(static_cast<std::byte*>(p)+offset);};
    const auto integer=[](void* p,std::size_t offset){return *reinterpret_cast<int*>(static_cast<std::byte*>(p)+offset);};
    alignas(16) std::array<std::byte,0x180> trace{},mesh{};
    alignas(16) std::array<std::byte,0x58> trace_task{},mesh_task{},trace_event{},mesh_event{};
    alignas(16) std::array<std::byte,0x940> world{};
    alignas(16) std::array<std::byte,48> items{},nodes{};
    std::array<int,3> link_pool{0,1,1};
    std::array<std::uintptr_t,6> allocator_table{};
    allocator_table[3]=reinterpret_cast<std::uintptr_t>(&Realloc);
    allocator_table[5]=reinterpret_cast<std::uintptr_t>(&Quantize);
    auto* allocator=allocator_table.data();
    std::array<std::uintptr_t,1> graph_table{reinterpret_cast<std::uintptr_t>(&Enqueue)};
    auto* graph=graph_table.data();
    auto** item_global=reinterpret_cast<void**>(game+0x42a1150);
    auto* count_global=reinterpret_cast<int*>(game+0x42a115c);
    auto** allocator_global=reinterpret_cast<void**>(game+0x41971c8);
    auto** graph_global=reinterpret_cast<void**>(game+0x4166720);
    auto* slot_global=reinterpret_cast<DWORD*>(game+0x415d8d0);
    auto** node_global=reinterpret_cast<void**>(game+0x415dae8);
    // DONT_RESOLVE_DLL_REFERENCES leaves this real CRT import unresolved.
    // Swap-last uses it only for the separate two-edge case.
    auto* copy_import=reinterpret_cast<std::uintptr_t*>(game+0x322ce88);
    DWORD copy_protection{};
    if(!VirtualProtect(copy_import,sizeof(*copy_import),PAGE_READWRITE,&copy_protection)) {
        expect(false,"bind mapped prerequisite memcpy service");TlsFree(link_slot);return;
    }
    const auto old_copy=*copy_import;
    *copy_import=reinterpret_cast<std::uintptr_t>(&std::memcpy);
    const auto old_items=*item_global,old_allocator=*allocator_global,old_graph=*graph_global,old_nodes=*node_global;
    const auto old_count=*count_global;const auto old_slot=*slot_global;
    std::array<std::byte,16> old_scratch{};
    std::memcpy(old_scratch.data(),static_cast<std::byte*>(named)+0x20,old_scratch.size());
    std::array<std::byte,24> old_task_pool{};
    std::memcpy(old_task_pool.data(),task_pool,old_task_pool.size());
    void* scratch[1]{};
    put(named,0x20,scratch);put(named,0x28,0);put(named,0x2c,1);
    put(trace.data(),0xc,1);put(items.data(),24,trace.data());put(items.data(),24+16,11);
    *item_global=items.data();*count_global=2;*allocator_global=&allocator;*graph_global=&graph;
    *slot_global=link_slot;*node_global=nodes.data();
    const bool tls_bound=TlsSetValue(link_slot,link_pool.data())!=0;
    expect(tls_bound,"bind native dependency-node TLS storage");
    mapped_base=game;phase="construct";
    const auto fault_handler=AddVectoredExceptionHandler(1,&Fault);
    if(tls_bound) {
        auto* trace_tick=trace.data()+0x110;auto* mesh_tick=mesh.data()+0x110;
        std::array<std::uintptr_t,2> tick_table{0,reinterpret_cast<std::uintptr_t>(&observe_component_task_payload)};
        const auto construct=reinterpret_cast<void*(*)(void*,void**,int)>(game+0x2156650);
        const auto link=reinterpret_cast<void(*)(void*,void*,unsigned,bool)>(game+0x21679c0);
        const auto remove=reinterpret_cast<void(*)(void*,void*)>(game+0x1d58ed0);
        for(unsigned i=0;i<2;++i) {
            auto* task=i?mesh_task.data():trace_task.data();auto* tick=i?mesh_tick:trace_tick;
            auto* event=i?mesh_event.data():trace_event.data();
            put(event,0x48,7);void* reference=event;construct(task,&reference,i?1:0);
            expect(!reference && pointer(task,0x40)==event,"native dependent task takes completion reference");
            put(tick,0,tick_table.data());put(tick,0xd,std::uint8_t{1});put(tick,0x18,task);
            put(task,0x10,tick);put(task,0x18,0.25f);put(task,0x1c,2);put(task,0x24,2u);put(task,0x28,world.data());
        }
        struct Edge {int index,serial;void* tick;};static_assert(sizeof(Edge)==16);
        auto* edge=static_cast<Edge*>(std::malloc(sizeof(Edge)));expect(edge!=nullptr,"owned prerequisite array storage");
        if(edge) {
            *edge={1,11,trace_tick};put(mesh_tick,0x20,edge);put(mesh_tick,0x28,1);put(mesh_tick,0x2c,1);
            std::array<std::byte,0x30> dependencies{};
            queue_count=frees=0;
            phase="link_trace";
            link(trace_task.data(),dependencies.data(),2,true);
            expect(queue_count==1 && queued[0].task==trace_task.data() && queued[0].desired==2
                && queued[0].current==2 && integer(trace_task.data(),0xc)==0,"native trace construction gate enqueues once");
            put(dependencies.data(),0,trace_event.data());put(dependencies.data(),0x28,1);put(dependencies.data(),0x2c,1);
            phase="link_mesh";link(mesh_task.data(),dependencies.data(),2,true);
            const auto linked=*reinterpret_cast<std::uint64_t*>(trace_event.data()+8);
            expect(linked && !(linked&(1ull<<26)) && integer(mesh_task.data(),0xc)==1 && queue_count==1,
                "native dependency link holds mesh behind incomplete trace event");
            phase="remove_one";remove(mesh.data(),trace.data());
            expect(!pointer(mesh_tick,0x20) && !integer(mesh_tick,0x28) && !integer(mesh_tick,0x2c) && frees==1,
                "public native removal constructs weak key and frees sole persistent edge");
            expect(*reinterpret_cast<std::uint64_t*>(trace_event.data()+8)==linked && integer(mesh_task.data(),0xc)==1
                && queue_count==1,"persistent removal leaves frozen native graph dependency intact");
            const auto prior_pool_count=integer(task_pool,0x10);
            component_task_calls=0;component_task_expected_delta=0.25f;component_task_expected_previous=-1.0f;
            for(unsigned i=0;i<2;++i) {
                auto* task=i?mesh_task.data():trace_task.data();auto* event=i?mesh_event.data():trace_event.data();
                component_task_expected_task=task;component_task_expected_event=task+0x40;
                phase=i?"execute_mesh":"execute_trace";
                expect(queue_count==i+1 && queued[i].task==task,"execute only task actually enqueued by native graph");
                expect(ReplayTaskGroupTestAccess::Dispatch(game,task,named)==Sc6ReplayTaskGroup::DispatchOutcome::Completed,
                    "production dispatch invokes original dependent wrapper");
                expect(component_task_calls==i+1 && (*reinterpret_cast<std::uint64_t*>(event+8)&(1ull<<26))
                    && integer(event,0x48)==6,"native callback precedes one event completion and reference release");
                expect(integer(task_pool,0x10)==prior_pool_count+static_cast<int>(i)+1 && pointer(task_pool,8)==task,
                    "native dependent wrapper recycles exactly once");
                if(!i)expect(queue_count==2 && queued[1].task==mesh_task.data() && queued[1].desired==2
                    && queued[1].current==2 && integer(mesh_task.data(),0xc)==0,
                    "real trace completion releases mesh dependency and enqueues once");
            }
            expect(queue_count==2 && integer(named,0x28)==0,"native completion drains scratch with no duplicate enqueue");
            // Separate actual two-entry removal exercises swap-last without
            // pretending this second array represents the graph executed above.
            auto* pair=static_cast<Edge*>(std::malloc(2*sizeof(Edge)));expect(pair!=nullptr,"owned two-edge storage");
            if(pair) {
                pair[0]={1,11,trace_tick};pair[1]={1,11,mesh_tick};
                put(mesh_tick,0x20,pair);put(mesh_tick,0x28,2);put(mesh_tick,0x2c,2);
                phase="remove_two";remove(mesh.data(),trace.data());
                auto* remaining=static_cast<Edge*>(pointer(mesh_tick,0x20));
                expect(integer(mesh_tick,0x28)==1 && remaining && remaining[0].index==1
                    && remaining[0].serial==11 && remaining[0].tick==mesh_tick,"native swap-remove preserves distinct last edge");
                std::free(remaining);put(mesh_tick,0x20,static_cast<void*>(nullptr));put(mesh_tick,0x28,0);put(mesh_tick,0x2c,0);
            }
            std::cout<<"Shipped prerequisite removal/frozen dependency/callback/event/pool lifecycle PASS; no UObject lifetime or B recovery claim\n";
        }
    }
    TlsSetValue(link_slot,nullptr);*slot_global=old_slot;*node_global=old_nodes;
    if(fault_handler)RemoveVectoredExceptionHandler(fault_handler);
    *item_global=old_items;*count_global=old_count;*allocator_global=old_allocator;*graph_global=old_graph;
    std::memcpy(static_cast<std::byte*>(named)+0x20,old_scratch.data(),old_scratch.size());
    std::memcpy(task_pool,old_task_pool.data(),old_task_pool.size());
    *copy_import=old_copy;DWORD ignored{};
    expect(VirtualProtect(copy_import,sizeof(*copy_import),copy_protection,&ignored)!=0,"restore mapped memcpy import protection");
    expect(TlsFree(link_slot)!=0,"release dependency-node TLS service");
}
}
