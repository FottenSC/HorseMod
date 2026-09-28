// A owns immutable CPU render metadata and private GPU images. The operation
// owns its separate, mutable publication maps and complete B undo. No native
// release is allowed from a checkpoint's arbitrary-thread shared_ptr deleter.
struct Sc6ReplayParticleCopy::CapturedImage
{
    CaptureIdentity identity;
    std::uintptr_t base{}, world{}, scene{}, system{}, pool{}, view_state{};
    DWORD owner_thread{};
    HMODULE module{};
    std::size_t reservation{};
    ReplayCaptureAccounting::Reservation accounting;
    std::array<Texture, 6> images;
    std::array<ReplayGpuPackedImage::Handle,4> packed;
    std::array<Texture, 6> sources;
    std::array<void*, 6> wrappers{};
    LightingImage lighting;
    VisibilityImage visibility;
    std::array<History, 3> histories;
    Witness witness;
    LocalFields local_fields;
    CapturedImage* next{};
    bool poisoned{};

    static inline std::atomic<CapturedImage*> retired{};
    static inline ReplayCaptureAccounting ledger;
    // RHI-thread-only hint. It retains no checkpoint/native owner. Sharing
    // requires a live owner plus a fresh complete GPU equality comparison.
    static inline std::weak_ptr<const CapturedImage> last_sealed;
    static void Queue(const CapturedImage* immutable) noexcept
    {
        auto* image = const_cast<CapturedImage*>(immutable);
        auto* head = retired.load(std::memory_order_acquire);
        do { image->next = head; }
        while (!retired.compare_exchange_weak(head, image, std::memory_order_release, std::memory_order_acquire));
    }
    bool ReleaseNative() noexcept
    {
        if (poisoned || owner_thread != GetCurrentThreadId()) return false;
        __try {
            const auto release = reinterpret_cast<void(*)(void**)>(base + 0x11b9260);
            for (auto& row : lighting.uniforms) if (row.value) { release(&row.value); row.value = nullptr; }
            if (visibility.material_uniform) { release(&visibility.material_uniform); visibility.material_uniform = nullptr; }
            if (visibility.material_scene_uniform) { release(&visibility.material_scene_uniform); visibility.material_scene_uniform = nullptr; }
            while (visibility.leases) {
                release(&visibility.queries[visibility.leases - 1]);
                visibility.queries[visibility.leases - 1] = nullptr;
                --visibility.leases;
            }
            for (auto& history : histories) if (history.a) {
                reinterpret_cast<int(*)(void*)>(base + 0x146b380)(reinterpret_cast<void*>(history.a));
                history.a = 0;
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            // A fault inside a native decrement has unknown side effects.
            // Keep its module, memory and accounting; never retry that call.
            poisoned = true;
            return false;
        }
    }
};

bool Sc6ReplayParticleCopy::PrepareImageEquality(std::size_t budget)
{
    image_capture_budget_=budget;
    auto basis=CapturedImage::last_sealed.lock();
    if(!basis || basis->poisoned || !basis->accounting.registered
        || basis->owner_thread!=GetCurrentThreadId() || basis->base!=base_
        || basis->world!=world_ || basis->pool!=pool_)basis.reset();
    if(basis)for(unsigned i=0;i<6;++i)if(basis->sources[i].Get()!=sources_[i].Get()){basis.reset();break;}
    if(witness_.bytes>budget || ReplayGpuImageEquality::reservation_bytes>budget-witness_.bytes)return false;
    witness_.bytes+=ReplayGpuImageEquality::reservation_bytes;
    image_equality_basis_=std::move(basis);
    image_equality_=std::make_unique<ReplayGpuImageEquality>();
    ReplayGpuImageEquality::Images candidate,retained;
    for(unsigned i=0;i<4;++i){candidate[i]=images_[i];retained[i]=image_equality_basis_ && image_equality_basis_->images[i]
        ?image_equality_basis_->images[i]:images_[i];}
    return SUCCEEDED(image_equality_->Prepare(device_.Get(),candidate,retained,ReplayGpuImageEquality::reservation_bytes));
}

bool Sc6ReplayParticleCopy::FinishImageEquality() noexcept
{
    if(!image_equality_)return true;
    ReplayGpuImageEquality::TileResult tiles;
    if(!image_equality_->ReadTiles(context_.Get(),completion_,tiles))return false;
    witness_.immutable_image_compared=true;
    for(unsigned i=0;i<4;++i)for(auto flag:tiles.nonuniform[i])witness_.immutable_image_nonuniform_tiles[i]+=flag;
    // Drop comparison SRVs before replacing an image; otherwise an obsolete
    // private allocation could survive behind a credited shared image.
    image_equality_.reset();
    for(unsigned i=0;i<4;++i)if(tiles.equal[i] && image_equality_basis_ && image_equality_basis_->images[i]) {
        images_[i]=image_equality_basis_->images[i];
        witness_.immutable_image_shared_mask|=1u<<i;
        witness_.immutable_image_shared_bytes+=std::size_t(1024)*1024*pixel_bytes[i];
    }
    if(witness_.diagnostic_readbacks)return true;
    bool packed=false;
    // The capture owner excludes all other producers. The original budget
    // remains valid; every provisional allocation is charged before creation.
    for(unsigned i=0;i<4;++i)if(const auto bytes=ReplayGpuPackedImage::RequiredBytes(i,tiles.nonuniform[i])) {
        if(witness_.bytes>image_capture_budget_ || bytes>image_capture_budget_-witness_.bytes)return false;
        witness_.bytes+=bytes;
        try {packed_images_[i]=std::make_shared<ReplayGpuPackedImage>();}catch(...){return false;}
        if(FAILED(packed_images_[i]->Prepare(device_.Get(),images_[i].Get(),i,tiles.nonuniform[i],bytes)))return false;
        packed=true;
    }
    if(packed) {
        if(!PrepareTransfer())return false;
        for(const auto& image:packed_images_)if(image && !image->CanSubmitCopies(context_.Get(),completion_))return false;
        for(const auto& image:packed_images_)if(image && FAILED(image->SubmitCopies(context_.Get(),completion_)))return false;
        witness_.phase=Phase::ReadPackedA;
        if(FAILED(completion_.Submit(context_.Get(),request_deadline_)))return false;
    }
    return true;
}

const Sc6ReplayParticleCopy::CaptureIdentity* Sc6ReplayParticleCopy::identity(const CaptureHandle& image) noexcept
{ return image ? &image->identity : nullptr; }
std::size_t Sc6ReplayParticleCopy::captured_bytes(const CaptureHandle& image) noexcept
{ return image ? image->reservation : 0; }
std::size_t Sc6ReplayParticleCopy::reopen_shared_bytes(const CaptureHandle& image) noexcept
{
    // The caller pins this registered immutable owner through loading. Loading
    // borrows these exact COM allocations; it allocates only operation-private
    // B/history storage, never replacement A/source textures. BindTextures
    // verifies all native sources before those private allocations are made.
    if(!image || image->poisoned || !image->accounting.registered) return 0;
    std::size_t shared=image_bytes;
    for(std::size_t i=0;i<image->images.size();++i) {
        if(!image->sources[i])return 0;
        if(image->images[i])shared+=std::size_t(1024)*1024*pixel_bytes[i];
        else if(i<4 && image->packed[i] && image->packed[i]->ready())shared+=image->packed[i]->owned_bytes();
        else return 0;
    }
    for(const auto& history:image->histories) {
        const auto bytes=std::uint64_t(history.descriptor.Width)*history.descriptor.Height*history.pixel_bytes;
        for(auto* texture:{history.source_a.Get(),history.image_a.Get()}) if(texture) {
            if(bytes>image->witness.bytes || shared>image->witness.bytes-bytes)return 0;
            shared+=static_cast<std::size_t>(bytes);
        }
    }
    return shared<=image->reservation && shared<=image->witness.bytes?shared:0;
}
std::size_t Sc6ReplayParticleCopy::shared_capture_bytes() const noexcept
{
    // Begin charges private A and the retained native source image separately.
    // Seal/Load keep both exact COM owners in the immutable capture. Credit
    // each image only after all six identities match; neither lifetime nor
    // native-source storage disappears from the capture reservation.
    if (!captured_) return 0;
    const auto same = [](const auto& operation, const auto& retained) {
        for (std::size_t i = 0; i < operation.size(); ++i)
            if (!operation[i] || operation[i].Get() != retained[i].Get()) return false;
        return true;
    };
    auto shared = image_bytes * unsigned(same(sources_, captured_->sources));
    for(std::size_t i=0;i<images_.size();++i) {
        if(images_[i] && images_[i].Get()==captured_->images[i].Get())shared+=std::size_t(1024)*1024*pixel_bytes[i];
        if(i<4 && packed_images_[i] && packed_images_[i]==captured_->packed[i])shared+=packed_images_[i]->owned_bytes();
    }
    for(std::size_t i=0;i<histories_.size();++i) {
        const auto& operation=histories_[i];
        const auto& retained=captured_->histories[i];
        if(operation.descriptor.Width!=retained.descriptor.Width
            || operation.descriptor.Height!=retained.descriptor.Height
            || operation.descriptor.Format!=retained.descriptor.Format
            || operation.pixel_bytes!=retained.pixel_bytes) continue;
        const auto bytes=std::uint64_t(operation.descriptor.Width)*operation.descriptor.Height*operation.pixel_bytes;
        // Both sides own these exact allocations. B/native-C/private undo
        // images are deliberately excluded, including during failed recovery.
        if(operation.source_a && operation.source_a.Get()==retained.source_a.Get()) shared+=bytes;
        if(operation.image_a && operation.image_a.Get()==retained.image_a.Get()) shared+=bytes;
    }
    return shared <= captured_->reservation && shared <= witness_.bytes ? shared : 0;
}
std::size_t Sc6ReplayParticleCopy::retained_capture_bytes() noexcept
{ return CapturedImage::ledger.bytes(); }
bool Sc6ReplayParticleCopy::capture_retirement_pending() noexcept
{ return CapturedImage::retired.load(std::memory_order_acquire) != nullptr; }
bool Sc6ReplayParticleCopy::RetireCapturedImages() noexcept
{
    auto* image = CapturedImage::retired.exchange(nullptr, std::memory_order_acq_rel);
    bool complete = true;
    while (image) {
        auto* next = image->next;
        if (!image->ReleaseNative()) { CapturedImage::Queue(image); complete = false; }
        else {
            const auto module = image->module;
            if(!CapturedImage::ledger.Retire(image->accounting,[&]() noexcept {delete image;})) {
                image->poisoned=true;CapturedImage::Queue(image);complete=false;
                image=next;continue;
            }
            if (module) FreeLibrary(module);
        }
        image = next;
    }
    return complete;
}

bool Sc6ReplayParticleCopy::CloneCaptureOwners(CapturedImage& output) noexcept
{
    __try {
        const auto assign = reinterpret_cast<void**(*)(void**,void*)>(base_ + 0x15b83f0);
        for (std::size_t i = 0; i < lighting_a_.uniforms.size(); ++i)
            assign(&output.lighting.uniforms[i].value, lighting_a_.uniforms[i].value);
        assign(&output.visibility.material_uniform, visibility_a_.material_uniform);
        assign(&output.visibility.material_scene_uniform, visibility_a_.material_scene_uniform);
        for (auto* query : output.visibility.queries) {
            if (!query || Field<LONG>(reinterpret_cast<std::uintptr_t>(query), 8) <= 0) return false;
            InterlockedIncrement(&Field<LONG>(reinterpret_cast<std::uintptr_t>(query), 8));
            ++output.visibility.leases;
        }
        for (std::size_t i = 0; i < histories_.size(); ++i) {
            const auto& source = histories_[i];
            if (!source.a) continue;
            if (!ReflectionBinding(source.a, source.source_a.Get())) return false;
            reinterpret_cast<int(*)(void*)>(base_ + 0x144ac80)(reinterpret_cast<void*>(source.a));
            output.histories[i].a = source.a;
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { output.poisoned = true; return false; }
}

bool Sc6ReplayParticleCopy::SealCapture(const CaptureIdentity& identity, std::size_t remaining_budget) noexcept
{
    const auto reject=[&](const char* check) {
        RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] checkpoint render seal rejected tick={} check={} phase={} gpu_retired={} remaining_bytes={}\n"),
            identity.tick,RC::to_generic_string(check),static_cast<unsigned>(witness_.phase),completion_.retired(),remaining_budget);
        return false;
    };
    if(captured_ || witness_.phase!=Phase::ReadyA || !completion_.retired()) return reject("completion");
    if(!identity.session || !identity.source.active()) return reject("source_identity");
    if(!lighting_a_.captured || !visibility_a_.captured || lighting_b_.captured) return reject("captured_domains");
    if(!Bindings(reinterpret_cast<void*>(world_),false)) return reject("live_bindings");
    std::size_t replaced_bytes{};
    for(unsigned i=0;i<4;++i)if(packed_images_[i]) {
        if(!packed_images_[i]->ready())return reject("packed_completion");
        replaced_bytes+=std::size_t(1024)*1024*pixel_bytes[i];
    }
    // The immutable image receives A only. RetainReflection charged native
    // A/B and private A/B history images; the operation still owns private B
    // here, and its full witness.bytes remains charged until it retires.
    // Reopening uses that unchanged witness, not this resident reservation.
    std::size_t operation_history_bytes{};
    for(const auto& history:histories_) {
        if(history.b || history.c || history.source_b || history.source_c)
            return reject("history_undo_owned");
        if(!history.a && !history.source_a && !history.image_a && !history.image_b)
            continue; // Texture-only diagnostic: no history charge to credit.
        if(!history.a || !history.source_a
            || !history.descriptor.Width || history.descriptor.Width>4096
            || !history.descriptor.Height || history.descriptor.Height>4096
            || (history.pixel_bytes!=4 && history.pixel_bytes!=8)
            || bool(history.image_a)!=history.copy_pixels
            || bool(history.image_b)!=history.copy_pixels)
            return reject("history_reservation");
        const auto bytes=std::size_t(history.descriptor.Width)*history.descriptor.Height*history.pixel_bytes;
        const auto operation_bytes=bytes*(history.copy_pixels?2:1)+0xb8;
        if(operation_history_bytes>witness_.bytes || operation_bytes>witness_.bytes-operation_history_bytes)
            return reject("history_reservation");
        operation_history_bytes+=operation_bytes;
    }
    const auto gross = witness_.bytes + sizeof(CapturedImage) + 4096;
    if (gross < witness_.bytes || sizeof(CapturedImage)>remaining_budget) return reject("reservation");
    if(replaced_bytes>gross-operation_history_bytes)return reject("packed_reservation");
    const auto reservation = gross-operation_history_bytes-replaced_bytes;
    CapturedImage* raw{};
    try { raw = new CapturedImage; }
    catch (...) { return reject("allocation"); }
    // These references exist before publishing their identities to the ledger
    // and survive through native retirement, including all failure paths.
    raw->sources=sources_;
    raw->images=images_;
    raw->packed=packed_images_;
    for(unsigned i=0;i<4;++i)if(raw->packed[i])raw->images[i]=Texture{};
    ReplayCaptureAccounting::Histories history_ids{};
    for(std::size_t i=0;i<histories_.size();++i) {
        const auto& history=histories_[i];
        // Retain before registering the identity. Even a failed seal keeps
        // these COM allocations alive until its ledger reservation retires.
        raw->histories[i].source_a=history.source_a;
        if(history.source_a) history_ids[i]={reinterpret_cast<std::uintptr_t>(history.source_a.Get()),
            std::size_t(history.descriptor.Width)*history.descriptor.Height*history.pixel_bytes};
    }
    ReplayCaptureAccounting::Sources source_ids{};
    for(std::size_t i=0;i<source_ids.size();++i)
        source_ids[i]=reinterpret_cast<std::uintptr_t>(raw->sources[i].Get());
    ReplayCaptureAccounting::Images image_ids{};
    for(std::size_t i=0;i<image_ids.size();++i)
        if(raw->images[i])image_ids[i]={reinterpret_cast<std::uintptr_t>(raw->images[i].Get()),std::size_t(1024)*1024*pixel_bytes[i]};
    if(!CapturedImage::ledger.Register(raw->accounting,source_ids,reservation,image_bytes,remaining_budget,history_ids,image_ids)) {
        delete raw;return reject("accounting"); // No native leases or new GPU work yet.
    }
    raw->reservation = reservation;
    raw->owner_thread = GetCurrentThreadId();
    raw->base = base_;
    CaptureHandle result;
    try {
        result = CaptureHandle(raw, &CapturedImage::Queue);
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(&CapturedImage::Queue), &raw->module)) return reject("module_lease");
        raw->identity = identity;
        raw->local_fields = local_fields_a_;
        raw->world = world_; raw->scene = scene_; raw->system = system_; raw->pool = pool_; raw->view_state = view_state_;
        // Copies may throw. Temporary images own no native references, so
        // first finish copying and erase ownership fields before transferring.
        auto lighting = lighting_a_;
        for (auto& row : lighting.uniforms) row.value = nullptr;
        for (auto& map : lighting.maps) map.installed = {};
        for (auto& row : lighting.allocations) row.installed = nullptr;
        raw->lighting = std::move(lighting);
        auto visibility = visibility_a_;
        visibility.leases = 0;
        visibility.material_uniform = visibility.material_scene_uniform = nullptr;
        raw->visibility = std::move(visibility);
        for (std::size_t i = 0; i < histories_.size(); ++i) {
            auto& saved = raw->histories[i];
            const auto& source = histories_[i];
            saved.offset = source.offset; saved.source_a = source.source_a; saved.image_a = source.image_a;
            saved.descriptor = source.descriptor; saved.pixel_bytes = source.pixel_bytes;
            saved.hash_a = source.hash_a; saved.external = source.external; saved.copy_pixels = source.copy_pixels;
        }
        raw->sources = sources_;
        raw->wrappers = wrappers_;
        raw->witness = witness_;
        if (!CloneCaptureOwners(*raw)) return reject("owner_leases");
        captured_ = std::move(result);
        CapturedImage::last_sealed=captured_;
        witness_.capture_sealed = true;
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] checkpoint render image sealed session={} tick={} epoch={} reserved_bytes={} retained_reserved_bytes={} maps={}\n"),
            identity.session, identity.tick, identity.epoch, reservation, retained_capture_bytes(), witness_.readback_maps);
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] checkpoint immutable GPU sharing tick={} mask={} shared_bytes={} full_integer_comparison={} completion_observed=true nonuniform_tiles={},{},{},{} tile_count=4096 required_readback_bytes={}\n"),
            identity.tick,witness_.immutable_image_shared_mask,witness_.immutable_image_shared_bytes,witness_.immutable_image_compared,
            witness_.immutable_image_nonuniform_tiles[0],witness_.immutable_image_nonuniform_tiles[1],
            witness_.immutable_image_nonuniform_tiles[2],witness_.immutable_image_nonuniform_tiles[3],
            witness_.immutable_image_compared?ReplayGpuImageEquality::readback_bytes:0);
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] checkpoint packed GPU image tick={} replaced_full_bytes={} independent_snapshot=true packing_complete=true\n"),identity.tick,replaced_bytes);
        return true;
    }
    catch (...) { return reject("copy_exception"); } // shared_ptr construction failure queues raw itself.
}

