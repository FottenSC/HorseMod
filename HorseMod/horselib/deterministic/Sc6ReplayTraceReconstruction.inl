// Native1408D907A uses parts-setting index1 for child traces. Resolve that
// exact factory source from retained A fields; never read A's retired payload.
// This inventory does not publish state or acquire a native strong reference.
struct ChildSource {
    Ref historical; Object* component{}; Object* parts{}; Object* kind{};
    Object* mesh_asset{}; Object* animation_class{};
    unsigned char parts_id{},kind_id{},charge{};
};
const std::byte* RetainedSpan(const std::vector<Image>& images,const void* owner,
    std::size_t offset,std::size_t bytes) const noexcept {
    const auto start=reinterpret_cast<std::uintptr_t>(owner);
    if(!start || !bytes || offset>UINTPTR_MAX-start || bytes>UINTPTR_MAX-start-offset)return nullptr;
    const auto wanted=start+offset;const std::byte* result{};
    for(const auto& image:images) {
        const auto address=reinterpret_cast<std::uintptr_t>(image.address);
        if(wanted<address || wanted-address>image.bytes.size()
            || bytes>image.bytes.size()-(wanted-address))continue;
        if(result)return nullptr;
        result=image.bytes.data()+(wanted-address);
    }
    return result;
}
template<class T> bool RetainedField(const std::vector<Image>& images,
    const void* owner,std::size_t offset,T& value) const noexcept {
    const auto start=reinterpret_cast<std::uintptr_t>(owner);
    if(!start || offset>UINTPTR_MAX-start || sizeof(T)>UINTPTR_MAX-start-offset)return false;
    const auto wanted=start+offset;unsigned matches{};
    for(const auto& image:images) {
        const auto address=reinterpret_cast<std::uintptr_t>(image.address);
        if(wanted<address || wanted-address>image.bytes.size()
            || sizeof(T)>image.bytes.size()-(wanted-address))continue;
        if(++matches!=1)return false;
        std::memcpy(&value,image.bytes.data()+(wanted-address),sizeof(T));
    }
    return matches==1;
}
template<class Accept> Status VisitHistoricalChildSources(Accept accept) const {
    if(!captured_ || thread_!=GetCurrentThreadId())return Fail("child_source_context");
    std::array<Ref,16> visited{};std::size_t count{};
    for(std::size_t i=0;i<dynamic_count_;++i) {
        const auto& d=dynamic_[i];if(d.kind!=DynamicImage::Kind::ChildStrongReferences)continue;
        const auto n=At<int>(const_cast<std::byte*>(d.header.data()),8);
        if(n<0 || n>16 || std::size_t(n)>d.bytes.size()/sizeof(Ref))return Fail("child_source_count");
        const auto root=std::find_if(roots_.begin(),roots_.end(),[&](const auto& r) {
            return reinterpret_cast<std::byte*>(r.component.object)+0x428==d.address;
        });
        if(root==roots_.end() || !Live(root->component) || !Live(root->chara) || !Live(root->manager) || !Live(root->scene))
            return Fail("child_source_root");
        if(!n)continue;
        auto* component=root->component.object;Object* asset{};unsigned char charge{};
        if(!RetainedField(bindings_,component,0x488,asset) || !asset
            || !RetainedField(bindings_,component,0x49c,charge) || charge>2
            || At<Object*>(component,0x488)!=asset || At<unsigned char>(component,0x49c)!=charge
            || !Identify(asset).object)return Fail("child_source_asset");
        auto* list=At<Object*>(asset,0x30);
        if(!list || !Identify(list).object)return Fail("child_source_parts_list");
        const auto parts=At<Array>(list,0x30);
        if(parts.count<0 || parts.count>256 || parts.capacity<parts.count || parts.capacity>256
            || (parts.count&&!parts.data))return Fail("child_source_parts_list");
        for(int entry=0;entry<n;++entry) {
            Ref ref{};std::memcpy(&ref,d.bytes.data()+std::size_t(entry)*sizeof(Ref),sizeof(ref));
            if(!ref.controller || reinterpret_cast<std::uintptr_t>(ref.state)!=reinterpret_cast<std::uintptr_t>(ref.controller)+16
                || count==visited.size() || std::any_of(visited.begin(),visited.begin()+count,[&](const auto& old) {
                    return old.state==ref.state || old.controller==ref.controller;
                }))return Fail("child_source_membership");
            visited[count++]=ref;
            const auto state=std::find_if(states_.begin(),states_.end(),[&](const auto& s) {
                return s.ref.state==ref.state && s.ref.controller==ref.controller;
            });
            if(state==states_.end())return Fail("child_source_state");
            ChildSource source{};source.historical=ref;source.component=component;source.charge=charge;
            if(!RetainedField(values_,ref.state,0,source.parts_id) || !RetainedField(values_,ref.state,1,source.kind_id))
                return Fail("child_source_tags");
            for(int p=0;p<parts.count;++p) {
                auto* candidate=At<Object*>(parts.data,std::size_t(p)*8);
                if(!candidate)continue;
                if(!Identify(candidate).object)return Fail("child_source_parts_generation");
                if(At<unsigned char>(candidate,0x30)==source.parts_id) {
                    if(source.parts)return Fail("child_source_parts_ambiguous");
                    source.parts=candidate;
                }
            }
            if(!source.parts)return Fail("child_source_parts_missing");
            const auto settings=At<Array>(source.parts,0x50);
            if(settings.count<2 || settings.count>16 || settings.capacity<settings.count || settings.capacity>16 || !settings.data)
                return Fail("child_source_settings");
            auto* setting=settings.data+0x18;
            auto* kinds=At<Object*>(setting,0x10);
            if(!kinds || !Identify(kinds).object)return Fail("child_source_kinds");
            auto table=At<Array>(kinds,0x30+std::size_t(charge)*16);
            if(charge && !table.count)table=At<Array>(kinds,0x30);
            if(table.count<0 || table.count>256 || table.capacity<table.count || table.capacity>256
                || (table.count&&!table.data))return Fail("child_source_kind_table");
            unsigned kind_matches{};
            for(int k=0;k<table.count;++k)if(At<unsigned char>(table.data,std::size_t(k)*16)==source.kind_id) {
                if(++kind_matches!=1)return Fail("child_source_kind_ambiguous");
                source.kind=At<Object*>(table.data,std::size_t(k)*16+8);
            }
            if(!source.kind || !Identify(source.kind).object)return Fail("child_source_kind_missing");
            auto* mesh_data=At<Object*>(source.kind,0x30);
            if(!mesh_data || !Identify(mesh_data).object)return Fail("child_source_mesh_data");
            source.mesh_asset=At<Object*>(mesh_data,0x30);source.animation_class=At<Object*>(mesh_data,0x38);
            Object* saved_mesh{};Object* saved_class{};void* saved_curve{};void* saved_effect{};void* saved_delta{};
            int saved_slot{},saved_bones{};unsigned saved_width{};unsigned char saved_scale{};
            if(!RetainedField(bindings_,state->mesh.object,0x910,saved_mesh) || saved_mesh!=source.mesh_asset
                || !source.mesh_asset || !Identify(source.mesh_asset).object
                || !source.animation_class || !Identify(source.animation_class).object
                || !RetainedField(bindings_,state->animation.object,0x10,saved_class) || saved_class!=source.animation_class
                || !RetainedField(values_,ref.state,0x50,saved_width) || saved_width!=At<unsigned>(setting,4)
                || !RetainedField(values_,ref.state,0x54,saved_scale) || saved_scale!=At<unsigned char>(setting,0)
                || !RetainedField(bindings_,ref.state,0x58,saved_curve) || saved_curve!=At<void*>(setting,8)
                || !RetainedField(bindings_,ref.state,0x60,saved_effect) || saved_effect!=At<void*>(source.kind,0x50)
                || !RetainedField(bindings_,ref.state,0x68,saved_delta) || saved_delta!=At<void*>(source.kind,0x58)
                || !RetainedField(values_,ref.state,0xb0,saved_slot) || saved_slot!=At<int>(source.kind,0x48)
                || !RetainedField(values_,ref.state,0x20,saved_bones) || saved_bones!=At<int>(mesh_data,0x50))
                return Fail("child_source_configuration");
            const auto status=accept(source);if(!status.ok())return status;
        }
    }
    return Status::success();
}
