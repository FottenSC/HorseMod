// Continue the existing shipped-PhysX fixture in its now-empty native scene.
// Independent authored control, real constructors/removal/recovery/solver;
// no snapshot bytes or expected observations are installed into either body.
auto* initial_control_scene=reinterpret_cast<void*(*)(void*,const void*)>(base+0x368f0)(sdk,desc.data());
auto* initial_body=reinterpret_cast<void*(*)(void*,const float*)>(base+0x36cc0)(sdk,pose);
auto* initial_control=reinterpret_cast<void*(*)(void*,const float*)>(base+0x36cc0)(sdk,pose);
expect(initial_control_scene && initial_body && initial_control,"construct independent initial kinematic bodies");
if(!initial_control_scene || !initial_body || !initial_control)std::exit(2);
const auto initial_aggregates=make_aggregates(initial_control_scene);
for(auto* actor:{initial_body,initial_control}) {
    reinterpret_cast<void(*)(void*,float)>(base+0x3d7c0)(actor,3.25f);
    const float inertia[3]{2.0f,3.0f,4.0f};
    reinterpret_cast<void(*)(void*,const float*)>(base+0x3d7e0)(actor,inertia);
    const float centre_of_mass[7]{0,0.38268343f,0,0.9238795f,0.1234f,1.2345f,-0.321f};
    reinterpret_cast<void(*)(void*,const float*)>(base+0x3a390)(actor,centre_of_mass);
    reinterpret_cast<void(*)(void*,unsigned char,bool)>(base+0x3da30)(actor,1,true);
    reinterpret_cast<void(*)(void*,unsigned char,bool)>(base+0x3da30)(actor,2,true);
    for(const unsigned char flags:{shape_flags,disabled_flags}) {
        auto* owned_shape=reinterpret_cast<void*(*)(void*,void*,const void*,void*,const unsigned char*)>(base+0x10130)
            (nullptr,actor,&box,material,&flags);
        expect(owned_shape!=nullptr,"initial kinematic owns independent simulation and disabled shapes");
        if(!owned_shape)std::exit(2);
        const float shape_pose[7]{0,0,0.38268343f,0.9238795f,0.2f,-0.1f,0.3f};
        const unsigned simulation_filter[4]{0xffff,0x20,1001,0x40000010};
        const unsigned query_filter[4]{2001,0x20,0x40,0x40000013};
        reinterpret_cast<void(*)(void*,const void*)>(base+0x55370)(owned_shape,shape_pose);
        reinterpret_cast<void(*)(void*,const void*)>(base+0x55490)(owned_shape,simulation_filter);
        reinterpret_cast<void(*)(void*,const void*)>(base+0x554d0)(owned_shape,query_filter);
        reinterpret_cast<void(*)(void*,float)>(base+0x55800)(owned_shape,0.043f);
        reinterpret_cast<void(*)(void*,float)>(base+0x55830)(owned_shape,-0.007f);
    }
}
add_actor(scene,initial_body);add_actor(initial_control_scene,initial_control);
step_scene(scene);step_scene(initial_control_scene);
const auto initial_read=[](std::uintptr_t p,auto& value){if(!p)return false;std::memcpy(&value,reinterpret_cast<void*>(p),sizeof(value));return true;};
const auto initial_address=reinterpret_cast<std::uintptr_t>(initial_body);
alignas(16) std::array<std::byte,0xe8> initial_before{},initial_after{};
values(initial_before.data(),initial_body);
expect(initial_before[0x9c]==std::byte{3},"native initial body has exact T617 rigid flags3");
expect(ReplayGroundBodyAdmission::Flags(initial_read,initial_address,3,std::to_integer<unsigned>(initial_before[0x10])),
    "production admits native initial kinematic only without a target or buffered writes");
