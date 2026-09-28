#pragma once
#include "Types.hpp"
#include <memory>
#include <span>

namespace Horse::Deterministic
{
// GC reachability only. Explicit component destruction and render ownership
// remain the enclosing transaction's responsibility. Call Acquire/Release at
// the completed application boundary on the native game thread.
class Sc6ReplayObjectLease final
{
public:
    Sc6ReplayObjectLease();
    ~Sc6ReplayObjectLease();
    Sc6ReplayObjectLease(const Sc6ReplayObjectLease&) = delete;
    Sc6ReplayObjectLease& operator=(const Sc6ReplayObjectLease&) = delete;
    Status Acquire(std::uintptr_t base, std::span<void* const> objects, std::size_t budget) noexcept;
    Status Validate() const noexcept;
    // Validate both the native lease and exact retained membership. This never
    // dereferences the supplied identity; it cannot admit an unrelated live
    // object merely because some other lease happens to be valid.
    Status ValidateObject(const void* object) const noexcept;
    // Failure-only diagnostic; no reference, registry or object mutation.
    struct FailureWitness {std::size_t ordinal{};std::int32_t index{},serial{};void* original{};
        std::uint64_t object_name{},class_name{};std::uintptr_t vtable{};
        std::uint32_t before_component_flags{},before_template_flags{},before_auto_destroy{};
        std::uint32_t captured_component_flags{},captured_template_flags{},captured_auto_destroy{};
        std::size_t captured_emitters{},captured_attachment_users{};
        std::size_t captured_manager_slots{},captured_cpu_emitters{},captured_gpu_emitters{};
        std::size_t captured_material_users{},captured_material_parent_users{};
        // Captured VFX references only, not proof of all engine consumers.
        std::size_t captured_provider_users{},captured_completion_users{},captured_own_completions{};
        std::size_t captured_manager_listeners{},captured_listener_users{};
        bool captured_reference_inventory_valid{};
        std::int32_t captured_slot_id{-1};unsigned captured_slot_kind{};
        bool before_weak_live{},before_particle_state{},captured_component{};
        bool reference{},collector_changed{},invalidated{},membership{},captured_identity{};};
    using FailureSink=void(*)(const void*,const FailureWitness&);
    void DiagnoseFailure(const void* context,FailureSink sink) const noexcept;
    // Failure preserves registration and callback storage. Caller must retain
    // this lease and retry retirement at an admitted boundary before teardown.
    Status Release() noexcept;
    bool registered() const noexcept;
    std::size_t object_count() const noexcept;
    std::size_t owned_bytes() const noexcept;
    // Failed destructor retirement transfers ownership without allocating.
    // Only the admitted native owner thread may retry registry removal.
    static Status RetireQuarantined(std::uintptr_t base) noexcept;
    static std::size_t quarantined_bytes() noexcept;
private:
    struct Control;
    std::unique_ptr<Control> control_;
};
}
