#include "RollbackVtableSlotPatch.hpp"

#include <cstdio>

namespace
{
    struct MockOps
    {
        void* value {reinterpret_cast<void*>(0x1000)};
        bool writable_ok {true};
        bool restore[2] {true, true};
        uint32_t restore_calls {0};
        uint32_t exchange_calls {0};
        bool exchange_mismatch {false};
        bool rollback_mismatch {false};
    };

    bool MakeWritable(void* p, void**, uint32_t& old) noexcept
    {
        old = 0x20;
        return static_cast<MockOps*>(p)->writable_ok;
    }

    void* Exchange(
        void* p, void**, void* desired, void* expected) noexcept
    {
        auto& m = *static_cast<MockOps*>(p);
        ++m.exchange_calls;
        if ((m.exchange_calls == 1 && m.exchange_mismatch)
            || (m.exchange_calls == 2 && m.rollback_mismatch))
        {
            if (m.exchange_calls == 2) m.value = reinterpret_cast<void*>(0x9999);
            return reinterpret_cast<void*>(0x9999);
        }
        void* const prior = m.value;
        if (prior == expected) m.value = desired;
        return prior;
    }

    bool Restore(void* p, void**, uint32_t) noexcept
    {
        auto& m = *static_cast<MockOps*>(p);
        const uint32_t index = m.restore_calls < 2 ? m.restore_calls : 1;
        ++m.restore_calls;
        return m.restore[index];
    }

    Horse::RollbackVtableSlotPatchReport Run(MockOps& m) noexcept
    {
        void* sentinel_slot = m.value;
        const Horse::RollbackVtableSlotPatchOps ops {
            &m, &MakeWritable, &Exchange, &Restore,
        };
        const auto report = Horse::InstallRollbackVtableSlotOnce(
            &sentinel_slot, reinterpret_cast<void*>(0x1000),
            reinterpret_cast<void*>(0x2000), ops);
        return sentinel_slot == reinterpret_cast<void*>(0x1000)
            ? report : Horse::RollbackVtableSlotPatchReport {};
    }
}