auto* reconstructed_scene=reinterpret_cast<void*(*)(void*,const void*)>(base+0x368f0)(sdk,desc.data());
void* reconstructed_body{};
expect(reconstructed_scene!=nullptr,"construct separate native reconstruction scene");
if(!reconstructed_scene)std::exit(2);
{
    using Horse::Deterministic::ReplayGroundInitialBodyState;
    using Horse::Deterministic::ReplayGroundPrivateBody;
    const auto get_values=[&](std::uintptr_t actor,auto& output) {
        values(output.data(),reinterpret_cast<void*>(actor));return true;
    };
    ReplayGroundInitialBodyState captured;
    expect(captured.Capture(initial_read,get_values,initial_address,{1001,2001}),"capture owned historical initial body and shape semantics");
    if(!captured.ready)std::exit(2);
    const float different_pose[7]{0,0,0,1,3,4,5};
    auto* fresh=reinterpret_cast<void*(*)(void*,const float*)>(base+0x36cc0)(sdk,different_pose);
    if(!fresh)std::exit(2);
    for(const unsigned char flags:{shape_flags,disabled_flags}) {
        auto* fresh_shape=reinterpret_cast<void*(*)(void*,void*,const void*,void*,const unsigned char*)>(base+0x10130)
            (nullptr,fresh,&box,material,&flags);
        if(!fresh_shape)std::exit(2);
        const unsigned fresh_simulation_filter[4]{0xffff,0x20,1002,0x40000010};
        const unsigned fresh_query_filter[4]{2001,0x20,0x40,0x40000013};
        reinterpret_cast<void(*)(void*,const void*)>(base+0x55490)(fresh_shape,fresh_simulation_filter);
        reinterpret_cast<void(*)(void*,const void*)>(base+0x554d0)(fresh_shape,fresh_query_filter);
    }
    const auto fresh_address=reinterpret_cast<std::uintptr_t>(fresh);
    const auto get_scene=[&](std::uintptr_t actor){return reinterpret_cast<std::uintptr_t(*)(std::uintptr_t)>(base+0x3c8e0)(actor);};
    ReplayGroundPrivateBody private_owner;
    expect(private_owner.Capture(initial_read,get_scene,fresh_address,base,0),"fresh body semantic target stays private");
    unsigned mismatch{};
    expect(!captured.Matches(get_values,fresh_address,&mismatch),"constructor completion does not restore historical physics values");
    const auto private_write=[](auto p,const auto& value){std::memcpy(reinterpret_cast<void*>(p),&value,sizeof(value));return true;};
    alignas(16) std::array<std::byte,0xa0> untouched{},rejected{};
    const auto first_shape=private_owner.shapes[0];
    reinterpret_cast<void*(*)(void*,std::uintptr_t)>(base+0x88a0)(untouched.data(),first_shape);
    expect(!captured.shapes.Install(initial_read,private_write,get_scene,private_owner,{1001,2001},&mismatch),
        "shape rebinding rejects historical component index as fresh identity");
    expect(!captured.shapes.Install(initial_read,private_write,get_scene,private_owner,{1002,2002},&mismatch),
        "shape rebinding rejects a different owning actor identity");
    auto foreign_material=captured.shapes;
    foreign_material.rows[0].materials[0]^=8;
    expect(!foreign_material.Install(initial_read,private_write,get_scene,private_owner,{1002,2001},&mismatch),
        "shape restoration rejects foreign native material ownership");
    auto foreign_geometry=captured.shapes;
    foreign_geometry.rows[0].values[8]^=std::byte{1};
    expect(!foreign_geometry.Install(initial_read,private_write,get_scene,private_owner,{1002,2001},&mismatch),
        "shape restoration rejects changed authored geometry");
    reinterpret_cast<void(*)(void*,float)>(base+0x324e0)(material,0.75f);
    const auto changed_material_accepted=captured.shapes.Install(initial_read,private_write,get_scene,private_owner,{1002,2001},&mismatch);
    expect(!changed_material_accepted,"shape restoration rejects in-place shared material mutation");
    if(changed_material_accepted)std::exit(2);
    reinterpret_cast<void(*)(void*,float)>(base+0x324e0)(material,0.5f);
    reinterpret_cast<void*(*)(void*,std::uintptr_t)>(base+0x88a0)(rejected.data(),first_shape);
    expect(untouched==rejected,"shape binding and dependency failures perform no partial shape writes");
    const auto restored=captured.Install(initial_read,private_write,get_scene,get_values,private_owner,&mismatch,{1002,2001});
    if(!restored)std::fprintf(stderr,"Historical private body state rejected field=%x control=%x buffered=%x scene=%llx sim=%llx shapes=%u first_shape_control=%x\n",
        mismatch,read32(fresh,0x68),read32(fresh,0x17c),static_cast<unsigned long long>(readptr(fresh,0x60)),
        static_cast<unsigned long long>(readptr(fresh,0x80)),private_owner.shape_count,
        private_owner.shape_count?read32(reinterpret_cast<void*>(private_owner.shapes[0]),0x38):0);
    expect(restored,"restore exact captured mass inertia pose flags and sleep state through production boundary");
    if(!restored)std::exit(2);
    const auto source_shapes=readptr(initial_body,0x28);
    for(unsigned i=0;i<private_owner.shape_count;++i) {
        alignas(16) std::array<std::byte,0xa0> source_shape{},fresh_shape{};
        reinterpret_cast<void*(*)(void*,void*)>(base+0x88a0)(source_shape.data(),reinterpret_cast<void*>(readptr(reinterpret_cast<void*>(source_shapes),i*8)));
        reinterpret_cast<void*(*)(void*,void*)>(base+0x88a0)(fresh_shape.data(),reinterpret_cast<void*>(private_owner.shapes[i]));
        // UObject identities differ by design. Shape pose, policy, offsets and
        // flags must be owned historical state, not the current factory default.
        const unsigned destination_component=1002;
        std::memcpy(source_shape.data()+0x58,&destination_component,4);
        for(unsigned offset=0x34;offset<0x7a;++offset)if(source_shape[offset]!=fresh_shape[offset]) {
            std::fprintf(stderr,"Reconstructed shape state mismatch shape=%u property=%x\n",i,offset);std::exit(2);
        }
    }
    std::cout<<"Native historical shapes restored=true typed_component_rebinding=true owner_identity_validated=true material_values_validated=true dependency_rejections_atomic=true"<<std::endl;
    std::cout<<"Native private historical body state exact=true scene_published=false expected_state_installed=false"<<std::endl;
    reconstructed_body=fresh;
    add_actor(reconstructed_scene,reconstructed_body);
    expect(!private_owner.Validate(initial_read,get_scene),"scene insertion ends private-body admission");
}
ReplayPhysicsRemovalOperation initial_operation;
std::array<std::uintptr_t,1> initial_selected{initial_address};
const auto initial_callbacks=events.calls;
const auto initial_prepared=initial_operation.Prepare(base,reinterpret_cast<std::uintptr_t>(scene),initial_selected);
if(!initial_prepared)std::fprintf(stderr,"Initial kinematic preparation rejected check=%s\n",initial_operation.failed_check());
expect(initial_prepared,"production removal prepares actual initial kinematic body");
if(!initial_prepared)std::exit(2);
using InitialResult=ReplayPhysicsRemovalOperation::Result;
auto initial_result=InitialResult::Pending;
for(unsigned poll=0;poll<128 && initial_result==InitialResult::Pending;++poll)initial_result=initial_operation.Advance();
if(initial_result!=InitialResult::Complete) {
    std::fprintf(stderr,"Initial kinematic removal failed check=%s\n",initial_operation.failed_check());std::exit(2);
}
initial_result=InitialResult::Pending;
for(unsigned poll=0;poll<128 && initial_result==InitialResult::Pending;++poll)initial_result=initial_operation.Recover();
if(initial_result!=InitialResult::Complete || !initial_operation.ReleaseRecoveredStorage()) {
    std::fprintf(stderr,"Initial kinematic recovery failed check=%s\n",initial_operation.failed_check());std::exit(2);
}
values(initial_after.data(),initial_body);
expect(initial_before==initial_after && events.calls==initial_callbacks,
    "initial kinematic recovery preserves complete public body values without callback loss or synthesis");