bool Sc6ReplayParticleCopy::RetainLoadedCapture(const CapturedImage& source) noexcept
{
    __try {
        const auto assign = reinterpret_cast<void**(*)(void**,void*)>(base_ + 0x15b83f0);
        for (std::size_t i = 0; i < lighting_a_.uniforms.size(); ++i)
            assign(&lighting_a_.uniforms[i].value, source.lighting.uniforms[i].value);
        assign(&visibility_a_.material_uniform, source.visibility.material_uniform);
        assign(&visibility_a_.material_scene_uniform, source.visibility.material_scene_uniform);
        for (auto* query : visibility_a_.queries) {
            if (!query || Field<LONG>(reinterpret_cast<std::uintptr_t>(query),8) <= 0) return false;
            InterlockedIncrement(&Field<LONG>(reinterpret_cast<std::uintptr_t>(query),8));
            ++visibility_a_.leases;
        }
        for (std::size_t i = 0; i < histories_.size(); ++i) {
            const auto& saved = source.histories[i];
            if (!saved.a) continue;
            if (!ReflectionBinding(saved.a, saved.source_a.Get())) return false;
            reinterpret_cast<int(*)(void*)>(base_ + 0x144ac80)(reinterpret_cast<void*>(saved.a));
            histories_[i].a = saved.a;
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { capture_owner_fault_ = true; return false; }
}

bool Sc6ReplayParticleCopy::LoadCaptureUnchecked(const CaptureHandle& image)
{
    if (!BindTexturesUnchecked() || wrappers_ != image->wrappers) return false;
    for (std::size_t i = 0; i < sources_.size(); ++i)
        if (sources_[i].Get() != image->sources[i].Get()) return false;
    // Build fresh, operation-owned publication storage. No pointer/header in
    // the immutable checkpoint may be remapped by PrepareLighting or undo.
    auto lighting = image->lighting;
    for (auto& row : lighting.uniforms) row.value = nullptr;
    lighting_a_ = std::move(lighting);
    auto visibility = image->visibility;
    visibility.leases = 0;
    visibility.material_uniform = visibility.material_scene_uniform = nullptr;
    visibility_a_ = std::move(visibility);
    images_ = image->images;
    packed_images_=image->packed;
    bool packed=false;
    for(unsigned i=0;i<4;++i)if(packed_images_[i]) {
        if(!packed_images_[i]->ready())return false;
        D3D11_TEXTURE2D_DESC d{};sources_[i]->GetDesc(&d);
        d.Format=ReplayGpuImageEquality::StorageFormat(i);d.Usage=D3D11_USAGE_DEFAULT;
        d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;d.CPUAccessFlags=d.MiscFlags=0;
        if(FAILED(device_->CreateTexture2D(&d,nullptr,&images_[i])))return false;
        packed=true;
    }
    if(packed) {
        // Reopening replaces the capture census scratch with the smaller
        // materializer. The full A scratch remains in witness.bytes and is
        // not credited as immutable shared storage.
        static_assert(ReplayGpuImageEquality::reservation_bytes>=ReplayGpuImageMaterializer::reservation_bytes);
        image_materializer_=std::make_unique<ReplayGpuImageMaterializer>();
        ReplayGpuImageMaterializer::Images targets;
        for(unsigned i=0;i<4;++i)targets[i]=images_[i];
        if(FAILED(image_materializer_->Prepare(device_.Get(),packed_images_,targets,ReplayGpuImageMaterializer::reservation_bytes)))return false;
    }
    local_fields_a_ = image->local_fields;
    for (std::size_t i = 0; i < histories_.size(); ++i) {
        const auto& saved = image->histories[i];
        auto& history = histories_[i];
        history.offset = saved.offset; history.source_a = saved.source_a; history.image_a = saved.image_a;
        history.descriptor = saved.descriptor; history.pixel_bytes = saved.pixel_bytes;
        history.hash_a = saved.hash_a; history.external = saved.external; history.copy_pixels = saved.copy_pixels;
        if (history.copy_pixels) {
            auto descriptor = history.descriptor;
            descriptor.Usage = D3D11_USAGE_DEFAULT;
            descriptor.BindFlags = descriptor.CPUAccessFlags = descriptor.MiscFlags = 0;
            if (FAILED(device_->CreateTexture2D(&descriptor, nullptr, history.image_b.GetAddressOf()))) return false;
        }
    }
    if (!RetainLoadedCapture(*image)) return false;
    captured_ = image;
    witness_.phase = Phase::ReadyA;
    witness_.capture_sealed = witness_.capture_reopened = true;
    RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] checkpoint render image reopened session={} tick={} epoch={} current_epoch={} fresh_operation=true maps={}\n"),
        image->identity.session, image->identity.tick, image->identity.epoch,
        Field<std::uint64_t>(base_,0x4197170), witness_.readback_maps);
    return true;
}

