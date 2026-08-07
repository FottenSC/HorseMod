#pragma once

#include <cstddef>
#include <cstdint>

namespace Horse
{
    enum class RollbackVtableSlotPatchFailure : uint8_t
    {
        None,
        InvalidArgument,
        MakeWritableFailed,
        ExchangeMismatch,
        RestoreFailed,
        RollbackExchangeMismatch,
        PersistentRestoreFailed,
    };

    struct RollbackVtableSlotPatchOps
    {
        void* context {nullptr};
        bool (*make_writable)(void*, void**, uint32_t&) noexcept {nullptr};
        void* (*compare_exchange)(
            void*, void**, void*, void*) noexcept {nullptr};
        bool (*restore_protection)(void*, void**, uint32_t) noexcept {nullptr};
    };

    struct RollbackVtableSlotPatchReport
    {
        bool installed {false};
        RollbackVtableSlotPatchFailure failure {
            RollbackVtableSlotPatchFailure::InvalidArgument};
        void* original {nullptr};
        uint32_t restore_attempts {0};
    };

    enum class RollbackVtableSlotBindingFailure : uint8_t
    {
        None,
        InvalidArgument,
        DispatcherReadFailed,
        VtableReadFailed,
        SlotReadFailed,
        VtableChanged,
        SlotChanged,
        HookReplaced,
    };

    inline RollbackVtableSlotBindingFailure ValidateRollbackVtableSlotBinding(
        void* current_dispatcher,
        void* installed_vtable,
        void** installed_slot,
        void* current_vtable,
        void** current_slot,
        void* current_target,
        void* replacement) noexcept
    {
        if (!current_dispatcher || !installed_vtable || !installed_slot
            || !current_vtable || !current_slot || !current_target
            || !replacement)
            return RollbackVtableSlotBindingFailure::InvalidArgument;
        if (current_vtable != installed_vtable)
            return RollbackVtableSlotBindingFailure::VtableChanged;
        if (current_slot != installed_slot)
            return RollbackVtableSlotBindingFailure::SlotChanged;
        if (current_target != replacement)
            return RollbackVtableSlotBindingFailure::HookReplaced;
        return RollbackVtableSlotBindingFailure::None;
    }

    inline RollbackVtableSlotPatchReport InstallRollbackVtableSlotOnce(
        void** slot,
        void* expected,
        void* replacement,
        const RollbackVtableSlotPatchOps& ops) noexcept
    {
        RollbackVtableSlotPatchReport report {};
        if (!slot || !expected || !replacement || !ops.make_writable
            || !ops.compare_exchange || !ops.restore_protection)
            return report;

        uint32_t old_protect = 0;
        if (!ops.make_writable(ops.context, slot, old_protect))
        {
            report.failure =
                RollbackVtableSlotPatchFailure::MakeWritableFailed;
            return report;
        }

        void* const prior = ops.compare_exchange(
            ops.context, slot, replacement, expected);
        if (prior != expected)
        {
            ++report.restore_attempts;
            if (!ops.restore_protection(ops.context, slot, old_protect))
            {
                ++report.restore_attempts;
                if (!ops.restore_protection(ops.context, slot, old_protect))
                {
                    report.failure = RollbackVtableSlotPatchFailure::
                        PersistentRestoreFailed;
                    return report;
                }
            }
            report.failure = RollbackVtableSlotPatchFailure::ExchangeMismatch;
            return report;
        }

        ++report.restore_attempts;
        if (ops.restore_protection(ops.context, slot, old_protect))
        {
            report.installed = true;
            report.failure = RollbackVtableSlotPatchFailure::None;
            report.original = expected;
            return report;
        }

        // Undo the installed pointer while the page is still writable, then
        // retry restoration. Failed installs never publish installed state.
        void* const rollback_prior = ops.compare_exchange(
            ops.context, slot, expected, replacement);
        ++report.restore_attempts;
        const bool restored = ops.restore_protection(
            ops.context, slot, old_protect);
        if (!restored)
        {
            report.failure =
                RollbackVtableSlotPatchFailure::PersistentRestoreFailed;
            return report;
        }
        report.failure = rollback_prior == replacement
            ? RollbackVtableSlotPatchFailure::RestoreFailed
            : RollbackVtableSlotPatchFailure::RollbackExchangeMismatch;
        return report;
    }

    inline const char* RollbackVtableSlotPatchFailureName(
        RollbackVtableSlotPatchFailure failure) noexcept
    {
        switch (failure)
        {
        case RollbackVtableSlotPatchFailure::None: return "ok";
        case RollbackVtableSlotPatchFailure::InvalidArgument:
            return "vfx-slot-invalid-argument";
        case RollbackVtableSlotPatchFailure::MakeWritableFailed:
            return "vfx-slot-make-writable-failed";
        case RollbackVtableSlotPatchFailure::ExchangeMismatch:
            return "vfx-slot-exchange-mismatch";
        case RollbackVtableSlotPatchFailure::RestoreFailed:
            return "vfx-slot-protection-restore-failed";
        case RollbackVtableSlotPatchFailure::RollbackExchangeMismatch:
            return "vfx-slot-rollback-exchange-mismatch";
        case RollbackVtableSlotPatchFailure::PersistentRestoreFailed:
            return "vfx-slot-protection-restore-persistent-failure";
        }
        return "vfx-slot-unknown-failure";
    }

    constexpr bool RollbackVtableSlotPatchFailureRequiresRestart(
        RollbackVtableSlotPatchFailure failure) noexcept
    {
        return failure == RollbackVtableSlotPatchFailure::ExchangeMismatch
            || failure
                == RollbackVtableSlotPatchFailure::RollbackExchangeMismatch
            || failure
                == RollbackVtableSlotPatchFailure::PersistentRestoreFailed;
    }

    struct RollbackVtableSlotInstallLatch
    {
        bool restart_required {false};
        size_t failed_slot {static_cast<size_t>(-1)};
        RollbackVtableSlotPatchFailure failure {
            RollbackVtableSlotPatchFailure::None};

        void observe(size_t slot,
            RollbackVtableSlotPatchFailure observed) noexcept
        {
            if (!restart_required
                && RollbackVtableSlotPatchFailureRequiresRestart(observed))
            {
                restart_required = true;
                failed_slot = slot;
                failure = observed;
            }
        }
    };
}