expect(ReplayGroundBodyAdmission::Flags(initial_read,initial_address,3,std::to_integer<unsigned>(initial_after[0x10])),
    "native re-add preserves absence of initial kinematic target");
expect(readptr(reinterpret_cast<void*>(readptr(initial_body,0)),0x1b0)==base+0x39bd0,
    "verify native authored kinematic target setter");
for(unsigned tick=0;tick<120;++tick) {
    if(tick==0 || tick==10) {
        const float authored_target[7]{0,0,0,1,0.25f+tick*0.1f,0,0};
        for(auto* actor:{initial_body,initial_control,reconstructed_body})
            reinterpret_cast<void(*)(void*,const float*)>(base+0x39bd0)(actor,authored_target);
        expect(!ReplayGroundBodyAdmission::Flags(initial_read,initial_address,3,0),
            "production rejects an actual newly authored pending kinematic target");
    }
    if(tick==20)for(auto* actor:{initial_body,initial_control,reconstructed_body}) {
        reinterpret_cast<void(*)(void*,unsigned char,bool)>(base+0x3da30)(actor,1,false);
        // NpRigidDynamic v130=3AB40 writes +15C linear velocity, then
        // dispatches E0890 and 3B4B0 with the authored autowake argument.
        expect(readptr(reinterpret_cast<void*>(readptr(actor,0)),0x130)==base+0x3ab40,
            "verify native authored linear velocity setter");
        const float authored_velocity[3]{0.5f,0,0};
        reinterpret_cast<void(*)(void*,const float*,bool)>(base+0x3ab40)(actor,authored_velocity,true);
    }
    if(tick==21 || tick==60)for(auto* actor:{initial_body,initial_control,reconstructed_body}) {
        // Shipped NpRigidDynamic slots148/150 pass linear/angular impulses
        // to3BD00 with force mode1; these consume inverse mass and inertia.
        expect(readptr(reinterpret_cast<void*>(readptr(actor,0)),0x148)==base+0x3ad60
            && readptr(reinterpret_cast<void*>(readptr(actor,0)),0x150)==base+0x3aee0,
            "verify native linear and angular impulse entry points");
        const float linear_impulse[3]{3.25f,-0.5f,1.25f};
        const float angular_impulse[3]{0.25f,0.5f,-0.75f};
        reinterpret_cast<void(*)(void*,const float*,int,bool)>(base+0x3ad60)(actor,linear_impulse,1,true);
        reinterpret_cast<void(*)(void*,const float*,int,bool)>(base+0x3aee0)(actor,angular_impulse,1,true);
    }
    step_scene(scene);step_scene(initial_control_scene);step_scene(reconstructed_scene);
    alignas(16) std::array<std::byte,0xe8> actual{},independent{},reconstructed{};
    values(actual.data(),initial_body);values(independent.data(),initial_control);values(reconstructed.data(),reconstructed_body);
    if(tick==0 || tick==20) {
        const auto sim=readptr(initial_body,0x80),sc=readptr(reinterpret_cast<void*>(sim),0x40);
        const auto count=read32(reinterpret_cast<void*>(sc),0x28);
        const auto capacity=read32(reinterpret_cast<void*>(sc),0x2c)&0x7fffffffu;
        const auto prefix=read32(reinterpret_cast<void*>(sc),0x30);
        const auto index=read32(reinterpret_cast<void*>(sim),0xb8);
        const auto storage=readptr(reinterpret_cast<void*>(sc),0x20);
        const auto flags=std::to_integer<unsigned>(actual[0x9c]);
        expect(index<count && bool(flags&1)==(index<prefix),
            "shipped active body list places kinematic bodies in prefix and dynamic bodies in suffix");
        expect(Sc6ReplayWorldState::PhysicsActiveBodyIndexValid(flags,count,capacity,prefix,index,storage),
            "production capture accepts native active dynamic suffix after kinematic transition");
        expect(!Sc6ReplayWorldState::PhysicsActiveBodyIndexValid(flags^1,count,capacity,prefix,index,storage),
            "production capture rejects body flag and active-list partition disagreement");
        auto shape=readptr(reinterpret_cast<void*>(sim),0x38);unsigned shapes{};
        while(shape) {
            unsigned attributes{};std::array<unsigned,4> filter{};
            reinterpret_cast<void(*)(void*,unsigned*,void*)>(base+0x1143e0)(reinterpret_cast<void*>(shape),&attributes,filter.data());
            expect(attributes==(tick==0?0x11u:1u),"native shape attributes distinguish kinematic and dynamic body");
            expect(ReplayPhysicsSapImage::ShapeAttributesSupported(attributes,filter[3]),
                "production SAP capture accepts native ordinary dynamic shape attributes");
            expect(!ReplayPhysicsSapImage::ShapeAttributesSupported(attributes,7u<<21)
                && !ReplayPhysicsSapImage::ShapeAttributesSupported(attributes|0x20,filter[3]),
                "SAP capture keeps excluded category and trigger restrictions");
            ++shapes;shape=readptr(reinterpret_cast<void*>(shape),8);
        }
        expect(shapes==2,"native SAP attribute regression visits both owned shapes");
    }
    if(std::memcmp(actual.data()+0x28,independent.data()+0x28,0xe0-0x28)) {
        std::fprintf(stderr,"Initial kinematic continuation mismatch tick=%u\n",tick+1);std::exit(2);
    }
    for(unsigned offset=0x28;offset<0xe0;++offset)if(reconstructed[offset]!=independent[offset]) {
        std::fprintf(stderr,"Reconstructed initial body continuation mismatch tick=%u property=%x actual=%x control=%x\n",
            tick+1,offset,std::to_integer<unsigned>(reconstructed[offset]),std::to_integer<unsigned>(independent[offset]));
        std::exit(2);
    }
}
expect(events.calls==initial_callbacks,"initial kinematic continuation preserves callback-ineligible ownership");
std::cout<<"Native initial kinematic removal recovered=true independent_ticks=120 future_targets=2 dynamic_transition=true expected_state_installed=false"<<std::endl;
std::cout<<"Native reconstructed initial body independent_ticks=120 future_targets=2 dynamic_transition=true linear_impulses=2 angular_impulses=2 expected_state_installed=false"<<std::endl;
reinterpret_cast<void(*)(void*)>(base+0x40440)(reconstructed_scene);
reinterpret_cast<void(*)(void*)>(base+0x396a0)(reconstructed_body);
remove_actor(scene,initial_body,false);complete(0);
reinterpret_cast<void(*)(void*)>(base+0x396a0)(initial_body);
reinterpret_cast<void(*)(void*)>(base+0x40440)(initial_control_scene);
release_aggregates(initial_aggregates);
reinterpret_cast<void(*)(void*)>(base+0x396a0)(initial_control);