bool Sc6ReplayParticleCopy::BeginFromCapture(std::uintptr_t base, void* world,
    const CaptureHandle& image, std::size_t budget, void* view_state) noexcept
{
    if (witness_.phase != Phase::Empty || !image || image->poisoned || image->base != base
        || image->world != reinterpret_cast<std::uintptr_t>(world)
        || image->view_state != reinterpret_cast<std::uintptr_t>(view_state)
        || image->owner_thread != GetCurrentThreadId() || image->witness.diagnostic_readbacks) return false;
    const auto shared=reopen_shared_bytes(image);
    if(image->witness.bytes-shared>budget) {
        RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] checkpoint reopen budget rejected tick={} gross={} retained_shared={} private_required={} available={} before_binding=true\n"),
            image->identity.tick,image->witness.bytes,shared,image->witness.bytes-shared,budget);
        return false;
    }
    RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] checkpoint reopen budget admitted tick={} gross={} retained_shared={} private_required={} available={} immutable_owner_retained=true\n"),
        image->identity.tick,image->witness.bytes,shared,image->witness.bytes-shared,budget);
    base_ = base; world_ = image->world; view_state_ = image->view_state;
    witness_ = image->witness;
    if (!Bindings(world, true) || scene_ != image->scene || system_ != image->system || pool_ != image->pool)
    { Fail(E_INVALIDARG); return false; }
    try {
        if (LoadCaptureUnchecked(image)) {
            if(shared_capture_bytes()!=shared) {Fail(E_INVALIDARG);return false;}
            return true;
        }
    } catch (...) {}
    Fail(E_FAIL);
    return false;
}
