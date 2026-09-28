// Native release can fail after decrement; uncertainty is terminal for this owner.
bool Sc6ReplayParticleCopy::ReleaseVisibilityImage(VisibilityImage& image) noexcept
{
    // An SEH fault can follow a completed native decrement. Its unchanged
    // pointer is not evidence that this lease remains ours to release again.
    if(capture_owner_fault_)return false;
    __try {
        const auto release=reinterpret_cast<void(*)(void**)>(base_+0x11b9260);
        if(image.material_uniform) {release(&image.material_uniform);image.material_uniform=nullptr;}
        if(image.material_scene_uniform) {release(&image.material_scene_uniform);image.material_scene_uniform=nullptr;}
        while(image.leases) {
            auto*& query=image.queries[image.leases-1];release(&query);query=nullptr;--image.leases;
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {capture_owner_fault_=true;return false;}
}
