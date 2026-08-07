#include "ReplayAnimationPresentation.hpp"

#include <array>
#include <cassert>
#include <cstring>
#include <cstdint>
#include <iostream>

namespace
{
    constexpr uintptr_t kImageBase = 0x140000000ull;
    constexpr uintptr_t kBase = 0x1000;
    constexpr size_t kBufferBytes = 0x700;
    constexpr uintptr_t kPresentationActor = 0x1000;
    constexpr uintptr_t kPresentationOutput = 0x3000;
    constexpr uintptr_t kPresentationRows = 0x6000;
    constexpr uintptr_t kPresentationProvider = 0x9000;
    constexpr uintptr_t kAlternateOutput = 0xA000;
    constexpr uintptr_t kAlternateRows = 0xD000;
    constexpr size_t kPresentationMemoryBytes = 0x10000;

    struct Memory
    {
        std::array<uint8_t, kBufferBytes> bytes{};
        uintptr_t vtable {0};
        uintptr_t tick_function {0};
        uintptr_t failed_write {0};
        uintptr_t failed_read {0};
        bool failed_write_consumed {false};

        bool read(uintptr_t address, uint8_t& value) const noexcept
        {
            if (address == failed_read || address < kBase
                || address - kBase >= bytes.size())
                return false;
            value = bytes[static_cast<size_t>(address - kBase)];
            return true;
        }

        bool write(uintptr_t address, uint8_t value) noexcept
        {
            if (address == failed_write && !failed_write_consumed)
            {
                failed_write_consumed = true;
                return false;
            }
            if (address < kBase
                || address - kBase >= bytes.size())
                return false;
            bytes[static_cast<size_t>(address - kBase)] = value;
            return true;
        }

        void put_pointer(uintptr_t value) noexcept
        {
            vtable = value;
            std::memcpy(bytes.data(), &value, sizeof(value));
        }

        void put_tick_function(uintptr_t value) noexcept
        {
            tick_function = value;
        }

        bool read_pointer(uintptr_t address,
                          uintptr_t& value) const noexcept
        {
            if (address == kBase)
            {
                std::memcpy(&value, bytes.data(), sizeof(value));
                return true;
            }
            if (vtable
                && address
                    == vtable
                        + Horse::kReplayActorTickVtableSlotOffset)
            {
                value = tick_function;
                return true;
            }
            return false;
        }
    };

    struct PresentationMemory
    {
        std::array<uint8_t, kPresentationMemoryBytes> bytes {};
        uintptr_t provider {kPresentationProvider};
        uintptr_t failed_write {0};
        bool failed_write_consumed {false};

        bool read(uintptr_t address, void* dst, size_t size) const noexcept
        {
            if (!dst || address + size > bytes.size())
                return false;
            std::memcpy(dst, bytes.data() + address, size);
            return true;
        }

        bool write(uintptr_t address, const void* src,
                   size_t size) noexcept
        {
            if (address == failed_write && !failed_write_consumed)
            {
                failed_write_consumed = true;
                return false;
            }
            if (!src || address + size > bytes.size())
                return false;
            std::memcpy(bytes.data() + address, src, size);
            return true;
        }

        template <typename T>
        void put(uintptr_t address, const T& value) noexcept
        {
            assert(write(address, &value, sizeof(value)));
        }

        template <typename T>
        T get(uintptr_t address) const noexcept
        {
            T value {};
            assert(read(address, &value, sizeof(value)));
            return value;
        }

        bool resolve_provider(uintptr_t actor,
                              uintptr_t& out) const noexcept
        {
            if (actor != kPresentationActor)
                return false;
            out = provider;
            return true;
        }
    };

