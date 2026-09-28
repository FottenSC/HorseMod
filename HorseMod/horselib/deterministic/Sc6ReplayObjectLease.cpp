#include "Sc6ReplayObjectLease.hpp"
#include <Windows.h>
#include <atomic>
#include <algorithm>

namespace Horse::Deterministic
{
namespace
{
template<class T> T& At(std::uintptr_t address) { return *reinterpret_cast<T*>(address); }
bool EnterAdmission(std::uintptr_t base) noexcept
{
    LPCRITICAL_SECTION acquired{};
    __try
    {
        if (!At<bool>(base + 0x419718c) || At<DWORD>(base + 0x419716c) != GetCurrentThreadId()) return false;
        auto* lock = reinterpret_cast<LPCRITICAL_SECTION>(base + 0x429f678);
        if (!TryEnterCriticalSection(lock)) return false;
        acquired = lock;
        // Both GC entry paths reserve +674 under this lock before collection.
        // Reject pending purge as well: new leases must not resurrect objects
        // already classified as unreachable by an earlier collection.
        if (At<int>(base + 0x429f674) || At<int>(base + 0x429fa54)
            || At<byte>(base + 0x429f648) || At<byte>(base + 0x429fa0c))
        { LeaveCriticalSection(lock); return false; }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        if (acquired) LeaveCriticalSection(acquired);
        return false;
    }
}
void LeaveAdmission(std::uintptr_t base) noexcept
{ LeaveCriticalSection(reinterpret_cast<LPCRITICAL_SECTION>(base + 0x429f678)); }
}

struct Sc6ReplayObjectLease::Control
{
    struct Reference { void* object{}; void* original{}; std::array<std::int32_t, 2> weak{};
        std::uintptr_t captured_vtable{};std::uint64_t captured_object_name{},captured_class_name{}; };
    using Destroy = void* (*)(Control*, unsigned);
    using Enumerate = void (*)(Control*, void*);
    struct Vtable { Destroy destroy; Enumerate enumerate; };
    const Vtable* vtable{}; // Audited native non-UObject owner prefix.
    std::vector<Reference> references;
    std::uintptr_t base{}, registry{};
    HMODULE module{};
    Control* quarantine_next{};
    static inline std::atomic<Control*> quarantine{};
    static inline std::atomic<std::size_t> quarantine_bytes{};
    bool registered{};
    std::atomic<bool> invalidated{};
    std::atomic_flag invalidation_claimed{};
    std::atomic<bool> invalidation_ready{};
    FailureWitness first_invalidation{};
    static FailureWitness IdentityBeforeVisit(const Reference& r,std::size_t ordinal,std::uintptr_t base) noexcept {
        FailureWitness w{};w.ordinal=ordinal;w.index=r.weak[0];w.serial=r.weak[1];w.original=r.original;w.reference=true;
        // GC's reference visitor is about to inspect this allocated UObject.
        // Copy only stable FName IDs/type metadata before it can clear the
        // reference. No strings, allocation, logging or pointer resurrection.
        __try {
            const auto address=reinterpret_cast<std::uintptr_t>(r.object);
            const auto type=At<std::uintptr_t>(address+0x10);
            w.vtable=At<std::uintptr_t>(address);w.object_name=At<std::uint64_t>(address+0x18);
            w.class_name=At<std::uint64_t>(type+0x18);w.captured_identity=true;
            // Read before the collector clears the slot: later weak-item flags
            // cannot distinguish prior native destruction from GC reclamation.
            w.before_weak_live=reinterpret_cast<void*(*)(const void*)>(base+0xf823f0)(r.weak.data())==r.original;
            if(w.vtable==base+0x335db28) {
                w.before_component_flags=At<std::uint32_t>(address+0x188);
                w.before_template_flags=At<std::uint32_t>(address+0x830);
                w.before_auto_destroy=At<std::uint32_t>(address+0x8b8);
                w.before_particle_state=true;
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
        return w;
    }
    static void* RejectExternalDeletion(Control* self, unsigned)
    {
        // Registry enumeration does not own/delete its members. An unexpected
        // destructor dispatch invalidates admission and preserves the storage.
        self->invalidated.store(true); return self;
    }
    static void EnumerateReferences(Control* self, void* collector)
    {
        const auto visit = At<void (*)(void*, void**, void*, void*)>(At<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(collector)) + 0x38);
        for (auto& reference : self->references)
        {
            const auto identity=reference.object && !self->invalidation_ready.load()
                ?IdentityBeforeVisit(reference,std::size_t(&reference-self->references.data()),self->base):FailureWitness{};
            if (reference.object) visit(collector, &reference.object, nullptr, nullptr);
            if (reference.object != reference.original) {
                if(!self->invalidation_claimed.test_and_set()) {
                    self->first_invalidation=identity;
                    self->first_invalidation.collector_changed=true;
                    self->invalidation_ready.store(true);
                }
                self->invalidated.store(true);
            }
        }
    }
    static inline const Vtable callbacks{RejectExternalDeletion, EnumerateReferences};
    ~Control() { if (module) FreeLibrary(module); }
    std::size_t bytes() const noexcept { return sizeof(Control) + references.capacity() * sizeof(Reference) + sizeof(void*); }
    static void Retain(Control* owner, bool already_charged = false) noexcept
    {
        if (!already_charged) quarantine_bytes.fetch_add(owner->bytes());
        auto* head = quarantine.load();
        do { owner->quarantine_next = head; }
        while (!quarantine.compare_exchange_weak(head, owner));
    }

    bool ReadRegistry() noexcept
    {
        __try { registry = At<std::uintptr_t>(base + 0x429eac8); return registry != 0; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool Bind() noexcept
    {
        __try
        {
            for (auto& reference : references)
            {
                reinterpret_cast<void (*)(void*, void*)>(base + 0xf7bad0)(reference.weak.data(), reference.object);
                if (reinterpret_cast<void* (*)(const void*)>(base + 0xf823f0)(reference.weak.data()) != reference.object) return false;
                // Capture diagnostic identity while weak membership is proven.
                // Explicit destruction need not invoke the GC visitor before
                // a later failure; never inspect the dead object then.
                const auto identity=IdentityBeforeVisit(reference,std::size_t(&reference-references.data()),base);
                reference.captured_vtable=identity.vtable;
                reference.captured_object_name=identity.object_name;
                reference.captured_class_name=identity.class_name;
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    bool Live() const noexcept
    {
        if (invalidated.load()) return false;
        __try
        {
            for (const auto& reference : references)
                if (reference.object != reference.original
                    || reinterpret_cast<void* (*)(const void*)>(base + 0xf823f0)(reference.weak.data()) != reference.original) return false;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    std::size_t FirstInvalidReference() const noexcept {
        __try {
            for(std::size_t i=0;i<references.size();++i) {
                const auto& r=references[i];
                if(r.object!=r.original || reinterpret_cast<void*(*)(const void*)>(base+0xf823f0)(r.weak.data())!=r.original)return i;
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
        return references.size();
    }
    bool Membership() const noexcept
    {
        __try
        {
            if (vtable != &callbacks || At<std::uintptr_t>(base + 0x429eac8) != registry
                || !registry || At<std::uintptr_t>(registry) != base + 0x350f7e0) return false;
            auto* lock = reinterpret_cast<LPCRITICAL_SECTION>(registry + 0x38);
            if (!TryEnterCriticalSection(lock)) return false;
            __try
            {
                const auto count = At<int>(registry + 0x30), capacity = At<int>(registry + 0x34);
                auto** entries = At<void**>(registry + 0x28);
                if (count < 0 || capacity < count || (capacity && !entries)) return false;
                std::size_t found{};
                for (int i = 0; i < count; ++i) found += entries[i] == this;
                return found == 1;
            }
            __finally { LeaveCriticalSection(lock); }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    Status MutateRegistry(bool add) noexcept
    {
        __try
        {
            if (At<std::uintptr_t>(base + 0x429eac8) != registry || !registry
                || At<std::uintptr_t>(registry) != base + 0x350f7e0) return Status::failure(FailureCode::GenerationMismatch);
            auto* lock = reinterpret_cast<LPCRITICAL_SECTION>(registry + 0x38);
            if (!TryEnterCriticalSection(lock)) return Status::failure(FailureCode::ContextUnavailable);
            __try
            {
                auto count = At<int>(registry + 0x30);
                const auto capacity = At<int>(registry + 0x34);
                auto** entries = At<void**>(registry + 0x28);
                if (count < 0 || capacity < count || (capacity && !entries)) return Status::failure(FailureCode::GenerationMismatch);
                std::size_t occurrences{};
                for (int i = 0; i < count; ++i) occurrences += entries[i] == this;
                // Resolve an earlier uncertain removal only under the same
                // GC exclusion and registry identity checks.
                if (!add && occurrences == 0) { registered = false; return Status::success(); }
                if (occurrences != (add ? 0u : 1u)) return Status::failure(FailureCode::GenerationMismatch);
                // Do not allocate unaccounted shared registry backing. Existing
                // reserved capacity must admit this owner before publication.
                if (add && count == capacity) return Status::failure(FailureCode::CapacityExceeded);
                if (add)
                {
                    // Mark possible publication before entering native code;
                    // an exception must never free potentially registered data.
                    registered = true;
                    reinterpret_cast<void (*)(void*, void*)>(base + 0xe42ed0)(reinterpret_cast<void*>(registry), this);
                }
                else reinterpret_cast<void (*)(void*)>(base + 0xe3f520)(this);
                // The native base destructor resets the vtable before removing
                // membership. Keep our callback valid if removal verification
                // fails and this control has to remain registered.
                vtable = &callbacks;
                count = At<int>(registry + 0x30); entries = At<void**>(registry + 0x28);
                if (count < 0 || count > capacity || (count && !entries)) return Status::failure(FailureCode::GenerationMismatch);
                occurrences = 0;
                for (int i = 0; i < count; ++i) occurrences += entries[i] == this;
                if (occurrences != (add ? 1u : 0u)) return Status::failure(FailureCode::GenerationMismatch);
                registered = add;
                return Status::success();
            }
            __finally { LeaveCriticalSection(lock); }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            vtable = &callbacks;
            return Status::failure(FailureCode::ContextUnavailable);
        }
    }
};

Sc6ReplayObjectLease::Sc6ReplayObjectLease()
{
    static_assert(offsetof(Control, vtable) == 0);
    static_assert(sizeof(Control::Vtable) == 2 * sizeof(void*));
}
Sc6ReplayObjectLease::~Sc6ReplayObjectLease()
{
    // Keep the registered callback, module and accounting reachable. This
    // transfer allocates nothing and never calls native retirement off-thread.
    if (control_ && !Release().ok()) Control::Retain(control_.release());
}
std::size_t Sc6ReplayObjectLease::quarantined_bytes() noexcept { return Control::quarantine_bytes.load(); }
Status Sc6ReplayObjectLease::RetireQuarantined(std::uintptr_t base) noexcept
{
    if (!Control::quarantine.load()) return Status::success();
    if (!EnterAdmission(base)) return Status::failure(FailureCode::ContextUnavailable);
    auto* current = Control::quarantine.exchange(nullptr);
    auto status = Status::success();
    while (current) {
        auto* next = current->quarantine_next;
        const auto retired = current->base == base ? current->MutateRegistry(false)
            : Status::failure(FailureCode::GenerationMismatch);
        if (retired.ok()) { Control::quarantine_bytes.fetch_sub(current->bytes()); delete current; }
        else { Control::Retain(current, true); status = retired; }
        current = next;
    }
    LeaveAdmission(base);
    return status;
}
bool Sc6ReplayObjectLease::registered() const noexcept { return control_ && control_->registered; }
std::size_t Sc6ReplayObjectLease::object_count() const noexcept { return control_ ? control_->references.size() : 0; }
std::size_t Sc6ReplayObjectLease::owned_bytes() const noexcept
{ return sizeof(*this) + (control_ ? sizeof(Control) + control_->references.capacity() * sizeof(Control::Reference) + sizeof(void*) : 0); }
Status Sc6ReplayObjectLease::Acquire(std::uintptr_t base, std::span<void* const> objects, std::size_t budget) noexcept
{
    if (control_) return Status::failure(FailureCode::IllegalTransition);
    if (!base || base != reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)) || objects.empty()) return Status::failure(FailureCode::IdentityMismatch);
    if (budget < sizeof(*this) + sizeof(Control) + sizeof(void*)
        || objects.size() > (budget - sizeof(*this) - sizeof(Control) - sizeof(void*)) / sizeof(Control::Reference)) return Status::failure(FailureCode::CapacityExceeded);
    try
    {
        control_ = std::make_unique<Control>();
        control_->base = base; control_->vtable = &Control::callbacks;
        // A registered vtable must not outlive its module even when the
        // enclosing checkpoint dies during failed teardown.
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                reinterpret_cast<LPCWSTR>(&Control::callbacks), &control_->module)) {
            control_.reset(); return Status::failure(FailureCode::ContextUnavailable);
        }
        control_->references.reserve(objects.size());
        for (auto* object : objects)
        {
            if (!object) { control_.reset(); return Status::failure(FailureCode::IdentityMismatch); }
            if (std::none_of(control_->references.begin(), control_->references.end(), [&](const auto& r) { return r.object == object; }))
                control_->references.push_back({object, object, {}});
        }
        if (owned_bytes() > budget) { control_.reset(); return Status::failure(FailureCode::CapacityExceeded); }
    }
    catch (...) { control_.reset(); return Status::failure(FailureCode::CapacityExceeded); }
    if (!EnterAdmission(base)) { control_.reset(); return Status::failure(FailureCode::ContextUnavailable); }
    // The registry exists during replay; do not construct/root a new singleton.
    auto status = control_->ReadRegistry() && control_->Bind()
        ? control_->MutateRegistry(true) : Status::failure(FailureCode::GenerationMismatch);
    LeaveAdmission(base);
    if (!status.ok() && !control_->registered) control_.reset();
    return status;
}
Status Sc6ReplayObjectLease::Validate() const noexcept
{
    if (!registered()) return Status::failure(FailureCode::IllegalTransition);
    if (!EnterAdmission(control_->base)) return Status::failure(FailureCode::ContextUnavailable);
    const auto valid = control_->Live() && control_->Membership();
    LeaveAdmission(control_->base);
    return valid ? Status::success() : Status::failure(FailureCode::GenerationMismatch);
}
Status Sc6ReplayObjectLease::ValidateObject(const void* object) const noexcept
{
    if(!object || !registered())return Status::failure(FailureCode::IllegalTransition);
    if(!EnterAdmission(control_->base))return Status::failure(FailureCode::ContextUnavailable);
    const bool valid=control_->Live() && control_->Membership()
        && std::any_of(control_->references.begin(),control_->references.end(),
            [&](const auto& reference){return reference.original==object && reference.object==object;});
    LeaveAdmission(control_->base);
    return valid?Status::success():Status::failure(FailureCode::GenerationMismatch);
}
void Sc6ReplayObjectLease::DiagnoseFailure(const void* context,FailureSink sink) const noexcept
{
    if(!sink || !registered() || !EnterAdmission(control_->base))return;
    try {
        const auto ordinal=control_->FirstInvalidReference();
        FailureWitness witness{};witness.ordinal=ordinal;witness.invalidated=control_->invalidated.load();witness.membership=control_->Membership();
        if(ordinal<control_->references.size()) {
            const auto& r=control_->references[ordinal];
            witness.reference=true;witness.original=r.original;witness.index=r.weak[0];witness.serial=r.weak[1];witness.collector_changed=r.object!=r.original;
            witness.vtable=r.captured_vtable;witness.object_name=r.captured_object_name;
            witness.class_name=r.captured_class_name;witness.captured_identity=r.captured_vtable!=0;
        }
        if(control_->invalidation_ready.load()) {
            const auto membership=witness.membership;
            witness=control_->first_invalidation;witness.invalidated=true;witness.membership=membership;
        }
        // Synchronous read-only observer runs under the existing GC exclusion.
        // The reusable lease core does not depend on UI/reflection/logging.
        sink(context,witness);
    } catch(...) {}
    LeaveAdmission(control_->base);
}
Status Sc6ReplayObjectLease::Release() noexcept
{
    if (!control_) return Status::success();
    if (!control_->registered) { control_.reset(); return Status::success(); }
    if (!EnterAdmission(control_->base)) return Status::failure(FailureCode::ContextUnavailable);
    const auto status = control_->MutateRegistry(false);
    LeaveAdmission(control_->base);
    if (status.ok()) control_.reset();
    return status;
}
}
