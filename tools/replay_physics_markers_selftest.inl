// The shared predicate is the production body/shape/filter admission. Only
// engine weak-object lookup is supplied by the fixture; actors and scenes below
// are constructed by the shipped SDK.
struct GroundCollisionFixture {
    std::uintptr_t base{},world{};
    std::array<std::uintptr_t,2> scenes{};
    bool engine_physics_quiescent()const{return true;}
#include "deterministic/ReplayGroundSceneLock.inl"
    struct Mesh {std::uintptr_t component{};};std::vector<Mesh> meshes;
    const char* physics_check{"none"};
    struct BodyConsumerDiagnostic {
        std::uintptr_t actor{},component{},body{},shape{},query_handle{},type{};
        unsigned actor_index{},mesh_index{},shape_index{},body_flags{},actor_flags{},shape_flags{},core_flags{},constraints{};
    } body_diagnostic;
    ReplayPhysicsBodyInventory body_inventory{};
    static inline int component_serial=17;
    std::array<int,2> component_identity(std::uintptr_t component) const noexcept {
        return component?std::array<int,2>{268798,component_serial}:std::array<int,2>{-1,0};
    }
    bool unrelated_dynamic{};std::uintptr_t unrelated_dynamic_actor{};
    struct NotificationDiagnostic {
        std::uintptr_t scene{},core{},actor{},simulation{},callback{},callback_type{};
        unsigned count{},slot{},client{},actor_flags{},simulation_flags{};bool owned{};
    } notification_diagnostic;
    struct BodyProof {std::uintptr_t actor{},body{};};
    std::array<BodyProof,32> body_proofs{};unsigned body_proof_count{};
    template<class T> static bool read(std::uintptr_t p,T& value) noexcept {
        __try {if(!p)return false;std::memcpy(&value,reinterpret_cast<void*>(p),sizeof(T));return true;}
        __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
#include "deterministic/ReplayGroundCollisionDomain.inl"
#include "deterministic/ReplayGroundCollisionUpdate.inl"
};
// Exercise synchronous native island retirement with the real Foundation
// allocator contract. This is topology evidence, not a solver/restore proof.
void test_native_physics_island_retirement(const char* physics_path,const char* game_path,bool kinematic_obstacle=false)
{
    struct Allocator {
        std::unordered_map<void*,std::size_t> live;
        std::size_t bytes{},peak{};
        unsigned allocations{},deallocations{};
        bool invalid_free{};
        virtual ~Allocator()=default;
        virtual void* allocate(std::size_t n,const char*,const char*,int) {
            auto* p=_aligned_malloc(n,16);
            if(p){++allocations;live.emplace(p,n);bytes+=n;peak=(std::max)(peak,bytes);}
            return p;
        }
        virtual void deallocate(void* p) {
            if(!p)return;
            auto it=live.find(p);
            if(it==live.end()){invalid_free=true;return;}
            ++deallocations;bytes-=it->second;live.erase(it);_aligned_free(p);
        }
    } allocator;
    struct Errors {
        unsigned count{};
        virtual ~Errors()=default;
        virtual void report(int code,const char* message,const char*,int) {
            ++count;std::cerr<<"Native island error "<<code<<": "<<message<<'\n';
        }
    } errors;
    const auto physics=LoadLibraryExA(physics_path,nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
    expect(physics!=nullptr,"load shipped physics with resolved Foundation imports");
    if(!physics)return;
    const auto foundation_module=GetModuleHandleA("PxFoundation_x64.dll");
    const auto create=reinterpret_cast<void*(*)(unsigned,Allocator&,Errors&)>(
        GetProcAddress(foundation_module,"PxCreateFoundation"));
    const auto release=reinterpret_cast<void(*)(void*)>(GetProcAddress(foundation_module,
        "?release@Foundation@shdfnd@physx@@UEAAXXZ"));
    expect(create && release,"resolve shipped Foundation creation and release");
    if(!create || !release){FreeLibrary(physics);return;}
    auto* foundation=create(0x01000000,allocator,errors);
    expect(foundation!=nullptr,"create native Foundation with tracked allocation");
    if(!foundation){FreeLibrary(physics);return;}
    const auto base=reinterpret_cast<std::uintptr_t>(physics);
    alignas(16) std::array<std::byte,0x640> manager{};
    alignas(16) std::array<std::byte,0x80> kinematic{},debris{},contact{};
    auto read32=[](const void* p,std::size_t offset){unsigned v;std::memcpy(&v,
        static_cast<const std::byte*>(p)+offset,sizeof(v));return v;};
    auto readptr=[](const void* p,std::size_t offset){std::uintptr_t v;std::memcpy(&v,
        static_cast<const std::byte*>(p)+offset,sizeof(v));return v;};
    reinterpret_cast<void(*)(void*,bool,void*)>(base+0x137f10)(manager.data(),false,nullptr);
    const auto add=reinterpret_cast<unsigned*(*)(void*,unsigned*,void*,bool,bool)>(base+0x138360);
    unsigned k{},d{};
    add(manager.data(),&k,kinematic.data(),true,true);
    add(manager.data(),&d,debris.data(),false,true);
    expect(k==0 && d==64,"native island allocates distinct node handles");
    const auto edge=reinterpret_cast<unsigned(*)(void*,void*,unsigned,unsigned,void*)>(
        base+0x138510)(manager.data(),contact.data(),k,d,nullptr);
    reinterpret_cast<void(*)(void*,unsigned)>(base+0x138c60)(manager.data(),edge);
    auto settle=[&]{
        reinterpret_cast<void(*)(void*)>(base+0x1389d0)(manager.data());
        reinterpret_cast<void(*)(void*)>(base+0x138a70)(manager.data());
    };
    settle();
    for(auto offset:{0xb0u,0x310u}) {
        const auto nodes=readptr(manager.data(),offset+0x18);
        expect(readptr(reinterpret_cast<void*>(nodes),0x18)==reinterpret_cast<std::uintptr_t>(kinematic.data()) &&
            readptr(reinterpret_cast<void*>(nodes),0x38)==reinterpret_cast<std::uintptr_t>(debris.data()),
            "both native island passes own the original nodes");
    }
    reinterpret_cast<void(*)(void*,unsigned)>(base+0x1388f0)(manager.data(),edge);
    const auto remove=reinterpret_cast<void(*)(void*,unsigned)>(base+0x138410);
    remove(manager.data(),d);
    expect(read32(manager.data(),0x38)==1,"native remove queues retirement before completion");
    settle();
    expect(read32(manager.data(),0x38)==0,"synchronous passes complete pending node retirement");
    for(auto offset:{0xb0u,0x310u}) {
        const auto nodes=readptr(manager.data(),offset+0x18);
        expect(readptr(reinterpret_cast<void*>(nodes),0x18)==reinterpret_cast<std::uintptr_t>(kinematic.data()) &&
            readptr(reinterpret_cast<void*>(nodes),0x38)==0,
            "retirement preserves other owner and clears removed owner");
    }
    unsigned reused{};add(manager.data(),&reused,debris.data(),false,true);
    expect(reused==d,"completed native retirement permits node handle reuse");
    remove(manager.data(),reused);settle();
    // The earlier constrained node retains legitimate dirty work. Use a new
    // native edge-free sleeping kinematic to isolate activation semantics.
    alignas(16) std::array<std::byte,0x80> wake_body{};
    unsigned waking{};add(manager.data(),&waking,wake_body.data(),true,false);
    for(unsigned offset:{0xb0u,0x310u}) {
        const auto graph=reinterpret_cast<std::uintptr_t>(manager.data())+offset;
        const auto ids=readptr(reinterpret_cast<void*>(graph),0xf0);
        const auto before_nodes=readptr(reinterpret_cast<void*>(graph),0x18);
        std::printf("Native activation before slot=%x flags=%x active=%u queued=%u id=%x\n",offset,
            *reinterpret_cast<unsigned char*>(before_nodes+(waking>>6)*32+4),read32(reinterpret_cast<void*>(graph),0xa0),
            read32(reinterpret_cast<void*>(graph),0x198),read32(reinterpret_cast<void*>(ids),(waking>>6)*4));
        expect(ids && read32(reinterpret_cast<void*>(ids),(waking>>6)*4)==0xffffffffu,
            "native isolated kinematic activation has no dynamic island identity");
        reinterpret_cast<void(*)(std::uintptr_t,unsigned)>(base+0x147330)(graph,waking);
        const auto native_nodes=readptr(reinterpret_cast<void*>(graph),0x18);
        const auto inputs=readptr(reinterpret_cast<void*>(graph),0x190);
        std::printf("Native activation requested slot=%x flags=%x active=%u queued=%u input=%x\n",offset,
            *reinterpret_cast<unsigned char*>(native_nodes+(waking>>6)*32+4),read32(reinterpret_cast<void*>(graph),0xa0),
            read32(reinterpret_cast<void*>(graph),0x198),inputs?read32(reinterpret_cast<void*>(inputs),0):0xffffffffu);
        expect(*reinterpret_cast<unsigned char*>(native_nodes+(waking>>6)*32+4)==0x24
            && read32(reinterpret_cast<void*>(graph),0x198)==1
            && read32(reinterpret_cast<void*>(inputs),0)==waking,
            "native wake retains flag24 and exact pending activation input");
    }
    settle();
    for(unsigned offset:{0xb0u,0x310u}) {
        const auto graph=reinterpret_cast<std::uintptr_t>(manager.data())+offset;
        const auto native_nodes=readptr(reinterpret_cast<void*>(graph),0x18);
        expect(*reinterpret_cast<unsigned char*>(native_nodes+(waking>>6)*32+4)==6
            && read32(reinterpret_cast<void*>(graph),0x198)==0,
            "normal native island processing consumes activation once");
    }
    remove(manager.data(),waking);remove(manager.data(),k);settle();
    reinterpret_cast<void(*)(void*)>(base+0x138130)(manager.data());
    // Extend the topology proof across actual NpScene/NpRigidDynamic ownership.
    // Simulation establishes native broadphase history first; removal/re-add
    // itself may complete broadphase tasks but must not advance simulation.
    struct Dispatcher {
        unsigned submissions{};
        bool deferred{};
        std::vector<void*> queued;
        static void execute(void* task) {
            const auto* table=*reinterpret_cast<const std::uintptr_t* const*>(task);
            // Shipped PxBaseTask: destructor0, run8, release30. Release can
            // retire the task, so never inspect it afterward.
            reinterpret_cast<void(*)(void*)>(table[1])(task);
            reinterpret_cast<void(*)(void*)>(table[6])(task);
        }
        virtual void submit(void* task) {
            ++submissions;
            if(deferred)queued.push_back(task);else execute(task);
        }
        virtual unsigned workers() const {return 0;}
        // PxCpuDispatcher differs from PxBaseTask: submit0, workers8,
        // destructor10. Declaring the destructor first silently changes ABI.
        virtual ~Dispatcher()=default;
        void drain() {
            while(!queued.empty()) {auto* task=queued.front();queued.erase(queued.begin());execute(task);}
        }
    } dispatcher;
    using Completion=ReplayPhysicsTaskCompletion;
    {
        Completion receipt;
        expect(reinterpret_cast<const std::byte*>(&receipt.context)-reinterpret_cast<const std::byte*>(&receipt)==8
            && reinterpret_cast<const std::byte*>(&receipt.manager)-reinterpret_cast<const std::byte*>(&receipt)==0x10,
            "production completion retains the shipped native task prefix");
        receipt.add();receipt.remove();
        expect(!receipt.completed() && receipt.count()==1,"partial completion cannot grant retirement");
        receipt.remove();expect(receipt.completed_once(),"the last owned reference completes once");
        receipt.remove();expect(!receipt.valid() && receipt.count()==0,"duplicate removal poisons without reference underflow");
        receipt.add();expect(!receipt.valid() && receipt.count()==0,"poisoned completed work cannot reopen");
        Completion premature;
        premature.add();premature.release();
        expect(!premature.valid() && !premature.completed() && premature.count()==2,
            "premature native release cannot signal completion with references outstanding");
        premature.remove();premature.remove();
        expect(!premature.valid() && !premature.completed_once(),
            "draining a poisoned receipt does not turn it into a successful completion");
    }
    struct FilterData {unsigned words[4];};
    // Shipped E7670 dispatches wake/sleep at vtable+8/+10; E5AA0,
    // E6680, E6010 and E6860 supply the remaining native callback slots.
    // A registered observer creates notification bookkeeping even for bodies
    // whose actor flags do not request user-visible wake/sleep callbacks.
    struct SimulationEvents {
        unsigned calls{};
        unsigned wakes{},sleeps{};
        virtual void constraint(void*,unsigned){++calls;}
        virtual void wake(void*,unsigned){++calls;++wakes;}
        virtual void sleep(void*,unsigned){++calls;++sleeps;}
        virtual void contact(const void*,const void*,unsigned){++calls;}
        virtual void trigger(void*,unsigned){++calls;}
        virtual void advance(const void*,const void*,unsigned){++calls;}
    } events;
    // Shipped125790 passes a hidden PxFilterFlags return destination first.
    // An unsigned-return C callback shifts every argument and crashes on the
    // first interacting pair; an isolated-body scene never exercises this ABI.
    const auto filter=+[](unsigned short* result,unsigned,const FilterData*,unsigned,const FilterData*,
                         unsigned short* pairs,const void*,unsigned)->unsigned short* {
        *pairs=0x401;*result=0;return result;
    };
    const float scale[3]{1.0f,1.0f,10.0f};
    const auto create_physics=reinterpret_cast<void*(*)(unsigned,void*,const float*,bool,void*)>(
        GetProcAddress(physics,"PxCreateBasePhysics"));
    expect(create_physics!=nullptr,"resolve shipped base physics factory");
    auto* sdk=create_physics?create_physics(0x03040000,foundation,scale,false,nullptr):nullptr;
    expect(sdk!=nullptr,"create actual native physics for body retirement");
    if(sdk) {
        alignas(16) std::array<std::byte,0x120> desc{};
        const auto put=[&](std::size_t offset,const auto& value){std::memcpy(desc.data()+offset,&value,sizeof(value));};
        // Offsets come from shipped B280/F530 and scene creation consumers.
        // Do not use current upstream headers: their scene descriptor differs.
        put(0x10,static_cast<void*>(&events));
        put(0x38,filter);put(0x80,2.0f);put(0x84,0.04f);put(0x88,0.04f);
        put(0x90,static_cast<void*>(&dispatcher));put(0xa0,1u);put(0xa4,1u);put(0xa8,100u);
        put(0xb8,128u);put(0xc0,65536u);put(0xc4,8192u);put(0xc8,1u);put(0xcc,0.4f);
        for(auto offset:{0xd0u,0xd4u,0xd8u})put(offset,-1.0e20f);
        for(auto offset:{0xdcu,0xe0u,0xe4u})put(offset,1.0e20f);
        put(0x108,8u);std::memcpy(desc.data()+0x110,scale,sizeof(scale));
        const bool descriptor_valid=reinterpret_cast<bool(*)(const void*)>(base+0xf530)(desc.data());
        expect(descriptor_valid,"native descriptor validator accepts bounded CPU scene");
        std::cout<<"Native body retirement create_scene"<<std::endl;
        auto* scene=descriptor_valid?reinterpret_cast<void*(*)(void*,const void*)>(base+0x368f0)(sdk,desc.data()):nullptr;
        expect(scene!=nullptr,"construct actual NpScene");
        if(scene) {
            // Real native empty aggregates reproduce the replay's retained
            // aggregate-root handles; a registry with no aggregates missed
            // this prerequisite for integrating selective body retirement.
            const auto make_aggregates=[&](void* target_scene) {
                std::array<void*,2> result{};
                expect(readptr(reinterpret_cast<void*>(readptr(sdk,0)),0x18)==base+0x36dc0
                    && readptr(reinterpret_cast<void*>(readptr(target_scene,0)),0x78)==base+0x40bc0,
                    "verify native aggregate factory and scene admission signatures");
                for(unsigned i=0;i<result.size();++i) {
                    result[i]=reinterpret_cast<void*(*)(void*,unsigned,bool)>(base+0x36dc0)(sdk,16,i!=0);
                    expect(result[i] && readptr(result[i],0)==base+0x196be8,"native factory constructs aggregate owner");
                    if(!result[i])std::exit(2);
                    reinterpret_cast<void(*)(void*,void*)>(base+0x40bc0)(target_scene,result[i]);
                }
                return result;
            };
            const auto release_aggregates=[&](const std::array<void*,2>& owners) {
                for(auto* aggregate:owners) {
                    const auto release=readptr(reinterpret_cast<void*>(readptr(aggregate,0)),0);
                    expect(release==base+0x125c0,"verify native aggregate release after scene retirement");
                    if(release==base+0x125c0)reinterpret_cast<void(*)(void*)>(release)(aggregate);
                }
            };
            const auto aggregates=make_aggregates(scene);
            auto* material=reinterpret_cast<void*(*)(void*,float,float,float)>(base+0x36f80)(sdk,0.5f,0.5f,0.1f);
            expect(material!=nullptr,"create native material for retained body shape");
            const float pose[7]{0,0,0,1,0,0,0};
            auto* body=reinterpret_cast<void*(*)(void*,const float*)>(base+0x36cc0)(sdk,pose);
            expect(body && readptr(body,0)==base+0x19b7c0,"native factory returns expected dynamic owner");
            if(body) {
                struct Box {unsigned geometry;float x,y,z;} box{3,1,1,1};
                const unsigned char shape_flags=9;
                auto* shape=material?reinterpret_cast<void*(*)(void*,void*,const void*,void*,const unsigned char*)>(
                    base+0x10130)(nullptr,body,&box,material,&shape_flags):nullptr;
                expect(shape!=nullptr,"native helper attaches retained simulation-only box shape");
                const unsigned char disabled_flags=0;
                auto* disabled_shape=material?reinterpret_cast<void*(*)(void*,void*,const void*,void*,const unsigned char*)>(
                    base+0x10130)(nullptr,body,&box,material,&disabled_flags):nullptr;
                expect(disabled_shape!=nullptr,"selected native body also owns a disabled non-broadphase shape");
                // Exercise the production private-body guard against actual
                // SDK-created actors/shapes before adding them to any scene.
                Horse::Deterministic::ReplayGroundPrivateBody private_body;
                const auto private_read=[](std::uintptr_t p,auto& value) {
                    std::memcpy(&value,reinterpret_cast<void*>(p),sizeof(value));return true;
                };
                const auto private_scene=[&](std::uintptr_t p) {
                    return reinterpret_cast<std::uintptr_t(*)(void*)>(base+0x3c8e0)(reinterpret_cast<void*>(p));
                };
                const auto private_ok=private_body.Capture(private_read,private_scene,reinterpret_cast<std::uintptr_t>(body),base,0);
                expect(private_ok,
                    "production private-body admission accepts native scene-less actor and owned shapes");
                if(!private_ok)std::exit(2);
                auto private_foreign_shape=private_body;
                if(private_foreign_shape.shape_count)private_foreign_shape.shapes[0]=reinterpret_cast<std::uintptr_t>(body);
                expect(!private_foreign_shape.Validate(private_read,private_scene),"private-body guard rejects substituted shape owner");
                // Author the engine's typed user-data wrapper around this real
                // SDK owner. Only scene IDs/body slots below are controlled
                // engine dependencies; actor and shape ownership are native.
                std::array<std::byte,0x230> private_instance{};
                const auto instance=reinterpret_cast<std::uintptr_t>(private_instance.data());
                const auto actor_address=reinterpret_cast<std::uintptr_t>(body);
                const auto put_private=[](std::uintptr_t p,const auto& value){std::memcpy(reinterpret_cast<void*>(p),&value,sizeof(value));};
                put_private(instance+0x200,1u);put_private(instance+0x208,instance);
                put_private(instance+0xf0,actor_address);put_private(instance+0x10,short{17});
                put_private(actor_address+0x10,instance+0x200);
                Horse::Deterministic::ReplayGroundPrivateBody typed_private;
                expect(typed_private.Capture(private_read,private_scene,actor_address,base,instance),"capture native owner with typed engine body and scene lane");
                put_private(instance+0x10,short{18});
                const bool changed_scene_accepted=typed_private.Validate(private_read,private_scene);
                expect(!changed_scene_accepted,"private-body guard rejects changed nonzero engine scene ID");
                if(changed_scene_accepted)std::exit(2);
                put_private(instance+0x10,short{17});
                put_private(instance+0xf0,actor_address+8);
                expect(!typed_private.Validate(private_read,private_scene),"private-body guard rejects changed engine actor slot");
                put_private(instance+0xf0,actor_address);
                expect(typed_private.Validate(private_read,private_scene),"restored engine lane preserves private native ownership");
                // Real SDK scene/vtable and actor; only the engine registry,
                // helper/world and typed user-data wrapper are controlled.
                std::array<std::byte,0x1d0> engine_world{};
                std::array<std::byte,0x100> engine_owner{};
                std::array<std::byte,0x98> engine_helper{};
                std::array<std::uintptr_t,2> engine_user{3,reinterpret_cast<std::uintptr_t>(engine_owner.data())};
                const auto world_address=reinterpret_cast<std::uintptr_t>(engine_world.data());
                const auto owner_address=reinterpret_cast<std::uintptr_t>(engine_owner.data());
                const auto helper_address=reinterpret_cast<std::uintptr_t>(engine_helper.data());
                const auto scene_address=reinterpret_cast<std::uintptr_t>(scene);
                const auto old_user=readptr(scene,8);
                put_private(world_address+0x1c8,owner_address);put_private(helper_address+0x20,owner_address);
                put_private(owner_address+4,3u);put_private(owner_address+0xf8,short{17});
                put_private(helper_address+0x88,scene_address);
                put_private(scene_address+8,reinterpret_cast<std::uintptr_t>(engine_user.data()));
                std::uintptr_t resolved_scene=scene_address;
                const auto resolve_private=[&](std::int16_t id){return id==17?resolved_scene:std::uintptr_t{};};
                Horse::Deterministic::ReplayGroundPrivateScene scene_binding;
                expect(scene_binding.Capture(private_read,resolve_private,base,world_address,owner_address,helper_address)
                    && scene_binding.Matches(typed_private),"private scene guard accepts actual SDK lock targets and typed engine owner");
                put_private(owner_address+0xf8,short{18});
                expect(!scene_binding.Validate(private_read,resolve_private),"private scene rejects changed nonzero FPhysScene ID");
                put_private(owner_address+0xf8,short{17});
                resolved_scene=0;
                expect(!scene_binding.Validate(private_read,resolve_private),"private scene rejects missing registry target");
                resolved_scene=scene_address+8;
                expect(!scene_binding.Validate(private_read,resolve_private),"private scene rejects registry replacement under unchanged ID");
                resolved_scene=scene_address;
                for(const auto field:{world_address+0x1c8,helper_address+0x20,helper_address+0x88}) {
                    std::uintptr_t prior{};private_read(field,prior);put_private(field,prior+8);
                    expect(!scene_binding.Validate(private_read,resolve_private),"private scene rejects changed world/helper owner or scene");
                    put_private(field,prior);
                }
                engine_user[0]=1;
                expect(!scene_binding.Validate(private_read,resolve_private),"private scene rejects wrong teardown user-data tag");
                engine_user[0]=3;engine_user[1]+=8;
                expect(!scene_binding.Validate(private_read,resolve_private),"private scene rejects foreign teardown consumer owner");
                engine_user[1]-=8;
                const auto scene_table=readptr(scene,0);
                for(const auto slot:{0x330u,0x338u}) {
                    const auto changed_lock_read=[&](std::uintptr_t p,auto& value) {
                        private_read(p,value);
                        if(p==scene_table+slot)std::memset(&value,0,sizeof(value));
                        return true;
                    };
                    expect(!scene_binding.Validate(changed_lock_read,resolve_private),"private scene rejects changed native lock/unlock target");
                }
                put_private(owner_address,static_cast<unsigned char>(1));
                expect(!scene_binding.Validate(private_read,resolve_private),"private scene rejects changed async configuration");
                put_private(owner_address,static_cast<unsigned char>(0));
                put_private(instance+0x12,short{18});
                expect(!typed_private.Validate(private_read,private_scene),"private body rejects changed inactive lane used by engine teardown");
                put_private(instance+0x12,short{0});
                expect(scene_binding.Validate(private_read,resolve_private) && scene_binding.Matches(typed_private)
                    && typed_private.Validate(private_read,private_scene),"restored bindings preserve safe private native owner");
                put_private(scene_address+8,old_user);
                put_private(actor_address+0x10,std::uintptr_t{});
                const float obstacle_pose[7]{0,0,0,1,1.5f,0,0};
                auto* obstacle=reinterpret_cast<void*(*)(void*,const float*)>(base+(kinematic_obstacle?0x36cc0:0x36c00))(sdk,obstacle_pose);
                expect(obstacle && readptr(obstacle,0)==base+(kinematic_obstacle?0x19b7c0:0x19be60),"native collider has verified owner type");
                if(obstacle && kinematic_obstacle)reinterpret_cast<void(*)(void*,unsigned char,bool)>(base+0x3da30)(obstacle,1,true);
                auto* obstacle_shape=obstacle && material?reinterpret_cast<void*(*)(void*,void*,const void*,void*,const unsigned char*)>(
                    base+0x10130)(nullptr,obstacle,&box,material,&shape_flags):nullptr;
                expect(obstacle_shape!=nullptr,"attach overlapping unaffected native collider");
                const float peer_pose[7]{0,0,0,1,3.0f,0,0};
                auto* peer=reinterpret_cast<void*(*)(void*,const float*)>(base+0x36cc0)(sdk,peer_pose);
                auto* peer_shape=peer && material?reinterpret_cast<void*(*)(void*,void*,const void*,void*,const unsigned char*)>(
                    base+0x10130)(nullptr,peer,&box,material,&shape_flags):nullptr;
                expect(peer_shape!=nullptr,"attach independent contact partner on opposite side of static box");
                const auto add_actor=reinterpret_cast<void(*)(void*,void*)>(base+0x407b0);
                const auto remove_actor=reinterpret_cast<void(*)(void*,void*,bool)>(base+0x408c0);
                // Independent native scene receives only the same authored
                // initial conditions. Never install its observations in the
                // candidate; it witnesses feedback into the unaffected body.
                auto* control_scene=reinterpret_cast<void*(*)(void*,const void*)>(base+0x368f0)(sdk,desc.data());
                auto* control_static=reinterpret_cast<void*(*)(void*,const float*)>(base+(kinematic_obstacle?0x36cc0:0x36c00))(sdk,obstacle_pose);
                auto* control_peer=reinterpret_cast<void*(*)(void*,const float*)>(base+0x36cc0)(sdk,peer_pose);
                auto* control_body=reinterpret_cast<void*(*)(void*,const float*)>(base+0x36cc0)(sdk,pose);
                expect(control_scene && control_static && control_peer && control_body,"create independent native retirement control");
                if(!control_scene || !control_static || !control_peer || !control_body)std::exit(2);
                if(kinematic_obstacle)reinterpret_cast<void(*)(void*,unsigned char,bool)>(base+0x3da30)(control_static,1,true);
                const auto control_aggregates=make_aggregates(control_scene);
                for(auto* actor:{control_static,control_peer,control_body}) {
                    auto* control_shape=reinterpret_cast<void*(*)(void*,void*,const void*,void*,const unsigned char*)>(
                        base+0x10130)(nullptr,actor,&box,material,&shape_flags);
                    expect(control_shape!=nullptr,"independent control uses identical authored geometry");
                    if(!control_shape)std::exit(2);
                    if(actor==control_body) {
                        auto* disabled=reinterpret_cast<void*(*)(void*,void*,const void*,void*,const unsigned char*)>(
                            base+0x10130)(nullptr,actor,&box,material,&disabled_flags);
                        expect(disabled!=nullptr,"independent control has the same disabled authored shape");
                        if(!disabled)std::exit(2);
                    }
                    add_actor(control_scene,actor);
                }
                const auto islands=readptr(scene,0x758);
                const auto aabb=readptr(scene,0x738),broadphase=readptr(reinterpret_cast<void*>(aabb),0x100);
                auto complete=[&](unsigned remaining_boxes,const ReplayPhysicsRemovalGuard::Packet* removal_packet=nullptr){
                    reinterpret_cast<void(*)(void*)>(base+0x1389d0)(reinterpret_cast<void*>(islands));
                    reinterpret_cast<void(*)(void*)>(base+0x138a70)(reinterpret_cast<void*>(islands));
                    const auto removed=readptr(reinterpret_cast<void*>(aabb),0x58);
                    const auto words=read32(reinterpret_cast<void*>(aabb),0x60)&0x7fffffffu;
                    bool pending{};for(unsigned i=0;i<words;++i)pending|=read32(reinterpret_cast<void*>(removed),i*4)!=0;
                    if(pending) {
                        Completion done,unlocked;
                        const auto table=*reinterpret_cast<const std::uintptr_t* const*>(scene);
                        done.manager=unlocked.manager=reinterpret_cast<void*(*)(void*)>(table[0x398/8])(scene);
                        const auto cpu_context=readptr(scene,0x730);
                        dispatcher.deferred=true;
                        std::cout<<"Native body retirement complete_broadphase boxes="<<read32(reinterpret_cast<void*>(broadphase),0x140)<<std::endl;
                        if(removal_packet) {
                            // 150290's single-worker tail forwards its fourth
                            // argument to150880, then1569C0. Do not substitute
                            // the separate parallel bounds/task input at+7B0.
                            reinterpret_cast<void(*)(void*,unsigned,void*,const void*,void*,void*)>(base+0x1569c0)(
                                reinterpret_cast<void*>(broadphase),1,reinterpret_cast<void*>(cpu_context),
                                removal_packet,&done,&unlocked);
                        } else {
                            // Fixture cleanup only; the bounded undo experiment
                            // uses the production removal-only packet above.
                            reinterpret_cast<void(*)(void*,unsigned,void*,void*,bool,void*,void*)>(base+0x150290)(
                                reinterpret_cast<void*>(aabb),1,reinterpret_cast<void*>(readptr(reinterpret_cast<void*>(cpu_context),0x7b0)),
                                reinterpret_cast<void*>(cpu_context),false,&done,&unlocked);
                        }
                        done.remove();
                        expect(!done.completed() && !dispatcher.queued.empty(),
                            "native submission leaves the owned completion pending until tasks run");
                        for(unsigned poll=0;poll<3;++poll)
                            expect(!done.completed() && read32(scene,0x1f14)==0,
                                "pending retirement is neither cancellation nor a simulation step");
                        // Actual shipped SAP tasks complete on another thread.
                        // The operation/receipts stay alive through the join;
                        // no polling result is treated as cancellation.
                        std::thread native_worker([&]{dispatcher.drain();});
                        if(WaitForSingleObject(native_worker.native_handle(),5000)!=WAIT_OBJECT_0) {
                            // Test-process failure, never production recovery:
                            // do not destruct receipts underneath native work.
                            std::cerr<<"Native retirement worker did not complete within fixture timeout"<<std::endl;
                            std::exit(2);
                        }
                        native_worker.join();dispatcher.deferred=false;
                        expect(done.completed_once() && unlocked.completed_once(),
                            "native broadphase completion releases both owned continuations exactly once");
                        if(!done.completed() || !unlocked.completed())std::exit(2);
                        reinterpret_cast<void(*)(void*)>(base+0x150cf0)(reinterpret_cast<void*>(aabb));
                        reinterpret_cast<void(*)(void*)>(base+0x156bb0)(reinterpret_cast<void*>(broadphase));
                        expect(read32(reinterpret_cast<void*>(broadphase),0x140)==remaining_boxes,
                            "native completion retires removed shape endpoints before re-add");
                    }
                    reinterpret_cast<void(*)(void*)>(base+0xe75d0)(static_cast<std::byte*>(scene)+0x10);
                    // Element IDs are a separate release domain, absent from
                    // ScScene::clearReleasedBodyIDs (E75D0).
                    reinterpret_cast<void(*)(void*)>(base+0xfdb00)(reinterpret_cast<void*>(readptr(scene,0x1140)));
                };
                std::cout<<"Native body retirement add"<<std::endl;
                if(obstacle)add_actor(scene,obstacle);
                if(peer)add_actor(scene,peer);
                add_actor(scene,body);
                expect(!private_body.Validate(private_read,private_scene),"private-body admission rejects actual native scene publication");
                expect(readptr(body,0x80)!=0,"native add owns a BodySim");
                auto step_scene=[&](void* target_scene){
                    reinterpret_cast<void(*)(void*,float,void*,void*,unsigned,bool)>(base+0x41c80)(target_scene,1.0f/60.0f,nullptr,nullptr,0,true);
                    unsigned native_error{};
                    const bool completed=reinterpret_cast<bool(*)(void*,bool,unsigned*)>(base+0x41e90)(target_scene,false,&native_error);
                    expect(completed && !native_error,"complete native physics update used to establish history");
                    if(!completed) {
                        // Do not block forever or destroy pending native work.
                        // This isolated test process owns every allocation.
                        std::cerr<<"Native fixture has unfinished simulation tasks"<<std::endl;
                        std::exit(2);
                    }
                };
                const auto step=[&]{step_scene(scene);};
                std::cout<<"Native body retirement establish_broadphase"<<std::endl;
                step();step_scene(control_scene);
                expect(read32(reinterpret_cast<void*>(broadphase),0x190)==2,
                    "native update establishes an actual overlapping pair before retirement");
                alignas(16) std::array<std::byte,0xe8> original_values{},readded_values{};
                const auto values=reinterpret_cast<void*(*)(void*,const void*)>(base+0x7810);
                alignas(16) std::array<std::byte,0xe8> peer_before{},peer_after{};
                if(peer)values(peer_before.data(),peer);
                const auto peer_sim=peer?readptr(peer,0x80):0;
                const auto peer_element=readptr(reinterpret_cast<void*>(peer_sim),0x38);
                const auto peer_handle=read32(reinterpret_cast<void*>(peer_element),0x18)&0x1fffffffu;
                const auto peer_endpoints=[&]{
                    std::array<unsigned,6> result{};
                    for(unsigned axis=0;axis<3;++axis)for(unsigned side=0;side<2;++side) {
                        const auto boxes=readptr(reinterpret_cast<void*>(broadphase),0xd0+axis*8);
                        const auto values=readptr(reinterpret_cast<void*>(broadphase),0xe8+axis*8);
                        const auto index=read32(reinterpret_cast<void*>(boxes),peer_handle*8+side*4);
                        result[axis*2+side]=read32(reinterpret_cast<void*>(values),index*4);
                    }
                    return result;
                };
                const auto peer_endpoints_before=peer_endpoints();
                values(original_values.data(),body);
                const auto scene_phase_before=read32(scene,0x1f14);
                const auto collider_node_flags=[&](unsigned slot) {
                    const auto sim=readptr(obstacle,0x80);
                    const auto node=read32(reinterpret_cast<void*>(sim),0xb0)>>6;
                    const auto storage=readptr(reinterpret_cast<void*>(islands),(slot?0x310u:0xb0u)+0x18);
                    return read32(reinterpret_cast<void*>(storage),node*0x20+4)&0xffu;
                };
                std::array<unsigned,2> collider_flags{};
                if(kinematic_obstacle)for(unsigned slot=0;slot<2;++slot)collider_flags[slot]=collider_node_flags(slot);
                const auto observe=[](std::uintptr_t address,auto& value) noexcept {
                    if(!address)return false;std::memcpy(&value,reinterpret_cast<const void*>(address),sizeof(value));return true;
                };
                const auto game_image=LoadLibraryExA(game_path,nullptr,DONT_RESOLVE_DLL_REFERENCES);
                expect(game_image!=nullptr,"map shipped game filter for real native collision admission");
                if(!game_image)std::exit(2);
                const auto game_base=reinterpret_cast<std::uintptr_t>(game_image);
                const auto shader_slot=reinterpret_cast<std::uintptr_t>(scene)+0x10+0x1070;
                const auto saved_shader=readptr(reinterpret_cast<void*>(shader_slot),0);
                put_private(shader_slot,game_base+0x204cf60);
                // SDK defaults have owner ID0 on every shape and no engine
                // constant block. Author distinct IDs/mutual masks in the real
                // native filter records, then restore them before simulation.
                std::vector<std::pair<std::uintptr_t,std::array<unsigned,4>>> saved_filters;
                const auto filter_volumes=readptr(reinterpret_cast<void*>(aabb),0xa8);
                for(unsigned h=0;h<read32(reinterpret_cast<void*>(aabb),0x1c0);++h) {
                    const auto element=readptr(reinterpret_cast<void*>(filter_volumes),h*16);
                    if(!element || read32(reinterpret_cast<void*>(filter_volumes),h*16+8)!=0xffffffffu)continue;
                    const auto record=readptr(reinterpret_cast<void*>(element),0x40)+0x10;
                    std::array<unsigned,4> previous{};std::memcpy(previous.data(),reinterpret_cast<void*>(record),16);
                    saved_filters.emplace_back(record,previous);
                    put_private(record,std::array<unsigned,4>{0xffffu,0xffffffffu,h+1,0xa00000u});
                }
                std::array<std::array<std::byte,0x660>,2> collision_components{};
                const std::array collision_actors{reinterpret_cast<std::uintptr_t>(body),reinterpret_cast<std::uintptr_t>(peer)};
                std::array<std::uintptr_t,2> collision_children{},saved_users{};
                std::array<unsigned char,2> saved_body_flags{};
                for(unsigned i=0;i<2;++i) {
                    collision_children[i]=reinterpret_cast<std::uintptr_t>(collision_components[i].data());
                    const auto instance=collision_children[i]+0x430;
                    put_private(instance+0x200,1u);put_private(instance+0x208,instance);
                    saved_users[i]=readptr(reinterpret_cast<void*>(collision_actors[i]),0x10);
                    put_private(collision_actors[i]+0x10,instance+0x200);
                    alignas(16) std::array<std::byte,0xe8> values{};
                    reinterpret_cast<void*(*)(void*,const void*)>(base+0x7810)(values.data(),reinterpret_cast<void*>(collision_actors[i]));
                    saved_body_flags[i]=std::to_integer<unsigned char>(values[0x9c]);
                    reinterpret_cast<void(*)(void*,unsigned char,bool)>(base+0x3da30)(reinterpret_cast<void*>(collision_actors[i]),2,true);
                }
                const auto collision_check=[&](const char* expected=nullptr) {
                    GroundCollisionFixture domain;domain.base=game_base;
                    std::array<bool,64> found{};std::array<std::uintptr_t,16> selected{};unsigned count{};
                    const auto resolve=[&](std::uintptr_t weak) {
                        for(auto child:collision_children)if(weak==child+0x430+0x138)return child;
                        return std::uintptr_t{};
                    };
                    const bool ok=domain.physics_domain(base,reinterpret_cast<std::uintptr_t>(scene),found,selected,count,collision_children,resolve);
                    if(expected)expect(!ok && std::strcmp(domain.physics_check,expected)==0,expected);
                    else {if(!ok)std::cerr<<"Collision baseline check="<<domain.physics_check<<std::endl;
                        expect(ok && count==2 && found[0] && found[1],"production collision predicate admits real native scene and game filter");}
                    return ok;
                };
                if(!collision_check())std::exit(2);
                if(kinematic_obstacle) {
                    const auto sc=reinterpret_cast<std::uintptr_t>(scene)+0x10;
                    const auto core=reinterpret_cast<std::uintptr_t>(obstacle)+0x80;
                    const auto sim=readptr(reinterpret_cast<void*>(core),0);
                    auto& flags=*reinterpret_cast<unsigned char*>(core+0xc);
                    expect(!(flags&4),"native kinematic collider has no wake/sleep notification request");
                    const auto calls_before=events.calls;
                    reinterpret_cast<void(*)(std::uintptr_t,std::uintptr_t)>(base+0xec190)(sc,sim);
                    expect(read32(reinterpret_cast<void*>(sc),0x10ec)==1,
                        "native wake insertion queues internal work without notification request");
                    if(!collision_check()) {
                        std::cerr<<"collision admission rejected owned callback-free native wake bookkeeping"<<std::endl;std::exit(47);
                    }
                    const auto dense=readptr(reinterpret_cast<void*>(sc),0x10c0);
                    const auto saved_key=readptr(reinterpret_cast<void*>(dense),0);
                    put_private(dense,std::uintptr_t{0x12345678});collision_check("scene_notification_ownership");
                    put_private(dense,saved_key);
                    auto& sim_flags=*reinterpret_cast<unsigned short*>(sim+0xb4);
                    const auto saved_sim_flags=sim_flags;
                    sim_flags^=0x20;collision_check("scene_notification_flags");sim_flags=saved_sim_flags;
                    reinterpret_cast<void(*)(std::uintptr_t)>(base+0xe7670)(sc);
                    expect(events.calls==calls_before && !read32(reinterpret_cast<void*>(sc),0x10ec),
                        "native dispatch consumes bookkeeping without delivering an unrequested callback");
                    flags|=4;
                    reinterpret_cast<void(*)(std::uintptr_t,std::uintptr_t)>(base+0xec190)(sc,sim);
                    collision_check("scene_notification_callback");
                    expect(events.calls==calls_before && read32(reinterpret_cast<void*>(sc),0x10ec)==1,
                        "failed admission leaves required callback queued and undispatched");
                    reinterpret_cast<void(*)(std::uintptr_t)>(base+0xe7670)(sc);
                    expect(events.calls==calls_before+1,"native dispatch delivers rejected required callback exactly once");
                    flags&=static_cast<unsigned char>(~4u);
                    const auto selected_sim=readptr(body,0x80);
                    reinterpret_cast<void(*)(std::uintptr_t,std::uintptr_t)>(base+0xec190)(sc,selected_sim);
                    collision_check("scene_notification_debris");
                    reinterpret_cast<void(*)(std::uintptr_t)>(base+0xe7670)(sc);
                    expect(events.calls==calls_before+1,"selected-body bookkeeping is not a notification-delivery permission");
                    collision_check();
                }
                // Empty debris membership must still inspect the real scene:
                // these unrelated free bodies could receive the very first birth.
                std::array<std::byte,0x200> admission_world{},admission_owner{};
                const auto birth_world_address=reinterpret_cast<std::uintptr_t>(admission_world.data());
                const auto birth_owner_address=reinterpret_cast<std::uintptr_t>(admission_owner.data());
                std::array<std::uintptr_t,2> scene_owner{3,birth_owner_address};
                const auto saved_scene_user=readptr(scene,8);
                put_private(birth_world_address+0x1c8,birth_owner_address);
                put_private(birth_owner_address+4,1u);put_private(birth_owner_address+0xf8,short{7});
                put_private(reinterpret_cast<std::uintptr_t>(scene)+8,reinterpret_cast<std::uintptr_t>(scene_owner.data()));
                GroundCollisionFixture before_birth;before_birth.base=game_base;before_birth.world=birth_world_address;
                const char* birth_check="none";std::uintptr_t birth_owner{};unsigned scene_lookups{};
                const auto lookup_scene=[&](short id,std::uintptr_t& out){++scene_lookups;out=reinterpret_cast<std::uintptr_t>(scene);return id==7;};
                const auto resolve_birth=[&](std::uintptr_t weak){for(auto child:collision_children)if(weak==child+0x568)return child;return std::uintptr_t{};};
                const bool birth_admitted=before_birth.collision_update_domain(base,{},birth_check,birth_owner,lookup_scene,resolve_birth);
                if(birth_admitted || !scene_lookups || std::strcmp(birth_check,"body_unrelated_dynamic")) {
                    std::cerr<<"first debris birth bypassed the native collision domain check="<<birth_check<<std::endl;std::exit(46);
                }
                GroundCollisionFixture with_debris;with_debris.base=game_base;with_debris.world=birth_world_address;
                const bool current_admitted=with_debris.collision_update_domain(base,collision_children,birth_check,birth_owner,lookup_scene,resolve_birth);
                put_private(reinterpret_cast<std::uintptr_t>(scene)+8,saved_scene_user);
                expect(current_admitted && with_debris.body_proof_count==2,
                    "production application-entry path locks and validates the native scene with its actual debris members");


                put_private(collision_actors[1]+0x10,std::uintptr_t{});
                collision_check("body_unrelated_dynamic");
                put_private(collision_actors[1]+0x10,collision_children[1]+0x630);
                const auto reject_lane=[&](std::uintptr_t address,const auto& changed,const char* expected) {
                    using T=std::decay_t<decltype(changed)>;T saved{};std::memcpy(&saved,reinterpret_cast<void*>(address),sizeof(T));
                    put_private(address,changed);collision_check(expected);put_private(address,saved);
                };
                reject_lane(shader_slot,saved_shader,"scene_filter_shader");
                reject_lane(shader_slot+8,std::uintptr_t{1},"scene_filter_callback");
                reject_lane(reinterpret_cast<std::uintptr_t>(scene)+0x10+0x1080+0x34,1u,"scene_sleep_notifications");
                reject_lane(collision_children[0]+0x530,std::uintptr_t{1},"body_instance_consumers");
                reject_lane(collision_children[0]+0x640,std::uintptr_t{1},"body_instance_consumers");
                for(const auto& row:saved_filters)
                    reject_lane(row.first+12,0xa00008u,"contact_consumers");
                std::array<std::byte,0x58> constraint_owner{};
                std::array<std::byte,0x10> constraint_entry{};
                put_private(reinterpret_cast<std::uintptr_t>(constraint_owner.data())+0x48,reinterpret_cast<std::uintptr_t>(constraint_entry.data()));
                put_private(reinterpret_cast<std::uintptr_t>(constraint_owner.data())+0x50,1u);
                reject_lane(collision_actors[0]+0x20,reinterpret_cast<std::uintptr_t>(constraint_owner.data()),"body_constraints");
                reject_lane(reinterpret_cast<std::uintptr_t>(shape)+0x90,static_cast<unsigned char>(3),"body_shape_consumers");
                const auto query_handles=readptr(body,0x38);
                reject_lane(query_handles,std::uintptr_t{0},"body_shape_query_handle");
                collision_check();
                for(unsigned i=0;i<2;++i) {
                    put_private(collision_actors[i]+0x10,saved_users[i]);
                    if(!(saved_body_flags[i]&2))reinterpret_cast<void(*)(void*,unsigned char,bool)>(base+0x3da30)(reinterpret_cast<void*>(collision_actors[i]),2,false);
                }
                for(const auto& row:saved_filters)put_private(row.first,row.second);
                put_private(shader_slot,saved_shader);FreeLibrary(game_image);
                ReplayPhysicsRemovalGuard ownership;
                ReplayPhysicsRemovalGuard collision_aggregates;
                expect(collision_aggregates.ValidateCollisionAggregates(observe,base,reinterpret_cast<std::uintptr_t>(scene)),
                    "current collision admission validates real native empty aggregates without body removal");
                const auto aggregate_table=readptr(reinterpret_cast<void*>(aabb),0x1d0);
                const auto aggregate_owner=readptr(reinterpret_cast<void*>(aggregate_table),0);
                const auto aggregate_handle=read32(reinterpret_cast<void*>(aggregate_owner),0);
                const auto aggregate_volumes=readptr(reinterpret_cast<void*>(aabb),0xa8);
                const auto aggregate_sap=readptr(reinterpret_cast<void*>(aabb),0x100);
                const auto aggregate_endpoints=readptr(reinterpret_cast<void*>(aggregate_sap),0xd0);
                for(const auto address:std::array<std::uintptr_t,7>{aggregate_owner+0x10,aggregate_owner+0x20,
                    aabb+0x1e8,aabb+0x1cc,aggregate_volumes+aggregate_handle*16+8,
                    aggregate_endpoints+aggregate_handle*8,aggregate_owner+0x18}) {
                    ReplayPhysicsRemovalGuard changed_aggregate;
                    const auto changed_read=[&](std::uintptr_t p,auto& value) noexcept {
                        if(p<4096)return false;
                        if(!observe(p,value))return false;
                        if(p==address) {
                            const std::uintptr_t invalid=(address==aggregate_owner+0x10 || address==aabb+0x1e8
                                || address==aggregate_owner+0x18)?1u:0u;
                            std::memcpy(&value,&invalid,sizeof(value));
                        }
                        // An invalid optional self owner is rejected by the read
                        // boundary, without dereferencing an invented pointer.
                        return true;
                    };
                    expect(!changed_aggregate.ValidateCollisionAggregates(changed_read,base,reinterpret_cast<std::uintptr_t>(scene)),
                        "current collision admission rejects aggregate members, dirty work, free-list, tag, endpoint or self-owner changes");
                }
                const std::array selected{reinterpret_cast<std::uintptr_t>(body)};
                ReplayPhysicsRemovalGuard pending_node_input;
                const auto pending_node_read=[&](std::uintptr_t address,auto& value) noexcept {
                    if(!observe(address,value))return false;
                    if(sizeof(value)==4 && address==islands+0xb0+0x160) {const unsigned pending=1;std::memcpy(&value,&pending,4);}
                    return true;
                };
                expect(!pending_node_input.Capture(pending_node_read,base,reinterpret_cast<std::uintptr_t>(scene),selected)
                    && pending_node_input.failed_check()==ReplayPhysicsRemovalGuard::Check::Nodes,
                    "pending native island input rejects before any removal");
                expect(pending_node_input.Capture(observe,base,reinterpret_cast<std::uintptr_t>(scene),selected),
                    "read-only pending-input rejection permits a clean capture retry");
                ReplayPhysicsRemovalGuard duplicate_membership;
                const auto actor_table=readptr(scene,0x2590);
                const auto duplicate_read=[&](std::uintptr_t address,auto& value) noexcept {
                    if(address==actor_table+8)address=actor_table;
                    return observe(address,value);
                };
                expect(!duplicate_membership.Capture(duplicate_read,base,reinterpret_cast<std::uintptr_t>(scene),selected),
                    "production removal guard rejects duplicate unaffected actor membership");
                ReplayPhysicsRemovalGuard non_tail;
                const std::array peer_selected{reinterpret_cast<std::uintptr_t>(peer)};
                expect(!non_tail.Capture(observe,base,reinterpret_cast<std::uintptr_t>(scene),peer_selected),
                    "production removal guard rejects a body whose removal would reorder another owner");
                ReplayPhysicsRemovalGuard aggregate_domain;
                const auto first_element=readptr(reinterpret_cast<void*>(readptr(body,0x80)),0x38);
                ReplayPhysicsRemovalGuard cyclic_shapes,foreign_shape;
                const auto cyclic_read=[&](std::uintptr_t address,auto& value) noexcept {
                    if(!observe(address,value))return false;
                    if(sizeof(value)==8 && address==first_element+8)std::memcpy(&value,&first_element,8);
                    return true;
                };
                expect(!cyclic_shapes.Capture(cyclic_read,base,reinterpret_cast<std::uintptr_t>(scene),selected),
                    "complete native ID inventory rejects a cyclic shape list before removal");
                const auto foreign_shape_read=[&](std::uintptr_t address,auto& value) noexcept {
                    if(!observe(address,value))return false;
                    if(sizeof(value)==8 && address==first_element+0x10)std::memset(&value,0,sizeof(value));
                    return true;
                };
                expect(!foreign_shape.Capture(foreign_shape_read,base,reinterpret_cast<std::uintptr_t>(scene),selected),
                    "complete native ID inventory rejects a foreign shape owner before removal");
                auto selected_element=first_element;
                while(selected_element) {
                    const auto id=read32(reinterpret_cast<void*>(selected_element),0x18)&0x1fffffffu;
                    if(readptr(reinterpret_cast<void*>(readptr(reinterpret_cast<void*>(aabb),0xa8)),id*16)==selected_element)break;
                    selected_element=readptr(reinterpret_cast<void*>(selected_element),8);
                }
                expect(selected_element!=0,"fixture selects the actual broadphase shape for aggregate rejection");
                if(!selected_element)std::exit(2);
                auto disabled_element=first_element;
                while(disabled_element==selected_element)disabled_element=readptr(reinterpret_cast<void*>(disabled_element),8);
                expect(disabled_element!=0,"fixture includes a native shape outside broadphase");
                if(!disabled_element)std::exit(2);
                const auto disabled_core=readptr(reinterpret_cast<void*>(disabled_element),0x40);
                expect((read32(reinterpret_cast<void*>(disabled_core),0x40)&5)==0,
                    "disabled native shape has no simulation or trigger admission");
                ReplayPhysicsRemovalGuard missing_simulated_shape;
                const auto simulated_read=[&](std::uintptr_t address,auto& value) noexcept {
                    if(!observe(address,value))return false;
                    if(sizeof(value)==1 && address==disabled_core+0x40) {const unsigned char flags=1;std::memcpy(&value,&flags,1);}
                    return true;
                };
                expect(!missing_simulated_shape.Capture(simulated_read,base,reinterpret_cast<std::uintptr_t>(scene),selected),
                    "a simulation shape cannot be admitted without its broadphase ownership");
                const auto selected_handle=read32(reinterpret_cast<void*>(selected_element),0x18)&0x1fffffffu;
                const auto selected_aggregate=readptr(reinterpret_cast<void*>(aabb),0xa8)+selected_handle*16+8;
                const auto aggregate_read=[&](std::uintptr_t address,auto& value) noexcept {
                    if(!observe(address,value))return false;
                    if(address==selected_aggregate)std::memset(&value,0,sizeof(value));
                    return true;
                };
                expect(!aggregate_domain.Capture(aggregate_read,base,reinterpret_cast<std::uintptr_t>(scene),selected),
                    "production guard rejects aggregate-owned removal before native mutation");
                ReplayPhysicsRemovalGuard retried_ownership;
                const auto created_bits=readptr(reinterpret_cast<void*>(aabb),0x48);
                const auto selected_rigid_id=readptr(body,0x80)+0x50;
                bool read_partial_id{};
                const auto partial_acquisition=[&](std::uintptr_t address,auto& value) noexcept {
                    if(!observe(address,value))return false;
                    if(sizeof(value)==4 && address==selected_rigid_id) {const unsigned other=8191;std::memcpy(&value,&other,4);read_partial_id=true;}
                    if(sizeof(value)==4 && address==created_bits) {const unsigned pending=1;std::memcpy(&value,&pending,4);}
                    return true;
                };
                expect(!retried_ownership.Capture(partial_acquisition,base,reinterpret_cast<std::uintptr_t>(scene),selected),
                    "partial acquisition rejects pending creation after reading ID ownership");
                expect(read_partial_id,"fixture actually reads a provisional ID before rejection");
                expect(retried_ownership.Capture(observe,base,reinterpret_cast<std::uintptr_t>(scene),selected),
                    "rejected acquisition permits a fresh read-only retry");
                const auto admitted=ownership.Capture(observe,base,reinterpret_cast<std::uintptr_t>(scene),selected);
                if(!admitted)std::cerr<<"Native removal guard check="<<static_cast<unsigned>(ownership.failed_check())<<std::endl;
                expect(admitted,
                    "production guard captures the actual native tail-body removal domain");
                if(admitted) {
                ReplayPhysicsRemovalOperation operation;
                using RemovalResult=ReplayPhysicsRemovalOperation::Result;
                const bool prepared=operation.Prepare(base,reinterpret_cast<std::uintptr_t>(scene),selected);
                expect(prepared,"assembled native removal operation retains the actual scene/body domain");
                if(!prepared) {std::cerr<<"Removal preparation check="<<operation.failed_check()<<std::endl;std::exit(2);}
                expect(operation.Advance()==RemovalResult::Pending,"one native removal returns pending before retirement");
                expect(operation.Recover()==RemovalResult::Pending,"cancellation cannot re-add into unfinished native retirement");
                expect(ownership.Detached(observe),"production guard admits only selected native removal and unchanged peers");
                expect(readptr(body,0x80)==0,"native removal clears BodyCore simulation owner");
                expect(read32(reinterpret_cast<void*>(islands),0x38)==1,"body removal leaves pending island retirement");
                const auto* removal_packet=ownership.BuildRemovalPacket(observe);
                expect(removal_packet && removal_packet->removed_count==1 && !removal_packet->updated_count,
                    "production packet submits current selected removal without unrelated pending updates");
                expect(!ownership.BuildRemovalPacket(observe),"packet acquisition cannot overwrite retained native input storage");
                dispatcher.deferred=true;
                expect(operation.Advance()==RemovalResult::Pending,"assembled operation returns after native submission");
                if(kinematic_obstacle) {
                    const auto sim=readptr(obstacle,0x80);
                    const auto id=read32(reinterpret_cast<void*>(sim),0xb0)>>6;
                    const auto island=islands+0xb0;
                    const auto bits=readptr(reinterpret_cast<void*>(island),0x178);
                    const auto node=readptr(reinterpret_cast<void*>(island),0x18)+id*0x20;
                    const auto controlled_dirty=[&](std::uintptr_t address,auto& value) noexcept {
                        if(!observe(address,value))return false;
                        if(sizeof(value)==4 && address==bits+(id/32)*4) {const unsigned bit=1u<<(id%32);std::memcpy(&value,&bit,4);}
                        if(sizeof(value)==1 && address==node+4) {const unsigned char flags=0x15;std::memcpy(&value,&flags,1);}
                        return true;
                    };
                    unsigned mask{};
                    expect(ownership.PrepareNodeRetirement(controlled_dirty,mask) && (mask&1),
                        "controlled dependency reaches a retained dirty kinematic endpoint");
                    const auto foreign_dirty=[&](std::uintptr_t address,auto& value) noexcept {
                        if(!controlled_dirty(address,value))return false;
                        if(sizeof(value)==8 && address==node+0x18)std::memset(&value,0,sizeof(value));
                        return true;
                    };
                    expect(!ownership.PrepareNodeRetirement(foreign_dirty,mask),
                        "native dirty-node finalization rejects an unowned endpoint");
                    const auto dynamic_dirty=[&](std::uintptr_t address,auto& value) noexcept {
                        if(!controlled_dirty(address,value))return false;
                        if(sizeof(value)==1 && address==node+4) {const unsigned char flags=0x12;std::memcpy(&value,&flags,1);}
                        return true;
                    };
                    expect(!ownership.PrepareNodeRetirement(dynamic_dirty,mask),
                        "native dirty-node finalization rejects a dynamic dirty endpoint");
                }
                for(unsigned poll=0;poll<3;++poll) {
                    expect(operation.Advance()==RemovalResult::Pending,"assembled native completion remains pending across polls");
                    expect(operation.Recover()==RemovalResult::Pending,"pending cancellation retains the body and receipts");
                    expect(!operation.ReleaseRecoveredStorage(),"pending cancellation cannot release native packet storage");
                }
                std::thread operation_worker([&]{dispatcher.drain();});
                if(WaitForSingleObject(operation_worker.native_handle(),5000)!=WAIT_OBJECT_0)std::exit(2);
                operation_worker.join();dispatcher.deferred=false;
                std::uintptr_t admitted_bitmap{};std::size_t admitted_bitmap_bytes{};
                expect(ownership.PrepareIdRetirement(observe,admitted_bitmap,admitted_bitmap_bytes),
                    "native delayed IDs and deletion bitmap belong exactly to selected removal");
                std::uintptr_t retry_bitmap{};std::size_t retry_bitmap_bytes{};
                expect(retried_ownership.BuildRemovalPacket(observe)!=nullptr
                    && retried_ownership.PrepareIdRetirement(observe,retry_bitmap,retry_bitmap_bytes),
                    "fresh acquisition must discard all IDs read by a rejected prior attempt");
                const auto foreign_bitmap=[&](std::uintptr_t address,auto& value) noexcept {
                    if(!observe(address,value))return false;
                    if(address==admitted_bitmap && sizeof(value)==4) {
                        unsigned changed{};std::memcpy(&changed,&value,4);changed^=1u<<31;std::memcpy(&value,&changed,4);
                    }
                    return true;
                };
                std::uintptr_t rejected_bitmap{};std::size_t rejected_bytes{};
                expect(!ownership.PrepareIdRetirement(foreign_bitmap,rejected_bitmap,rejected_bytes),
                    "foreign element deletion bit rejects tail completion without writes");
                const auto constraint_ids=readptr(scene,0x1128);
                const auto foreign_pending=[&](std::uintptr_t address,auto& value) noexcept {
                    if(!observe(address,value))return false;
                    if(address==constraint_ids+0x30 && sizeof(value)==4) {const unsigned one=1;std::memcpy(&value,&one,4);}
                    return true;
                };
                expect(!ownership.PrepareIdRetirement(foreign_pending,rejected_bitmap,rejected_bytes),
                    "unowned constraint retirement rejects the shared native tail");
                expect(ownership.PrepareIdRetirement(observe,admitted_bitmap,admitted_bitmap_bytes),
                    "negative checks preserve the native removal domain");
                expect(operation.Advance()==RemovalResult::Complete,"assembled operation completes selective native retirement");
                expect(operation.Advance()==RemovalResult::Complete,"completed native retirement is not submitted twice");
                if(kinematic_obstacle)for(unsigned slot=0;slot<2;++slot) {
                    const auto after=collider_node_flags(slot);
                    std::printf("Native retained kinematic node slot=%u before=%x after=%x\n",slot,collider_flags[slot],after);
                    expect((after&0x10)==(collider_flags[slot]&0x10),
                        "selective retirement must not leave new dirty-node work on a retained kinematic owner");
                }
                const auto element_ids=readptr(scene,0x1140);
                const auto deleted_words=read32(reinterpret_cast<void*>(element_ids),0x20)&0x7fffffffu;
                const auto deleted_bits=readptr(reinterpret_cast<void*>(element_ids),0x18);
                bool element_ids_settled=read32(reinterpret_cast<void*>(element_ids),0x30)==0;
                for(unsigned i=0;i<deleted_words;++i)element_ids_settled&=read32(reinterpret_cast<void*>(deleted_bits),i*4)==0;
                expect(element_ids_settled,"completed selective retirement must finish native element-ID deletion bits before snapshot admission");
                const auto borrowed_removal=readptr(reinterpret_cast<void*>(broadphase),0x90);
                expect(borrowed_removal!=0,"native completion still borrows owned removal storage");
                expect(!operation.input_storage_detached(),"completed callbacks leave the native packet borrowed");
                expect(peer_endpoints_before==peer_endpoints(),
                    "retirement must not execute the other body's queued broadphase bounds update early");
                expect(ownership.Retired(observe),"production guard verifies selected handles retired and other native owners unchanged");
                expect(read32(reinterpret_cast<void*>(broadphase),0x190)==1,
                    "native retirement removes the old pair without a simulation step");
                if(peer)values(peer_after.data(),peer);
                expect(peer_before==peer_after && peer_sim && readptr(peer,0x80)==peer_sim,
                    "selective retirement preserves the other contacting body's owner and public state");
                expect(read32(reinterpret_cast<void*>(islands),0x38)==0,"body retirement completes without simulation");
                expect(operation.Recover()==RemovalResult::Pending,"B recovery accounts native re-add before verification completes");
                const auto wake_dense=readptr(scene,0x10d0);
                expect(read32(scene,0x10fc)==1 && wake_dense,"registered callback exposes native re-add notification bookkeeping");
                if(read32(scene,0x10fc)==1 && wake_dense) {
                    const auto original=*reinterpret_cast<std::uintptr_t*>(wake_dense);
                    *reinterpret_cast<std::uintptr_t*>(wake_dense)=reinterpret_cast<std::uintptr_t>(peer)+0x80;
                    expect(operation.Recover()==RemovalResult::Rejected && read32(scene,0x10fc)==1,
                        "recovery cannot clear another body's pending notification");
                    *reinterpret_cast<std::uintptr_t*>(wake_dense)=original;
                    auto& flags=*reinterpret_cast<unsigned char*>(original+0xc);const auto saved=flags;
                    flags|=4;
                    expect(operation.Recover()==RemovalResult::Rejected && read32(scene,0x10fc)==1,
                        "callback-eligible notification must reject without dispatch or clearing");
                    flags=saved;
                }
                const auto callbacks_before_recovery=events.calls;
                expect(operation.Recover()==RemovalResult::Complete,"assembled operation verifies recovered B body values");
                expect(events.calls==callbacks_before_recovery,"native bookkeeping retirement dispatches no simulation callbacks");
                expect(operation.Recover()==RemovalResult::Complete,"repeated B recovery does not duplicate scene registration");
                for(unsigned offset:{0x10c4u,0x10fcu}) {
                    const auto pending=read32(scene,offset);
                    std::fprintf(stderr,"Recovered scene notification count offset=%x count=%u\n",offset,pending);
                    expect(pending==0,"recovered B must pass the same empty notification admission before an immediate fallback");
                }
                std::array<std::byte,0x2a0> expected_sap{},observed_sap{};
                {
                    using Notifications=ReplayPhysicsNotifications;
                    const auto sc=reinterpret_cast<std::uintptr_t>(scene)+0x10;
                    const auto core=reinterpret_cast<std::uintptr_t>(body)+0x80,sim=readptr(reinterpret_cast<void*>(core),0);
                    auto& actor_flags=*reinterpret_cast<unsigned char*>(core+0xc);
                    auto& sim_flags=*reinterpret_cast<unsigned short*>(sim+0xb4);
                    const auto saved_actor_flags=actor_flags;
                    const auto saved_sim_flags=sim_flags;
                    const auto read=[](std::uintptr_t p,auto& value) noexcept {
                        std::memcpy(&value,reinterpret_cast<void*>(p),sizeof(value));return true;
                    };
                    const auto write=[](std::uintptr_t p,const auto& value) noexcept {
                        std::memcpy(reinterpret_cast<void*>(p),&value,sizeof(value));return true;
                    };
                    const auto owns=[&](std::uintptr_t p) noexcept {
                        return p==core && readptr(reinterpret_cast<void*>(core),0)==sim
                            && readptr(reinterpret_cast<void*>(sim),0x48)==core
                            && readptr(reinterpret_cast<void*>(sim),0x40)==sc;
                    };
                    Notifications empty,queued,actual;
                    expect(empty.Capture(sc,read,owns) && empty.Empty(),"capture actual native empty notification sets");
                    actor_flags|=4;
                    reinterpret_cast<void(*)(std::uintptr_t,std::uintptr_t)>(base+0xec190)(sc,sim);
                    expect(queued.Capture(sc,read,owns) && queued.sets[1].count==1
                        && queued.Membership(core)==0x20 && (sim_flags&0xf0)==0xa0,
                        "capture native wake insertion with exact retained body ownership");
                    const auto queued_flags=sim_flags;
                    const auto dense=readptr(reinterpret_cast<void*>(sc),0x10c0);
                    auto& key=*reinterpret_cast<std::uintptr_t*>(dense);const auto saved_key=key;
                    key=0x12345678;
                    expect(!actual.Capture(sc,read,owns),"foreign notification core rejects before dereferencing it");
                    key=saved_key;
                    const auto callbacks_before=events.calls;
                    expect(empty.CanWrite(read,[](std::uintptr_t,std::size_t){return true;})
                        && empty.Write(read,write),"publish captured empty A into current native queue allocation");
                    sim_flags=saved_sim_flags;
                    expect(actual.Capture(sc,read,owns) && actual==empty && events.calls==callbacks_before,
                        "queue restoration retains pending callbacks without dispatching them");
                    expect(queued.CanWrite(read,[](std::uintptr_t,std::size_t){return true;})
                        && queued.Write(read,write),"recover ordered B queue into its still-owned native allocation");
                    sim_flags=queued_flags;
                    expect(actual.Capture(sc,read,owns) && actual==queued && events.calls==callbacks_before,
                        "independent native queue reads establish B recovery without callback suppression");
                    reinterpret_cast<void(*)(std::uintptr_t)>(base+0xe7670)(sc);
                    expect(events.calls==callbacks_before+1 && !(sim_flags&0xf0)
                        && actual.Capture(sc,read,owns) && actual.Empty(),
                        "normal native dispatch delivers the recovered wake callback exactly once");
                    reinterpret_cast<void(*)(std::uintptr_t)>(base+0xe7670)(sc);
                    expect(events.calls==callbacks_before+1,"repeated native cleanup cannot redispatch recovered wake");
                    // Opposite events remain in both dense sets until native
                    // filtering. Restoring counts alone would dispatch a stale
                    // sleep, or lose the surviving wake if flags were cleared.
                    reinterpret_cast<void(*)(std::uintptr_t,std::uintptr_t)>(base+0xec240)(sc,sim);
                    reinterpret_cast<void(*)(std::uintptr_t,std::uintptr_t)>(base+0xec190)(sc,sim);
                    Notifications opposite;
                    expect(opposite.Capture(sc,read,owns) && opposite.sets[0].count==1
                        && opposite.sets[1].count==1 && opposite.filtered[1]==0
                        && (sim_flags&0xf0)==0xb0,"native opposite events retain two sets and pending filter work");
                    const auto opposite_flags=sim_flags;
                    expect(empty.Write(read,write),"publish A before testing cancelled-event B recovery");
                    sim_flags=saved_sim_flags;
                    expect(opposite.CanWrite(read,[](std::uintptr_t,std::size_t){return true;})
                        && opposite.Write(read,write),"recover both native pending sets without filtering early");
                    sim_flags=opposite_flags;
                    const auto wakes_before=events.wakes,sleeps_before=events.sleeps;
                    reinterpret_cast<void(*)(std::uintptr_t)>(base+0xe7670)(sc);
                    expect(events.wakes==wakes_before+1 && events.sleeps==sleeps_before
                        && !(sim_flags&0xf0) && actual.Capture(sc,read,owns) && actual.Empty(),
                        "native filtering cancels stale sleep and dispatches only recovered wake");
                    actor_flags=saved_actor_flags;
                    std::cout<<"Native notification queue recovery delivered one wake callback after restoration\n";
                }
                std::memcpy(expected_sap.data(),reinterpret_cast<void*>(broadphase),expected_sap.size());
                for(unsigned offset:{0x80u,0x90u,0xa0u}) {
                    std::memset(expected_sap.data()+offset,0,8);
                    std::memset(expected_sap.data()+offset+8,0,4);
                }
                const auto pending_maps=[&] {
                    std::vector<unsigned> result;
                    for(unsigned offset:{0x48u,0x58u,0x68u}) {
                        const auto pointer=readptr(reinterpret_cast<void*>(aabb),offset);
                        const auto count=read32(reinterpret_cast<void*>(aabb),offset+8)&0x7fffffffu;
                        if(count>256)std::exit(2);
                        result.push_back(count);
                        for(unsigned i=0;i<count;++i)result.push_back(read32(reinterpret_cast<void*>(pointer),i*4));
                    }
                    return result;
                };
                const auto pending_before=pending_maps();
                const auto allocated_before=allocator.allocations,freed_before=allocator.deallocations;
                expect(operation.ReleaseRecoveredStorage(),"B cancellation must retire its packet at the held boundary without advancing gameplay");
                std::memcpy(observed_sap.data(),reinterpret_cast<void*>(broadphase),observed_sap.size());
                expect(expected_sap==observed_sap,"empty native input publication changes only the spent input bindings/counts");
                expect(pending_before==pending_maps() && peer_endpoints_before==peer_endpoints(),
                    "input retirement preserves pending native creation/update maps and unrelated endpoints");
                expect(allocator.allocations==allocated_before && allocator.deallocations==freed_before,
                    "empty input retirement does not allocate or free native storage");
                {
                    // Controlled corruption must not turn another native
                    // owner's pending work into this operation's permission.
                    const auto callbacks_before_fallback=events.calls;
                    const auto graph=islands+0xb0;
                    const auto activation=readptr(reinterpret_cast<void*>(graph),0x190);
                    expect(read32(reinterpret_cast<void*>(graph),0x198)==1 && activation,
                        "native re-add creates one isolated activation receipt");
                    if(activation && read32(reinterpret_cast<void*>(graph),0x198)==1) {
                        auto& handle=*reinterpret_cast<unsigned*>(activation);const auto saved=handle;
                        handle=read32(reinterpret_cast<void*>(peer_sim),0xb0);
                        ReplayPhysicsRemovalGuard foreign;
                        expect(!foreign.Capture(observe,base,reinterpret_cast<std::uintptr_t>(scene),selected),
                            "another body's activation receipt cannot authorize re-removal");
                        handle=saved;
                    }
                    const auto created_bits=readptr(reinterpret_cast<void*>(aabb),0x48);
                    const auto created_words=read32(reinterpret_cast<void*>(aabb),0x50)&0x7fffffffu;
                    expect(created_bits && peer_handle/32<created_words,"native created bitmap covers controlled foreign handle");
                    if(created_bits && peer_handle/32<created_words) {
                        auto& bits=*reinterpret_cast<unsigned*>(created_bits+(peer_handle/32)*4);const auto saved=bits;
                        bits|=1u<<(peer_handle%32);
                        ReplayPhysicsRemovalGuard foreign;
                        expect(!foreign.Capture(observe,base,reinterpret_cast<std::uintptr_t>(scene),selected),
                            "unrelated pending broadphase creation cannot be consumed by re-removal");
                        bits=saved;
                    }
                    ReplayPhysicsRemovalOperation retry;
                    const bool prepared=retry.Prepare(base,reinterpret_cast<std::uintptr_t>(scene),selected);
                    if(!prepared)std::fprintf(stderr,"Immediate B removal retry rejected check=%s\n",retry.failed_check());
                    expect(prepared,"recovered B must admit the next fallback removal without a simulation step");
                    if(prepared) {
                        auto result=RemovalResult::Pending;
                        for(unsigned poll=0;poll<128 && result==RemovalResult::Pending;++poll)result=retry.Advance();
                        if(result!=RemovalResult::Complete) {
                            std::fprintf(stderr,"Immediate removal retry failed result=%u check=%s\n",unsigned(result),retry.failed_check());std::exit(2);
                        }
                        result=RemovalResult::Pending;
                        for(unsigned poll=0;poll<128 && result==RemovalResult::Pending;++poll)result=retry.Recover();
                        if(result!=RemovalResult::Complete || !retry.ReleaseRecoveredStorage()) {
                            std::fprintf(stderr,"Immediate removal retry recovery failed result=%u check=%s\n",unsigned(result),retry.failed_check());std::exit(2);
                        }
                        expect(retry.ReleaseRecoveredStorage(),"repeated retry retirement is idempotent");
                        expect(events.calls==callbacks_before_fallback,"fallback re-removal and recovery dispatch no callbacks");
                    }
                }
                expect(readptr(body,0x80)!=0,"native re-add reconstructs BodySim under retained Np owner");
                values(readded_values.data(),body);
                expect(original_values==readded_values,"native public body state survives removal and re-add");
                expect(!scene_phase_before && read32(scene,0x1f14)==scene_phase_before,
                    "retirement completes broadphase work without entering native simulation");
                step();
                expect(readptr(reinterpret_cast<void*>(broadphase),0x90)!=borrowed_removal,
                    "subsequent native update replaces the borrowed removal storage before owner retirement");
                expect(operation.ReleaseRecoveredStorage(),"assembled operation releases only after actual native input replacement");
                expect(operation.ReleaseRecoveredStorage(),"repeated storage retirement is idempotent");
                expect(read32(reinterpret_cast<void*>(broadphase),0x190)==2,
                    "native continuation recreates the interacting pair after re-add");
                bool continuation_matches=true,selected_continuation_matches=true;
                for(unsigned tick=0;tick<120;++tick) {
                    if(tick)step(); // The first candidate update was above.
                    step_scene(control_scene);
                    alignas(16) std::array<std::byte,0xe8> candidate{},control{};
                    values(candidate.data(),peer);values(control.data(),control_peer);
                    // Native metadata constructors73D0/74D0/7810:28..DF
                    // contain pose/body/dynamic values. Exclude process-local
                    // actor bindings before28 and the concrete type pointerE0.
                    if(std::memcmp(candidate.data()+0x28,control.data()+0x28,0xe0-0x28)) {
                        std::cerr<<"Native unaffected-body continuation first_mismatch="<<tick+1<<std::endl;
                        continuation_matches=false;break;
                    }
                    values(candidate.data(),body);values(control.data(),control_body);
                    if(selected_continuation_matches && std::memcmp(candidate.data()+0x28,control.data()+0x28,0xe0-0x28)) {
                        for(unsigned offset=0x28;offset<0xe0;offset+=4)if(std::memcmp(candidate.data()+offset,control.data()+offset,4)) {
                            unsigned actual{},expected{};std::memcpy(&actual,candidate.data()+offset,4);std::memcpy(&expected,control.data()+offset,4);
                            std::cerr<<"Native re-added-body continuation first_mismatch="<<tick+1<<" offset="<<std::hex<<offset
                                <<" expected="<<expected<<" actual="<<actual<<std::dec<<std::endl;break;
                        }
                        selected_continuation_matches=false;
                    }
                }
                expect(continuation_matches,"120 independent native ticks preserve unaffected-body simulation after retirement/re-add");
                if(continuation_matches)std::cout<<"Native unaffected-body continuation matched_ticks=120 independent_scene=true expected_state_installed=false"<<std::endl;
                expect(selected_continuation_matches,"120 independent native ticks preserve the re-added body's own simulation");
                if(selected_continuation_matches)std::cout<<"Native re-added-body continuation matched_ticks=120 independent_scene=true expected_state_installed=false"<<std::endl;
                }
                reinterpret_cast<void(*)(void*)>(base+0x40440)(control_scene);
                release_aggregates(control_aggregates);
                reinterpret_cast<void(*)(void*)>(base+0x396a0)(control_body);
                reinterpret_cast<void(*)(void*)>(base+0x396a0)(control_peer);
                reinterpret_cast<void(*)(void*)>(base+(kinematic_obstacle?0x396a0:0x3eb90))(control_static);
                remove_actor(scene,body,false);complete(2);
                // NpRigidDynamic slot0 is release (396A0); slot20 is isKindOf.
                // Resolve/check the native release route before SDK teardown,
                // which would otherwise hide a fixture that never released it.
                const auto release_body=readptr(reinterpret_cast<void*>(readptr(body,0)),0);
                expect(release_body==base+0x396a0,"verify native dynamic release route");
                if(release_body==base+0x396a0)reinterpret_cast<void(*)(void*)>(release_body)(body);
                if(peer) {
                    remove_actor(scene,peer,false);complete(1);
                    reinterpret_cast<void(*)(void*)>(base+0x396a0)(peer);
                }
                if(obstacle) {
                    remove_actor(scene,obstacle,false);complete(0);
                    const auto release_static=readptr(reinterpret_cast<void*>(readptr(obstacle,0)),0);
                    expect(release_static==base+(kinematic_obstacle?0x396a0:0x3eb90),"verify native collider release route before SDK cleanup");
                    if(release_static==base+(kinematic_obstacle?0x396a0:0x3eb90))reinterpret_cast<void(*)(void*)>(release_static)(obstacle);
                }
                if(!kinematic_obstacle) {
#include "replay_ground_initial_kinematic_selftest.inl"
                }
            }
            if(material)reinterpret_cast<void(*)(void*)>(*reinterpret_cast<const std::uintptr_t* const*>(material)[0])(material);
            reinterpret_cast<void(*)(void*)>(base+0x40440)(scene);
            release_aggregates(aggregates);
        }
        // Keep writer side effects out of the retirement-control scene.
        auto* writer_scene=descriptor_valid?reinterpret_cast<void*(*)(void*,const void*)>(base+0x368f0)(sdk,desc.data()):nullptr;
        const float writer_pose[7]{0,0,0,1,0,0,0};
        auto* writer_actor=writer_scene?reinterpret_cast<void*(*)(void*,const float*)>(base+0x36cc0)(sdk,writer_pose):nullptr;
        const auto writer_game=writer_actor?LoadLibraryExA(game_path,nullptr,DONT_RESOLVE_DLL_REFERENCES):nullptr;
        expect(writer_scene && writer_actor && writer_game,"create isolated native writer/census scene");
        if(writer_scene && writer_actor && writer_game) {
            const auto writer_base=reinterpret_cast<std::uintptr_t>(writer_game);
            std::array<std::byte,0x200> writer_world{},writer_owner{};
            const auto owner_address=reinterpret_cast<std::uintptr_t>(writer_owner.data());
            std::array<std::uintptr_t,2> writer_user{3,owner_address};
            const auto previous_user=readptr(writer_scene,8);
            const auto previous_shader=readptr(writer_scene,0x10+0x1070);
            const auto put_writer=[](std::uintptr_t address,const auto& value){
                std::memcpy(reinterpret_cast<void*>(address),&value,sizeof(value));
            };
            put_writer(reinterpret_cast<std::uintptr_t>(writer_world.data())+0x1c8,owner_address);
            put_writer(owner_address+4,1u);put_writer(owner_address+0xf8,short{7});
            put_writer(reinterpret_cast<std::uintptr_t>(writer_scene)+8,
                reinterpret_cast<std::uintptr_t>(writer_user.data()));
            put_writer(reinterpret_cast<std::uintptr_t>(writer_scene)+0x10+0x1070,writer_base+0x204cf60);
            const auto set_kinematic=reinterpret_cast<void(*)(void*,unsigned char,bool)>(base+0x3da30);
            set_kinematic(writer_actor,1,true);
            reinterpret_cast<void(*)(void*,void*)>(base+0x407b0)(writer_scene,writer_actor);
            const auto lookup_writer=[&](short id,std::uintptr_t& out){
                out=reinterpret_cast<std::uintptr_t>(writer_scene);return id==7;
            };
            const auto scan_writer=[&](GroundCollisionFixture& census,const char*& check) {
                census.base=writer_base;census.world=reinterpret_cast<std::uintptr_t>(writer_world.data());
                std::uintptr_t rejected{};
                return census.collision_update_domain(base,{},check,rejected,lookup_writer,
                    [](std::uintptr_t){return std::uintptr_t{};});
            };
            const char* writer_check="none";
            GroundCollisionFixture before_writer,after_writer;
            const bool before_writer_admitted=scan_writer(before_writer,writer_check);
            set_kinematic(writer_actor,1,false);
            const bool after_writer_admitted=scan_writer(after_writer,writer_check);
            const auto writer_change=before_writer.body_inventory.FirstDifference(after_writer.body_inventory);
            expect(before_writer_admitted && !after_writer_admitted
                && std::strcmp(writer_check,"body_unrelated_dynamic")==0
                && before_writer.body_inventory.scan_complete && after_writer.body_inventory.scan_complete
                && writer_change.kind==ReplayPhysicsBodyInventory::Difference::Changed
                && writer_change.before.actor==reinterpret_cast<std::uintptr_t>(writer_actor)
                && writer_change.after.actor==reinterpret_cast<std::uintptr_t>(writer_actor),
                "production census detects shipped in-scene kinematic-to-free writer");
            put_writer(reinterpret_cast<std::uintptr_t>(writer_scene)+8,previous_user);
            put_writer(reinterpret_cast<std::uintptr_t>(writer_scene)+0x10+0x1070,previous_shader);
            reinterpret_cast<void(*)(void*,void*,bool)>(base+0x408c0)(writer_scene,writer_actor,false);
            reinterpret_cast<void(*)(void*)>(base+0x396a0)(writer_actor);
            FreeLibrary(writer_game);
        } else if(writer_actor)reinterpret_cast<void(*)(void*)>(base+0x396a0)(writer_actor);
        if(writer_scene)reinterpret_cast<void(*)(void*)>(base+0x40440)(writer_scene);
        reinterpret_cast<void(*)(void*)>(base+0x368a0)(sdk);
    }
    expect(dispatcher.submissions>0,"native task dispatcher actually ran the history-establishing update");
    release(foundation);
    expect(!errors.count && !allocator.invalid_free && allocator.live.empty(),
        "native island lifecycle releases every allocation without invalid frees");
    std::cout<<"Native island retirement peak_bytes="<<allocator.peak
        <<" outstanding_bytes="<<allocator.bytes<<" errors="<<errors.count<<'\n';
    FreeLibrary(physics);
}

// Run the shipped constructor/destructor/registration code against private
// native-layout memory. No game process, expected replay or native allocator
// is involved. Native functions must fit preallocated capacities throughout.
void test_native_physics_markers(const char* physics_path,const char* game_path)
{
    const auto physics=LoadLibraryExA(physics_path,nullptr,DONT_RESOLVE_DLL_REFERENCES);
    const auto game=LoadLibraryExA(game_path,nullptr,DONT_RESOLVE_DLL_REFERENCES);
    expect(physics && game,"map exact shipped binaries for native marker lifecycle fixture");
    if(!physics || !game) {if(physics)FreeLibrary(physics);if(game)FreeLibrary(game);return;}
    std::cout<<"Physics SAP image bytes="<<sizeof(ReplayPhysicsSapImage)
        <<" checkpoint_addition_bytes="<<2*sizeof(ReplayPhysicsSapImage)<<" GPU_readbacks=0\n";
    struct Fixture {
        std::array<std::byte,0x1140> scene{};
        std::array<std::byte,0x1600> nphase{};
        std::array<std::array<std::byte,0xc8>,4> actors{};
        std::array<std::array<std::byte,0x100>,4> native_actors{};
        std::array<std::byte,0x25a0> native_scene{};
        std::array<std::uintptr_t,4> native_members{};
        std::array<std::array<std::byte,0x50>,3> cores{},shape_cores{};
        alignas(16) std::array<std::array<std::byte,0x50>,3> shapes{};
        std::array<std::uintptr_t,8> scene_list{};
        std::array<std::uintptr_t,8> contacts{};
        std::array<std::byte,0x70> contact{};
        std::array<std::byte,8*24> map_entries{};
        std::array<unsigned,8> map_next{};
        std::array<std::uintptr_t,8> dirty_entries{};
        std::array<unsigned,8> dirty_next{};
        std::array<unsigned,16> dirty_buckets{};
        std::array<unsigned,16> map_buckets{};
        alignas(16) std::array<std::byte,16*64> slab{};
        std::array<std::uintptr_t,1> slabs{};
        std::array<std::uintptr_t,3> original{};
        std::uintptr_t module{},game{};
        std::array<std::byte,0x300> aabb{},sap{};
        std::array<std::byte,0x20> bounds_owner{},distance_owner{};
        std::array<std::array<unsigned,16>,3> pending{};
        std::array<std::array<unsigned,12>,3> endpoints{};
        std::array<std::array<unsigned,8>,3> values{},owners{};
        std::array<unsigned,8> next_links{},previous_links{};
        std::array<std::array<std::uintptr_t,2>,6> volumes{};
        std::array<unsigned,6> groups{},distances{};
        std::array<std::array<unsigned,6>,6> bounds{};
        std::array<std::array<std::byte,0x38>,4> id_trackers{};
        std::array<std::array<unsigned,16>,4> id_free{},id_pending{},id_bitmap{};
        std::array<std::array<unsigned,32>,4> relocated_id_free{};
        std::array<std::byte,0x20> temporary_element{};
        alignas(16) std::array<std::byte,0x50> inactive_shape{},inactive_core{};
        std::array<std::byte,0x58> anchor{};std::array<std::byte,0x30> anchor_core{};
        alignas(16) std::array<std::byte,0x50> aggregate{},aggregate_self{},aggregate_user{};
        std::array<std::uintptr_t,2> aggregate_slots{};
        std::array<unsigned,64> sap_hash{},sap_next{};
        std::array<std::array<std::uintptr_t,2>,64> sap_pairs{};
        std::array<unsigned char,64> sap_states{};
        std::array<std::array<std::uintptr_t,2>,128> relocated_pairs{};
        std::array<unsigned,128> relocated_next{};
        std::array<unsigned char,128> relocated_states{};
    };
    static_assert(offsetof(Fixture,shapes)%16==0); // Native AABB volume tags consume the low four bits.
    const auto address=[](auto& bytes){return reinterpret_cast<std::uintptr_t>(bytes.data());};
    const auto put=[](std::uintptr_t p,unsigned offset,auto value){std::memcpy(reinterpret_cast<void*>(p+offset),&value,sizeof(value));};
    const auto word=[](std::uintptr_t p,unsigned offset){unsigned v{};std::memcpy(&v,reinterpret_cast<void*>(p+offset),4);return v;};
    const auto pointer=[](std::uintptr_t p,unsigned offset=0){std::uintptr_t v{};std::memcpy(&v,reinterpret_cast<void*>(p+offset),8);return v;};
    for(unsigned mode:{0u,1u,2u,3u,4u,5u})for(unsigned fault:{0u,1u,3u,4u,5u,6u,7u}) {
        const bool with_sap=mode!=0,with_aggregate=mode==2,absent_target=mode>=3;
        const unsigned b_count=mode==5?0u:mode==4?1u:absent_target?2u:3u;
        const unsigned extent=with_aggregate?4:3;
        auto f=std::make_unique<Fixture>();f->module=reinterpret_cast<std::uintptr_t>(physics);f->game=reinterpret_cast<std::uintptr_t>(game);
        const auto scene=address(f->scene),nphase=address(f->nphase),pool=nphase+0xdf0,map=nphase+0x1558;
        const auto sap=address(f->sap),aabb=address(f->aabb);
        put(scene,0x1058,nphase);put(scene,0x1070,f->game+0x204cf60);
        put(scene,0x58,address(f->scene_list));put(scene,0x64,8u);
        put(map,8,address(f->map_entries));put(map,0x10,address(f->map_next));put(map,0x18,address(f->map_buckets));
        put(map,0x20,8u);put(map,0x24,16u);f->map_buckets.fill(0xffffffffu);f->map_next.fill(0xffffffffu);
        f->slabs[0]=address(f->slab);put(pool,0x210,address(f->slabs));put(pool,0x218,1u);put(pool,0x21c,1u);
        put(pool,0x220,16u);put(pool,0x228,16u*64);put(pool,0x230,address(f->slab));
        for(unsigned i=0;i<16;++i)put(address(f->slab),i*64,i==15?std::uintptr_t{}:address(f->slab)+(i+1)*64);
        for(unsigned i=0;i<3;++i) {
            const auto actor=address(f->actors[i]),shape=address(f->shapes[i]),native=address(f->native_actors[i]);
            put(native,0,f->module+(i<2?0x19b7c0:0x19be60));put(native,8,static_cast<unsigned short>(i<2?6:7));
            put(native,0x80,actor);f->native_members[i]=native;
            put(actor,0x28,actor+8);put(actor,0x30,4u);put(actor,0x40,scene);put(actor,0x48,native+0x80);put(actor,0x50,i+1);
            f->native_actors[i][0x8d]=std::byte(i<2?1:0);f->native_actors[i][0xac]=std::byte(i<2?1:0);
            put(shape,0,f->module+0x1aae40);put(shape,0x10,actor);put(shape,0x40,address(f->shape_cores[i]));put(shape,0x18,0x80000000u|i);put(shape,0x48,i);
            put(actor,0x38,shape);
            put(address(f->shape_cores[i]),0x1c,5u<<21);
        }
        if(with_sap) {
            put(address(f->native_scene),0,f->module+0x19d008);
            put(address(f->native_scene),0x2590,address(f->native_members));put(address(f->native_scene),0x2598,with_aggregate?4u:3u);put(address(f->native_scene),0x259c,4u);
            put(scene,0x728,aabb);put(aabb,0x100,sap);put(sap,0,f->module+0x1ace30);
            put(aabb,0xa8,address(f->volumes));put(aabb,0xb0,6u);put(aabb,0xb4,6u);
            put(aabb,0x90,address(f->groups));put(aabb,0x98,6u);put(aabb,0x9c,6u);put(aabb,0x1c0,extent);
            put(scene,0x818,address(f->bounds_owner));put(aabb,0x108,address(f->bounds_owner));put(address(f->bounds_owner),8,address(f->bounds));put(address(f->bounds_owner),0x10,6u);put(address(f->bounds_owner),0x14,6u);
            put(aabb,0xa0,address(f->distance_owner));put(address(f->distance_owner),8,address(f->distances));put(address(f->distance_owner),0x10,6u);
            put(sap,0xc8,6u);put(sap,0x140,3u);put(sap,0x144,3u);put(sap,0x148,8u);
            put(sap,0x130,address(f->next_links));put(sap,0x138,address(f->previous_links));
            for(unsigned i=0;i<8;++i){f->next_links[i]=i<7?i+1:i;f->previous_links[i]=i?i-1:0;}
            for(unsigned axis=0;axis<3;++axis) {
                put(aabb,0x48+axis*16,address(f->pending[axis]));put(aabb,0x50+axis*16,16u);
                put(sap,0xd0+axis*8,address(f->endpoints[axis]));put(sap,0xe8+axis*8,address(f->values[axis]));put(sap,0x100+axis*8,address(f->owners[axis]));
                f->owners[axis]={0x3ffffffe,0,2,1,3,4,5,0x3fffffff};
                f->values[axis]={0,10,20,30,40,50,60,0xffffffff};
                f->endpoints[axis]={1,3,2,4,5,6,0x3fffffff,0x3fffffff,0x3fffffff,0x3fffffff,0x3fffffff,0x3fffffff};
            }
            put(aabb,0x1cc,0xffffffffu);
            f->groups.fill(0xffffffffu);for(auto& volume:f->volumes)volume[1]=0xffffffffu;
            for(unsigned i=0;i<3;++i){f->volumes[i]={address(f->shapes[i]),0xffffffffu};f->groups[i]=i+1;}
            for(unsigned i=0;i<4;++i) {
                const auto id=address(f->id_trackers[i]);put(scene,0x1118+i*8,id);
                put(id,0,i==3?extent:i==0?0u:i==2?4u:3u);put(id,8,address(f->id_free[i]));put(id,0x14,16u);
                put(id,0x18,address(f->id_bitmap[i]));put(id,0x20,16u);put(id,0x28,address(f->id_pending[i]));put(id,0x34,16u);
            }
            const auto anchor=address(f->anchor),anchor_core=address(f->anchor_core),rigid_ids=address(f->id_trackers[2]);
            reinterpret_cast<void*(*)(void*,unsigned)>(f->module+0x106da0)(reinterpret_cast<void*>(anchor_core),0);
            put(anchor_core,0x1c,0x3f800000u);put(rigid_ids,0,0u);
            reinterpret_cast<void*(*)(void*,std::uintptr_t,void*)>(f->module+0x1118f0)(reinterpret_cast<void*>(anchor),scene,reinterpret_cast<void*>(anchor_core));
            put(anchor,0,f->module+0x1aae70);put(scene,0x1138,anchor);put(rigid_ids,0,4u);
            put(sap,0x160,address(f->sap_hash));put(sap,0x168,address(f->sap_next));put(sap,0x170,64u);put(sap,0x174,64u);put(sap,0x178,64u);
            put(sap,0x180,address(f->sap_pairs));put(sap,0x188,address(f->sap_states));put(sap,0x194,64u);put(sap,0x198,63u);
            f->sap_hash.fill(0x3fffffff);f->sap_next.fill(0x3fffffff);
            if(with_aggregate) {
                // A live query-only sibling owns IDs without an AABB volume.
                const auto inactive=address(f->inactive_shape),core=address(f->inactive_core);
                const auto inactive_actor=address(f->actors[3]),native=address(f->native_actors[3]);
                put(native,0,f->module+0x19be60);put(native,8,static_cast<unsigned short>(7));put(native,0x80,inactive_actor);f->native_members[3]=native;
                put(inactive_actor,0x40,scene);put(inactive_actor,0x48,native+0x80);put(inactive_actor,0x50,4u);put(inactive_actor,0x38,inactive);
                put(inactive,0,f->module+0x1aae40);put(inactive,0x10,inactive_actor);
                put(inactive,0x18,0x80000004u);put(inactive,0x40,core);put(inactive,0x48,3u);
                put(core,0x40,static_cast<unsigned char>(2));
                put(address(f->id_trackers[1]),0,4u);put(address(f->id_trackers[2]),0,5u);put(address(f->id_trackers[3]),0,5u);
                const auto owner=address(f->aggregate),self=address(f->aggregate_self);
                // Shipped no-self constructor needs no native allocator. The
                // optional empty self-cache follows its verified native layout.
                reinterpret_cast<void*(*)(void*,unsigned,bool)>(f->module+0x151ef0)(reinterpret_cast<void*>(owner),3,false);
                put(owner,0x18,self);put(self,0,f->module+0x1acb50);put(self,8,0xffffffffu);put(self,0x48,owner);
                f->aggregate_slots={owner,0xffffffffu};
                put(aabb,0x1c8,1u);put(aabb,0x1cc,1u);put(aabb,0x1d0,address(f->aggregate_slots));put(aabb,0x1d8,2u);put(aabb,0x1dc,2u);
                f->volumes[3]={address(f->aggregate_user),1};f->groups[3]=0xfffffffdu;
                f->bounds[3]={0x7e7fffff,0x7e7fffff,0x7e7fffff,0xfe7fffff,0xfe7fffff,0xfe7fffff};
            }
        }
        const auto original_aggregate=f->aggregate,original_self=f->aggregate_self;
        const auto original_slots=f->aggregate_slots;
        const auto original_anchor=f->anchor;const auto original_anchor_core=f->anchor_core;
        const auto birth=[&](unsigned a,unsigned b) {
            const auto slot=pointer(pool,0x230);put(pool,0x230,pointer(slot));put(pool,0x224,word(pool,0x224)+1);
            reinterpret_cast<void*(*)(void*,std::uintptr_t,std::uintptr_t,bool)>(f->module+0x120030)(reinterpret_cast<void*>(slot),address(f->shapes[a]),address(f->shapes[b]),false);
            reinterpret_cast<void(*)(std::uintptr_t,std::uintptr_t)>(f->module+0x11c410)(nphase,slot);
            if(with_sap) {
                const auto pair=reinterpret_cast<std::uintptr_t(*)(std::uintptr_t,unsigned,unsigned,unsigned char)>(f->module+0x160eb0)(sap+0x160,a,b,0);
                expect(pair!=0,"native SAP AddPair fits retained backing");put(pair,8,slot);
            }
            return slot;
        };
        const auto capture=[&]() {
            auto image=std::make_unique<Sc6ReplayWorldState::PhysicsBoundary>();image->module=f->module;image->scene_count=1;image->scenes[0]=scene;
            image->observed_actors[0]=2;image->node_domains[0].filter_shader=f->game+0x204cf60;
            for(unsigned actor=0;actor<2;++actor) {
                auto& row=image->actors[0][actor];row.simulation=address(f->actors[actor]);
                std::memcpy(row.simulation_storage.data(),f->actors[actor].data(),f->actors[actor].size());
                row.interaction_count=word(row.simulation,0x34);
                if(absent_target) {
                    // Controlled projection prerequisites surround the actual
                    // shipped registration observations below. No scene lock,
                    // wake or transform writer is invoked by this preflight.
                    row.kind=2;row.actor=address(f->native_actors[actor]);row.body=row.actor+0x80;
                    row.component=address(f->shapes[actor]);row.kinematic=address(f->cores[actor]);
                    row.properties[0x9c]=std::byte{3};row.kinematic_storage[0x1f]=std::byte{1};
                    put(reinterpret_cast<std::uintptr_t>(row.dynamic_storage.data()),0x68,0x83000000u);
                    row.dynamic_storage[0xac]=std::byte{3};
                    put(reinterpret_cast<std::uintptr_t>(row.simulation_storage.data()),0,f->module+0x1aadc0);
                    row.isolated_kinematic=!row.interaction_count;
                }
                for(unsigned i=0;i<row.interaction_count;++i) {
                    auto& m=row.interactions[i];m.address=pointer(pointer(row.simulation,0x28),i*8);
                    std::memcpy(m.header.data(),reinterpret_cast<void*>(m.address),m.header.size());
                    std::memcpy(m.marker_elements.data(),reinterpret_cast<void*>(m.address+0x28),16);
                    m.marker_filter_pair=word(m.address,0x38);m.marker_registered=m.marker_filter_valid=true;
                    m.marker_scene_count=word(scene,0x60);m.marker_map_count=word(map,0x34);m.marker_scene_active=word(scene,0x70);
                    for(unsigned side=0;side<2;++side) {
                        const auto owner=pointer(m.address,8+side*8),shape=m.marker_elements[side];
                        m.marker_actor_counts[side]=word(owner,0x34);m.marker_shape_cores[side]=pointer(shape,0x40);
                        reinterpret_cast<void(*)(void*,unsigned*,void*)>(f->module+0x1143e0)(reinterpret_cast<void*>(shape),&m.marker_attributes[side],m.marker_filter_data[side].data());
                    }
                }
            }
            if(with_sap) {
                auto& bp=image->broadphases[0];bp.scene=scene;bp.manager=aabb;bp.broadphase=sap;
                bp.native_scene=address(f->native_scene);
                for(unsigned i=0;i<(with_aggregate?4u:3u);++i)bp.rigid_members[i+1]={address(f->native_actors[i]),address(f->actors[i])};
                bp.bounds_owner=address(f->bounds_owner);bp.distance_owner=address(f->distance_owner);
                bp.extent=word(aabb,0x1c0);bp.boxes=bp.previous_boxes=3;bp.pair_count=word(sap,0x190);bp.pending=f->pending;
                for(unsigned axis=0;axis<3;++axis) {
                    std::copy(f->values[axis].begin(),f->values[axis].end(),bp.values[axis].begin());
                    std::copy(f->owners[axis].begin(),f->owners[axis].end(),bp.owners[axis].begin());
                }
                for(unsigned axis=0;axis<3;++axis)for(unsigned i=0;i<bp.extent;++i)bp.box_endpoints[axis][i]={f->endpoints[axis][i*2],f->endpoints[axis][i*2+1]};
                for(unsigned i=0;i<3;++i) {
                    auto& v=bp.volumes[i];v.shape=address(f->shapes[i]);v.core=address(f->shape_cores[i]);v.actor=address(f->actors[i]);
                    v.group=f->groups[i];v.contact_distance=f->distances[i];v.bounds=f->bounds[i];
                    v.shape_id=word(v.shape,0x48);v.rigid_id=word(v.actor,0x50);
                    reinterpret_cast<void(*)(void*,unsigned*,void*)>(f->module+0x1143e0)(reinterpret_cast<void*>(v.shape),&v.attributes,v.filter_data.data());
                }
                for(unsigned i=0;i<4;++i) {
                    auto& id=bp.ids[i];id.owner=address(f->id_trackers[i]);id.next=word(id.owner,0);id.count=word(id.owner,0x10);
                    for(unsigned j=0;j<id.count;++j)id.free[j]=word(pointer(id.owner,8),j*4);
                }
                bp.anchor_owner=address(f->anchor);bp.anchor_core=address(f->anchor_core);bp.anchor_id=word(bp.anchor_owner,0x50);
                if(with_aggregate) {
                    auto& v=bp.non_broadphase[4];v.shape=address(f->inactive_shape);v.core=address(f->inactive_core);
                    v.actor=address(f->actors[3]);v.shape_id=3;v.rigid_id=4;v.flags=2;
                    reinterpret_cast<void(*)(void*,unsigned*,void*)>(f->module+0x1143e0)(reinterpret_cast<void*>(v.shape),&v.attributes,v.filter_data.data());
                }
                bp.aggregate_free_head=word(aabb,0x1cc);
                if(with_aggregate) {
                    bp.aggregate_free_links[1]=0xffffffffu;
                    bp.aggregate_slots=2;bp.aggregate_count=1;
                    bp.empty_aggregates[0]={address(f->aggregate),address(f->aggregate_self),0,3};
                    auto& v=bp.volumes[3];v.shape=address(f->aggregate_user);v.core=address(f->aggregate);v.actor=address(f->aggregate_self);
                    v.empty_aggregate=true;v.group=f->groups[3];v.bounds=f->bounds[3];
                }
                for(unsigned i=extent;i<bp.extent;++i){bp.volumes[i].group=f->groups[i];bp.volumes[i].bounds=f->bounds[i];}
                for(unsigned i=0;i<bp.pair_count;++i) {
                    const auto pairs=pointer(sap,0x180);
                    bp.pairs[i].handles={word(pairs,i*16),word(pairs,i*16+4)};
                    bp.pairs[i].interaction=pointer(pairs,i*16+8);bp.pairs[i].state=*reinterpret_cast<unsigned char*>(pointer(sap,0x188)+i);
                }
                bp.valid=bp.WellFormed();expect(bp.valid,"independent SAP observation validates its endpoint inverses");
            }
            return image;
        };
        f->original[0]=birth(0,1);auto a=capture();
        f->original[1]=birth(0,2);f->original[2]=birth(1,2);
        if(with_sap) {
            for(unsigned axis=0;axis<3;++axis){f->owners[axis]={0x3ffffffe,0,2,4,1,3,5,0x3fffffff};f->endpoints[axis]={1,4,2,5,3,6,0x3fffffff,0x3fffffff,0x3fffffff,0x3fffffff,0x3fffffff,0x3fffffff};}
            f->pending[2][0]=3;f->bounds[0][0]=73;
        }
        if(with_aggregate) {
            // Shipped retirement shrinks ID counters while AABB high water stays
            // enlarged. Non-LIFO release leaves an ordered free stack; backing stays native.
            for(unsigned i:{1u,2u,3u}) {
                const auto id=address(f->id_trackers[i]);const auto next=word(id,0);
                put(id,0,next+2);f->id_pending[i][0]=next;f->id_pending[i][1]=next+1;put(id,0x30,2u);
                reinterpret_cast<void(*)(void*)>(f->module+0xfdb00)(reinterpret_cast<void*>(id));
                expect(word(id,0)==next+1 && word(id,0x10)==1 && f->id_free[i][0]==next && !word(id,0x30),"native non-LIFO retirement changes free continuation while preserving active owners");
            }
            put(aabb,0x1c0,6u);
        }
        if(absent_target) {
            // The A pair genuinely ended in native execution. Both shapes and
            // their SAP IDs remain, but B has only the two different pairs.
            reinterpret_cast<void(*)(std::uintptr_t,void*)>(f->module+0x1220c0)(pool,reinterpret_cast<void*>(f->original[0]));
            expect(reinterpret_cast<unsigned(*)(std::uintptr_t,unsigned,unsigned)>(f->module+0x161070)(sap+0x160,0,1)!=0,
                "native B removes A pair from SAP and registration before capture");
            f->original[0]=f->original[1];f->original[1]=f->original[2];f->original[2]=0;
            if(mode>=4) {
                reinterpret_cast<void(*)(std::uintptr_t,void*)>(f->module+0x1220c0)(pool,reinterpret_cast<void*>(f->original[1]));
                expect(reinterpret_cast<unsigned(*)(std::uintptr_t,unsigned,unsigned)>(f->module+0x161070)(sap+0x160,1,2)!=0,
                    "B leaves the second fighter without any marker while its shape remains live");
                f->original[1]=0;
            }
            if(mode==5) {
                reinterpret_cast<void(*)(std::uintptr_t,void*)>(f->module+0x1220c0)(pool,reinterpret_cast<void*>(f->original[0]));
                expect(reinterpret_cast<unsigned(*)(std::uintptr_t,unsigned,unsigned)>(f->module+0x161070)(sap+0x160,0,2)!=0,
                    "native B ends the final overlap while retaining all shape/SAP owners");
                f->original[0]=0;
                expect(!word(scene,0x60) && !word(map,0x34) && !word(pool,0x224),
                    "empty B graph is independently observed in native scene, map and pool");
            }
        }
        auto b=capture();
        if(mode==5 && fault==0) {
            // Reverse the already verified empty-B case: historical A has no
            // overlap, current B does. Exercise actual pool/registration/SAP
            // publication, partial failure, C creation, undo and commit.
            for(unsigned boundary=0;boundary<4;++boundary) {
                const auto retained=birth(0,1);
                auto present=capture();
                if(boundary==0) {
                    const auto owner=b->broadphases[0].native_scene;b->broadphases[0].native_scene=0;
                    Sc6ReplayPhysicsMarkers rejected;
                    expect(!rejected.Prepare(f->game,*b,*present,512*1024).ok() && rejected.released()
                        && word(pool,0x224)==1 && f->scene_list[0]==retained,
                        "empty A cannot borrow scene ownership from nonempty B");
                    b->broadphases[0].native_scene=owner;
                }
                Sc6ReplayPhysicsMarkers empty;
                const auto prepared=empty.Prepare(f->game,*b,*present,512*1024);
                if(!prepared.ok())std::fprintf(stderr,"Empty A preparation rejected check=%s\n",empty.diagnostic().check);
                expect(prepared.ok(),"historical empty A must own reversible nonempty B without constructing an A marker");
                std::uintptr_t current=retained;unsigned right=1;
                if(prepared.ok()) {
                    if(boundary==0) {
                        expect(empty.Undo().ok() && word(pool,0x224)==1 && word(scene,0x60)==1,
                            "empty-A cancellation before publication preserves native B");
                    } else {
                        if(boundary==1)expect(empty.ArmPublicationFailure(1),"empty-A transaction can fail after B detachment");
                        const auto published=empty.Publish();
                        expect(published.ok()==(boundary!=1),"empty-A publication has explicit partial-failure outcome");
                        if(published.ok()) {
                            expect(!word(scene,0x60) && !word(map,0x34) && !word(sap,0x190) && word(pool,0x224)==1,
                                "empty A publishes zero native pairs while retaining complete B slot");
                            expect(empty.BeginExecution().ok(),"empty A can enter native execution with B retained");
                            current=birth(0,2);right=2;
                        }
                        if(boundary==3 && published.ok()) {
                            expect(empty.Commit().ok() && empty.Commit().ok() && word(pool,0x224)==1,
                                "empty-A commit retires B exactly once and preserves native C creation");
                        } else {
                            expect(empty.Undo().ok() && empty.Undo().ok() && word(pool,0x224)==1
                                && word(scene,0x60)==1 && f->scene_list[0]==retained,
                                "empty-A partial or executed transaction restores exact B marker ownership");
                            current=retained;right=1;
                        }
                    }
                }
                reinterpret_cast<void(*)(std::uintptr_t,void*)>(f->module+0x1220c0)(pool,reinterpret_cast<void*>(current));
                expect(reinterpret_cast<unsigned(*)(std::uintptr_t,unsigned,unsigned)>(f->module+0x161070)(sap+0x160,0,right)!=0,
                    "empty-A fixture retires only its final native pair");
                expect(!word(scene,0x60) && !word(map,0x34) && !word(pool,0x224),"empty-A fixture releases every native slot");
                b=capture();
            }
            const auto bound=f->bounds[0][0];f->bounds[0][0]=bound+1;
            auto empty_b=capture();
            {
                Sc6ReplayPhysicsMarkers both_empty;
                const auto prepared=both_empty.Prepare(f->game,*b,*empty_b,512*1024);
                expect(prepared.ok() && both_empty.Publish().ok() && both_empty.BeginExecution().ok(),
                    "different empty A/B SAP histories retain their verified scene without a marker-derived binding");
                if(prepared.ok()) {
                    birth(1,2);
                    expect(both_empty.Undo().ok() && !word(pool,0x224) && !word(scene,0x60) && !word(sap,0x190),
                        "both-empty transaction retires native C creation and restores empty B");
                }
            }
            f->bounds[0][0]=bound;b=capture();
        }
        if(mode==5) {
            for(unsigned field=0;field<2;++field) {
                auto& owner=field?b->broadphases[0].native_scene:b->broadphases[0].scene;
                const auto saved=owner;owner=0;
                Sc6ReplayPhysicsMarkers rejected;
                expect(!rejected.Prepare(f->game,*a,*b,512*1024).ok() && rejected.released()
                    && !word(scene,0x60) && !word(map,0x34) && !word(pool,0x224),
                    "empty B cannot use an absent or incompatible retained SAP scene owner");
                owner=saved;
            }
        }
        if(absent_target) {
            ReplayPhysicsMarkerGraph ga{},gb{};
            expect(ga.Read(*a,0) && gb.Read(*b,0) && !ga.TargetRetainedIn(gb),
                "fixture proves target pair is absent rather than merely allocated elsewhere");
            Sc6ReplayPhysicsMarkers rejected;
            put(address(f->shapes[0]),0x40,address(f->shape_cores[1]));
            expect(!rejected.Prepare(f->game,*a,*b,512*1024).ok() && word(pool,0x224)==b_count,
                "absent target cannot bypass changed live shape core ownership");
            put(address(f->shapes[0]),0x40,address(f->shape_cores[0]));
        } else {
            Sc6ReplayPhysicsMarkers capacity;
            put(scene,0x728,scene); // An uncovered real broadphase owner is never admitted.
            expect(!capacity.Prepare(f->game,*a,*b,512*1024).ok() && capacity.released()
                && std::string_view(capacity.diagnostic().check)==(with_sap?"sap_binding":"marker_broadphase_ownership_unproven")
                && word(pool,0x224)==3 && word(scene,0x60)==3 && word(map,0x34)==3,
                "unowned broadphase pair pointers reject before B is detached");
            put(scene,0x728,with_sap?aabb:std::uintptr_t{});
            expect(!capacity.Prepare(f->game,*a,*b,0).ok() && capacity.released()
                && word(pool,0x224)==3 && f->scene_list[0]==f->original[0],
                "capacity rejection precedes any native pool or membership mutation");
            put(map,0x20,2u);
            expect(!capacity.Prepare(f->game,*a,*b,512*1024).ok() && capacity.released()
                && word(pool,0x224)==3,"insufficient pair-map backing rejects without growing native storage");
            put(map,0x20,8u);
            if(with_sap) {
                // Every previously grouped pending-work guard identifies its exact owner.
                unsigned step=11;
                for(unsigned o:{0x150u,0x1a0u,0x1b0u,0x200u,0x248u,0x290u,0x1c0u}) {
                    put(sap,o,1u);
                    expect(!capacity.Prepare(f->game,*a,*b,512*1024).ok()
                        && std::string_view(capacity.diagnostic().check)==ReplayPhysicsSapImage::AdmissionName(step++)
                        && word(pool,0x224)==3,"pending SAP work rejects without detaching B");
                    put(sap,o,0u);
                }
                for(unsigned o:{0x1c8u,0x1e8u,0x22cu,0x264u,0x118u,0x128u}) {
                    const auto saved=word(aabb,o);put(aabb,o,saved+1u);
                    expect(!capacity.Prepare(f->game,*a,*b,512*1024).ok()
                        && std::string_view(capacity.diagnostic().check)==ReplayPhysicsSapImage::AdmissionName(step++)
                        && word(pool,0x224)==3,"unsupported manager work rejects without detaching B");
                    put(aabb,o,saved);
                }
                if(with_aggregate) {
                    put(address(f->aggregate),0x10,1u);
                    expect(!capacity.Prepare(f->game,*a,*b,512*1024).ok() && word(pool,0x224)==3,"nonempty aggregate rejects before B detachment");
                    put(address(f->aggregate),0x10,0u);
                    f->pending[0][0]=8;
                    expect(!capacity.Prepare(f->game,*a,*b,512*1024).ok() && word(pool,0x224)==3,"empty aggregate pending native admission rejects");
                    f->pending[0][0]=0;
                    f->aggregate_slots[1]=1;
                    expect(!capacity.Prepare(f->game,*a,*b,512*1024).ok() && word(pool,0x224)==3,"aggregate free-list cycle rejects");
                    f->aggregate_slots=original_slots;
                }
                put(address(f->anchor_core),0x20,1u);
                expect(!capacity.Prepare(f->game,*a,*b,512*1024).ok()
                    && std::string_view(capacity.diagnostic().check)=="sap_static_anchor" && word(pool,0x224)==3,
                    "a changed static anchor is rejected, never normalized or overwritten");
                put(address(f->anchor_core),0x20,0u);
                const auto element_ids=address(f->id_trackers[3]),saved_next=std::uintptr_t(word(address(f->id_trackers[3]),0));
                put(element_ids,0,static_cast<unsigned>(saved_next+1));
                expect(!capacity.Prepare(f->game,*a,*b,512*1024).ok()
                    && std::string_view(capacity.diagnostic().check)=="sap_element_id_coverage" && word(pool,0x224)==3,"changed future native ID allocation rejects before B detachment");
                put(element_ids,0,static_cast<unsigned>(saved_next));
                put(address(f->shapes[0]),0x48,1u);put(address(f->shapes[1]),0x48,0u);
                expect(!capacity.Prepare(f->game,*a,*b,512*1024).ok()
                    && std::string_view(capacity.diagnostic().check)=="sap_shape_id_owner"
                    && capacity.diagnostic().row==0 && word(pool,0x224)==3,"same active IDs with different shape ownership reject before publication");
                put(address(f->shapes[0]),0x48,0u);put(address(f->shapes[1]),0x48,1u);
                if(with_aggregate) {
                    put(address(f->inactive_shape),8,address(f->inactive_shape));
                    expect(!capacity.Prepare(f->game,*a,*b,512*1024).ok() && word(pool,0x224)==3,
                        "cyclic native shape membership rejects before B detachment");
                    put(address(f->inactive_shape),8,std::uintptr_t{});
                    put(address(f->actors[3]),0x38,std::uintptr_t{});
                    expect(!capacity.Prepare(f->game,*a,*b,512*1024).ok()
                        && std::string_view(capacity.diagnostic().check)=="sap_shape_id_coverage" && word(pool,0x224)==3,
                        "unaccounted live query shape IDs cannot become free IDs");
                    put(address(f->actors[3]),0x38,address(f->inactive_shape));
                    put(address(f->inactive_core),0x40,static_cast<unsigned char>(1));
                    expect(!capacity.Prepare(f->game,*a,*b,512*1024).ok() && word(pool,0x224)==3,
                        "simulation shape missing its AABB volume rejects before publication");
                    put(address(f->inactive_core),0x40,static_cast<unsigned char>(2));
                    put(address(f->id_trackers[2]),8,address(f->id_free[3]));
                    expect(!capacity.Prepare(f->game,*a,*b,512*1024).ok()
                        && std::string_view(capacity.diagnostic().check)=="sap_restore_storage"
                        && word(pool,0x224)==3,"aliased ID free buffers reject before any allocator write");
                    put(address(f->id_trackers[2]),8,address(f->id_free[2]));
                }
                f->distances[0]=0x3f000000;
                expect(!capacity.Prepare(f->game,*a,*b,512*1024).ok()
                    && std::string_view(capacity.diagnostic().check)=="sap_contact_distance"
                    && capacity.diagnostic().row==0 && capacity.diagnostic().source==0
                    && capacity.diagnostic().expected==0 && capacity.diagnostic().observed==0x3f000000
                    && word(pool,0x224)==3,"first consumed SAP input mismatch is identified before B detachment");
                f->distances[0]=0;
                const auto original_hash=f->sap_hash;
                const auto empty=std::find(f->sap_hash.begin(),f->sap_hash.end(),0x3fffffffu);
                *empty=0;
                expect(!capacity.Prepare(f->game,*a,*b,512*1024).ok() && word(pool,0x224)==3,
                    "foreign or duplicate SAP hash membership rejects before publication");
                f->sap_hash=original_hash;
                put(address(f->bounds_owner),8,address(f->sap_pairs));
                expect(!capacity.Prepare(f->game,*a,*b,512*1024).ok() && word(pool,0x224)==3,
                    "aliased bounds and pair backing rejects before any native mutation");
                put(address(f->bounds_owner),8,address(f->bounds));
            }
            expect(capacity.Prepare(f->game,*a,*b,512*1024).ok(),"prepare valid binding for publication rejection");
            put(scene,0x1070,f->game+0x204cf61);
            expect(!capacity.Publish().ok() && word(pool,0x224)==3 && word(scene,0x60)==3,
                "invalidated native filter binding rejects publication with complete B untouched");
            put(scene,0x1070,f->game+0x204cf60);
            expect(capacity.Undo().ok() && capacity.released(),"cancel a rejected pre-publication marker transaction");
        }
        Sc6ReplayPhysicsMarkers transaction;
        auto status=transaction.Prepare(f->game,*a,*b,512*1024);
        if(!status.ok())std::cerr<<"marker prepare detail: sap="<<with_sap<<" fault="<<fault<<" check="
            <<(transaction.diagnostic().check?transaction.diagnostic().check:"none")<<'\n';
        expect(status.ok(),"native marker preparation validates actual scene/map/actor/pool ownership");
        if(!status.ok())continue;
        if(absent_target) {
            Sc6ReplayWorldState::PhysicsProjectionFailure failure{};
            expect(Sc6ReplayWorldState::PreparePhysicsProjection(*a,*b,&failure,false,&transaction).ok(),
                "full projection accepts native marker topology with a derived empty-list admission witness");
            const auto old=a->actors[0][0].isolated_kinematic;
            a->actors[0][0].isolated_kinematic=true;
            expect(!Sc6ReplayWorldState::PreparePhysicsProjection(*a,*b,&failure,false,&transaction).ok(),
                "registration normalization does not admit an isolated witness on an occupied list");
            a->actors[0][0].isolated_kinematic=old;
        }
        if(absent_target)for(unsigned actor=0;actor<2;++actor) {
            auto normalized=a->actors[0][actor];
            expect(transaction.NormalizePreflight(*a,*b,0,actor,normalized)
                && normalized.interaction_count==b->actors[0][actor].interaction_count
                && normalized.interactions==b->actors[0][actor].interactions,
                "prepared transaction owns registration preflight for every A or B actor, including empty B lists");
            expect(std::memcmp(normalized.simulation_storage.data()+0x38,a->actors[0][actor].simulation_storage.data()+0x38,
                normalized.simulation_storage.size()-0x38)==0,
                "marker preflight never normalizes native shape or scene binding witnesses");
        }
        const bool injected=(fault && fault<5) || fault==7;
        if(injected)expect(transaction.ArmPublicationFailure(fault==7?b_count+2:(std::min)(fault,b_count+1)),"arm a completed-native-operation publication failure");
        status=transaction.Publish();
        if(injected)expect(!status.ok(),"injected partial marker/SAP publication preserves recovery phase");
        else {
            expect(status.ok() && word(scene,0x60)==1 && word(map,0x34)==1 && word(pool,0x224)==b_count+1,
                "native A publication detaches all B markers while keeping their pool slots occupied");
            expect(f->scene_list[0]!=f->original[0],"A has a separate native allocation from complete B undo");
            if(status.ok() && fault!=6)expect(transaction.BeginExecution().ok(),"hand A records to native execution while B remains private");
            if(status.ok() && fault!=6) {
                // Native C consumes/frees A's slot, then creates suppressed
                // pairs again. Original B must survive slot reuse.
                const auto old=with_sap?pointer(address(f->sap_pairs),8):f->scene_list[0];
                expect(old==f->scene_list[0],"native cached SAP interaction names A's clone, never retained B");
                reinterpret_cast<void(*)(std::uintptr_t,void*)>(f->module+0x1220c0)(pool,reinterpret_cast<void*>(old));
                if(with_sap)expect(reinterpret_cast<unsigned(*)(std::uintptr_t,unsigned,unsigned)>(f->module+0x161070)(sap+0x160,0,1)!=0,
                    "native SAP removal consumes the restored hash and pair ownership");
                birth(0,1);birth(0,2);birth(1,2);
                if(with_sap){
                    f->pending[2][0]=4;f->bounds[0][0]=91;
                    for(unsigned axis=0;axis<3;++axis){f->owners[axis]={0x3ffffffe,0,2,4,1,3,5,0x3fffffff};f->endpoints[axis]={1,4,2,5,3,6,0x3fffffff,0x3fffffff,0x3fffffff,0x3fffffff,0x3fffffff,0x3fffffff};f->values[axis][1]=11;}
                    if(with_aggregate) {
                        const auto element=address(f->temporary_element),id=address(f->id_trackers[3]);
                        reinterpret_cast<void*(*)(void*,std::uintptr_t,unsigned char)>(f->module+0x133d00)(reinterpret_cast<void*>(element),address(f->actors[0]),0);
                        expect((word(element,0x18)&0x1fffffffu)==a->broadphases[0].ids[3].next,"native C allocation consumes restored A ID continuation");
                        reinterpret_cast<void(*)(void*)>(f->module+0x133e10)(reinterpret_cast<void*>(element));
                        reinterpret_cast<void(*)(void*)>(f->module+0xfdb00)(reinterpret_cast<void*>(id));
                        f->id_bitmap[3].fill(0); // Native F1ED0's completed retirement clear.
                        for(unsigned i=1;i<4;++i) {
                            std::copy(f->id_free[i].begin(),f->id_free[i].end(),f->relocated_id_free[i].begin());
                            put(address(f->id_trackers[i]),8,address(f->relocated_id_free[i]));put(address(f->id_trackers[i]),0x14,32u);
                        }
                    }
                    std::copy(f->sap_pairs.begin(),f->sap_pairs.end(),f->relocated_pairs.begin());
                    std::copy(f->sap_next.begin(),f->sap_next.end(),f->relocated_next.begin());
                    std::copy(f->sap_states.begin(),f->sap_states.end(),f->relocated_states.begin());
                    put(sap,0x180,address(f->relocated_pairs));put(sap,0x168,address(f->relocated_next));
                    put(sap,0x188,address(f->relocated_states));put(sap,0x194,128u);
                }
                for(unsigned i=0;i<3;++i)for(unsigned j=0;j<b_count;++j)
                    expect(f->scene_list[i]!=f->original[j],"native C cannot recycle any retained B slot");
                if(with_sap && fault==0) {
                    const auto pairs=pointer(sap,0x180),saved=pointer(pairs,8);
                    put(pairs,8,scene);
                    expect(!transaction.Undo().ok() && word(pool,0x224)==b_count+3 && word(scene,0x60)==3,
                        "unknown external SAP interaction rejects recovery before C retirement or B writes");
                    put(pairs,8,saved);
                }
            }
        }
        if(fault==5) {
            if(with_sap && !with_aggregate) {
                // A genuine C-only element/actor domain, added after A/B were
                // captured. Historical ownership stays unchanged. Native
                // ElementSim construction allocates and links its element ID.
                const auto actor=address(f->actors[3]),native=address(f->native_actors[3]);
                const auto shape=address(f->inactive_shape),core=address(f->inactive_core);
                put(native,0,f->module+0x19be60);put(native,8,static_cast<unsigned short>(7));put(native,0x80,actor);
                put(actor,0x40,scene);put(actor,0x48,native+0x80);put(actor,0x50,4u);
                reinterpret_cast<void*(*)(void*,std::uintptr_t,unsigned char)>(f->module+0x133d00)(reinterpret_cast<void*>(shape),actor,0);
                put(shape,0,f->module+0x1aae40);put(shape,0x40,core);put(shape,0x48,3u);
                put(core,0x40,static_cast<unsigned char>(2));
                f->native_members[3]=native;put(address(f->native_scene),0x2598,4u);
                put(address(f->id_trackers[1]),0,4u);put(address(f->id_trackers[2]),0,5u);
                expect(transaction.ValidateCommit().ok(),
                    "settled C-only shape IDs do not require historical owner equality to commit B retirement");
            }
            // Replace one C-only marker with a controlled contact descriptor.
            // Real base constructor and actor/scene registration own all list
            // indices; solver payload is not used by this retirement boundary.
            const auto old=f->scene_list[0],contact=address(f->contact);
            const auto shape0=pointer(old,0x28),shape1=pointer(old,0x30);
            reinterpret_cast<void(*)(std::uintptr_t,void*)>(f->module+0x1220c0)(pool,reinterpret_cast<void*>(old));
            reinterpret_cast<void*(*)(std::uintptr_t,std::uintptr_t,std::uintptr_t,unsigned char,unsigned char)>(f->module+0x133ba0)
                (contact,pointer(shape0,0x10),pointer(shape1,0x10),0,0x4b);
            put(contact,0,f->module+0x1ab540);put(contact,0x28,shape0);put(contact,0x30,shape1);put(contact,0x38,0xffffffffu);
            for(unsigned side=0;side<2;++side)
                reinterpret_cast<void(*)(std::uintptr_t,std::uintptr_t)>(f->module+0x114080)(pointer(contact,8+side*8),contact);
            put(scene,0x38,address(f->contacts));put(scene,0x44,8u);
            reinterpret_cast<void(*)(std::uintptr_t,std::uintptr_t,bool)>(f->module+0xeb900)(scene,contact,true);
            // Native conversion does not repair SAP's dormant cached pointer.
            // The actual SDK probe independently verifies this producer behavior.
            expect(transaction.ValidateCommit().ok(),"native registered contact in C does not require historical marker-only actor membership");
            // Fresh native contacts use the same element map as markers.
            // Converted contacts above may already have lost their old map row.
            reinterpret_cast<void(*)(std::uintptr_t,std::uintptr_t)>(f->module+0x11c410)(nphase,contact);
            expect(transaction.ValidateCommit().ok(),"commit accepts native element map containing both markers and contacts");
            const auto map_row=(word(map,0x34)-1)*24;
            const auto mapped_contact=pointer(address(f->map_entries),map_row+16);
            expect(mapped_contact==contact,"native insertion appends the independently registered contact");
            put(address(f->map_entries),map_row+16,scene);
            expect(!transaction.ValidateCommit().ok() && !transaction.released(),"foreign element map owner rejects before B retirement");
            put(address(f->map_entries),map_row+16,mapped_contact);


            const auto contact_actor=pointer(contact,8),contact_list=pointer(contact_actor,0x28);
            const auto contact_count=word(contact_actor,0x34);
            expect(contact_count<word(contact_actor,0x30),"duplicate-list fault fits existing native backing");
            put(contact_list,contact_count*8,contact);put(contact_actor,0x34,contact_count+1);
            expect(!transaction.ValidateCommit().ok() && !transaction.released(),"duplicate contact in actor list rejects before B retirement");
            put(contact_actor,0x34,contact_count);

            // Shipped ActorSim dirty producer queues the existing C markers.
            const auto dirty=nphase+0x60;
            put(dirty,8,address(f->dirty_entries));put(dirty,0x10,address(f->dirty_next));
            put(dirty,0x18,address(f->dirty_buckets));put(dirty,0x20,8u);put(dirty,0x24,16u);
            f->dirty_buckets.fill(0xffffffffu);f->dirty_next.fill(0xffffffffu);
            reinterpret_cast<void(*)(std::uintptr_t,unsigned,std::uintptr_t,unsigned char)>(f->module+0x113ff0)(address(f->actors[0]),5,0,8);
            expect(word(dirty,0x34)>0,"native dirty producer queues C refilter work");
            // Match the authored kinematic-to-dynamic input consumed by1143E0.
            f->native_actors[0][0xac]=std::byte{};
            expect(transaction.ValidateCommit().ok(),"commit preserves native queued C refilter work");
            const auto queued=f->dirty_entries[0];
            put(queued,0x25,static_cast<unsigned char>(0xb));
            expect(!transaction.ValidateCommit().ok() && !transaction.released(),"dirty set and C membership flags must agree");
            put(queued,0x25,static_cast<unsigned char>(0x1b));
            f->dirty_entries[0]=f->original[0];
            expect(!transaction.ValidateCommit().ok() && !transaction.released(),"retained B cannot remain in the native dirty queue");
            f->dirty_entries[0]=queued;
            const auto saved_next=f->dirty_next[0];f->dirty_next[0]=0;
            expect(!transaction.ValidateCommit().ok() && !transaction.released(),"cyclic native dirty chains reject without retiring B");
            f->dirty_next[0]=saved_next;
            const auto dirty_entries=f->dirty_entries;const auto dirty_buckets=f->dirty_buckets;const auto dirty_next=f->dirty_next;
            const auto dirty_count=word(dirty,0x34);
            const auto contact_snapshot=f->contact;const auto element_map_snapshot=f->map_entries;

            const auto active=word(scene,0x70),used=word(pool,0x224);
            if(with_sap) {
                const auto pairs=pointer(sap,0x180),saved=pointer(pairs,8);
                put(pairs,8,scene);
                expect(!transaction.ValidateCommit().ok() && !transaction.released() && word(pool,0x224)==used,
                    "C-only IDs never admit an unknown SAP interaction or release B");
                if(b_count) {
                    put(pairs,8,f->original[0]);
                    expect(!transaction.ValidateCommit().ok() && !transaction.released() && word(pool,0x224)==used,
                        "even dormant SAP references cannot retain private B through retirement");
                }
                put(pairs,8,saved);
            }
            put(scene,0x70,1u);
            expect(!transaction.ValidateCommit().ok() && !transaction.released()
                && word(pool,0x224)==used,
                "marker commit admission rejects active native work before releasing complete B");
            put(scene,0x70,active);
            expect(transaction.ValidateCommit().ok() && word(pool,0x224)==used,
                "marker commit admission is read only and accepts settled C");
            expect(transaction.Commit().ok() && transaction.released() && word(pool,0x224)==2,
                "irreversible commit returns each detached B slot once while retaining live C");
            expect(transaction.Commit().ok() && word(pool,0x224)==2,"commit receipt does not double-return B slots");
            expect(f->dirty_entries==dirty_entries && f->dirty_buckets==dirty_buckets && f->dirty_next==dirty_next
                && word(dirty,0x34)==dirty_count && *reinterpret_cast<unsigned char*>(queued+0x25)==0x1b
                && *reinterpret_cast<unsigned char*>(queued+0x26)==5,
                "commit preserves pending native refilter queue and flags exactly");
            expect(f->contact==contact_snapshot && f->map_entries==element_map_snapshot,"commit never edits native contact, solver descriptor or element map bytes");
        } else {
            expect(transaction.Undo().ok() && transaction.released(),"native marker cancellation reconstructs complete B after publication or execution");
            auto recovered=capture();
            if(with_sap)expect(recovered->broadphases[0]==b->broadphases[0],
                "B undo independently recovers endpoint order, bounds, pending admission and external pair pointers");
            if(with_sap && fault==0)expect(pointer(sap,0x180)==address(f->relocated_pairs) && word(sap,0x194)==128,
                "B undo keeps validated current native allocation and never republishes retired pair backing");
            if(with_aggregate && fault!=6 && !injected)for(unsigned i=1;i<4;++i)
                expect(pointer(address(f->id_trackers[i]),8)==address(f->relocated_id_free[i])
                    && word(address(f->id_trackers[i]),0)==b->broadphases[0].ids[i].next,
                    "B undo restores ID continuation through current relocated backing");
            for(unsigned actor=0;actor<2;++actor) {
                expect(recovered->actors[0][actor].interaction_count==b->actors[0][actor].interaction_count,
                    "independent actor-list reads recover B membership count");
                for(unsigned i=0;i<b->actors[0][actor].interaction_count;++i) {
                    auto actual=recovered->actors[0][actor].interactions[i];const auto& expected=b->actors[0][actor].interactions[i];
                    actual.header[0x27]=expected.header[0x27];
                    expect(actual==expected,"independent native scene/map/actor/filter reads recover B including inverse order");
                }
            }
            expect(word(pool,0x224)==b_count && word(scene,0x60)==b_count && word(map,0x34)==b_count,
                "complete B recovery leaves no owned A/C slot or extra registration");
            expect(transaction.Undo().ok() && word(pool,0x224)==b_count,"recovery receipt is idempotent");
        }
        if(with_sap)expect(f->anchor==original_anchor && f->anchor_core==original_anchor_core,
            "native world anchor stays unchanged through publication execution recovery and commit");
        if(with_aggregate)expect(f->aggregate==original_aggregate && f->aggregate_self==original_self && f->aggregate_slots==original_slots,
            "publication execution undo and commit preserve empty aggregate native owners and caches");
    }
    {
        // Exercise the actual shipped registration wrapper/base virtual on
        // private non-ticking component memory; no scheduler or game process.
        const auto base=reinterpret_cast<std::uintptr_t>(game);
        std::array<std::byte,0x500> actor{};std::array<std::byte,0x1d0> component{};
        const auto object=reinterpret_cast<std::uintptr_t>(component.data());
        const auto put=[](std::uintptr_t p,const auto& value){std::memcpy(reinterpret_cast<void*>(p),&value,sizeof(value));};
        put(object,base+0x3865fb0);put(object+0x188,3u);put(object+0x11c,std::uint8_t{4});
        ReplayStageVisibility row{};row.kind=ReplayStageVisibility::Kind::Mesh;row.actor=reinterpret_cast<std::uintptr_t>(actor.data());
        row.enabled=1;row.fade_frames=10;row.component_count=1;row.values.alpha=0.5f;
        auto& c=row.components[0];c.object=object;c.type=base+0x3865fb0;c.flags=0x20000003;
        c.registration_entry=base+0x1d58ac0;c.base_registration=true;c.primary_tick_flags=4;
        const auto prefix=component;
        expect(ReplayStageStorageTestAccess::Write(base,{&row,1})
            && *reinterpret_cast<unsigned*>(object+0x188)==0x20000003,
            "native stage registration wrapper restores B logical registration");
        expect(!std::memcmp(prefix.data()+0x110,component.data()+0x110,0x58),
            "native dormant registration leaves full primary tick prefix unchanged");
        c.flags=3;
        expect(ReplayStageStorageTestAccess::Write(base,{&row,1})
            && component==prefix && !*reinterpret_cast<std::uintptr_t*>(base+0x4392300),
            "native A undo preserves all component bytes and clears registration context");
        for(unsigned flag:{2u,0x40u}) {
            put(object+0x11c,std::uint8_t(4|flag));c.primary_tick_flags=4|flag;c.flags=0x20000003;
            const auto before=component;
            expect(!ReplayStageStorageTestAccess::Write(base,{&row,1}) && component==before,
                "possible or registered tick rejects native publication before mutation");
        }
        put(object+0x11c,std::uint8_t{4});c.primary_tick_flags=4;
        auto* context=reinterpret_cast<std::uintptr_t*>(base+0x4392300);*context=object;
        const auto before=component;
        expect(!ReplayStageStorageTestAccess::Write(base,{&row,1}) && component==before && *context==object,
            "active native registration context rejects without disturbing B");
        *context=0;
    }
    FreeLibrary(game);FreeLibrary(physics);
}