    void SeedPresentationMemory(PresentationMemory& memory)
    {
        memory.put(kPresentationActor
                + Horse::kReplayPresentationOutputOffset,
            kPresentationOutput);
        const uintptr_t material = 0x8000;
        memory.put(kPresentationActor
                + Horse::kReplayPresentationDynamicMaterialOffset,
            material);
        memory.put(kPresentationOutput
                + Horse::kReplayOutputMaterialRowsOffset,
            kPresentationRows);
        const int32_t row_count = 4;
        memory.put(kPresentationOutput
                + Horse::kReplayOutputMaterialRowCountOffset,
            row_count);

        const int32_t effect_cursor = 10;
        const int32_t animation_cursor = 20;
        const int32_t material_index = 2;
        memory.put(kPresentationActor
                + Horse::kReplayEffectFadeCursorOffset,
            effect_cursor);
        memory.put(kPresentationActor
                + Horse::kReplayAnimOverrideCursorOffset,
            animation_cursor);
        memory.put(kPresentationActor
                + Horse::kReplayEffectMaterialIndexOffset,
            material_index);
        for (size_t i = 0;
             i < Horse::kReplayEffectParameterBlockBytes;
             ++i)
        {
            memory.bytes[kPresentationActor
                + Horse::kReplayEffectParameterBlockOffset + i] =
                    static_cast<uint8_t>(i ^ 0xA5u);
        }

        memory.put(kPresentationActor
                + Horse::kReplayAnimHoldFramesOffset,
            int32_t{30});
        memory.put(kPresentationActor
                + Horse::kReplayAnimFadeOutFramesOffset,
            int32_t{40});
        memory.put(kPresentationActor
                + Horse::kReplayAnimFadeInFramesOffset,
            int32_t{50});
        memory.put(kPresentationActor
                + Horse::kReplayAnimTargetRateOffset,
            125.0f);
        memory.put(kPresentationActor
                + Horse::kReplayAnimTargetParam844Offset,
            75.0f);
        memory.put(kPresentationActor
                + Horse::kReplayAnimTargetParam85COffset,
            3.5f);
        memory.put(kPresentationActor
                + Horse::kReplayAnimCachedDistanceOffset,
            12.25f);
        memory.put(kPresentationActor
                + Horse::kReplayAnimModeOffset,
            uint8_t{6});

        memory.put(kPresentationOutput
                + Horse::kReplayOutputEffectDirtyOffset,
            uint32_t{0x100});
        memory.put(kPresentationOutput
                + Horse::kReplayOutputAnimDirtyPrimaryOffset,
            uint32_t{0x200});
        memory.put(kPresentationOutput
                + Horse::kReplayOutputAnimDirtySecondaryOffset,
            uint32_t{0x400});
        memory.put(kPresentationOutput
                + Horse::kReplayOutputNeedsAnimUpdateOffset,
            uint8_t{0});

        const std::array<uintptr_t, 5> effect_offsets {
            Horse::kReplayOutputEffectScale420Offset,
            Horse::kReplayOutputEffectScale430Offset,
            Horse::kReplayOutputEffectScale440Offset,
            Horse::kReplayOutputEffectColorAOffset,
            Horse::kReplayOutputEffectColorBOffset,
        };
        for (size_t block = 0; block < effect_offsets.size(); ++block)
        {
            std::array<float, 4> values {
                static_cast<float>(block * 10 + 1),
                static_cast<float>(block * 10 + 2),
                static_cast<float>(block * 10 + 3),
                static_cast<float>(block * 10 + 4),
            };
            assert(memory.write(kPresentationOutput
                    + effect_offsets[block],
                values.data(), sizeof(values)));
        }

        memory.put(kPresentationOutput
                + Horse::kReplayOutputAnimParam838Offset,
            1.25f);
        memory.put(kPresentationOutput
                + Horse::kReplayOutputAnimParam844Offset,
            2.25f);
        memory.put(kPresentationOutput
                + Horse::kReplayOutputAnimParam848Offset,
            3.25f);
        memory.put(kPresentationOutput
                + Horse::kReplayOutputAnimParam858Offset,
            4.25f);
        memory.put(kPresentationOutput
                + Horse::kReplayOutputAnimParam85COffset,
            5.25f);

        const uintptr_t selected_row =
            kPresentationRows
            + static_cast<uintptr_t>(material_index)
                * Horse::kReplayOutputMaterialRowStride;
        memory.put(selected_row, 0.625f);
        for (size_t i = sizeof(float);
             i < Horse::kReplayOutputMaterialRowStride;
             ++i)
        {
            memory.bytes[selected_row + i] =
                static_cast<uint8_t>(0xD0u + i);
        }
    }

