#pragma once

#include <Unreal/UObjectArray.hpp>
#include "deterministic/UcrtRandBroker.hpp"
#include "deterministic/ReplayGroundLifecycleObservation.hpp"

// Read the runtime's native CRT lane, including in stock controls where
// HorseMod is absent. This does NOT observe the private MoveVM CRT lane;
// native PTD equality alone cannot certify combat RNG after policy v5.
// Split qualification requires a modified independent control and the broker
// checkpoint/diagnostic combat state and count. The getter can initialize TLS on its
// first use; it never draws or seeds. Calls occur on the native replay thread.
bool ReadReplayNativeCrt(std::uint32_t& state) noexcept
{
    using namespace Horse::Deterministic;
    static UcrtRandBroker observer;
    static const bool bound = [] {
        const auto module = GetModuleHandleW(L"ucrtbase.dll");
        return module && observer.Start().ok() && observer.BindNative(
            reinterpret_cast<UcrtRandFn>(GetProcAddress(module, "rand")),
            reinterpret_cast<UcrtSrandFn>(GetProcAddress(module, "srand"))).ok();
    }();
    return bound && observer.ObserveNative(GetCurrentThreadId(), state).ok();
}

// UObject::IsReal performs a full global-object scan. Observation runs at
// every native boundary and actor tick; use the object's validated array slot.
bool IsLiveReplayObject(const RC::Unreal::UObject* object) noexcept
{
    if (!object) return false;
    __try
    {
        const auto* item = RC::Unreal::FUObjectArray::IndexToObject(object->GetInternalIndex());
        return item && item->GetUObject() == object && item->IsValid(false);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

struct ReplaySourceExtent
{
    std::int32_t rounds{};
    std::array<std::int32_t, 16> recorder_counts{};
    std::array<std::array<std::uint32_t, 8>, 16> recorded_times{};
    std::array<std::array<std::uint32_t, 8>, 16> source_samples{};
};

// Import does not initialize future-round recorded time. For this verified
// DWORD recorder, round activation (140428510 -> 1408A4490 -> 14089F9D0)
// derives it from byte count / 4. Observe without invoking activation.
// Neither quantity is a simulation tick count or match-end marker.
bool ReadReplaySourceExtent(std::uintptr_t player, ReplaySourceExtent& result) noexcept
{
    __try
    {
        const auto* header = reinterpret_cast<const std::byte*>(player + 0x3b8);
        result.rounds = *reinterpret_cast<const std::int32_t*>(header + 8);
        const auto* rounds = *reinterpret_cast<const std::byte* const*>(header);
        if (!rounds || result.rounds <= 0 || result.rounds > 16
            || result.rounds > *reinterpret_cast<const std::int32_t*>(header + 12)) return false;
        for (std::int32_t round = 0; round < result.rounds; ++round)
        {
            const auto* row = rounds + round * 0x10;
            const auto* entries = *reinterpret_cast<const std::byte* const*>(row);
            const auto count = *reinterpret_cast<const std::int32_t*>(row + 8);
            if (!entries || count <= 0 || count > 8
                || count > *reinterpret_cast<const std::int32_t*>(row + 12)) return false;
            result.recorder_counts[round] = count;
            for (std::int32_t slot = 0; slot < count; ++slot)
            {
                const auto time = *reinterpret_cast<const std::uint32_t*>(entries + slot * 0x18 + 4);
                if (time > INT32_MAX) return false; // Native reader uses a signed cursor comparison.
                result.recorded_times[round][slot] = time;
                const auto* object = *reinterpret_cast<const std::byte* const*>(entries + slot * 0x18 + 0x10);
                const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
                if (!object || *reinterpret_cast<const std::uintptr_t*>(object) != base + 0x328e948)
                    return false;
                const auto bytes = *reinterpret_cast<const std::int32_t*>(object + 0x10);
                if (bytes < 0 || bytes % 4 || bytes > 64 * 1024 * 1024
                    || bytes > *reinterpret_cast<const std::int32_t*>(object + 0x14)) return false;
                result.source_samples[round][slot] = static_cast<std::uint32_t>(bytes / 4);
            }
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Passive EngineTickPost observation shared by the stock control and runtime
// probe. No detours, simulation writes, random draws or HorseMod exports.
struct ReplayTrajectorySample
{
    std::uintptr_t replay_player{};
    bool source_active{};
    std::uint8_t manager_phase{}, move_state{}, round_state{};
    std::uint32_t frame{};
    std::int32_t world_mode{};
    std::int32_t round{}, cursor{}, input_round{}, input_time{};
    std::uint32_t round_frame{};
    bool initial_reset_available{}, initial_reset_equal{}, initial_reset_raw_equal{};
    std::uint32_t initial_reset_difference{UINT32_MAX}, initial_reset_expected{}, initial_reset_actual{};
    std::uint32_t initial_reset_consumed_difference{UINT32_MAX};
    std::uint16_t initial_reset_expected_count{}, initial_reset_actual_count{};
    std::uint32_t stage_seed{}, applied_round{}, restore_mode{};
    std::uint32_t mt_cursor{};
    std::uint64_t mt_hash{};
    std::uint32_t crt_state{};
    Horse::Deterministic::ReplayGroundLifecycleObservation ground;
    std::array<std::uint32_t, 2> inputs{};
    std::int32_t published_count{};
    std::array<std::uint32_t, 4> published_pairs{};
    std::array<std::array<std::uint32_t, 9>, 2> positions{};
    std::array<std::array<std::uint32_t, 2>, 2> vital{};
    // Each lane contributes packed move, tick count, and animation-frame bits.
    std::array<std::array<std::uint32_t, 9>, 2> moves{};
    friend bool operator==(const ReplayTrajectorySample&, const ReplayTrajectorySample&) = default;
};

bool ReadReplayTrajectory(RC::Unreal::UObject* manager,
    ReplayTrajectorySample& sample, bool include_inactive = false) noexcept
{
    if (!IsLiveReplayObject(manager)) return false;
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    __try
    {
        auto** property = manager->GetValuePtrByPropertyNameInChain<RC::Unreal::UObject*>(
            L"BattleReplayPlayer");
        if (property == nullptr || *property == nullptr
            || !IsLiveReplayObject(*property)) return false;
        const auto* player = reinterpret_cast<const std::byte*>(*property);
        sample.replay_player = reinterpret_cast<std::uintptr_t>(player);
        // Replay import 140428040 and tracker source reader 140428D70.
        if (*reinterpret_cast<const std::uintptr_t*>(player + 0x390) != base + 0x3290d20) return false;
        sample.source_active = *reinterpret_cast<const std::uint8_t*>(player + 0x398) != 0;
        if (!sample.source_active && !include_inactive) return false;
        sample.round = *reinterpret_cast<const std::int32_t*>(player + 0x39c);
        sample.cursor = *reinterpret_cast<const std::int32_t*>(player + 0x3a0);
        if (sample.source_active && (sample.round < 0 || sample.cursor < 0)) return false;
        if (!ReadReplayNativeCrt(sample.crt_state)) return false;
        if(!sample.ground.Capture([](std::uintptr_t address,auto& value) noexcept {
            __try {if(!address)return false;std::memcpy(&value,reinterpret_cast<const void*>(address),sizeof(value));return true;}
            __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
        },[](std::uintptr_t object){return IsLiveReplayObject(reinterpret_cast<RC::Unreal::UObject*>(object));},
            base,reinterpret_cast<std::uintptr_t>(manager)))return false;
        // Independent native MT observation, not a runtime checkpoint hash.
        const auto* mt = reinterpret_cast<const unsigned char*>(base + 0x4100ea0);
        sample.mt_cursor = *reinterpret_cast<const std::uint32_t*>(mt);
        sample.mt_hash = 14695981039346656037ull;
        for (std::size_t i = 0; i < 5008; ++i) { sample.mt_hash ^= mt[i]; sample.mt_hash *= 1099511628211ull; }
        const auto* manager_bytes = reinterpret_cast<const std::byte*>(manager);
        sample.manager_phase = *reinterpret_cast<const std::uint8_t*>(manager_bytes + 0x1461);
        sample.move_state = *reinterpret_cast<const std::uint8_t*>(manager_bytes + 0x1463);
        sample.round_state = *reinterpret_cast<const std::uint8_t*>(manager_bytes + 0x1480);
        // 1403FCD10 publishes two DWORDs per player, consumed by 1403FE520.
        // InputLog +3B8 above is the latest source sample, not necessarily
        // the cached sample selected for this traversal in a multi-input interval.
        sample.published_count = *reinterpret_cast<const std::int32_t*>(manager_bytes + 0x14b0);
        const auto* pairs = *reinterpret_cast<const std::byte* const*>(manager_bytes + 0x14a8);
        if (sample.published_count < 0 || sample.published_count > 2
            || (sample.published_count && pairs == nullptr)) return false;
        if (sample.published_count)
            std::memcpy(sample.published_pairs.data(), pairs, sample.published_count * 8);
        const auto* input = *reinterpret_cast<const std::byte* const*>(manager_bytes + 0x478);
        if (input == nullptr) return false;
        sample.frame = *reinterpret_cast<const std::uint32_t*>(base + 0x470d0c4);
        // 1402DC420: native IsMatchFinished fallback is master world mode == 10.
        sample.world_mode = *reinterpret_cast<const std::int32_t*>(base + 0x4846364);
        sample.input_round = *reinterpret_cast<const std::int32_t*>(input + 0x3a0);
        sample.input_time = *reinterpret_cast<const std::int32_t*>(input + 0x3a4);
        sample.round_frame = *reinterpret_cast<const std::uint32_t*>(manager_bytes + 0x1490);
        std::memcpy(sample.inputs.data(), input + 0x3b8, sizeof(sample.inputs));
        if (sample.source_active && sample.round == 0 && sample.cursor == 0)
        {
            // 1403835B0 -> 1402FBA90 saves the native round-start image.
            // This compares that cached image, not a fresh live snapshot.
            const auto count = *reinterpret_cast<const std::int32_t*>(player + 0x3B0);
            const auto capacity = *reinterpret_cast<const std::int32_t*>(player + 0x3B4);
            const auto* recorded = *reinterpret_cast<const std::uint32_t* const*>(player + 0x3A8);
            const auto* cached = reinterpret_cast<const std::uint32_t*>(base + 0x4844070);
            sample.initial_reset_available = recorded != nullptr && count > 0 && count <= 16
                && count <= capacity
                && *reinterpret_cast<const std::uint32_t*>(base + 0x4844068) != 0
                && *reinterpret_cast<const std::uint32_t*>(base + 0x484406C) != 0;
            sample.stage_seed = *reinterpret_cast<const std::uint32_t*>(base + 0x4844010);
            sample.applied_round = *reinterpret_cast<const std::uint32_t*>(base + 0x48463A4);
            sample.restore_mode = *reinterpret_cast<const std::uint32_t*>(base + 0x4843EF4);
            sample.initial_reset_raw_equal = sample.initial_reset_available;
            if (sample.initial_reset_available)
            {
                for (std::uint32_t word = 0; word < 0xC0 / 4; ++word)
                    if (recorded[word] != cached[word])
                    {
                        sample.initial_reset_raw_equal = false;
                        sample.initial_reset_difference = word * 4;
                        sample.initial_reset_expected = recorded[word];
                        sample.initial_reset_actual = cached[word];
                        break;
                    }
                const auto* expected = reinterpret_cast<const std::byte*>(recorded);
                const auto* actual = reinterpret_cast<const std::byte*>(cached);
                std::memcpy(&sample.initial_reset_expected_count, expected + 0x7C, 2);
                std::memcpy(&sample.initial_reset_actual_count, actual + 0x7C, 2);
                sample.initial_reset_equal = sample.initial_reset_expected_count <= 16
                    && sample.initial_reset_expected_count == sample.initial_reset_actual_count;
                // 1402FBCB0 consumes these fields; Save writes all 16 motion
                // slots from scratch, including unused slots and padding.
                // Preserve raw inequality above. This does not change any
                // HorseMod checkpoint or canonical comparison.
                for (std::uint32_t offset = 0; offset < 0xC0; ++offset)
                {
                    const bool consumed = offset < 0x11 || (offset >= 0x14 && offset < 0x7E)
                        || (offset >= 0x80 && (offset - 0x80) / 4 < sample.initial_reset_expected_count
                            && (offset - 0x80) % 4 < 3);
                    if (consumed && expected[offset] != actual[offset])
                    {
                        sample.initial_reset_equal = false;
                        sample.initial_reset_consumed_difference = offset;
                        break;
                    }
                }
            }
        }
        for (std::size_t slot = 0; slot < 2; ++slot)
        {
            const auto* fighter = *reinterpret_cast<const std::byte* const*>(
                base + 0x470de90 + slot * sizeof(void*));
            if (fighter == nullptr) return false;
            // 140301E60: Lux simulation, root-step and smoothed render triples.
            constexpr std::array<std::size_t, 3> offsets{0xa0, 0xc0, 0x2090};
            for (std::size_t family = 0; family < offsets.size(); ++family)
                std::memcpy(sample.positions[slot].data() + family * 3,
                    fighter + offsets[family], 3 * sizeof(std::uint32_t));
            // Candidate vital is published by 1402D3A20 and tested/clamped by
            // 140386420. Read the displayed value separately; never infer
            // health from the historically mislabeled +0x1364 timer.
            std::memcpy(&sample.vital[slot][0], fighter + 0x43e08, 4);
            std::memcpy(&sample.vital[slot][1], fighter + 0x43e14, 4);
            // FLuxMoveLane: three lanes of 0x468 bytes. Native 1402FFEB0
            // advances +0x04 and +0x08; +0x02 is the packed move selector.
            for (std::size_t lane = 0; lane < 3; ++lane)
            {
                const auto* source = fighter + 0x444f0 + lane * 0x468;
                std::uint16_t packed{};
                std::memcpy(&packed, source + 2, 2);
                sample.moves[slot][lane * 3] = packed;
                std::memcpy(&sample.moves[slot][lane * 3 + 1], source + 4, 8);
            }
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