// Native conversion probe: authored kinematic pair becomes a dynamic contact.
// Read the SAP cache before/after; compare independent subsequent simulation.
{
    auto* conversion_control=reinterpret_cast<void*(*)(void*,const void*)>(base+0x368f0)(sdk,desc.data());
    std::array<void*,2> candidate_bodies{},control_bodies{};
    for(unsigned side=0;side<2;++side) {
        const float pair_pose[7]{0,0,0,1,float(side)*0.5f,0,0};
        for(unsigned trial=0;trial<2;++trial) {
            auto*& actor=trial?control_bodies[side]:candidate_bodies[side];
            actor=reinterpret_cast<void*(*)(void*,const float*)>(base+0x36cc0)(sdk,pair_pose);
            expect(actor!=nullptr,"native conversion body factory");if(!actor)std::exit(2);
            reinterpret_cast<void(*)(void*,unsigned char,bool)>(base+0x3da30)(actor,1,true);
            auto* shape=reinterpret_cast<void*(*)(void*,void*,const void*,void*,const unsigned char*)>(base+0x10130)
                (nullptr,actor,&box,material,&shape_flags);
            expect(shape!=nullptr,"native conversion shape factory");if(!shape)std::exit(2);
            add_actor(trial?conversion_control:scene,actor);
        }
    }
    step_scene(scene);step_scene(conversion_control);
    const auto conversion_scene=readptr(candidate_bodies[0],0x80)?readptr(reinterpret_cast<void*>(readptr(candidate_bodies[0],0x80)),0x40):0;
    const auto conversion_sap=readptr(reinterpret_cast<void*>(readptr(reinterpret_cast<void*>(conversion_scene),0x728)),0x100);
    const auto inspect=[&](const char* phase) {
        const auto pairs=readptr(reinterpret_cast<void*>(conversion_sap),0x180);
        std::cout<<"Native conversion "<<phase<<" markers="<<read32(reinterpret_cast<void*>(conversion_scene),0x60)
            <<" contacts="<<read32(reinterpret_cast<void*>(conversion_scene),0x40)
            <<" sap_pairs="<<read32(reinterpret_cast<void*>(conversion_sap),0x190)
            <<" cache="<<std::hex<<(read32(reinterpret_cast<void*>(conversion_sap),0x190)?readptr(reinterpret_cast<void*>(pairs),8):0)<<std::dec<<std::endl;
    };
    inspect("kinematic");
    expect(read32(reinterpret_cast<void*>(conversion_scene),0x60)==1 && read32(reinterpret_cast<void*>(conversion_sap),0x190)==1,
        "native authored kinematic pair establishes one suppressed marker");
    const auto cached_marker=readptr(reinterpret_cast<void*>(readptr(reinterpret_cast<void*>(conversion_sap),0x180)),8);
    for(auto* actor:{candidate_bodies[0],control_bodies[0]}) {
        reinterpret_cast<void(*)(void*,unsigned char,bool)>(base+0x3da30)(actor,1,false);
        const float velocity[3]{-2,0,0};reinterpret_cast<void(*)(void*,const float*,bool)>(base+0x3ab40)(actor,velocity,true);
    }
    for(unsigned tick=0;tick<120;++tick) {
        step_scene(scene);step_scene(conversion_control);
        if(tick==0 || tick==119)inspect(tick?"continued":"converted");
        if(tick==0) {
            expect(read32(reinterpret_cast<void*>(conversion_scene),0x60)==0 && read32(reinterpret_cast<void*>(conversion_scene),0x40)==1
                && readptr(reinterpret_cast<void*>(readptr(reinterpret_cast<void*>(conversion_sap),0x180)),8)==cached_marker,
                "native conversion preserves the dormant SAP marker cache while registering a contact");
            const auto pool=readptr(reinterpret_cast<void*>(conversion_scene),0x1058)+0xdf0;
            bool retired{};auto slot=readptr(reinterpret_cast<void*>(pool),0x230);
            for(unsigned i=0;slot && i<4096;++i){retired|=slot==cached_marker;slot=readptr(reinterpret_cast<void*>(slot),0);}
            expect(retired,"converted native marker cache can name an already freed pool slot");
        }
        for(unsigned side=0;side<2;++side) {
            alignas(16) std::array<std::byte,0xe8> actual{},independent{};
            values(actual.data(),candidate_bodies[side]);values(independent.data(),control_bodies[side]);
            expect(!std::memcmp(actual.data()+0x28,independent.data()+0x28,0xe0-0x28),"native refilter conversion independent continuation");
        }
    }
    expect(!read32(reinterpret_cast<void*>(conversion_sap),0x190) && read32(reinterpret_cast<void*>(conversion_scene),0x40)==1,
        "native registered contact can outlive its broadphase pair after independent continuation");
    for(unsigned i=0;i<2;++i){auto* actor=candidate_bodies[i];remove_actor(scene,actor,false);complete(1-i);reinterpret_cast<void(*)(void*)>(base+0x396a0)(actor);}
    reinterpret_cast<void(*)(void*)>(base+0x40440)(conversion_control);
    for(auto* actor:control_bodies)reinterpret_cast<void(*)(void*)>(base+0x396a0)(actor);
}
