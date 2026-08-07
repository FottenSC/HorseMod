// ============================================================================
// Horse::RollbackSnapshotStore
//
// Fixed-capacity ownership for full native rollback snapshots. Gekko receives
// only RollbackSnapshotHandle values; the heavyweight state never enters the
// Gekko state buffer or network transport.
// ============================================================================

#pragma once

#include "RollbackFrameStamp.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace Horse
{
    template <typename State, typename = void>
    struct RollbackHasCapacityPreserver : std::false_type {};

    template <typename State>
    struct RollbackHasCapacityPreserver<State, std::void_t<decltype(
        std::declval<State&>().preserve_capacities_from(
        std::declval<const State&>()))>> : std::true_type {};

    template <typename State, typename = void>
    struct RollbackHasCaptureRecycler : std::false_type {};

    template <typename State>
    struct RollbackHasCaptureRecycler<State, std::void_t<decltype(
        std::declval<State&>().recycle_for_capture())>> : std::true_type {};

    template <typename State, typename = void>
    struct RollbackHasPreallocatedCopier : std::false_type {};

    template <typename State>
    struct RollbackHasPreallocatedCopier<State, std::void_t<decltype(
        std::declval<State&>().copy_preallocated_from(
            std::declval<const State&>()))>> : std::true_type {};

    static constexpr size_t kRollbackProductionSnapshotCapacity = 64;

    struct RollbackSnapshotHandle
    {
        uint64_t epoch {0};
        uint32_t frame {0};
        uint32_t generation {0};
        uint64_t integrity_hash {0};
        uint64_t canonical_hash {0};

        bool valid() const noexcept
        {
            return epoch != 0 && generation != 0
                && integrity_hash != 0 && canonical_hash != 0;
        }
    };

    static_assert(sizeof(RollbackSnapshotHandle) == 32,
        "Gekko save state handle must remain compact and fixed-size");

    enum class RollbackSnapshotStoreStatus : uint8_t
    {
        Ok,
        InvalidArgument,
        ProtectedSlot,
        AllocationFailed,
        Missing,
        StaleHandle,
    };

    struct RollbackSnapshotStoreReport
    {
        bool ok {false};
        // True only when State supplied copy_preallocated_from() and that
        // state-specific copier proved the destination reused its existing
        // storage. Ordinary copy assignment can be correct for setup-time
        // stores, but it is not evidence for an allocation-free live
        // terminal checkpoint.
        bool preallocated_copy_verified {false};
        RollbackSnapshotStoreStatus status {
            RollbackSnapshotStoreStatus::InvalidArgument};
        uint32_t slot {0};
        uint32_t generation {0};
        const char* failure {"invalid-argument"};
    };

    template <typename State,
              size_t Capacity = kRollbackProductionSnapshotCapacity>
    class RollbackSnapshotStore
    {
        static_assert(Capacity >= 2, "snapshot store needs at least two slots");
        static_assert((Capacity & (Capacity - 1)) == 0,
            "snapshot capacity must be a power of two");

        struct Slot
        {
            RollbackSnapshotHandle handle {};
            std::unique_ptr<State> state;
            uint32_t generation {0};
        };

    public:
        RollbackSnapshotStore() = default;
        RollbackSnapshotStore(const RollbackSnapshotStore&) = delete;
        RollbackSnapshotStore& operator=(const RollbackSnapshotStore&) = delete;

        bool prepare(
            const State& exemplar,
            uint32_t maximum_load_window = 12) noexcept
        {
            size_t active_capacity = 2;
            const size_t required =
                static_cast<size_t>(maximum_load_window) + 2u;
            while (active_capacity < required && active_capacity < Capacity)
                active_capacity <<= 1u;
            if (active_capacity < required || active_capacity > Capacity)
                return false;
            try
            {
                for (size_t i = 0; i < m_slots.size(); ++i)
                {
                    Slot& slot = m_slots[i];
                    if (i >= active_capacity)
                    {
                        slot.state.reset();
                        slot.handle = {};
                        continue;
                    }
                    if (!slot.state)
                        slot.state = std::make_unique<State>(exemplar);
                    else
                        *slot.state = exemplar;
                    if constexpr (RollbackHasCapacityPreserver<State>::value)
                    {
                        slot.state->preserve_capacities_from(exemplar);
                    }
                    if constexpr (RollbackHasCaptureRecycler<State>::value)
                        slot.state->recycle_for_capture();
                    else
                        slot.state->clear();
                    slot.handle = {};
                }
            }
            catch (...)
            {
                clear();
                return false;
            }
            m_active_capacity = active_capacity;
            m_saves = 0;
            return true;
        }

        RollbackSnapshotStoreReport save_recycling(
            uint64_t epoch,
            uint32_t frame,
            uint64_t integrity_hash,
            uint64_t canonical_hash,
            State& state,
            RollbackFrameStamp current_frame,
            uint32_t maximum_load_window,
            RollbackSnapshotHandle& out) noexcept
        {
            out = {};
            RollbackSnapshotStoreReport report {};
            report.slot = slot_for(frame);
            if (epoch == 0 || integrity_hash == 0 || canonical_hash == 0
                || !current_frame.valid
                || maximum_load_window >= m_active_capacity)
            {
                return report;
            }
            Slot& slot = m_slots[report.slot];
            if (!slot.state)
            {
                report.status =
                    RollbackSnapshotStoreStatus::AllocationFailed;
                report.failure = "snapshot-store-not-prepared";
                return report;
            }
            if (slot.handle.valid())
            {
                const bool same_epoch = slot.handle.epoch == epoch;
                const bool same_frame = same_epoch
                    && slot.handle.frame == frame;
                const uint32_t age = RollbackFrameDistance(
                    current_frame.value, slot.handle.frame);
                if (!same_frame && same_epoch && age < 0x80000000u
                    && age <= maximum_load_window)
                {
                    report.status =
                        RollbackSnapshotStoreStatus::ProtectedSlot;
                    report.failure = "snapshot-slot-still-loadable";
                    return report;
                }
            }

            using std::swap;
            swap(*slot.state, state);
            uint32_t generation = slot.generation + 1u;
            if (generation == 0) generation = 1;
            slot.generation = generation;
            slot.handle = RollbackSnapshotHandle {
                epoch, frame, generation, integrity_hash, canonical_hash,
            };
            out = slot.handle;
            report.ok = true;
            report.status = RollbackSnapshotStoreStatus::Ok;
            report.generation = generation;
            report.failure = "ok";
            ++m_saves;
            return report;
        }

        // Copy into a slot allocated by prepare(). This is for the one
        // long-lived terminal checkpoint that must outlive the rolling Gekko
        // history without increasing that history window.
        RollbackSnapshotStoreReport save_preallocated_copy(
            uint64_t epoch,
            uint32_t frame,
            uint64_t integrity_hash,
            uint64_t canonical_hash,
            const State& state,
            RollbackSnapshotHandle& out) noexcept
        {
            out = {};
            RollbackSnapshotStoreReport report {};
            report.slot = slot_for(frame);
            if (epoch == 0 || integrity_hash == 0 || canonical_hash == 0)
                return report;
            Slot& slot = m_slots[report.slot];
            if (!slot.state)
            {
                report.status = RollbackSnapshotStoreStatus::AllocationFailed;
                report.failure = "snapshot-store-not-prepared";
                return report;
            }
            try
            {
                if constexpr (RollbackHasPreallocatedCopier<State>::value)
                {
                    if (!slot.state->copy_preallocated_from(state))
                    {
                        slot.handle = {};
                        report.status =
                            RollbackSnapshotStoreStatus::AllocationFailed;
                        report.failure =
                            "snapshot-preallocated-capacity-mismatch";
                        return report;
                    }
                    report.preallocated_copy_verified = true;
                }
                else
                {
                    *slot.state = state;
                }
            }
            catch (...)
            {
                slot.handle = {};
                report.status = RollbackSnapshotStoreStatus::AllocationFailed;
                report.failure = "snapshot-preallocated-copy-failed";
                return report;
            }
            uint32_t generation = slot.generation + 1u;
            if (generation == 0) generation = 1;
            slot.generation = generation;
            slot.handle = RollbackSnapshotHandle {
                epoch, frame, generation, integrity_hash, canonical_hash,
            };
            out = slot.handle;
            report.ok = true;
            report.status = RollbackSnapshotStoreStatus::Ok;
            report.generation = generation;
            report.failure = "ok";
            ++m_saves;
            return report;
        }

        RollbackSnapshotStoreReport save(
            uint64_t epoch,
            uint32_t frame,
            uint64_t integrity_hash,
            uint64_t canonical_hash,
            const State& state,
            RollbackFrameStamp current_frame,
            uint32_t maximum_load_window,
            RollbackSnapshotHandle& out) noexcept
        {
            return save_impl(
                epoch, frame, integrity_hash, canonical_hash, state,
                current_frame, maximum_load_window, out);
        }

        RollbackSnapshotStoreReport save(
            uint64_t epoch,
            uint32_t frame,
            uint64_t integrity_hash,
            uint64_t canonical_hash,
            State&& state,
            RollbackFrameStamp current_frame,
            uint32_t maximum_load_window,
            RollbackSnapshotHandle& out) noexcept
        {
            return save_impl(
                epoch, frame, integrity_hash, canonical_hash,
                std::move(state), current_frame, maximum_load_window, out);
        }

    private:
        template <typename StateArg>
        RollbackSnapshotStoreReport save_impl(
            uint64_t epoch,
            uint32_t frame,
            uint64_t integrity_hash,
            uint64_t canonical_hash,
            StateArg&& state,
            RollbackFrameStamp current_frame,
            uint32_t maximum_load_window,
            RollbackSnapshotHandle& out) noexcept
        {
            out = {};
            RollbackSnapshotStoreReport report {};
            report.slot = slot_for(frame);
            if (epoch == 0 || integrity_hash == 0 || canonical_hash == 0
                || !current_frame.valid || maximum_load_window >= 0x80000000u)
            {
                return report;
            }

            Slot& slot = m_slots[report.slot];
            if (slot.state)
            {
                if (slot.handle.epoch == epoch
                    && slot.handle.frame == frame
                    && slot.handle.integrity_hash == integrity_hash
                    && slot.handle.canonical_hash == canonical_hash)
                {
                    out = slot.handle;
                    report.ok = true;
                    report.status = RollbackSnapshotStoreStatus::Ok;
                    report.generation = slot.handle.generation;
                    report.failure = "ok-existing";
                    return report;
                }

                const bool same_epoch = slot.handle.epoch == epoch;
                const bool same_frame = same_epoch
                    && slot.handle.frame == frame;
                const uint32_t age = RollbackFrameDistance(
                    current_frame.value, slot.handle.frame);
                const bool old_not_in_future = age < 0x80000000u;
                // Gekko replaces the saved state for a corrected frame after
                // rollback. Its internal state buffer is overwritten with the
                // new handle, so a same-frame save must replace our full state
                // too. Generation bumping below invalidates any stale copy.
                if (!same_frame && same_epoch && old_not_in_future
                    && age <= maximum_load_window)
                {
                    report.status = RollbackSnapshotStoreStatus::ProtectedSlot;
                    report.failure = "snapshot-slot-still-loadable";
                    return report;
                }
            }

            std::unique_ptr<State> copy;
            try
            {
                copy = std::make_unique<State>(
                    std::forward<StateArg>(state));
            }
            catch (const std::bad_alloc&)
            {
                report.status = RollbackSnapshotStoreStatus::AllocationFailed;
                report.failure = "snapshot-allocation-failed";
                return report;
            }
            catch (...)
            {
                report.status = RollbackSnapshotStoreStatus::AllocationFailed;
                report.failure = "snapshot-copy-failed";
                return report;
            }

            uint32_t generation = slot.handle.generation + 1u;
            if (generation == 0) generation = 1;
            slot.handle = RollbackSnapshotHandle {
                epoch,
                frame,
                generation,
                integrity_hash,
                canonical_hash,
            };
            slot.state = std::move(copy);
            out = slot.handle;
            report.ok = true;
            report.status = RollbackSnapshotStoreStatus::Ok;
            report.generation = generation;
            report.failure = "ok";
            ++m_saves;
            return report;
        }

    public:

        RollbackSnapshotStoreReport load(
            const RollbackSnapshotHandle& handle,
            const State*& out) const noexcept
        {
            out = nullptr;
            RollbackSnapshotStoreReport report {};
            report.slot = slot_for(handle.frame);
            if (!handle.valid())
                return report;

            const Slot& slot = m_slots[report.slot];
            if (!slot.state)
            {
                report.status = RollbackSnapshotStoreStatus::Missing;
                report.failure = "snapshot-missing";
                return report;
            }
            if (slot.handle.epoch != handle.epoch
                || slot.handle.frame != handle.frame
                || slot.handle.generation != handle.generation
                || slot.handle.integrity_hash != handle.integrity_hash
                || slot.handle.canonical_hash != handle.canonical_hash)
            {
                report.status = RollbackSnapshotStoreStatus::StaleHandle;
                report.failure = "snapshot-handle-stale";
                return report;
            }
            out = slot.state.get();
            report.ok = true;
            report.status = RollbackSnapshotStoreStatus::Ok;
            report.generation = slot.handle.generation;
            report.failure = "ok";
            return report;
        }

        RollbackSnapshotStoreReport find(
            uint64_t epoch,
            uint32_t frame,
            RollbackSnapshotHandle& out) const noexcept
        {
            out = {};
            RollbackSnapshotStoreReport report {};
            report.slot = slot_for(frame);
            if (epoch == 0) return report;
            const Slot& slot = m_slots[report.slot];
            if (!slot.state || !slot.handle.valid())
            {
                report.status = RollbackSnapshotStoreStatus::Missing;
                report.failure = "snapshot-missing";
                return report;
            }
            if (slot.handle.epoch != epoch || slot.handle.frame != frame)
            {
                report.status = RollbackSnapshotStoreStatus::StaleHandle;
                report.failure = "snapshot-handle-stale";
                return report;
            }
            out = slot.handle;
            report.ok = true;
            report.status = RollbackSnapshotStoreStatus::Ok;
            report.generation = slot.handle.generation;
            report.failure = "ok";
            return report;
        }

        void clear() noexcept
        {
            for (Slot& slot : m_slots)
            {
                slot.state.reset();
                slot.handle = {};
                slot.generation = 0;
            }
            m_saves = 0;
            m_active_capacity = Capacity;
        }

        // Invalidate every handle at a round boundary without releasing any
        // arena storage. Old handles remain stale because both the round epoch
        // and monotonically increasing slot generation change.
        void invalidate_recycling() noexcept
        {
            for (size_t i = 0; i < m_active_capacity; ++i)
            {
                Slot& slot = m_slots[i];
                if (slot.state)
                {
                    if constexpr (RollbackHasCaptureRecycler<State>::value)
                        slot.state->recycle_for_capture();
                    else
                        slot.state->clear();
                }
                slot.handle = {};
            }
            m_saves = 0;
        }

        template <typename ExpectedCapacity, typename CaptureCapacityFn>
        bool preallocated_matches(
            uint32_t maximum_load_window,
            const ExpectedCapacity& expected_capacity,
            CaptureCapacityFn&& capture_capacity) const noexcept
        {
            size_t required_capacity = 2;
            const size_t required =
                static_cast<size_t>(maximum_load_window) + 2u;
            while (required_capacity < required
                && required_capacity < Capacity)
            {
                required_capacity <<= 1u;
            }
            if (required_capacity != m_active_capacity
                || !expected_capacity.valid)
            {
                return false;
            }
            for (size_t i = 0; i < m_active_capacity; ++i)
            {
                const Slot& slot = m_slots[i];
                if (!slot.state
                    || !(capture_capacity(*slot.state)
                        == expected_capacity))
                {
                    return false;
                }
            }
            return true;
        }

        size_t occupied() const noexcept
        {
            size_t count = 0;
            for (const Slot& slot : m_slots)
                if (slot.state) ++count;
            return count;
        }

        uint64_t saves() const noexcept { return m_saves; }

    private:
        uint32_t slot_for(uint32_t frame) const noexcept
        {
            return frame & static_cast<uint32_t>(m_active_capacity - 1u);
        }

        std::array<Slot, Capacity> m_slots {};
        size_t m_active_capacity {Capacity};
        uint64_t m_saves {0};
    };

}
