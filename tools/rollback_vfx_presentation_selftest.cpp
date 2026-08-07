#include "RollbackVfxPresentation.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <limits>

int main()
{
    using namespace Horse;

    std::array<uint8_t, 0xB0> world_request {};
    const int32_t mesh_actor_id = 42;
    const int32_t kind_arg = 7;
    const int32_t group = 9;
    const int32_t variant = 11;
    const int32_t time_scale = 3;
    const int32_t attachment_index = -1;
    std::array<float, 12> transform {};
    transform[3] = 1.0f;
    transform[8] = transform[9] = transform[10] = 1.0f;
    std::memcpy(world_request.data(), &mesh_actor_id, 4);
    world_request[4] = 2;
    std::memcpy(world_request.data() + 8, &kind_arg, 4);
    std::memcpy(world_request.data() + 12, &group, 4);
    std::memcpy(world_request.data() + 16, &variant, 4);
    std::memcpy(world_request.data() + 20, &time_scale, 4);
    std::memcpy(world_request.data() + 0x20, transform.data(),
        sizeof(transform));
    std::memcpy(world_request.data() + 0x50, &attachment_index, 4);
    RollbackVfxWorldSpawnInvocation world_invocation {};
    const uint32_t world_logical = MakeRollbackVfxLogicalSlotId(17, 3);
    if (!CaptureRollbackVfxWorldSpawnInvocation(
            world_request, world_logical, world_invocation)
        || !world_invocation.valid())
        return 47;
    std::array<uint8_t, 0xB0> rebuilt_world_request {};
    if (!RebuildRollbackVfxWorldSpawnRequest(
            world_invocation, rebuilt_world_request)
        || rebuilt_world_request != world_request)
        return 48;
    const RollbackVfxAttachmentSelector world_bone {
        RollbackVfxAttachmentKind::SkeletonBone, 1, 0, 96, 7};
    if (!CaptureRollbackVfxWorldSpawnInvocation(
            world_request, world_logical, world_invocation, world_bone)
        || world_invocation.attachment.kind
            != RollbackVfxAttachmentKind::SkeletonBone
        || world_invocation.attachment.character_role != 1
        || world_invocation.attachment.selector != 96
        || sizeof(world_invocation) > 96)
        return 50;
    world_request[0x58] = 1;
    if (CaptureRollbackVfxWorldSpawnInvocation(
            world_request, world_logical, world_invocation))
        return 49;

    if (kRollbackEventHubDeferralSupported
        || kRollbackVfxRoutes.size() != 38
        || kRollbackVfxSlotCount != 40
        || kRollbackVfxSupportedSlotsMask != 0x3FFFFFFFFFull)
        return 1;

    std::array<std::array<char, 48>, kRollbackVfxRouteCount * 3>
        trace_keys {};
    size_t trace_key_count = 0;
    for (const auto& descriptor : kRollbackVfxRoutes)
    {
        for (const char* suffix : {
                 "queued", "committed", "committed_digest"})
        {
            auto& key = trace_keys[trace_key_count++];
            if (!FormatRollbackVfxRouteTraceKey(
                    key.data(), key.size(), descriptor.vtable_offset, suffix))
                return 40;
        }
    }
    if (trace_key_count != trace_keys.size()) return 41;
    for (size_t left = 0; left < trace_keys.size(); ++left)
        for (size_t right = left + 1; right < trace_keys.size(); ++right)
            if (std::strcmp(trace_keys[left].data(),
                    trace_keys[right].data()) == 0)
                return 42;
    char invalid_key[8] {};
    if (FormatRollbackVfxRouteTraceKey(
            invalid_key, sizeof(invalid_key), 0x138, "queued"))
        return 43;

    const auto* enable_vfx = FindRollbackVfxRoute(
        RollbackVfxRoute::EnableVfx);
    if (!enable_vfx || enable_vfx->vtable_offset != 0x08
        || enable_vfx->request_bytes != 0x44
        || std::strcmp(enable_vfx->name, "enable-vfx") != 0
        || FindRollbackVfxRoute(RollbackVfxRoute::PositionalContactSound)
            ->vtable_offset != 0x28
        || FindRollbackVfxRoute(RollbackVfxRoute::TerrainContact)
            ->vtable_offset != 0x78
        || FindRollbackVfxRoute(RollbackVfxRoute::StateEdge)
            ->vtable_offset != 0xB8
        || FindRollbackVfxRoute(RollbackVfxRoute::StateCue)
            ->vtable_offset != 0xC8
        || FindRollbackVfxRoute(RollbackVfxRoute::SlotState)
            ->vtable_offset != 0xC0
        || FindRollbackVfxRoute(
            RollbackVfxRoute::WeaponNodeAlphaVisibility)
            ->vtable_offset != 0x100
        || FindRollbackVfxRoute(RollbackVfxRoute::HitCategoryVfx)
            ->vtable_offset != 0x128)
        return 2;

    uint64_t observed_slots = 0;
    for (const auto& descriptor : kRollbackVfxRoutes)
    {
        const size_t descriptor_slot = RollbackVfxSlotIndex(
            descriptor.vtable_offset);
        if (descriptor_slot >= 38
            || (observed_slots & (uint64_t {1} << descriptor_slot)) != 0
            || descriptor.value_range_count == 0
            || descriptor.value_range_count > descriptor.value_ranges.size())
            return 3;
        observed_slots |= uint64_t {1} << descriptor_slot;

        std::array<uint8_t, 0x50> source {};
        for (size_t i = 0; i < source.size(); ++i)
            source[i] = static_cast<uint8_t>(i + 1u);
        RollbackProductionVfxInvocation invocation {};
        if (!NormalizeRollbackVfxRequest(descriptor.route,
                source.data(), source.size(), invocation)
            || !RollbackVfxInvocationValid(invocation)
            || invocation.request_bytes != descriptor.request_bytes)
            return 4;

        // Every byte not consumed by the verified native adapter is zero and
        // cannot influence the normalized event identity.
        std::array<bool, 0x50> consumed {};
        for (size_t range_index = 0;
             range_index < descriptor.value_range_count; ++range_index)
        {
            const auto& range = descriptor.value_ranges[range_index];
            if (range.bytes == 0
                || static_cast<size_t>(range.offset) + range.bytes
                    > descriptor.request_bytes)
                return 5;
            for (size_t byte = range.offset;
                 byte < static_cast<size_t>(range.offset) + range.bytes;
                 ++byte)
                consumed[byte] = true;
        }
        std::array<uint8_t, 0x50> second = source;
        for (size_t byte = 0; byte < descriptor.request_bytes; ++byte)
            if (!consumed[byte]) second[byte] ^= 0xFF;
        RollbackProductionVfxInvocation normalized_second {};
        if (!NormalizeRollbackVfxRequest(descriptor.route,
                second.data(), second.size(), normalized_second)
            || std::memcmp(&invocation, &normalized_second,
                sizeof(invocation)) != 0)
            return 6;

        // Every proven value range participates in the normalized request.
        for (size_t range_index = 0;
             range_index < descriptor.value_range_count; ++range_index)
        {
            std::array<uint8_t, 0x50> mutated = source;
            mutated[descriptor.value_ranges[range_index].offset] ^= 0xFF;
            RollbackProductionVfxInvocation normalized_mutated {};
            if (!NormalizeRollbackVfxRequest(descriptor.route,
                    mutated.data(), mutated.size(), normalized_mutated)
                || std::memcmp(&invocation, &normalized_mutated,
                    sizeof(invocation)) == 0)
                return 7;
        }

        RollbackProductionVfxInvocation short_read {};
        if (NormalizeRollbackVfxRequest(descriptor.route,
                source.data(), descriptor.request_bytes - 1u, short_read))
            return 8;

        std::array<uint64_t, kRollbackVfxSlotCount> functions {};
        for (size_t i = 0; i < functions.size(); ++i)
            functions[i] = 0x1000u + i;
        const auto commit = SelectRollbackVfxCommitTarget(
            invocation, functions);
        if (!commit.valid
            || commit.function
                != functions[RollbackVfxSlotIndex(
                    descriptor.vtable_offset)]
            || commit.request != invocation.request.data())
            return 9;

        std::array<std::atomic<uint64_t>, kRollbackVfxSlotCount>
            atomic_functions {};
        const size_t slot = RollbackVfxSlotIndex(
            descriptor.vtable_offset);
        atomic_functions[slot].store(
            functions[slot], std::memory_order_release);
        const auto atomic_commit = SelectRollbackVfxCommitTarget(
            invocation, atomic_functions);
        if (!atomic_commit.valid
            || atomic_commit.function != functions[slot])
            return 10;
    }

    if (observed_slots != kRollbackVfxSupportedSlotsMask) return 11;

    RollbackProductionVfxInvocation invalid {};
    invalid.route = RollbackVfxRoute::Count;
    if (RollbackVfxInvocationValid(invalid)) return 12;
    std::array<uint64_t, kRollbackVfxSlotCount> missing {};
    if (SelectRollbackVfxCommitTarget(invalid, missing).valid) return 13;
    if (RollbackVfxSlotIndex(0) != kRollbackVfxSlotCount
        || RollbackVfxSlotIndex(0x148) != kRollbackVfxSlotCount
        || FindRollbackVfxRouteByOffset(0x138) != nullptr
        || FindRollbackVfxRouteByOffset(0x140) != nullptr)
        return 14;

    const uint32_t logical0 = MakeRollbackVfxLogicalSlotId(91, 0);
    const uint32_t logical1 = MakeRollbackVfxLogicalSlotId(91, 1);
    if (!RollbackVfxLogicalSlotId(logical0)
        || !RollbackVfxLogicalSlotId(logical1)
        || logical0 == logical1
        || RollbackVfxLogicalSlotId(kRollbackVfxInvalidSlotId)
        || MakeRollbackVfxLogicalSlotId(
            91, kRollbackVfxOrdinalsPerFrame)
                != kRollbackVfxInvalidSlotId
        || MakeRollbackVfxLogicalSlotId(
            kRollbackVfxMaxLogicalFrame, 0)
                == kRollbackVfxInvalidSlotId
        || MakeRollbackVfxLogicalSlotId(
            kRollbackVfxMaxLogicalFrame + 1u, 0)
                != kRollbackVfxInvalidSlotId
        || !RollbackVfxNativeSlotId(0)
        || !RollbackVfxNativeSlotId(0x3FFFFFFF)
        || RollbackVfxNativeSlotId(0x40000000)
        || RollbackVfxNativeSlotId(-1))
        return 15;

    const RollbackVfxAttachmentSelector no_attachment {};
    const RollbackVfxAttachmentSelector front_edge {
        RollbackVfxAttachmentKind::WeaponFrontEdge, 1, 0, 0x8017, 0};
    const RollbackVfxAttachmentSelector skeleton_bone {
        RollbackVfxAttachmentKind::SkeletonBone, 0, 0, 96, 7};
    const RollbackVfxAttachmentSelector chest_midpoint {
        RollbackVfxAttachmentKind::PlayersChestMidpoint,
        0, 0, 0, 0};
    const RollbackVfxAttachmentSelector raw_fname_is_not_a_selector {
        RollbackVfxAttachmentKind::SkeletonBone,
        0, 0, kRollbackVfxSkeletonBoneCount, 0};
    if (!RollbackVfxAttachmentSelectorValid(no_attachment)
        || !RollbackVfxAttachmentSelectorValid(front_edge)
        || !RollbackVfxAttachmentSelectorValid(skeleton_bone)
        || !RollbackVfxAttachmentSelectorValid(chest_midpoint)
        || RollbackVfxAttachmentSelectorValid(raw_fname_is_not_a_selector))
        return 44;

    RollbackVfxSlotKey guarded_key {
        100, 2, {}, 7, 9, 11, true, {}};
    RollbackVfxSlotKey debris_key {
        100, 2, {}, 7, 9, 11, false, {}};
    RollbackVfxDisableFilter exact_filter {
        100, 2, {}, 7, 9, 11, false, {}};
    RollbackVfxDisableFilter wildcard_filter {
        -1, 0xFF, {}, -1, -1, -1, false, {}};
    RollbackVfxDisableFilter wrong_variant_filter = exact_filter;
    wrong_variant_filter.variant = 12;
    if (!RollbackVfxDisableMatches(guarded_key,
            RollbackVfxSlotKind::PrimaryParticle, exact_filter)
        || RollbackVfxDisableMatches(guarded_key,
            RollbackVfxSlotKind::PrimaryParticle,
            wrong_variant_filter)
        || RollbackVfxDisableMatches(guarded_key,
            RollbackVfxSlotKind::PrimaryParticle, wildcard_filter)
        || !RollbackVfxDisableMatches(debris_key,
            RollbackVfxSlotKind::SecondaryGroundDebris,
            wildcard_filter))
        return 16;

    RollbackVfxSlotShadow<2> shadow;
    if (!shadow.begin_epoch(0x1234)
        || !shadow.reserve_create(0x1234, logical0,
            RollbackVfxSlotKind::PrimaryParticle, guarded_key)
        || !shadow.reserve_create(0x1234, logical1,
            RollbackVfxSlotKind::SecondaryGroundDebris, debris_key,
            front_edge, 91)
        || shadow.reserve_create(0x1234, logical1,
            RollbackVfxSlotKind::SecondaryGroundDebris, debris_key)
        || shadow.failure()
            != RollbackVfxShadowFailure::DuplicateLogicalId)
        return 17;
    if (shadow.lookup(logical1)->attachment.kind
            != RollbackVfxAttachmentKind::WeaponFrontEdge
        || shadow.lookup(logical1)->attachment.character_role != 1)
        return 45;

    RollbackVfxAttachmentSelector invalid_attachment = front_edge;
    invalid_attachment.selector = 0x8020;
    const uint32_t logical2 = MakeRollbackVfxLogicalSlotId(91, 2);
    if (shadow.reserve_create(0x1234, logical2,
            RollbackVfxSlotKind::PrimaryParticle, guarded_key,
            invalid_attachment)
        || shadow.failure()
            != RollbackVfxShadowFailure::InvalidAttachmentSelector)
        return 46;

    // Resolve the exact source-frame membership before either UObject exists.
    // The terminal is preserved while chronological confirmation first
    // materializes each native slot and then applies the disable.
    exact_filter.remove_immediately = true;
    std::array<uint32_t, 1> undersized {};
    size_t undersized_count = 0;
    if (shadow.apply_disable(
            0x1234, exact_filter, undersized, undersized_count)
        || shadow.failure()
            != RollbackVfxShadowFailure::OutputCapacityExhausted
        || shadow.lookup(logical0)->pending_terminal
            != RollbackVfxPendingTerminal::None
        || shadow.lookup(logical1)->pending_terminal
            != RollbackVfxPendingTerminal::None)
        return 18;
    std::array<uint32_t, 2> matched {};
    size_t matched_count = 0;
    if (!shadow.apply_disable(
            0x1234, exact_filter, matched, matched_count)
        || matched_count != 2
        || shadow.lookup(logical0)->pending_terminal
            != RollbackVfxPendingTerminal::Remove
        || !shadow.complete_create(0x1234, logical0, 17)
        || !shadow.complete_create(0x1234, logical1, 18))
        return 19;

    int32_t native_id = -1;
    if (!shadow.resolve_native(0x1234, logical0, native_id)
        || native_id != 17
        || !shadow.finalize_terminal(0x1234, logical0)
        || !shadow.finalize_terminal(0x1234, logical1)
        || shadow.occupied_count() != 0)
        return 20;

    if (shadow.reserve_create(0x9999, logical0,
            RollbackVfxSlotKind::PrimaryParticle, guarded_key)
        || shadow.failure() != RollbackVfxShadowFailure::StaleEpoch)
        return 21;
    RollbackVfxSlotShadow<2> rewind_shadow;
    if (!rewind_shadow.begin_epoch(0x55)
        || !rewind_shadow.reserve_create(0x55, logical0,
            RollbackVfxSlotKind::PrimaryParticle, guarded_key, {}, 90)
        || !rewind_shadow.reserve_create(0x55, logical1,
            RollbackVfxSlotKind::SecondaryGroundDebris, debris_key, {}, 91))
        return 50;
    rewind_shadow.discard_reserved_after(0x55, 90);
    if (rewind_shadow.lookup(logical0) == nullptr
        || rewind_shadow.lookup(logical1) != nullptr)
        return 51;
    std::array<uint32_t, 1> rewind_match {};
    size_t rewind_match_count = 0;
    if (!rewind_shadow.complete_create(0x55, logical0, 31)
        || !rewind_shadow.apply_disable(0x55, exact_filter,
            rewind_match, rewind_match_count, 92)
        || rewind_match_count != 1
        || rewind_shadow.lookup(logical0)->source_alive)
        return 52;
    rewind_shadow.discard_reserved_after(0x55, 90);
    if (!rewind_shadow.lookup(logical0)->source_alive
        || rewind_shadow.lookup(logical0)->pending_terminal
            != RollbackVfxPendingTerminal::None)
        return 53;
    shadow.revoke();
    if (shadow.epoch() != 0
        || shadow.reserve_create(0x1234, logical0,
            RollbackVfxSlotKind::PrimaryParticle, guarded_key)
        || shadow.failure() != RollbackVfxShadowFailure::EpochInvalid)
        return 22;

    RollbackVfxManagerInvocation time_dilation {};
    time_dilation.operation =
        RollbackVfxManagerOperation::SetGroupTimeDilation;
    time_dilation.group = 3;
    time_dilation.time_dilation = 0.5f;
    RollbackVfxManagerInvocation hidden_filter {};
    hidden_filter.operation = RollbackVfxManagerOperation::SetHiddenFilter;
    hidden_filter.hidden = 1;
    hidden_filter.time_dilation = 1.0f;
    hidden_filter.filter_kind_tag = 2;
    hidden_filter.filter_group = 3;
    hidden_filter.filter_variant = 4;
    RollbackVfxManagerInvocation clear_filters {};
    clear_filters.operation = RollbackVfxManagerOperation::ClearHiddenFilters;
    clear_filters.time_dilation = 1.0f;
    if (!time_dilation.valid() || !hidden_filter.valid()
        || !clear_filters.valid())
        return 54;
    auto invalid_manager = hidden_filter;
    invalid_manager.reserved[0] = 1;
    if (invalid_manager.valid()) return 55;
    invalid_manager = time_dilation;
    invalid_manager.time_dilation =
        std::numeric_limits<float>::quiet_NaN();
    if (invalid_manager.valid()) return 56;
    invalid_manager = clear_filters;
    invalid_manager.filter_group = 1;
    if (invalid_manager.valid()) return 57;

    std::puts("rollback-vfx-presentation-selftest: ok");
    return 0;
}
