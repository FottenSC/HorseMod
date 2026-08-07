#pragma once

#include "RollbackSnapshotStore.hpp"

#include <cstdint>

namespace Horse
{
    enum class RollbackTerminalCheckpointSource : uint8_t
    {
        None = 0,
        TerminalStore = 1,
        RollingStore = 2,
    };

    template <typename State, typename ValidateState>
    static bool ValidateRollbackTerminalCheckpointAuthority(
        const RollbackSnapshotHandle& handle,
        const State* state,
        uint64_t expected_canonical_hash,
        ValidateState&& validate_state) noexcept
    {
        return state
            && handle.valid()
            && expected_canonical_hash != 0
            && handle.canonical_hash == expected_canonical_hash
            && state->canonical_hash == expected_canonical_hash
            && state->combined_hash == handle.integrity_hash
            && validate_state(*state);
    }

    template <typename State, typename Store, typename ValidateState>
    static bool FindRollbackTerminalCheckpointAuthority(
        Store& store,
        uint64_t epoch,
        uint32_t frame,
        uint64_t expected_canonical_hash,
        ValidateState&& validate_state,
        RollbackSnapshotHandle& out_handle,
        const State*& out_state) noexcept
    {
        out_handle = {};
        out_state = nullptr;
        RollbackSnapshotHandle handle {};
        const State* state = nullptr;
        const RollbackSnapshotStoreReport found =
            store.find(epoch, frame, handle);
        const RollbackSnapshotStoreReport loaded = found.ok
            ? store.load(handle, state)
            : found;
        if (!found.ok || !loaded.ok
            || !ValidateRollbackTerminalCheckpointAuthority(
                handle, state, expected_canonical_hash,
                static_cast<ValidateState&&>(validate_state)))
        {
            return false;
        }
        out_handle = handle;
        out_state = state;
        return true;
    }

    template <typename State, typename Store, typename ValidateState>
    static RollbackTerminalCheckpointSource
    SelectRollbackTerminalCheckpointAuthority(
        Store& terminal_store,
        Store& rolling_store,
        uint64_t epoch,
        uint32_t frame,
        uint64_t expected_canonical_hash,
        ValidateState&& validate_state,
        RollbackSnapshotHandle& out_handle,
        const State*& out_state) noexcept
    {
        out_handle = {};
        out_state = nullptr;
        if (FindRollbackTerminalCheckpointAuthority<State>(
                terminal_store, epoch, frame, expected_canonical_hash,
                validate_state, out_handle, out_state))
        {
            return RollbackTerminalCheckpointSource::TerminalStore;
        }
        if (FindRollbackTerminalCheckpointAuthority<State>(
                rolling_store, epoch, frame, expected_canonical_hash,
                validate_state, out_handle, out_state))
        {
            return RollbackTerminalCheckpointSource::RollingStore;
        }
        out_handle = {};
        out_state = nullptr;
        return RollbackTerminalCheckpointSource::None;
    }
}