int main()
{
    using Failure = Horse::RollbackVtableSlotPatchFailure;
    using BindingFailure = Horse::RollbackVtableSlotBindingFailure;

    MockOps success {};
    const auto a = Run(success);
    if (!a.installed || a.failure != Failure::None
        || success.value != reinterpret_cast<void*>(0x2000)
        || success.exchange_calls != 1 || success.restore_calls != 1
        || a.original != reinterpret_cast<void*>(0x1000)) return 1;

    MockOps open_fail {};
    open_fail.writable_ok = false;
    const auto b = Run(open_fail);
    if (b.installed || b.failure != Failure::MakeWritableFailed
        || open_fail.exchange_calls != 0 || open_fail.restore_calls != 0)
        return 2;

    MockOps exchange_fail {};
    exchange_fail.exchange_mismatch = true;
    const auto c = Run(exchange_fail);
    if (c.installed || c.failure != Failure::ExchangeMismatch
        || exchange_fail.value != reinterpret_cast<void*>(0x1000)
        || exchange_fail.restore_calls != 1) return 3;

    MockOps restore_fail {};
    restore_fail.restore[0] = false;
    const auto d = Run(restore_fail);
    if (d.installed || d.failure != Failure::RestoreFailed
        || restore_fail.value != reinterpret_cast<void*>(0x1000)
        || restore_fail.exchange_calls != 2 || restore_fail.restore_calls != 2)
        return 4;

    MockOps rollback_fail {};
    rollback_fail.restore[0] = false;
    rollback_fail.rollback_mismatch = true;
    const auto e = Run(rollback_fail);
    if (e.installed || e.failure != Failure::RollbackExchangeMismatch
        || rollback_fail.restore_calls != 2) return 5;

    MockOps persistent {};
    persistent.restore[0] = false;
    persistent.restore[1] = false;
    const auto f = Run(persistent);
    if (f.installed || f.failure != Failure::PersistentRestoreFailed
        || persistent.value != reinterpret_cast<void*>(0x1000)
        || persistent.restore_calls != 2) return 6;

    MockOps mismatch_retry {};
    mismatch_retry.exchange_mismatch = true;
    mismatch_retry.restore[0] = false;
    const auto g = Run(mismatch_retry);
    if (g.installed || g.failure != Failure::ExchangeMismatch
        || mismatch_retry.restore_calls != 2) return 7;

    void* const old_dispatcher = reinterpret_cast<void*>(0x3000);
    void* const new_dispatcher = reinterpret_cast<void*>(0x3100);
    void* const vtable = reinterpret_cast<void*>(0x4000);
    auto** const slot = reinterpret_cast<void**>(0x40B8);
    void* const hook = reinterpret_cast<void*>(0x5000);
    if (Horse::ValidateRollbackVtableSlotBinding(
            old_dispatcher, vtable, slot, vtable, slot, hook, hook)
        != BindingFailure::None) return 8;
    // A replacement dispatcher instance is valid only when it resolves to
    // the same class vtable and therefore the same patched slot.
    if (Horse::ValidateRollbackVtableSlotBinding(
            new_dispatcher, vtable, slot, vtable, slot, hook, hook)
        != BindingFailure::None) return 9;
    if (Horse::ValidateRollbackVtableSlotBinding(
            new_dispatcher, vtable, slot,
            reinterpret_cast<void*>(0x6000),
            reinterpret_cast<void**>(0x60B8), hook, hook)
        != BindingFailure::VtableChanged) return 10;
    if (Horse::ValidateRollbackVtableSlotBinding(
            new_dispatcher, vtable, slot, vtable,
            reinterpret_cast<void**>(0x40C0), hook, hook)
        != BindingFailure::SlotChanged) return 11;
    if (Horse::ValidateRollbackVtableSlotBinding(
            new_dispatcher, vtable, slot, vtable, slot,
            reinterpret_cast<void*>(0x7000), hook)
        != BindingFailure::HookReplaced) return 12;

    Horse::RollbackVtableSlotInstallLatch latch {};
    latch.observe(0, Failure::RestoreFailed);
    latch.observe(1, Failure::None);
    if (latch.restart_required) return 13;
    latch.observe(4, Failure::ExchangeMismatch);
    if (!latch.restart_required || latch.failed_slot != 4
        || latch.failure != Failure::ExchangeMismatch) return 14;
    latch.observe(7, Failure::PersistentRestoreFailed);
    if (latch.failed_slot != 4
        || latch.failure != Failure::ExchangeMismatch) return 15;

    Horse::RollbackVtableSlotInstallLatch rollback_latch {};
    rollback_latch.observe(5, Failure::RollbackExchangeMismatch);
    if (!rollback_latch.restart_required
        || rollback_latch.failed_slot != 5) return 16;
    Horse::RollbackVtableSlotInstallLatch protection_latch {};
    protection_latch.observe(6, Failure::PersistentRestoreFailed);
    if (!protection_latch.restart_required
        || protection_latch.failed_slot != 6) return 17;

    uint64_t installed_mask = 0;
    Horse::RollbackVtableSlotInstallLatch sequence_latch {};
    MockOps slot_zero {};
    const auto slot_zero_report = Run(slot_zero);
    if (slot_zero_report.installed) installed_mask |= 1ull << 0;
    MockOps safe_slot_one {};
    safe_slot_one.restore[0] = false;
    const auto safe_slot_one_report = Run(safe_slot_one);
    sequence_latch.observe(1, safe_slot_one_report.failure);
    if (sequence_latch.restart_required || installed_mask != 1) return 18;
    MockOps retry_slot_one {};
    const auto retry_slot_one_report = Run(retry_slot_one);
    if (retry_slot_one_report.installed) installed_mask |= 1ull << 1;
    MockOps ambiguous_slot_two {};
    ambiguous_slot_two.exchange_mismatch = true;
    const auto ambiguous_slot_two_report = Run(ambiguous_slot_two);
    sequence_latch.observe(2, ambiguous_slot_two_report.failure);
    if (!sequence_latch.restart_required || installed_mask != 3
        || sequence_latch.failed_slot != 2) return 19;
    MockOps blocked_retry {};
    if (!sequence_latch.restart_required) (void)Run(blocked_retry);
    if (blocked_retry.exchange_calls != 0) return 20;

    std::puts("rollback-vtable-slot-patch-selftest: ok");
    return 0;
}