    Horse::ReplayAnimationPresentationSnapshot CapturePresentation(
        PresentationMemory& memory,
        Horse::ReplayAnimationPresentationCapturePhase phase)
    {
        Horse::ReplayAnimationPresentationSnapshot snapshot {};
        const bool ok = Horse::CaptureReplayAnimationPresentation(
            kPresentationActor, phase, snapshot,
            [&](uintptr_t address, void* dst, size_t size) {
                return memory.read(address, dst, size);
            },
            [&](uintptr_t actor, uintptr_t& provider) {
                return memory.resolve_provider(actor, provider);
            });
        assert(ok);
        assert(snapshot.valid);
        return snapshot;
    }
}

int main()
{
    using RestoreMode =
        Horse::ReplayAnimationPresentationRestoreMode;
    using CapturePhase =
        Horse::ReplayAnimationPresentationCapturePhase;
    assert(
        Horse::SelectReplayAnimationPresentationRestoreMode(
            true, false,
            Horse::kReplayAnimationPresentationSnapshotVersion,
            CapturePhase::Unavailable)
            == RestoreMode::NoPresentationLane);
    assert(
        Horse::SelectReplayAnimationPresentationRestoreMode(
            true, true,
            Horse::kReplayAnimationPresentationSnapshotVersion,
            CapturePhase::AfterNativeConsumers)
            == RestoreMode::Exact);
    assert(
        Horse::SelectReplayAnimationPresentationRestoreMode(
            true, false,
            Horse::kReplayAnimationPresentationSnapshotVersion,
            CapturePhase::AfterNativeConsumers)
            == RestoreMode::Reseed);
    assert(
        Horse::SelectReplayAnimationPresentationRestoreMode(
            true, true,
            Horse::kReplayAnimationPresentationSnapshotVersion + 1,
            CapturePhase::Unavailable)
            == RestoreMode::Reseed);

    {
        using CaptureFailure =
            Horse::ReplayAnimationPresentationCaptureFailure;
        Horse::ReplayAnimationPresentationSnapshot snapshot{};
        CaptureFailure failure = CaptureFailure::None;
        PresentationMemory memory{};
        SeedPresentationMemory(memory);
        auto read = [&](uintptr_t address, void* dst, size_t size) {
            return memory.read(address, dst, size);
        };
        auto resolve = [&](uintptr_t actor, uintptr_t& provider) {
            return memory.resolve_provider(actor, provider);
        };

        assert(!Horse::CaptureReplayAnimationPresentation(
            0, CapturePhase::AfterNativeConsumers, snapshot,
            read, resolve, &failure));
        assert(failure == CaptureFailure::ActorUnavailable);
        assert(!Horse::CaptureReplayAnimationPresentation(
            kPresentationActor, CapturePhase::Unavailable, snapshot,
            read, resolve, &failure));
        assert(failure == CaptureFailure::PhaseUnavailable);
        assert(!Horse::CaptureReplayAnimationPresentation(
            kPresentationActor, CapturePhase::AfterNativeConsumers,
            snapshot,
            [&](uintptr_t address, void* dst, size_t size) {
                if (address
                    == kPresentationActor
                        + Horse::kReplayPresentationOutputOffset)
                    return false;
                return memory.read(address, dst, size);
            },
            resolve, &failure));
        assert(failure == CaptureFailure::OutputReadFailed);
        assert(std::strcmp(
            Horse::ReplayAnimationPresentationCaptureFailureName(
                failure),
            "output-read-failed") == 0);
    }

    {
        constexpr auto normal =
            Horse::ReplayTimelinePresentationPolicyFor(false);
        static_assert(!normal.suppress_render_submission);
        static_assert(!normal.suppress_character_actor_ticks);
        static_assert(normal.retain_demo_animation_sync);
        static_assert(normal.retain_anim_instance_sync);

        constexpr auto no_render =
            Horse::ReplayTimelinePresentationPolicyFor(true);
        static_assert(no_render.suppress_render_submission);
        static_assert(!no_render.suppress_character_actor_ticks);
        static_assert(no_render.retain_demo_animation_sync);
        static_assert(no_render.retain_anim_instance_sync);
    }

    {
        using TargetKind =
            Horse::ReplayAnimationRefreshTargetKind;
        static_assert(
            Horse::ClassifyReplayAnimationRefreshTarget(
                kImageBase,
                kImageBase + Horse::kReplayNativePlayerVtableRva,
                kImageBase + Horse::kReplayBattleActorTickRva)
            == TargetKind::Invalid);
        static_assert(
            Horse::ClassifyReplayAnimationRefreshTarget(
                kImageBase,
                kImageBase + Horse::kReplayBattleActorVtableRva,
                kImageBase + Horse::kReplayBattleActorTickRva)
            == TargetKind::BattleActor);
        static_assert(
            Horse::ClassifyReplayAnimationRefreshTarget(
                kImageBase,
                kImageBase + Horse::kReplayDemoHumanActorVtableRva,
                kImageBase + Horse::kReplayDemoHumanActorTickRva)
            == TargetKind::DemoHumanActor);
        static_assert(
            Horse::ClassifyReplayAnimationRefreshTarget(
                kImageBase,
                kImageBase + Horse::kReplayBattleActorVtableRva,
                kImageBase + Horse::kReplayDemoHumanActorTickRva)
            == TargetKind::Invalid);

        auto arm = [](Memory& memory) {
            return Horse::ArmReplayAnimationPresentationRefresh(
                kImageBase, kBase,
                [&](uintptr_t address, uintptr_t& value) {
                    return memory.read_pointer(address, value);
                },
                [&](uintptr_t address, uint8_t& value) {
                    return memory.read(address, value);
                },
                [&](uintptr_t address, uint8_t value) {
                    return memory.write(address, value);
                });
        };

        Memory native_player{};
        native_player.put_pointer(
            kImageBase + Horse::kReplayNativePlayerVtableRva);
        native_player.put_tick_function(
            kImageBase + Horse::kReplayBattleActorTickRva);
        native_player.bytes[0x530] = 3;
        native_player.bytes[0x531] = 4;
        native_player.bytes[0x616] = 5;
        const auto native_report = arm(native_player);
        assert(native_report.identity_read_ok);
        assert(native_report.target_kind == TargetKind::Invalid);
        assert(!native_report.ok);
        assert(native_player.bytes[0x530] == 3);
        assert(native_player.bytes[0x531] == 4);
        assert(native_player.bytes[0x616] == 5);

        Memory battle_actor{};
        battle_actor.put_pointer(
            kImageBase + Horse::kReplayBattleActorVtableRva);
        battle_actor.put_tick_function(
            kImageBase + Horse::kReplayBattleActorTickRva);
        battle_actor.bytes[0x530] = 0;
        battle_actor.bytes[0x531] = 7;
        battle_actor.bytes[0x616] = 9;
        const auto battle_report = arm(battle_actor);
        assert(battle_report.ok);
        assert(battle_report.target_kind == TargetKind::BattleActor);
        assert(battle_report.field_count == 2);
        assert(battle_actor.bytes[0x530] == 1);
        assert(battle_actor.bytes[0x531] == 1);
        assert(battle_actor.bytes[0x616] == 9);

        Memory demo_actor{};
        demo_actor.put_pointer(
            kImageBase + Horse::kReplayDemoHumanActorVtableRva);
        demo_actor.put_tick_function(
            kImageBase + Horse::kReplayDemoHumanActorTickRva);
        demo_actor.bytes[0x530] = 0;
        demo_actor.bytes[0x531] = 7;
        demo_actor.bytes[0x616] = 0;
        const auto demo_report = arm(demo_actor);
        assert(demo_report.ok);
        assert(demo_report.target_kind == TargetKind::DemoHumanActor);
        assert(demo_report.field_count == 3);
        assert(demo_report.fields[0].before == 0);
        assert(demo_report.fields[1].before == 7);
        assert(demo_report.fields[2].before == 0);
        assert(demo_actor.bytes[0x530] == 1);
        assert(demo_actor.bytes[0x531] == 1);
        assert(demo_actor.bytes[0x616] == 1);
        assert(demo_actor.bytes[0x52F] == 0);
        assert(demo_actor.bytes[0x532] == 0);
        assert(demo_actor.bytes[0x615] == 0);
        assert(demo_actor.bytes[0x617] == 0);

        Memory failed_write{};
        failed_write.put_pointer(
            kImageBase + Horse::kReplayDemoHumanActorVtableRva);
        failed_write.put_tick_function(
            kImageBase + Horse::kReplayDemoHumanActorTickRva);
        failed_write.failed_write = kBase + 0x531;
        const auto failed_write_report = arm(failed_write);
        assert(!failed_write_report.ok);
        assert(failed_write_report.rollback_attempted);
        assert(failed_write_report.rollback_ok);
        assert(failed_write_report.fields[0].ok());
        assert(!failed_write_report.fields[1].ok());
        assert(failed_write_report.fields[2].ok());
        assert(failed_write.bytes[0x530] == 0);
        assert(failed_write.bytes[0x531] == 0);
        assert(failed_write.bytes[0x616] == 0);

        Memory failed_read{};
        failed_read.put_pointer(
            kImageBase + Horse::kReplayDemoHumanActorVtableRva);
        failed_read.put_tick_function(
            kImageBase + Horse::kReplayDemoHumanActorTickRva);
        failed_read.failed_read = kBase + 0x616;
        const auto failed_read_report = arm(failed_read);
        assert(!failed_read_report.ok);
        assert(!failed_read_report.rollback_attempted);
        assert(!failed_read_report.fields[2].write_ok);
        assert(failed_read.bytes[0x616] == 0);

        Memory unknown_actor{};
        unknown_actor.put_pointer(kImageBase + 0x123456);
        unknown_actor.put_tick_function(
            kImageBase + Horse::kReplayBattleActorTickRva);
        const auto unknown_report = arm(unknown_actor);
        assert(unknown_report.identity_read_ok);
        assert(unknown_report.target_kind == TargetKind::Invalid);
        assert(!unknown_report.ok);

        Memory null_actor{};
        const auto null_report =
            Horse::ArmReplayAnimationPresentationRefresh(
                kImageBase, 0,
                [&](uintptr_t address, uintptr_t& value) {
                    return null_actor.read_pointer(address, value);
                },
                [&](uintptr_t address, uint8_t& value) {
                    return null_actor.read(address, value);
                },
                [&](uintptr_t address, uint8_t value) {
                    return null_actor.write(address, value);
                });
        assert(!null_report.identity_read_ok);
        assert(!null_report.ok);
    }

    {
        PresentationMemory memory {};
        SeedPresentationMemory(memory);
        const auto post = CapturePresentation(
            memory,
            Horse::ReplayAnimationPresentationCapturePhase::
                AfterNativeConsumers);
        assert(post.effect.fade_cursor == 10);
        assert(post.animation.blend_cursor == 20);
        assert(post.effect.parameters.front() == 0xA5u);
        assert(post.effect.parameters.back()
            == static_cast<uint8_t>(63u ^ 0xA5u));
        assert(post.effect.selected_material_row_valid);
        assert(post.effect.selected_material_row_value == 0.625f);

        auto effect_cursor_mutation = post;
        ++effect_cursor_mutation.effect.fade_cursor;
        assert(Horse::HashReplayAnimationPresentationState(post)
            != Horse::HashReplayAnimationPresentationState(
                effect_cursor_mutation));
        assert(effect_cursor_mutation.animation.blend_cursor
            == post.animation.blend_cursor);

        auto animation_cursor_mutation = post;
        ++animation_cursor_mutation.animation.blend_cursor;
        assert(Horse::HashReplayAnimationPresentationState(post)
            != Horse::HashReplayAnimationPresentationState(
                animation_cursor_mutation));
        assert(animation_cursor_mutation.effect.fade_cursor
            == post.effect.fade_cursor);
    }

    {
        PresentationMemory memory {};
        SeedPresentationMemory(memory);
        const auto pre = CapturePresentation(
            memory,
            Horse::ReplayAnimationPresentationCapturePhase::
                BeforeNativeConsumers);

        memory.put(kPresentationActor
                + Horse::kReplayEffectFadeCursorOffset,
            int32_t{11});
        memory.put(kPresentationActor
                + Horse::kReplayAnimOverrideCursorOffset,
            int32_t{21});
        memory.put(kPresentationOutput
                + Horse::kReplayOutputAnimParam838Offset,
            99.0f);
        const auto post = CapturePresentation(
            memory,
            Horse::ReplayAnimationPresentationCapturePhase::
                AfterNativeConsumers);

        const std::array<uint8_t, 32> gameplay_before = [&] {
            std::array<uint8_t, 32> bytes {};
            for (size_t i = 0; i < bytes.size(); ++i)
            {
                bytes[i] = static_cast<uint8_t>(i * 3u);
                memory.bytes[0x100 + i] = bytes[i];
            }
            return bytes;
        }();

        auto restore = [&](const auto& snapshot, auto phase) {
            return Horse::RestoreReplayAnimationPresentation(
                kPresentationActor, phase, snapshot,
                [&](uintptr_t address, void* dst, size_t size) {
                    return memory.read(address, dst, size);
                },
                [&](uintptr_t address, const void* src, size_t size) {
                    return memory.write(address, src, size);
                },
                [&](uintptr_t actor, uintptr_t& provider) {
                    return memory.resolve_provider(actor, provider);
                });
        };

        auto report = restore(
            pre,
            Horse::ReplayAnimationPresentationCapturePhase::
                BeforeNativeConsumers);
        assert(report.ok());
        assert(memory.get<int32_t>(kPresentationActor
                + Horse::kReplayEffectFadeCursorOffset) == 10);
        assert(memory.get<int32_t>(kPresentationActor
                + Horse::kReplayAnimOverrideCursorOffset) == 20);

        report = restore(
            post,
            Horse::ReplayAnimationPresentationCapturePhase::
                AfterNativeConsumers);
        assert(report.ok());
        assert(memory.get<int32_t>(kPresentationActor
                + Horse::kReplayEffectFadeCursorOffset) == 11);
        assert(memory.get<int32_t>(kPresentationActor
                + Horse::kReplayAnimOverrideCursorOffset) == 21);
        assert(memory.get<float>(kPresentationOutput
                + Horse::kReplayOutputAnimParam838Offset) == 99.0f);
        assert((memory.get<uint32_t>(kPresentationOutput
                + Horse::kReplayOutputEffectDirtyOffset)
            & Horse::kReplayEffectRefreshMask)
            == Horse::kReplayEffectRefreshMask);
        assert((memory.get<uint32_t>(kPresentationOutput
                + Horse::kReplayOutputAnimDirtyPrimaryOffset)
            & Horse::kReplayAnimRefreshPrimaryMask)
            == Horse::kReplayAnimRefreshPrimaryMask);
        assert((memory.get<uint32_t>(kPresentationOutput
                + Horse::kReplayOutputAnimDirtySecondaryOffset)
            & Horse::kReplayAnimRefreshSecondaryMask)
            == Horse::kReplayAnimRefreshSecondaryMask);
        assert(memory.get<uint8_t>(kPresentationOutput
                + Horse::kReplayOutputNeedsAnimUpdateOffset) == 1);
        assert(std::memcmp(
            memory.bytes.data() + 0x100,
            gameplay_before.data(), gameplay_before.size()) == 0);

        report = restore(
            post,
            Horse::ReplayAnimationPresentationCapturePhase::
                BeforeNativeConsumers);
        assert(report.result
            == Horse::ReplayAnimationPresentationRestoreResult::
                PhaseMismatch);
    }

    {
        PresentationMemory memory {};
        SeedPresentationMemory(memory);
        const auto snapshot = CapturePresentation(
            memory,
            Horse::ReplayAnimationPresentationCapturePhase::
                AfterNativeConsumers);

        // Only the selected row's first float is presentation history.
        const uintptr_t selected_row =
            kPresentationRows + 2
                * Horse::kReplayOutputMaterialRowStride;
        const auto row_tail_before = memory.bytes[selected_row + 8];
        memory.put(selected_row, 9.0f);
        const auto report = Horse::RestoreReplayAnimationPresentation(
            kPresentationActor,
            Horse::ReplayAnimationPresentationCapturePhase::
                AfterNativeConsumers,
            snapshot,
            [&](uintptr_t address, void* dst, size_t size) {
                return memory.read(address, dst, size);
            },
            [&](uintptr_t address, const void* src, size_t size) {
                return memory.write(address, src, size);
            },
            [&](uintptr_t actor, uintptr_t& provider) {
                return memory.resolve_provider(actor, provider);
            });
        assert(report.ok());
        assert(memory.get<float>(selected_row) == 0.625f);
        assert(memory.bytes[selected_row + 8] == row_tail_before);
    }

    {
        PresentationMemory memory {};
        SeedPresentationMemory(memory);
        const auto snapshot = CapturePresentation(
            memory,
            Horse::ReplayAnimationPresentationCapturePhase::
                AfterNativeConsumers);
        auto restore = [&] {
            return Horse::RestoreReplayAnimationPresentation(
                kPresentationActor,
                Horse::ReplayAnimationPresentationCapturePhase::
                    AfterNativeConsumers,
                snapshot,
                [&](uintptr_t address, void* dst, size_t size) {
                    return memory.read(address, dst, size);
                },
                [&](uintptr_t address, const void* src, size_t size) {
                    return memory.write(address, src, size);
                },
                [&](uintptr_t actor, uintptr_t& provider) {
                    return memory.resolve_provider(actor, provider);
                });
        };

        // Rebound output identity is refused before any saved pointer bytes
        // or presentation state are applied.
        std::memcpy(memory.bytes.data() + kAlternateOutput,
                    memory.bytes.data() + kPresentationOutput,
                    0x900);
        memory.put(kPresentationActor
                + Horse::kReplayPresentationOutputOffset,
            kAlternateOutput);
        auto report = restore();
        assert(report.result
            == Horse::ReplayAnimationPresentationRestoreResult::
                OutputChanged);
        memory.put(kPresentationActor
                + Horse::kReplayPresentationOutputOffset,
            kPresentationOutput);

        memory.provider = kPresentationProvider + 0x100;
        report = restore();
        assert(report.result
            == Horse::ReplayAnimationPresentationRestoreResult::
                ProviderChanged);
        memory.provider = kPresentationProvider;

        const uintptr_t alternate_material = 0x8100;
        memory.put(kPresentationActor
                + Horse::kReplayPresentationDynamicMaterialOffset,
            alternate_material);
        report = restore();
        assert(report.result
            == Horse::ReplayAnimationPresentationRestoreResult::
                DynamicMaterialChanged);
        memory.put(kPresentationActor
                + Horse::kReplayPresentationDynamicMaterialOffset,
            uintptr_t{0x8000});

        std::memcpy(memory.bytes.data() + kAlternateRows,
                    memory.bytes.data() + kPresentationRows,
                    4 * Horse::kReplayOutputMaterialRowStride);
        memory.put(kPresentationOutput
                + Horse::kReplayOutputMaterialRowsOffset,
            kAlternateRows);
        report = restore();
        assert(report.result
            == Horse::ReplayAnimationPresentationRestoreResult::
                MaterialRowsChanged);
        memory.put(kPresentationOutput
                + Horse::kReplayOutputMaterialRowsOffset,
            kPresentationRows);

        memory.put(kPresentationOutput
                + Horse::kReplayOutputMaterialRowCountOffset,
            int32_t{5});
        report = restore();
        assert(report.result
            == Horse::ReplayAnimationPresentationRestoreResult::
                MaterialRowCountChanged);
    }

    {
        PresentationMemory memory {};
        SeedPresentationMemory(memory);
        const auto snapshot = CapturePresentation(
            memory,
            Horse::ReplayAnimationPresentationCapturePhase::
                AfterNativeConsumers);
        memory.put(kPresentationActor
                + Horse::kReplayEffectFadeCursorOffset,
            int32_t{77});
        memory.failed_write = kPresentationOutput
            + Horse::kReplayOutputAnimParam844Offset;
        const auto report = Horse::RestoreReplayAnimationPresentation(
            kPresentationActor,
            Horse::ReplayAnimationPresentationCapturePhase::
                AfterNativeConsumers,
            snapshot,
            [&](uintptr_t address, void* dst, size_t size) {
                return memory.read(address, dst, size);
            },
            [&](uintptr_t address, const void* src, size_t size) {
                return memory.write(address, src, size);
            },
            [&](uintptr_t actor, uintptr_t& provider) {
                return memory.resolve_provider(actor, provider);
            });
        assert(report.result
            == Horse::ReplayAnimationPresentationRestoreResult::
                WriteFailed);
        assert(report.rollback_attempted);
        assert(report.rollback_ok);
        assert(memory.get<int32_t>(kPresentationActor
                + Horse::kReplayEffectFadeCursorOffset) == 77);
    }

    {
        PresentationMemory memory {};
        SeedPresentationMemory(memory);
        memory.put(kPresentationOutput
                + Horse::kReplayOutputEffectDirtyOffset,
            uint32_t{0});
        memory.put(kPresentationOutput
                + Horse::kReplayOutputAnimDirtyPrimaryOffset,
            uint32_t{0});
        memory.put(kPresentationOutput
                + Horse::kReplayOutputAnimDirtySecondaryOffset,
            uint32_t{0});
        assert(Horse::ReseedReplayAnimationPresentation(
            kPresentationActor,
            [&](uintptr_t address, void* dst, size_t size) {
                return memory.read(address, dst, size);
            },
            [&](uintptr_t address, const void* src, size_t size) {
                return memory.write(address, src, size);
            }));
        assert(memory.get<int32_t>(kPresentationActor
                + Horse::kReplayEffectFadeCursorOffset) == 10);
        assert(memory.get<int32_t>(kPresentationActor
                + Horse::kReplayAnimOverrideCursorOffset) == 20);
        assert(memory.get<uint8_t>(kPresentationOutput
                + Horse::kReplayOutputNeedsAnimUpdateOffset) == 1);
    }

    std::cout << "replay animation presentation self-test passed\n";
    return 0;
}
